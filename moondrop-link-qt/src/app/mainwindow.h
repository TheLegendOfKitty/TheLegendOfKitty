#pragma once

#include "app/bluetoothlink.h"
#include "gaia/gaiasession.h"

#include <QBluetoothDeviceInfo>
#include <QMainWindow>
#include <QPointer>

class EqPage;
class QAction;
class QBluetoothDeviceDiscoveryAgent;
class QBluetoothLocalDevice;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;
class TouchPage;

namespace sim {
class SimulatedDevice;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    struct Options
    {
        bool demo = false;
        QString address;      // connect to this address on start
        int channel = -1;     // -1 = use the saved setting
    };

    explicit MainWindow(const Options &options, QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    enum class Connection { Disconnected, Connecting, Connected };

    void buildUi();
    void buildMenus();
    QWidget *buildOverviewTab();
    QWidget *buildLogTab();

    void addDevice(const QBluetoothDeviceInfo &info);
    void addAddress(const QString &address, const QString &name);
    QString selectedAddress() const;
    void toggleScan();
    void onScanFinished();

    void toggleConnection();
    void startConnection(bool automatic);
    void disconnectDevice();
    void onLinkConnected();
    void onLinkDisconnected();
    void onLinkFailed(const QString &reason);
    void onSessionReady();
    void onSessionFailed(const QString &reason);
    void onHandover(bool isStatic, int delaySeconds);
    void scheduleReconnect(int delayMs);
    void setConnection(Connection state, const QString &text = {});
    void setPagesReady(bool ready);
    void refreshOverview();

    void appendLog(bool outgoing, const QByteArray &raw, const QString &summary);
    void saveLog();
    void chooseChannel();
    void showAbout();
    void showStatus(const QString &message);

    Options m_options;
    gaia::Session m_session;
    BluetoothLink m_link;
    QPointer<sim::SimulatedDevice> m_simulated;
    QBluetoothDeviceDiscoveryAgent *m_discovery = nullptr;
    QBluetoothLocalDevice *m_localDevice = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    Connection m_connection = Connection::Disconnected;
    bool m_userDisconnect = false;
    bool m_autoAttempt = false;
    int m_reconnectAttempts = 0;
    int m_handoverDelayMs = 0;
    QString m_connectedAddress;
    QString m_connectedName;

    QComboBox *m_deviceCombo = nullptr;
    QPushButton *m_scanButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QTabWidget *m_tabs = nullptr;
    TouchPage *m_touchPage = nullptr;
    EqPage *m_eqPage = nullptr;

    QLabel *m_infoName = nullptr;
    QLabel *m_infoAddress = nullptr;
    QLabel *m_infoLink = nullptr;
    QLabel *m_infoApi = nullptr;
    QLabel *m_infoFrame = nullptr;
    QLabel *m_infoFirmware = nullptr;
    QLabel *m_infoSerial = nullptr;
    QLabel *m_infoVariant = nullptr;
    QLabel *m_infoPrimary = nullptr;
    QLabel *m_infoFeatures = nullptr;

    QPlainTextEdit *m_log = nullptr;
    QCheckBox *m_logRaw = nullptr;
    QAction *m_autoConnectAction = nullptr;
    QAction *m_autoReconnectAction = nullptr;
};
