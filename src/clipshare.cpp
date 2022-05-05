#include <QClipboard>
#include <QSystemTrayIcon>
#include <QMimeData>
#include <QUrl>
#include <QBuffer>
#include <spdlog/spdlog.h>
#include "clipshare.h"

clipshare::clipshare(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    systemTrayIcon.setIcon(QApplication::windowIcon());

    auto systemTrayMenu = new QMenu(this);
    systemTrayMenu->addAction("Exit", QApplication::instance(), &QApplication::quit);

    systemTrayIcon.setContextMenu(systemTrayMenu);
   
    connect(QApplication::clipboard(), &QClipboard::changed, [=]
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
                systemTrayIcon.showMessage("Cannot display data", mimeData->text());
            }
        });
    systemTrayIcon.show();
}
