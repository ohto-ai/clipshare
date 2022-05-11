#pragma once

#include <QtWidgets/QMainWindow>
#include <QSystemTrayIcon>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMetaEnum>
#include <QMimeData>
#include <QDateTime>
#include <QTimer>

#include "Adapter.h"
#include "ui_ClipShareWindow.h"

class QClipboard;

/// <summary>
/// Config for ClipShare
/// </summary>
struct ClipShareConfig
{
    /// <summary>
    /// Constant
    /// </summary>
    constexpr static inline auto DefaultHeartbeatMulticastGroupHost{ "239.99.115.102" };
    constexpr static inline auto DefaultHeartbeatBroadcastIntervalMSecs{ 20000 };
    constexpr static inline auto DefaultHeartbeatSuvivalTimeout{ 60000 };
    constexpr static inline auto DefaultHeatbeatPort{ 41688 };
    constexpr static inline auto DefaultPackagePort{ 41688 };
    constexpr static inline auto DefaultMimeImageType{ "png" };
    constexpr static inline auto DefaultLogLevel{ "debug" };

    /// <summary>
    /// Config
    /// </summary>
    QString heartbeatMulticastGroupHost{ DefaultHeartbeatMulticastGroupHost };
    int heartbeatBroadcastIntervalMSecs{ DefaultHeartbeatBroadcastIntervalMSecs };
    int heartbeatSuvivalTimeoutMSecs{ DefaultHeartbeatSuvivalTimeout };
    int heartbeatPort{ DefaultHeatbeatPort };
    int packagePort{ ClipShareConfig::DefaultPackagePort };
    QString logLevel{ DefaultLogLevel };

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ClipShareConfig
        , heartbeatPort
        , heartbeatBroadcastIntervalMSecs
        , heartbeatSuvivalTimeoutMSecs
        , heartbeatMulticastGroupHost
        , packagePort);
};

/// <summary>
/// Package
/// </summary>
struct ClipShareDataPackage
{
    QStringList mimeFormats;
    QByteArrayList mimeData;
    QString mimeImageType;
    QByteArray mimeImageData;

    QString sender;
    QString receiver;

    void encodeMimeData(const QMimeData*);

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ClipShareDataPackage, mimeFormats, mimeData, mimeImageType, mimeImageData, sender, receiver);
};

/// <summary>
/// hearbeat
/// </summary>
struct ClipShareHeartbeatPackage
{
    enum
    {
        Heartbeat = 0x63,
        Response = 0x73,
        BroadcastPort = 0x66,
        Offline = 0xff
    };

	std::uint8_t magic[3]{ 0x63, 0x73, 0x66 };
    std::uint8_t command { Heartbeat };
    std::uint32_t port {};

    bool valid() const
    {
        return magic[0] == 0x63 && magic[1] == 0x73 && magic[2] == 0x66
            && (command == Heartbeat
            || command == Response
            || command == BroadcastPort
            || command == Offline);
    }
};

struct ClipShareNeighbor
{
    // host info
    QString hostName;
    QHostAddress serverAddr;
    int serverPort;
};

struct NeighborDeviceInfo
{
    static inline int HeartbeatSuvivalTimeout{ ClipShareConfig::DefaultHeartbeatSuvivalTimeout };
    time_t lastOnlineMSecsSinceEpoch{ QDateTime::currentMSecsSinceEpoch() };

    QHostAddress addr;
    bool alive() const
    {
        return QDateTime::QDateTime::currentMSecsSinceEpoch() - lastOnlineMSecsSinceEpoch < HeartbeatSuvivalTimeout;
    }
};

class NeighborDeviceInfoManager
{
public:
    QList<NeighborDeviceInfo> fetchOnlineNeighborDevices()
    {
        QList<NeighborDeviceInfo> list;
        for (auto& device : neighborDevices)
        {

        }
    }
    QList<NeighborDeviceInfo> neighborDevices;
};

class ClipShareWindow : public QMainWindow
{
    Q_OBJECT

public:
    ClipShareWindow(QWidget *parent = Q_NULLPTR);
    virtual ~ClipShareWindow();

public slots:

    void broadcastHeartbeat(std::uint8_t command = ClipShareHeartbeatPackage::Heartbeat);
    void handlePackageReceived(QTcpSocket*, const ClipShareDataPackage&);
    void handleHeartbeatReceived(const QNetworkDatagram&, const ClipShareHeartbeatPackage&);
    void sendToNeighbor(QString neighbor, const ClipShareDataPackage& package);
    QString makeNeighborId(const QTcpSocket*);
    void addClientNeighborSocket(QTcpSocket*);
    void addServerNeighborSocket(QTcpSocket*);

protected:
    ClipShareConfig config{};

    QTcpServer packageReciver{ this };
    QMap<QString, QTcpSocket*> neighborClientSockets; // ip-port -> socket
    QMap<QString, QTcpSocket*> neighborServerSockets; // ip-port -> socket
    QMap<QString, ClipShareNeighbor> neighbors; // ip-prot - ClipShareNeighbor

    QSystemTrayIcon systemTrayIcon{ this };
    QUdpSocket heartbeatBroadcaster{ this };
    QTimer heartbeatTimer{ this };

    static bool isLocalHost(QHostAddress);

private:
    Ui::ClipShareWindow ui{};
};
