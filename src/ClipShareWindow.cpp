#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QBuffer>
#include <QHostInfo>
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
                connect(conn, &QTcpSocket::disconnected, [=]
                    {
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

            if (mimeData->hasImage()) {
                auto imageData = mimeData->imageData().value<QImage>();
                spdlog::info("Image[{}x{}]", imageData.width(), imageData.height());
                systemTrayIcon.showMessage("Image", "", QIcon(QPixmap::fromImage(imageData)));

                QByteArray imageByteArray;
                QBuffer buf(&imageByteArray);
                imageData.save(&buf, "png");
                imageByteArray = "data:image/png;base64,"+imageByteArray.toBase64();
                spdlog::info("Image base64[{}bytes]", imageByteArray.size());


                ClipSharePackage heartbeatPkg;
                heartbeatPkg.data = imageByteArray;
                heartbeatPkg.type = ClipSharePackage::ClipSharePackageImage;
                heartbeatPkg.sender = QHostInfo::localHostName();
                heartbeatPkg.receiver = QHostAddress(config.heartbeatMulticastGroupHost).toString();
                auto data = QByteArray::fromStdString(nlohmann::json{ heartbeatPkg }.dump());
                heartbeatBroadcaster.writeDatagram(data.data(), data.length(), QHostAddress(config.heartbeatMulticastGroupHost), config.heartbeatPort);
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

                ClipSharePackage heartbeatPkg;
                heartbeatPkg.data = content.toLocal8Bit().toBase64();
                heartbeatPkg.type = ClipSharePackage::ClipSharePackageRichText;
                heartbeatPkg.sender = QHostInfo::localHostName();
                heartbeatPkg.receiver = QHostAddress(config.heartbeatMulticastGroupHost).toString();
                auto data = QByteArray::fromStdString(nlohmann::json{ heartbeatPkg }.dump());
                heartbeatBroadcaster.writeDatagram(data.data(), data.length(), QHostAddress(config.heartbeatMulticastGroupHost), config.heartbeatPort);
            }
            else if (mimeData->hasText()) {
                auto text = mimeData->text();
                if (text.startsWith("data:image/png;base64,"))
                {
                    auto data = text.split(",").back();
                    QImage image;
                    image.loadFromData(QByteArray::fromBase64(data.toLocal8Bit()));
                    image.save("test.png");
                    spdlog::info("Image[{}x{}] saved", image.width(), image.height());
                }
                spdlog::info("Plain Text[{}]: {}", mimeData->text().count(), mimeData->text());
                systemTrayIcon.showMessage("Plain Text", text);

                ClipSharePackage heartbeatPkg;
                heartbeatPkg.data = text.toLocal8Bit().toBase64();
                heartbeatPkg.type = ClipSharePackage::ClipSharePackagePlainText;
                heartbeatPkg.sender = QHostInfo::localHostName();
                heartbeatPkg.receiver = QHostAddress(config.heartbeatMulticastGroupHost).toString();
                auto data = QByteArray::fromStdString(nlohmann::json{ heartbeatPkg }.dump());
                heartbeatBroadcaster.writeDatagram(data.data(), data.length(), QHostAddress(config.heartbeatMulticastGroupHost), config.heartbeatPort);
            }
            else {
                systemTrayIcon.showMessage("Cannot display data", QString{"Formats:(%1) \n Content:(%2)"}.arg(mimeData->formats().join("; "), mimeData->text()));
            }
        });
    systemTrayIcon.show();
}

void ClipShareWindow::broadcastHeartbeat()
{
    heartbeatBroadcaster.writeDatagram(reinterpret_cast<const char*>(&ClipShareHeartbeatPackage_Heartbeat), sizeof(ClipShareHeartbeatPackage), QHostAddress(config.heartbeatMulticastGroupHost), config.heartbeatPort);
}

void ClipShareWindow::handlePackageReceived(const QTcpSocket*conn, const ClipSharePackage& package)
{
    ClipSharePackage p = nlohmann::json::parse(R"({"data":"","receiver":"","sender":"","type":"ClipSharePackagePlainText"})");
    spdlog::info("{} type {} should be {}", nlohmann::json(package).dump(), package.type, p.type);
    switch (package.type)
    {
    case ClipSharePackage::ClipSharePackageNull:
        spdlog::info("[Server] Receive Null: {}, from {}:{} {}", package.data, conn->peerAddress().toString(), conn->peerPort(), package.sender);
        break;
    case ClipSharePackage::ClipSharePackageImage:
        spdlog::info("[Server] Receive Image: {}, from {}:{} {}", package.data, conn->peerAddress().toString(), conn->peerPort(), package.sender);
        break;
    case ClipSharePackage::ClipSharePackagePlainText:
        spdlog::info("[Server] Receive PlainText: {}, from {}:{} {}", package.data, conn->peerAddress().toString(), conn->peerPort(), package.sender);
        break;
    case ClipSharePackage::ClipSharePackageRichText:
        spdlog::info("[Server] Receive RichText: {}, from {}:{} {}", package.data, conn->peerAddress().toString(), conn->peerPort(), package.sender);
        break;
    case ClipSharePackage::ClipSharePackageCustom:
        spdlog::info("[Server] Receive Custom: {}, from {}:{} {}", package.data, conn->peerAddress().toString(), conn->peerPort(), package.sender);
        break;
    default:
        break;
    }
}

bool ClipShareWindow::isLocalHost(QHostAddress addr)
{
    return QNetworkInterface::allAddresses().contains(addr);
}
