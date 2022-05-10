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

    friend void to_json(nlohmann::json& nlohmann_json_j, const ClipSharePackage& nlohmann_json_t)
    {
        nlohmann_json_j["mimeFormats"] = nlohmann_json_t.mimeFormats;
        nlohmann_json_j["mimeData"] = nlohmann_json_t.mimeData;
        nlohmann_json_j["mimeImageType"] = nlohmann_json_t.mimeImageType;
        nlohmann_json_j["mimeImageData"] = nlohmann_json_t.mimeImageData;
        nlohmann_json_j["sender"] = nlohmann_json_t.sender;
        nlohmann_json_j["receiver"] = nlohmann_json_t.receiver;
    }
    friend void from_json(const nlohmann::json& nlohmann_json_j, ClipSharePackage& nlohmann_json_t)
    {
        ClipSharePackage nlohmann_json_default_obj;
        nlohmann_json_t.mimeFormats = nlohmann_json_j.value("mimeFormats", nlohmann_json_default_obj.mimeFormats);
        nlohmann_json_t.mimeData = nlohmann_json_j.value("mimeData", nlohmann_json_default_obj.mimeData);
        nlohmann_json_t.mimeImageType = nlohmann_json_j.value("mimeImageType", nlohmann_json_default_obj.mimeImageType);
        nlohmann_json_t.mimeImageData = nlohmann_json_j.value("mimeImageData", nlohmann_json_default_obj.mimeImageData);
        nlohmann_json_t.sender = nlohmann_json_j.value("sender", nlohmann_json_default_obj.sender);
        nlohmann_json_t.receiver = nlohmann_json_j.value("receiver", nlohmann_json_default_obj.receiver);
    };

    //NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ClipSharePackage, mimeFormats, mimeData, mimeImageType, mimeImageData, sender, receiver);
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

struct ClipShareConfig
{
    int heartbeatPort{ 41688 };
    int heartbeatInterval{ 20000 };
    int heartbeatSuvivalTimeout{ 60000 };
    QString heartbeatMulticastGroupHost{ "239.99.115.102" };

    int packagePort{ 41688 };

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ClipShareConfig, heartbeatPort, heartbeatInterval, heartbeatMulticastGroupHost, packagePort);
};


class ClipShareWindow : public QMainWindow
{
    Q_OBJECT

public:
    ClipShareWindow(QWidget *parent = Q_NULLPTR);
    virtual ~ClipShareWindow();

public slots:

    void broadcastHeartbeat(std::uint8_t command = ClipShareHeartbeatPackage::Heartbeat);
    void handlePackageReceived(const QTcpSocket*, const ClipSharePackage&);
    void sendToNeighbor(QString neighbor, const ClipSharePackage& package);
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
