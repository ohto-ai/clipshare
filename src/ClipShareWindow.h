#pragma once

#include <QtWidgets/QMainWindow>
#include <QSystemTrayIcon>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMetaEnum>
#include <QMimeData>
#include <QTimer>

#include "Adapter.h"
#include "ui_ClipShareWindow.h"

class QClipboard;

/// <summary>
/// Package
/// </summary>
struct ClipSharePackage
{
    static constexpr auto DefaultMimeImageType{ "png" };
    QStringList mimeFormats;
    QByteArrayList mimeData;
    QString mimeImageType;
    QByteArray mimeImageData;

    QString sender;
    QString receiver;

    void encodeMimeData(const QMimeData*);

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ClipSharePackage, mimeFormats, mimeData, mimeImageType, mimeImageData, sender, receiver);
};

/// <summary>
/// hearbeat
/// </summary>
struct ClipShareHeartbeatPackage
{
    enum
    {
        Heartbeat = 0x73,
        Response = 0x66
    };

	std::uint8_t magic[4]{ 0x63, 0x73, 0x66, 0x80 };
    std::uint32_t command { Heartbeat };

    bool valid() const
    {
        return magic[0] == 0x63 && magic[1] == 0x73 && magic[2] == 0x66
            && magic[3] == 0x80 && (command == Heartbeat || command == Response);
    }

};
constexpr ClipShareHeartbeatPackage ClipShareHeartbeatPackage_Heartbeat{ { 0x63, 0x73, 0x66, 0x80 }, ClipShareHeartbeatPackage::Heartbeat };
constexpr ClipShareHeartbeatPackage ClipShareHeartbeatPackage_Response{ { 0x63, 0x73, 0x66, 0x80 }, ClipShareHeartbeatPackage::Response };


struct ClipShareNeighbor
{
    QString hostname;
};




struct ClipShareConfig
{
    int heartbeatPort{ 41688 };
    int heartbeatInterval{ 20000 };
    QString heartbeatMulticastGroupHost{ "239.99.115.102" };

    int packagePort{ 41690 };

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ClipShareConfig, heartbeatPort, heartbeatInterval, heartbeatMulticastGroupHost, packagePort);
};


class ClipShareWindow : public QMainWindow
{
    Q_OBJECT

public:
    ClipShareWindow(QWidget *parent = Q_NULLPTR);

public slots:

    void broadcastHeartbeat();
    void handlePackageReceived(const QTcpSocket*, const ClipSharePackage&);

protected:
    ClipShareConfig config{};

    QTcpServer packageReciver{ this };
    QMultiMap<QString, QTcpSocket*> clientSockets;

    QSystemTrayIcon systemTrayIcon{ this };
    QUdpSocket heartbeatBroadcaster{ this };
    QTimer heartbeatTimer{ this };

    static bool isLocalHost(QHostAddress);

private:
    Ui::ClipShareWindow ui{};
};
