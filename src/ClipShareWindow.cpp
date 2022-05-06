#include <QClipboard>
#include <QSystemTrayIcon>
#include <QMimeData>
#include <QUrl>
#include <QBuffer>
#include <QTimer>
#include <spdlog/spdlog.h>
#include <QNetworkDatagram>
#include "ClipShareWindow.h"

ClipShareWindow::ClipShareWindow(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    broadcastReceiver.bind(QHostAddress::AnyIPv4, config.heartBeatPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(&broadcastReceiver, &QUdpSocket::readyRead, [=]{
        QByteArray datagram;
        while (broadcastReceiver.hasPendingDatagrams()) {
            //datagram.resize(broadcastReceiver.pendingDatagramSize());
            //QHostAddress senderHost;
            //quint16 senderPort;
            auto pendingSize = broadcastReceiver.pendingDatagramSize();
            auto networkDatagram = broadcastReceiver.receiveDatagram(broadcastReceiver.pendingDatagramSize());
            auto datagramData = networkDatagram.data();
            
            //broadcastReceiver.readDatagram(datagram.data(), datagram.size(), &senderHost, &senderPort);
            spdlog::info("Data receive [{}bytes/{}] {}:{}=>{}:{}: {}", datagramData.length(), pendingSize, networkDatagram.senderAddress().toString().toStdString(), networkDatagram.senderPort()
                , networkDatagram.destinationAddress().toString().toStdString(), networkDatagram.destinationPort(), datagramData.toStdString());

            auto senderHost = networkDatagram.senderAddress();

            QTimer::singleShot(1000, [=]
                {
                    ClipSharePackage heartBeatPkg;
                    heartBeatPkg.type = ClipSharePackage::ClipSharePackageHeartBeat;
                    heartBeatPkg.receiverIPv4Address = senderHost.toString();
                    auto data = QByteArray::fromStdString(nlohmann::json{ heartBeatPkg }.dump());
                    spdlog::info("Data send to from {}:{}: {}", senderHost.toString().toStdString(), config.heartBeatPort, data.toStdString());

                    broadcastReceiver.writeDatagram(datagram.data(), datagram.size(), QHostAddress::Broadcast, config.heartBeatPort);
                });
        }
    });

    broadcastSender.bind(QHostAddress(QHostAddress::AnyIPv4));
    QTimer::singleShot(config.heartBeatInterval, [=]
        {
            ClipSharePackage heartBeatPkg;
            heartBeatPkg.type = ClipSharePackage::ClipSharePackageHeartBeat;
            heartBeatPkg.senderIPv4Address = "localhost";
            heartBeatPkg.receiverIPv4Address = QHostAddress(QHostAddress::Broadcast).toString();
            auto data = QByteArray::fromStdString(nlohmann::json{ heartBeatPkg }.dump());

            broadcastSender.writeDatagram(data.data(), data.length(), QHostAddress::Broadcast, config.heartBeatPort);
        });

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
            }
            else {
                systemTrayIcon.showMessage("Cannot display data", QString{"Formats:(%1) \n Content:(%2)"}.arg(mimeData->formats().join("; "), mimeData->text()));
            }
        });
    systemTrayIcon.show();
}
