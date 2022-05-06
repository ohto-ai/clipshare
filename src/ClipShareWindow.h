#pragma once

#include <QtWidgets/QMainWindow>
#include <QSystemTrayIcon>
#include <QUdpSocket>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QMetaEnum>

#include "Utils_JsonConvert.h"
#include "ui_ClipShareWindow.h"

class QClipboard;

struct ClipSharePackage
{
    enum ClipSharePackageType{
        ClipSharePackageNull,
    	ClipSharePackageHeartBeat,
    	ClipSharePackageImage,
        ClipSharePackagePlainText,
        ClipSharePackageRichText,
        ClipSharePackageCustom
    };

    ClipSharePackageType type {ClipSharePackageType::ClipSharePackageNull };
    QByteArray data;
    QString senderIPv4Address;
    QString receiverIPv4Address;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ClipSharePackage, type, data, senderIPv4Address, receiverIPv4Address);
};

NLOHMANN_JSON_SERIALIZE_ENUM(ClipSharePackage::ClipSharePackageType, {
    {ClipSharePackage::ClipSharePackageNull, nullptr},
    {ClipSharePackage::ClipSharePackageHeartBeat, "ClipSharePackageHeartBeat"},
    {ClipSharePackage::ClipSharePackageImage, "ClipSharePackageImage"},
    {ClipSharePackage::ClipSharePackagePlainText, "ClipSharePackagePlainText"},
    {ClipSharePackage::ClipSharePackageRichText, "ClipSharePackageRichText"},
    {ClipSharePackage::ClipSharePackageCustom, "ClipSharePackageCustom"},
});


struct ClipShareConfig
{
    int heartBeatPort{ 41688 };
    int heartBeatInterval{ 4000 };

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ClipShareConfig, heartBeatPort, heartBeatInterval);
};


class ClipShareWindow : public QMainWindow
{
    Q_OBJECT

public:
    ClipShareWindow(QWidget *parent = Q_NULLPTR);

public slots:

protected:
    ClipShareConfig config{};

    QSystemTrayIcon systemTrayIcon{ this };
    QUdpSocket broadcastSender{ this };
    QUdpSocket broadcastReceiver{ this };

private:
    Ui::clipshareClass ui{};
};
