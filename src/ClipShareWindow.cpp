#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QBuffer>
#include <QHostInfo>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <QNetworkDatagram>
#include "ClipShareWindow.h"

ClipShareWindow::ClipShareWindow(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    spdlog::info("[Config] Heartbeat Port = {}", config.heartbeatPort);
    spdlog::info("[Config] Heartbeat Interval = {}", config.heartbeatInterval);
    spdlog::info("[Config] Heartbeat Multicast Group Host = {}", config.heartbeatMulticastGroupHost);
    spdlog::info("[Config] Package Port = {}", config.packagePort);

    connect(&packageReciver, &QTcpServer::newConnection, [=]
        {
            while (packageReciver.hasPendingConnections())
            {
                auto sock = packageReciver.nextPendingConnection();
                spdlog::info("[Server] Client {}:{} connected.", sock->peerAddress().toString(), sock->peerPort());
                addClientNeighborSocket(sock);
            }
        });

    // start package listen
    packageReciver.listen(QHostAddress::AnyIPv4, config.packagePort);
    spdlog::info("[Server] Listen on {}.", config.packagePort);

    // setup heartbeat response
    heartbeatBroadcaster.bind(QHostAddress::AnyIPv4, config.heartbeatPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    heartbeatBroadcaster.joinMulticastGroup(QHostAddress(config.heartbeatMulticastGroupHost));
    //heartbeatBroadcaster.setSocketOption(QAbstractSocket::MulticastLoopbackOption, 0);
    connect(&heartbeatBroadcaster, &QUdpSocket::readyRead, [=]{
        while (heartbeatBroadcaster.hasPendingDatagrams()) {
            auto datagram = heartbeatBroadcaster.receiveDatagram();
            auto datagramData = datagram.data();

            spdlog::trace("[Heartbeat] Receive [{}bytes] {}:{}=>{}:{} {:a}", datagramData.length()
                , datagram.senderAddress().toString(), datagram.senderPort()
                , datagram.destinationAddress().toString(), datagram.destinationPort(), spdlog::to_hex(datagramData));

            if (datagramData.size() == sizeof(ClipShareHeartbeatPackage))
            {
                const auto pkg = *(reinterpret_cast<const ClipShareHeartbeatPackage*>(datagramData.data()));
                if (pkg.valid())
                {
                    if (pkg.command == ClipShareHeartbeatPackage::Heartbeat)
                    {
                        spdlog::info("[Heartbeat] Heartbeat from ({}:{})", datagram.senderAddress().toString(), datagram.senderPort());
                        broadcastHeartbeat(ClipShareHeartbeatPackage::Response);
                        // todo send device info to it / ignore local
                    }
                    else if (pkg.command == ClipShareHeartbeatPackage::Response)
                    {
                        spdlog::info("[Heartbeat] Response from {}:{}", datagram.senderAddress().toString(), datagram.senderPort());
                        neighbors.insert(QString{"%1_%2"}.arg(datagram.senderAddress().toString()).arg(pkg.port)
                            , ClipShareNeighbor{datagram.senderAddress().toString(), datagram.senderAddress(), static_cast<int>(pkg.port)});
                    }
                }
                else
                {
                    spdlog::warn("[Invalid] {}:{} heartbeat package [magic = 0x{:xns} command = 0x{:x}]"
                        , datagram.senderAddress().toString(), datagram.senderPort()
                        , spdlog::to_hex(pkg.magic, pkg.magic + sizeof(pkg.magic) / sizeof(pkg.magic[0])), pkg.command);
                }
            }
            else
            {
                spdlog::warn("[Heartbeat] {}:{} Incorrect heartbeat package size: {}"
                    , datagram.senderAddress().toString(), datagram.senderPort()
                    , datagramData.size());
                spdlog::warn("[Heartbeat] {}:{} Incorrect heartbeat package content: {:a}"
                    , datagram.senderAddress().toString(), datagram.senderPort()
                    , spdlog::to_hex(datagramData));
            }
        }
    });

    // setup heartbeat sender
    heartbeatTimer.setInterval(config.heartbeatInterval);
    connect(&heartbeatTimer, &QTimer::timeout, this, [=]{
        broadcastHeartbeat();
    });

    // start timer
    heartbeatTimer.start();
    // send heartbeat
    broadcastHeartbeat();

    systemTrayIcon.setIcon(QApplication::windowIcon());

    auto systemTrayMenu = new QMenu(this);
    systemTrayMenu->addAction("Exit", QApplication::instance(), &QApplication::quit);

    systemTrayIcon.setContextMenu(systemTrayMenu);

    connect(QApplication::clipboard(), &QClipboard::dataChanged, [=]
        {
            const auto clipboard = QApplication::clipboard();
            auto mimeData = clipboard->mimeData();

            auto formats = mimeData->formats();
            spdlog::trace("[Clipboard][MimeData] contains {} formats", formats.count());
            for (auto& key : formats)
            {
                auto val = mimeData->data(key);
                spdlog::trace("[Clipboard][MimeData] {} = [{}bytes]{}", key, val.size(), val);
            }

            // preview
            if (mimeData->hasImage()) {
                auto imageData = mimeData->imageData().value<QImage>();
                spdlog::info("Image[{}x{}]", imageData.width(), imageData.height());
                systemTrayIcon.showMessage("Image", "", QIcon(QPixmap::fromImage(imageData)));
            }
            else if (mimeData->hasUrls()) {
                QStringList urlStringList;
                auto urls = mimeData->urls();
                for(int i = 0; i < urls.count(); ++i)
                {
                    spdlog::info("Urls[{}/{}]: {}", i + 1, urls.count(), urls[i].toString());
                    urlStringList.push_back(urls[i].toString());
                }
                systemTrayIcon.showMessage("Urls", urlStringList.join("\n"));
            }
            else if (mimeData->hasHtml()) {
                auto content = mimeData->html();
                spdlog::info("Rich Text[{} <{}bytes>]: {}", mimeData->text().count(), content.size(), mimeData->text());
                systemTrayIcon.showMessage("Rich Text:", mimeData->text());
            }
            else if (mimeData->hasText()) {
                auto text = mimeData->text();
                spdlog::info("Plain Text[{}]: {}", mimeData->text().count(), mimeData->text());
                systemTrayIcon.showMessage("Plain Text", text);
            }
            else {
                systemTrayIcon.showMessage("Cannot display data", QString{"Formats:(%1) \n Content:(%2)"}.arg(mimeData->formats().join("; "), mimeData->text()));
            }

            ClipSharePackage package;

            package.encodeMimeData(mimeData);

            package.sender = QHostInfo::localHostName();
            for(auto neighbor: neighbors.keys())
            {
                package.receiver = neighbor;
                sendToNeighbor(neighbor, package);
            }
        });
    systemTrayIcon.show();
}

ClipShareWindow::~ClipShareWindow()
{
    // send offline heartbeat package
    broadcastHeartbeat(ClipShareHeartbeatPackage::Offline);
}

QString ClipShareWindow::makeNeighborId(const QTcpSocket* sock)
{
    return QString{ "%1_%2" }.arg(sock->peerAddress().toString()).arg(sock->peerPort());
}

void ClipShareWindow::addClientNeighborSocket(QTcpSocket* sock)
{
    auto neighbor = makeNeighborId(sock);
    if (neighborClientSockets.contains(neighbor))
    {
        spdlog::warn("[Neighbor] {} has existed, try to close.", neighbor);
        neighborClientSockets.value(neighbor)->disconnectFromHost();
    }
    neighborClientSockets.insert(neighbor, sock);

    // register disconnected & readyRead
    connect(sock, &QTcpSocket::disconnected, [=]
        {
            neighborClientSockets.remove(neighbor);
            spdlog::info("[Server] Client {}:{} disconnected.", sock->peerAddress().toString(), sock->peerPort());
        });
    connect(sock, &QTcpSocket::readyRead, [=]
        {
            auto data = sock->readAll();
            spdlog::trace("[Server] Receive [{}bytes] {}:{} {}", data.length()
                , sock->peerAddress().toString(), sock->peerPort(), data);

            try {
                handlePackageReceived(sock, ClipSharePackage(nlohmann::json::parse(data)));
            }
            catch (nlohmann::json::parse_error e)
            {
                spdlog::error("[Server] Invaild package from {}:{} {:a}", sock->peerAddress().toString(), sock->peerPort(), spdlog::to_hex(data));
                spdlog::error("[Server] {}", e.what());
            }
        });
}

void ClipShareWindow::addServerNeighborSocket(QTcpSocket* sock)
{
    auto neighbor = makeNeighborId(sock);
    if (neighborServerSockets.contains(neighbor))
    {
        spdlog::warn("[Neighbor] {} has existed, try to close.", neighbor);
        neighborServerSockets.value(neighbor)->disconnectFromHost();
    }
    neighborServerSockets.insert(neighbor, sock);

    // register disconnected & readyRead
    connect(sock, &QTcpSocket::disconnected, [=]
        {
            neighborServerSockets.remove(neighbor);
            spdlog::info("[Server] Client {}:{} disconnected.", sock->peerAddress().toString(), sock->peerPort());
        });
    connect(sock, &QTcpSocket::readyRead, [=]
        {
            auto data = sock->readAll();
            spdlog::trace("[Server] Receive [{}bytes] {}:{} {}", data.length()
                , sock->peerAddress().toString(), sock->peerPort(), data);

            try {
                handlePackageReceived(sock, ClipSharePackage{ nlohmann::json::parse(data) });
            }
            catch (nlohmann::json::parse_error e)
            {
                spdlog::error("[Server] Invaild package from {}:{} {:a}", sock->peerAddress().toString(), sock->peerPort(), spdlog::to_hex(data));
                spdlog::error("[Server] {}", e.what());
            }
        });
}

void ClipShareWindow::broadcastHeartbeat(std::uint8_t command)
{
    ClipShareHeartbeatPackage heartbeat{ { 0x63, 0x73, 0x66 }, command, static_cast<std::uint32_t>(config.packagePort) };
    heartbeatBroadcaster.writeDatagram(reinterpret_cast<const char*>(&heartbeat)
        , sizeof(heartbeat)
        , QHostAddress(config.heartbeatMulticastGroupHost)
        , config.heartbeatPort);
}

void ClipShareWindow::handlePackageReceived(const QTcpSocket*conn, const ClipSharePackage& package)
{
    spdlog::info("[Server] Receive: {}, from {}:{} {}", package.mimeFormats.join("; ")
        , conn->peerAddress().toString(), conn->peerPort(), package.sender);

    auto mimeData = new QMimeData();
    for (int i = 0; i < package.mimeFormats.size(); i++)
    {
        mimeData->setData(package.mimeFormats.at(i), QByteArray::fromBase64(package.mimeData.at(i)));
    }
    spdlog::info("[Mime] Get {} formt(s)", package.mimeFormats.size());
    QApplication::clipboard()->setMimeData(mimeData);
}

void ClipShareWindow::sendToNeighbor(QString neighbor, const ClipSharePackage&package)
{
    QTcpSocket* sock{ nullptr };
    if(sock == nullptr && neighborClientSockets.contains(neighbor))
    {
        spdlog::info("[Server] Find socket to client {}", neighbor);
        sock = neighborClientSockets.value(neighbor);
        if (!sock->isValid() && sock->state() == QTcpSocket::ConnectedState)
        {
            spdlog::warn("[Server] Invalid socket to client {}", neighbor);
            sock->disconnectFromHost();
            sock = nullptr;
        }
    }
    if (sock == nullptr && neighborServerSockets.contains(neighbor))
    {
        spdlog::info("[Server] Find socket to server {}", neighbor);
        sock = neighborServerSockets.value(neighbor);
        if (!sock->isValid() && sock->state() == QTcpSocket::ConnectedState)
        {
            spdlog::warn("[Server] Invalid socket to client {}", neighbor);
            sock->disconnectFromHost();
            sock = nullptr;
        }
    }

    if (sock == nullptr)
    {
        spdlog::info("[Server] Establish new socket to {}", neighbor);
        auto sock = new QTcpSocket{ this };
        auto& neighborHost = neighbors.value(neighbor);
        sock->connectToHost(neighborHost.serverAddr, neighborHost.serverPort);
        addServerNeighborSocket(sock);

        connect(sock, &QTcpSocket::connected, [=]
            {
                if (sock->state() == QTcpSocket::ConnectedState)
                {
                    spdlog::info("[Server] Connect to {}({}:{})", sock->peerName(), sock->peerAddress().toString(), sock->peerPort());
                    auto data = QByteArray::fromStdString(nlohmann::json(package).dump());
                    sock->write(data.data(), data.length());
                }
                {
                    spdlog::error("[Server] Connect to {}:{} failed", sock->peerAddress().toString(), sock->peerPort());
                }
            });
    }
    else
    {
        auto data = QByteArray::fromStdString(nlohmann::json(package).dump());
        sock->write(data.data(), data.length());
    }
}

bool ClipShareWindow::isLocalHost(QHostAddress addr)
{
    return QNetworkInterface::allAddresses().contains(addr);
}

void ClipSharePackage::encodeMimeData(const QMimeData*mimeData)
{
    auto formats = mimeData->formats();
    for (auto format : formats)
    {
        auto data = mimeData->data(format);
        spdlog::trace("[Mime] format [{}bytes]: {}", data.size(), format);
        this->mimeFormats.push_back(format);
        this->mimeData.push_back(data.toBase64());
    }

    // attach image
    if (mimeData->hasImage()) {

        // attach file
        if (mimeData->hasUrls())
        {
            spdlog::trace("[Mime] Image from file {}", mimeData->urls().front().toLocalFile());
            QFile file(mimeData->urls().front().toLocalFile());
            if (file.open(QFile::ReadOnly)) {
                mimeImageType = QFileInfo{ file }.suffix();
                mimeImageData = file.readAll().toBase64();
                file.close();
            }
            else
            {
                spdlog::warn("[Mime] Cannot load file from image url: {}", mimeData->urls().front().toString());
            }
        }

        // from capture image / cannot load file; use image in clipboard
        if (mimeImageData.isEmpty())
        {
            auto image = qvariant_cast<QImage>(mimeData->imageData());
            spdlog::trace("[Mime] Image from clipboard {}x{}", image.width(), image.height());
            QByteArray imageData;
            QBuffer buffer(&imageData);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, DefaultMimeImageType);
            mimeImageData = imageData.toBase64();
        }

        // use default type
        if (mimeImageType.isEmpty())
        {
            spdlog::trace("[Mime] Set mimeImageType with {}", DefaultMimeImageType);
            mimeImageType = DefaultMimeImageType;
        }
    }
}
