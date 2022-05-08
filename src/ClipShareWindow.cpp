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

    // setup heartbeat respond
    heartbeatBroadcastReceiver.bind(QHostAddress::AnyIPv4, config.heartbeatPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    heartbeatBroadcastReceiver.joinMulticastGroup(QHostAddress(config.heartbeatMulticastGroupHost));
    connect(&heartbeatBroadcastReceiver, &QUdpSocket::readyRead, [=]{
        while (heartbeatBroadcastReceiver.hasPendingDatagrams()) {
            auto datagram = heartbeatBroadcastReceiver.receiveDatagram();
            auto datagramData = datagram.data();

            spdlog::trace("[Heartbeat] Receive [{}bytes] {}:{}=>{}:{} {:a}", datagramData.length()
                , datagram.senderAddress().toString().toStdString(), datagram.senderPort()
                , datagram.destinationAddress().toString().toStdString(), datagram.destinationPort(), spdlog::to_hex(datagramData));

            if (datagramData.size() == sizeof(ClipShareHeartbeatPackage))
            {
                const auto pkg = *(reinterpret_cast<const ClipShareHeartbeatPackage*>(datagramData.data()));
                if (pkg.valid())
                {
                    if (!isLocalHost(datagram.senderAddress()))
                    {
                        if (pkg.command == ClipShareHeartbeatPackage::Heartbeat)
                        {
                            spdlog::info("[Heartbeat] Heartbeat from {}:{}", datagram.senderAddress().toString().toStdString(), datagram.senderPort());
                            heartbeatBroadcastSender.writeDatagram(reinterpret_cast<const char*>(&ClipShareHeartbeatPackage_Respond)
                                , sizeof(ClipShareHeartbeatPackage), datagram.senderAddress(), config.heartbeatPort);
                            // todo send device info to it / ignore local
                        }
                        else if (pkg.command == ClipShareHeartbeatPackage::Respond)
                        {
                            spdlog::info("[Heartbeat] Respond from {}:{}", datagram.senderAddress().toString().toStdString(), datagram.senderPort());
                        }
                    }
                }
                else
                {
                    spdlog::warn("[Invalid] heartbeat package magic = 0x{:xns} command = 0x{:x}"
                        , spdlog::to_hex(pkg.magic, pkg.magic + 4), pkg.command);
                }
            }
            else
            {
                spdlog::warn("[Heartbeat] Incorrect heartbeat package size: {}", datagramData.size());
                spdlog::warn("[Heartbeat] Incorrect heartbeat package content(hex): {}", datagramData.toHex());
            }
        }
    });

    // setup heartbeat sender
    heartbeatBroadcastSender.bind(QHostAddress(QHostAddress::AnyIPv4));
    heartbeatBroadcastSender.joinMulticastGroup(QHostAddress(config.heartbeatMulticastGroupHost));
    heartbeatTimer.setInterval(config.heartbeatInterval);
    connect(&heartbeatTimer, &QTimer::timeout, this, &ClipShareWindow::broadcastHeartbeat);

    // start timer
    heartbeatTimer.start();

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
                heartbeatBroadcastSender.writeDatagram(data.data(), data.length(), QHostAddress(config.heartbeatMulticastGroupHost), config.heartbeatPort);
            }
            else if (mimeData->hasUrls()) {
                QStringList urlStringList;
                auto urls = mimeData->urls();
                for(int i = 0; i < urls.count(); ++i)
                {
                    spdlog::info("Urls[{}/{}]: {}", i + 1, urls.count(), urls[i].toString().toStdString());
                    urlStringList.push_back(urls[i].toString());
                }
                systemTrayIcon.showMessage("Urls", urlStringList.join("\n"));
            }
            else if (mimeData->hasHtml()) {
                auto content = mimeData->html();
                spdlog::info("Rich Text[{} <{}bytes>]: {}", mimeData->text().count(), content.size(), mimeData->text().toStdString());
                systemTrayIcon.showMessage("Rich Text:", mimeData->text());

                ClipSharePackage heartbeatPkg;
                heartbeatPkg.data = content.toLocal8Bit();
                heartbeatPkg.type = ClipSharePackage::ClipSharePackageRichText;
                heartbeatPkg.sender = QHostInfo::localHostName();
                heartbeatPkg.receiver = QHostAddress(config.heartbeatMulticastGroupHost).toString();
                auto data = QByteArray::fromStdString(nlohmann::json{ heartbeatPkg }.dump());
                heartbeatBroadcastSender.writeDatagram(data.data(), data.length(), QHostAddress(config.heartbeatMulticastGroupHost), config.heartbeatPort);
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
                spdlog::info("Plain Text[{}]: {}", mimeData->text().count(), mimeData->text().toStdString());
                systemTrayIcon.showMessage("Plain Text", text);

                ClipSharePackage heartbeatPkg;
                heartbeatPkg.data = text.toLocal8Bit();
                heartbeatPkg.type = ClipSharePackage::ClipSharePackagePlainText;
                heartbeatPkg.sender = QHostInfo::localHostName();
                heartbeatPkg.receiver = QHostAddress(config.heartbeatMulticastGroupHost).toString();
                auto data = QByteArray::fromStdString(nlohmann::json{ heartbeatPkg }.dump());
                heartbeatBroadcastSender.writeDatagram(data.data(), data.length(), QHostAddress(config.heartbeatMulticastGroupHost), config.heartbeatPort);
            }
            else {
                systemTrayIcon.showMessage("Cannot display data", QString{"Formats:(%1) \n Content:(%2)"}.arg(mimeData->formats().join("; "), mimeData->text()));
            }
        });
    systemTrayIcon.show();
}

void ClipShareWindow::broadcastHeartbeat()
{
    heartbeatBroadcastSender.writeDatagram(reinterpret_cast<const char*>(&ClipShareHeartbeatPackage_Heartbeat), sizeof(ClipShareHeartbeatPackage), QHostAddress(config.heartbeatMulticastGroupHost), config.heartbeatPort);
}

bool ClipShareWindow::isLocalHost(QHostAddress addr)
{
    return QNetworkInterface::allAddresses().contains(addr);
}
