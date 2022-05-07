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
    broadcastReceiver.joinMulticastGroup(QHostAddress(config.multicastGroupHost));
    connect(&broadcastReceiver, &QUdpSocket::readyRead, [=]{
        while (broadcastReceiver.hasPendingDatagrams()) {
            auto networkDatagram = broadcastReceiver.receiveDatagram();
            auto datagramData = networkDatagram.data();

            spdlog::info("Data receive [{}bytes] {}:{}=>{}:{}: {}", datagramData.length(), networkDatagram.senderAddress().toString().toStdString(), networkDatagram.senderPort()
                , networkDatagram.destinationAddress().toString().toStdString(), networkDatagram.destinationPort(), datagramData.toStdString());
        }
        spdlog::info("==================================");
    });

    broadcastSender.bind(QHostAddress(QHostAddress::AnyIPv4));
    broadcastSender.joinMulticastGroup(QHostAddress(config.multicastGroupHost));
    QTimer::singleShot(config.heartBeatInterval, [=]
        {
            ClipSharePackage heartBeatPkg;
            heartBeatPkg.type = ClipSharePackage::ClipSharePackageHeartBeat;
            heartBeatPkg.sender = QHostInfo::localHostName();
            heartBeatPkg.receiver = QHostAddress(config.multicastGroupHost).toString();
            auto data = QByteArray::fromStdString(nlohmann::json{ heartBeatPkg }.dump());

            broadcastSender.writeDatagram(data.data(), data.length(), QHostAddress(config.multicastGroupHost), config.heartBeatPort);
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


                ClipSharePackage heartBeatPkg;
                heartBeatPkg.data = imageByteArray;
                heartBeatPkg.type = ClipSharePackage::ClipSharePackageImage;
                heartBeatPkg.sender = QHostInfo::localHostName();
                heartBeatPkg.receiver = QHostAddress(config.multicastGroupHost).toString();
                auto data = QByteArray::fromStdString(nlohmann::json{ heartBeatPkg }.dump());
                broadcastSender.writeDatagram(data.data(), data.length(), QHostAddress(config.multicastGroupHost), config.heartBeatPort);
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

                ClipSharePackage heartBeatPkg;
                heartBeatPkg.data = content.toLocal8Bit();
                heartBeatPkg.type = ClipSharePackage::ClipSharePackageRichText;
                heartBeatPkg.sender = QHostInfo::localHostName();
                heartBeatPkg.receiver = QHostAddress(config.multicastGroupHost).toString();
                auto data = QByteArray::fromStdString(nlohmann::json{ heartBeatPkg }.dump());
                broadcastSender.writeDatagram(data.data(), data.length(), QHostAddress(config.multicastGroupHost), config.heartBeatPort);
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

                ClipSharePackage heartBeatPkg;
                heartBeatPkg.data = text.toLocal8Bit();
                heartBeatPkg.type = ClipSharePackage::ClipSharePackagePlainText;
                heartBeatPkg.sender = QHostInfo::localHostName();
                heartBeatPkg.receiver = QHostAddress(config.multicastGroupHost).toString();
                auto data = QByteArray::fromStdString(nlohmann::json{ heartBeatPkg }.dump());
                broadcastSender.writeDatagram(data.data(), data.length(), QHostAddress(config.multicastGroupHost), config.heartBeatPort);
            }
            else {
                systemTrayIcon.showMessage("Cannot display data", QString{"Formats:(%1) \n Content:(%2)"}.arg(mimeData->formats().join("; "), mimeData->text()));
            }
        });
    systemTrayIcon.show();
}
