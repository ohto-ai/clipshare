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
                auto conn = packageReciver.nextPendingConnection();
                spdlog::info("[Server] Client {}:{} connected.", conn->peerAddress().toString(), conn->peerPort());

                // todo
                clientSockets.insertMulti(conn->peerAddress().toString(), conn);

                connect(conn, &QTcpSocket::disconnected, [=]
                    {
                        clientSockets.remove(conn->peerAddress().toString(), conn);
                        spdlog::info("[Server] Client {}:{} disconnected.", conn->peerAddress().toString(), conn->peerPort());
                    });
                connect(conn, &QTcpSocket::readyRead, [=]
                    {
                        auto data = conn->readAll();
                        spdlog::trace("[Server] Receive [{}bytes] {}:{} {}", data.length()
                            , conn->peerAddress().toString(), conn->peerPort(), data);

                        try {
                            // todo fix parse error here
                            handlePackageReceived(conn, ClipSharePackage{ nlohmann::json::parse(data) });
                        }
                        catch (nlohmann::json::parse_error e)
                        {
                            spdlog::error("[Server] Invaild package from {}:{} {:a}", conn->peerAddress().toString(), conn->peerPort(), spdlog::to_hex(data));
                            spdlog::error("[Server] {}", e.what());
                        }
                    });
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
                const auto pkg = *(reinterpret_cast<const ClipShareHeartbeatPackage*>(datagramData.data()));
                if (pkg.valid())
                {
                    if (pkg.command == ClipShareHeartbeatPackage::Heartbeat)
                    {
                        spdlog::info("[Heartbeat] Heartbeat from ({}:{})", datagram.senderAddress().toString(), datagram.senderPort());
                        heartbeatBroadcaster.writeDatagram(reinterpret_cast<const char*>(&ClipShareHeartbeatPackage_Response)
                            , sizeof(ClipShareHeartbeatPackage), datagram.senderAddress(), datagram.senderPort());
                        // todo send device info to it / ignore local
                    }
                    else if (pkg.command == ClipShareHeartbeatPackage::Response)
                    {
                        spdlog::info("[Heartbeat] Response from {}:{}", datagram.senderAddress().toString(), datagram.senderPort());
                    }
                }
                else
                {
                    spdlog::warn("[Invalid] {}:{} heartbeat package [magic = 0x{:xns} command = 0x{:x}]"
                        , datagram.senderAddress().toString(), datagram.senderPort()
                        , spdlog::to_hex(pkg.magic, pkg.magic + 4), pkg.command);
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
    connect(&heartbeatTimer, &QTimer::timeout, this, &ClipShareWindow::broadcastHeartbeat);

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
            package.receiver = QHostAddress(config.heartbeatMulticastGroupHost).toString();
            auto data = QByteArray::fromStdString(nlohmann::json{ package }.dump());
            for (auto conn : clientSockets)
                conn->write(data.data(), data.length());
        });
    systemTrayIcon.show();
}

void ClipShareWindow::broadcastHeartbeat()
{
    heartbeatBroadcaster.writeDatagram(reinterpret_cast<const char*>(&ClipShareHeartbeatPackage_Heartbeat), sizeof(ClipShareHeartbeatPackage), QHostAddress(config.heartbeatMulticastGroupHost), config.heartbeatPort);
}

void ClipShareWindow::handlePackageReceived(const QTcpSocket*conn, const ClipSharePackage& package)
{
    spdlog::info("[Server] Receive: {}, from {}:{} {}", package.mimeFormats.join("; ")
        , conn->peerAddress().toString(), conn->peerPort(), package.sender);
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
