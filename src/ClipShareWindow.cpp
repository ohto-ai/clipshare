#include <QClipboard>
#include <QMimeData>
#include <QMimeDatabase>
#include <QNetworkInterface>
#include <QNetworkDatagram>
#include <QHostInfo>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QBuffer>
#include <QUrl>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <QMenu>

#include "ClipShareWindow.h"

ClipShareWindow::ClipShareWindow(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    spdlog::info("[Config] Heartbeat Port = {}", config.heartbeatPort);
    spdlog::info("[Config] Heartbeat Interval = {}", config.heartbeatBroadcastIntervalMSecs);
    spdlog::info("[Config] Heartbeat Multicast Group Host = {}", config.heartbeatMulticastGroupHost);
    spdlog::info("[Config] Package Port = {}", config.packagePort);

    NeighborDeviceInfo::HeartbeatSuvivalTimeout = config.heartbeatSuvivalTimeoutMSecs;

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
    heartbeatBroadcaster.setSocketOption(QAbstractSocket::MulticastLoopbackOption, 0);
    connect(&heartbeatBroadcaster, &QUdpSocket::readyRead, [=]{
        while (heartbeatBroadcaster.hasPendingDatagrams()) {
            auto datagram = heartbeatBroadcaster.receiveDatagram();
            auto datagramData = datagram.data();

            spdlog::trace("[Heartbeat] Receive [{}bytes] {}:{}=>{}:{} {:a}", datagramData.length()
                , datagram.senderAddress().toString(), datagram.senderPort()
                , datagram.destinationAddress().toString(), datagram.destinationPort(), spdlog::to_hex(datagramData));

            if (datagramData.size() == sizeof(ClipShareHeartbeatPackage))
            {
                handleHeartbeatReceived(datagram, *(reinterpret_cast<const ClipShareHeartbeatPackage*>(datagramData.data())));
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
    heartbeatTimer.setInterval(config.heartbeatBroadcastIntervalMSecs);
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

            ClipShareDataPackage package;

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
    neighbors.insert(neighbor, ClipShareNeighbor{ sock->peerName(), sock->peerAddress(), sock->peerPort() });
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
                handlePackageReceived(sock, ClipShareDataPackage(nlohmann::json::parse(data)));
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
            if (neighborServerSockets.value(neighbor) == sock)
            {
                neighborServerSockets.remove(neighbor);
                spdlog::info("[Server] Client {}:{} disconnected.", sock->peerAddress().toString(), sock->peerPort());
            }
            else
            {
                spdlog::warn("[Server] Client socket {}:{} no found.", sock->peerAddress().toString(), sock->peerPort());
            }
        });
    connect(sock, &QTcpSocket::readyRead, [=]
        {
            auto data = sock->readAll();
            spdlog::trace("[Server] Receive [{}bytes] {}:{} {}", data.length()
                , sock->peerAddress().toString(), sock->peerPort(), data);

            try {
                handlePackageReceived(sock, ClipShareDataPackage(nlohmann::json::parse(data)));
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

void ClipShareWindow::handlePackageReceived(QTcpSocket* conn, const ClipShareDataPackage& package)
{
    spdlog::info("[Server] Receive mimeData: {} formats, from {}:{} {}", package.mimeFormats.size()
        , conn->peerAddress().toString(), conn->peerPort(), package.sender);

    auto mimeData = new QMimeData();

    for (int i = 0; i < package.mimeFormats.size(); i++)
    {
        spdlog::info("[Server] mimeData: {}", package.mimeFormats.at(i));
        mimeData->setData(package.mimeFormats.at(i), QByteArray::fromBase64(package.mimeData.at(i)));
    }

    // todo receive image
    if (!package.mimeImageType.isEmpty())
    {
        auto data = QByteArray::fromBase64(package.mimeImageData);
        QTemporaryFile temp;
        temp.setFileTemplate(QString{ "%1%2%3-XXXXXX.%4" }
            .arg(QDir::tempPath()
                , QDir::separator()
                , QApplication::applicationName()
                , package.mimeImageType));
        temp.setAutoRemove(false);
        if (temp.open())
        {
            temp.write(data);
            temp.close();

            QImage image;
            spdlog::info("[Server][Mime] Temp path {}", temp.fileName());
            image.load(temp.fileName());
            spdlog::info("[Server][Mime] Receive Image[{}x{}]", image.width(), image.height());
            mimeData->setUrls({ QUrl::fromLocalFile(temp.fileName()) });
            mimeData->setImageData(image);
        }
    }
    

    QApplication::clipboard()->blockSignals(true);
    QApplication::clipboard()->setMimeData(mimeData);
    QApplication::clipboard()->blockSignals(false);
}

void ClipShareWindow::handleHeartbeatReceived(const QNetworkDatagram& datagram, const ClipShareHeartbeatPackage& pkg)
{
    if (pkg.valid())
    {
        if (pkg.command == ClipShareHeartbeatPackage::Heartbeat)
        {
            spdlog::trace("[Heartbeat] Heartbeat from ({}:{})", datagram.senderAddress().toString(), datagram.senderPort());
            broadcastHeartbeat(ClipShareHeartbeatPackage::Response);
            // todo send device info to it / ignore local ************************************************************************************
        }
        else if (pkg.command == ClipShareHeartbeatPackage::Response)
        {
            spdlog::trace("[Heartbeat] Response from {}:{}", datagram.senderAddress().toString(), datagram.senderPort());
            neighbors.insert(QString{ "%1_%2" }.arg(datagram.senderAddress().toString()).arg(pkg.port)
                , ClipShareNeighbor{ datagram.senderAddress().toString(), datagram.senderAddress(), static_cast<int>(pkg.port) });
        }
    }
    else
    {
        spdlog::warn("[Invalid] {}:{} heartbeat package [magic = 0x{:xns} command = 0x{:x}]"
            , datagram.senderAddress().toString(), datagram.senderPort()
            , spdlog::to_hex(pkg.magic, pkg.magic + sizeof(pkg.magic) / sizeof(pkg.magic[0])), pkg.command);
    }
}

void ClipShareWindow::sendToNeighbor(QString neighbor, const ClipShareDataPackage&package)
{
    spdlog::info("Send to neighbor {}", neighbor);
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
        
        connect(sock, &QTcpSocket::connected, [=]
            {
                if (sock->state() == QTcpSocket::ConnectedState)
                {
                    addServerNeighborSocket(sock);
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

void ClipShareDataPackage::encodeMimeData(const QMimeData*mimeData)
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
            image.save(&buffer, ClipShareConfig::DefaultMimeImageType);
            mimeImageType = QMimeDatabase().mimeTypeForData(imageData).name().split("/").back();
            mimeImageData = imageData.toBase64();
        }

        // use default type
        if (mimeImageType.isEmpty())
        {
            spdlog::trace("[Mime] Set mimeImageType with {}", ClipShareConfig::DefaultMimeImageType);
            mimeImageType = ClipShareConfig::DefaultMimeImageType;
        }
    }
}
