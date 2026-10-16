#include "app/mainwindow.h"

#include "app/eqpage.h"
#include "app/touchpage.h"
#include "sim/simulateddevice.h"

#include <QApplication>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothLocalDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QString kDemoAddress = QStringLiteral("00:00:00:00:00:00");
const QRegularExpression kMacPattern(QStringLiteral("([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})"));

bool looksLikeMoondrop(const QString &name)
{
    return name.contains(QLatin1String("moondrop"), Qt::CaseInsensitive)
           || name.contains(QLatin1String("space travel"), Qt::CaseInsensitive);
}

QLabel *valueLabel()
{
    auto *label = new QLabel(QStringLiteral("—"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

} // namespace

MainWindow::MainWindow(const Options &options, QWidget *parent)
    : QMainWindow(parent)
    , m_options(options)
{
    setWindowTitle(tr("Moondrop Link (unofficial)"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("audio-headphones")));
    resize(760, 720);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_connection == Connection::Disconnected)
            startConnection(true);
    });

    if (!m_options.demo) {
        m_localDevice = new QBluetoothLocalDevice(this);
        m_discovery = new QBluetoothDeviceDiscoveryAgent(this);
        connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &MainWindow::addDevice);
        connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::finished, this, &MainWindow::onScanFinished);
        connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::canceled, this, &MainWindow::onScanFinished);
        connect(m_discovery, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
                [this](QBluetoothDeviceDiscoveryAgent::Error) {
                    showStatus(tr("Scan failed: %1").arg(m_discovery->errorString()));
                    onScanFinished();
                });
    }

    connect(&m_link, &BluetoothLink::connected, this, &MainWindow::onLinkConnected);
    connect(&m_link, &BluetoothLink::disconnected, this, &MainWindow::onLinkDisconnected);
    connect(&m_link, &BluetoothLink::failed, this, &MainWindow::onLinkFailed);
    connect(&m_link, &BluetoothLink::progress, this, [this](const QString &message) {
        setConnection(Connection::Connecting, message);
    });

    connect(&m_session, &gaia::Session::ready, this, &MainWindow::onSessionReady);
    connect(&m_session, &gaia::Session::failed, this, &MainWindow::onSessionFailed);
    connect(&m_session, &gaia::Session::deviceInfoChanged, this, &MainWindow::refreshOverview);
    connect(&m_session, &gaia::Session::earbudPositionChanged, this, &MainWindow::refreshOverview);
    connect(&m_session, &gaia::Session::handoverAnnounced, this, &MainWindow::onHandover);
    connect(&m_session, &gaia::Session::trafficLogged, this, &MainWindow::appendLog);
    connect(&m_session, &gaia::Session::commandFailed, this, [this](const QString &what, const QString &reason) {
        showStatus(tr("%1 failed: %2").arg(what, reason));
    });
    connect(&m_session, &gaia::Session::stateChanged, this, [this](gaia::Session::State state) {
        if (state == gaia::Session::State::Handshaking)
            setConnection(Connection::Connecting, tr("Talking to the earbuds…"));
        else if (state == gaia::Session::State::Discovering)
            setConnection(Connection::Connecting, tr("Reading supported features…"));
    });

    buildUi();
    buildMenus();
    setConnection(Connection::Disconnected);
    setPagesReady(false);

    QSettings settings;
    if (m_options.demo) {
        addAddress(kDemoAddress, tr("Simulated Space Travel (demo)"));
        QTimer::singleShot(0, this, [this] { startConnection(false); });
        return;
    }

    if (!m_localDevice->isValid()) {
        showStatus(tr("No Bluetooth adapter found."));
    } else {
        for (const QBluetoothAddress &address : m_localDevice->connectedDevices())
            addAddress(address.toString(), tr("Connected device"));
    }
    const QString last = settings.value(QStringLiteral("lastAddress")).toString();
    if (!last.isEmpty())
        addAddress(last, settings.value(QStringLiteral("lastName"), tr("Last used")).toString());
    if (!m_options.address.isEmpty()) {
        addAddress(m_options.address.toUpper(), tr("From command line"));
        m_deviceCombo->setCurrentIndex(m_deviceCombo->findData(m_options.address.toUpper()));
        QTimer::singleShot(0, this, [this] { startConnection(false); });
    } else if (!last.isEmpty()) {
        m_deviceCombo->setCurrentIndex(m_deviceCombo->findData(last));
        if (m_autoConnectAction->isChecked())
            QTimer::singleShot(0, this, [this] { startConnection(true); });
    }
    if (m_localDevice->isValid())
        QTimer::singleShot(0, this, &MainWindow::toggleScan);
}

MainWindow::~MainWindow()
{
    m_session.stop();
    m_link.disconnectFrom();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);

    auto *deviceRow = new QHBoxLayout;
    deviceRow->addWidget(new QLabel(tr("Earbuds:")));
    m_deviceCombo = new QComboBox;
    m_deviceCombo->setEditable(!m_options.demo);
    m_deviceCombo->setInsertPolicy(QComboBox::NoInsert);
    m_deviceCombo->setMinimumContentsLength(28);
    m_deviceCombo->setToolTip(tr("Pick paired earbuds, or type a Bluetooth address (AA:BB:CC:DD:EE:FF)."));
    deviceRow->addWidget(m_deviceCombo, 1);
    m_scanButton = new QPushButton(tr("Scan"));
    m_scanButton->setEnabled(!m_options.demo);
    deviceRow->addWidget(m_scanButton);
    m_connectButton = new QPushButton(tr("Connect"));
    deviceRow->addWidget(m_connectButton);
    layout->addLayout(deviceRow);

    m_connectionLabel = new QLabel;
    m_connectionLabel->setWordWrap(true);
    layout->addWidget(m_connectionLabel);

    m_tabs = new QTabWidget;
    m_touchPage = new TouchPage(&m_session);
    m_eqPage = new EqPage(&m_session);
    m_tabs->addTab(buildOverviewTab(), tr("Overview"));
    m_tabs->addTab(m_touchPage, tr("Touch controls"));
    m_tabs->addTab(m_eqPage, tr("Equaliser"));
    m_tabs->addTab(buildLogTab(), tr("Traffic log"));
    layout->addWidget(m_tabs, 1);
    setCentralWidget(central);

    connect(m_scanButton, &QPushButton::clicked, this, &MainWindow::toggleScan);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(m_touchPage, &TouchPage::statusMessage, this, &MainWindow::showStatus);
    connect(m_eqPage, &EqPage::statusMessage, this, &MainWindow::showStatus);
}

QWidget *MainWindow::buildOverviewTab()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    m_infoName = valueLabel();
    m_infoAddress = valueLabel();
    m_infoLink = valueLabel();
    m_infoApi = valueLabel();
    m_infoFrame = valueLabel();
    m_infoFirmware = valueLabel();
    m_infoSerial = valueLabel();
    m_infoVariant = valueLabel();
    m_infoPrimary = valueLabel();
    m_infoFeatures = valueLabel();
    form->addRow(tr("Name:"), m_infoName);
    form->addRow(tr("Address:"), m_infoAddress);
    form->addRow(tr("Connection:"), m_infoLink);
    form->addRow(tr("Firmware version:"), m_infoFirmware);
    form->addRow(tr("Serial number:"), m_infoSerial);
    form->addRow(tr("Variant:"), m_infoVariant);
    form->addRow(tr("Primary earbud:"), m_infoPrimary);
    form->addRow(tr("GAIA API:"), m_infoApi);
    form->addRow(tr("Frame protocol:"), m_infoFrame);
    form->addRow(tr("Features:"), m_infoFeatures);

    auto *refresh = new QPushButton(tr("Refresh"));
    connect(refresh, &QPushButton::clicked, this, [this] {
        if (m_session.state() == gaia::Session::State::Ready) {
            m_session.requestDeviceInfo();
            m_session.requestEarbudPosition();
        }
    });
    form->addRow(QString(), refresh);
    return page;
}

QWidget *MainWindow::buildLogTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    layout->addWidget(m_log, 1);

    auto *buttons = new QHBoxLayout;
    m_logRaw = new QCheckBox(tr("Show raw frames"));
    m_logRaw->setChecked(true);
    auto *clear = new QPushButton(tr("Clear"));
    auto *save = new QPushButton(tr("Save…"));
    buttons->addWidget(m_logRaw);
    buttons->addStretch();
    buttons->addWidget(clear);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    connect(clear, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
    connect(save, &QPushButton::clicked, this, &MainWindow::saveLog);
    return page;
}

void MainWindow::buildMenus()
{
    QSettings settings;
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    QMenu *connection = menuBar()->addMenu(tr("&Connection"));
    connection->addAction(tr("RFCOMM channel…"), this, &MainWindow::chooseChannel);
    m_autoConnectAction = connection->addAction(tr("Connect to last earbuds on start"));
    m_autoConnectAction->setCheckable(true);
    m_autoConnectAction->setChecked(settings.value(QStringLiteral("autoConnect"), false).toBool());
    connect(m_autoConnectAction, &QAction::toggled, this,
            [](bool on) { QSettings().setValue(QStringLiteral("autoConnect"), on); });
    m_autoReconnectAction = connection->addAction(tr("Reconnect when the connection drops"));
    m_autoReconnectAction->setCheckable(true);
    m_autoReconnectAction->setChecked(settings.value(QStringLiteral("autoReconnect"), true).toBool());
    connect(m_autoReconnectAction, &QAction::toggled, this,
            [](bool on) { QSettings().setValue(QStringLiteral("autoReconnect"), on); });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About"), this, &MainWindow::showAbout);
}

void MainWindow::addDevice(const QBluetoothDeviceInfo &info)
{
    if (!(info.coreConfigurations() & QBluetoothDeviceInfo::BaseRateCoreConfiguration))
        return;  // GAIA runs over classic Bluetooth RFCOMM
    addAddress(info.address().toString(), info.name());
}

void MainWindow::addAddress(const QString &address, const QString &name)
{
    if (address.isEmpty())
        return;
    QString label = name.isEmpty() ? address : QStringLiteral("%1 — %2").arg(name, address);
    if (m_localDevice && m_localDevice->isValid()
        && m_localDevice->pairingStatus(QBluetoothAddress(address)) != QBluetoothLocalDevice::Unpaired)
        label += tr(" (paired)");

    const int existing = m_deviceCombo->findData(address);
    if (existing >= 0) {
        // Replace placeholder names once discovery reports the real one.
        if (!name.isEmpty() && name != tr("Connected device") && name != tr("Last used"))
            m_deviceCombo->setItemText(existing, label);
        return;
    }
    const bool wasEmpty = m_deviceCombo->count() == 0;
    const bool currentIsMoondrop = looksLikeMoondrop(m_deviceCombo->currentText());
    if (looksLikeMoondrop(name))
        m_deviceCombo->insertItem(0, label, address);  // likely candidates first
    else
        m_deviceCombo->addItem(label, address);
    if (wasEmpty || (looksLikeMoondrop(name) && !currentIsMoondrop && m_connection == Connection::Disconnected))
        m_deviceCombo->setCurrentIndex(m_deviceCombo->findData(address));
}

QString MainWindow::selectedAddress() const
{
    const int index = m_deviceCombo->currentIndex();
    if (index >= 0 && m_deviceCombo->currentText() == m_deviceCombo->itemText(index))
        return m_deviceCombo->itemData(index).toString();
    const auto match = kMacPattern.match(m_deviceCombo->currentText());
    return match.hasMatch() ? match.captured(1).toUpper() : QString();
}

void MainWindow::toggleScan()
{
    if (!m_discovery)
        return;
    if (m_discovery->isActive()) {
        m_discovery->stop();
        return;
    }
    m_discovery->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
    m_scanButton->setText(tr("Stop scan"));
    showStatus(tr("Scanning for Bluetooth devices…"));
}

void MainWindow::onScanFinished()
{
    m_scanButton->setText(tr("Scan"));
    showStatus(tr("Scan finished."));
}

void MainWindow::toggleConnection()
{
    if (m_connection == Connection::Disconnected)
        startConnection(false);
    else
        disconnectDevice();
}

void MainWindow::startConnection(bool automatic)
{
    m_reconnectTimer->stop();
    m_userDisconnect = false;
    m_autoAttempt = automatic;
    if (!automatic)
        m_reconnectAttempts = 0;
    const QString address = selectedAddress();
    if (address.isEmpty()) {
        if (!automatic)
            QMessageBox::information(this, windowTitle(), tr("Pick your earbuds from the list first."));
        return;
    }
    m_connectedAddress = address;
    const int index = m_deviceCombo->findData(address);
    m_connectedName = index >= 0 ? m_deviceCombo->itemText(index).section(QStringLiteral(" — "), 0, 0) : address;
    m_eqPage->setDeviceKey(address);

    if (m_options.demo) {
        setConnection(Connection::Connecting, tr("Starting the simulated earbuds…"));
        if (m_simulated)
            m_simulated->deleteLater();
        m_simulated = new sim::SimulatedDevice(this);
        m_simulated->open(QIODevice::ReadWrite | QIODevice::Unbuffered);
        m_session.start(m_simulated);
        return;
    }

    if (m_discovery && m_discovery->isActive())
        m_discovery->stop();  // inquiry slows down RFCOMM connects
    QSettings settings;
    const int channel = m_options.channel >= 0 ? m_options.channel : settings.value(QStringLiteral("channel"), 0).toInt();
    setConnection(Connection::Connecting, tr("Connecting to %1…").arg(address));
    m_link.connectTo(QBluetoothAddress(address), static_cast<quint16>(channel));
}

void MainWindow::disconnectDevice()
{
    m_userDisconnect = true;
    m_reconnectTimer->stop();
    m_session.stop();
    if (m_simulated) {
        m_simulated->close();
        m_simulated->deleteLater();
    }
    m_link.disconnectFrom();
    setPagesReady(false);
    setConnection(Connection::Disconnected, tr("Disconnected."));
}

void MainWindow::onLinkConnected()
{
    setConnection(Connection::Connecting, tr("Connected via %1. Talking to the earbuds…").arg(m_link.serviceDescription()));
    m_session.start(m_link.device());
}

void MainWindow::onLinkDisconnected()
{
    const bool wasReady = m_connection == Connection::Connected;
    m_session.stop();
    setPagesReady(false);
    setConnection(Connection::Disconnected, tr("The earbuds disconnected."));
    if (!m_userDisconnect && wasReady && m_autoReconnectAction->isChecked()) {
        m_reconnectAttempts = 0;
        scheduleReconnect(m_handoverDelayMs > 0 ? m_handoverDelayMs : 3000);
    }
    m_handoverDelayMs = 0;
}

void MainWindow::onLinkFailed(const QString &reason)
{
    setConnection(Connection::Disconnected, reason);
    if (m_autoAttempt) {
        if (m_reconnectAttempts > 0)
            scheduleReconnect(5000);  // gives up after three attempts
        return;
    }
    QMessageBox::warning(this, tr("Connection failed"), reason);
}

void MainWindow::onSessionReady()
{
    m_autoAttempt = false;
    m_reconnectAttempts = 0;
    if (!m_options.demo) {
        QSettings settings;
        settings.setValue(QStringLiteral("lastAddress"), m_connectedAddress);
        settings.setValue(QStringLiteral("lastName"), m_connectedName);
    }
    setConnection(Connection::Connected);
    setPagesReady(true);
    m_session.requestEarbudPosition();
    refreshOverview();
}

void MainWindow::onSessionFailed(const QString &reason)
{
    m_userDisconnect = true;  // a protocol failure is not worth reconnecting for
    m_link.disconnectFrom();
    setPagesReady(false);
    setConnection(Connection::Disconnected, reason);
    if (!m_autoAttempt)
        QMessageBox::warning(this, tr("The earbuds did not respond"), reason);
}

void MainWindow::onHandover(bool isStatic, int delaySeconds)
{
    showStatus(isStatic ? tr("The earbuds are swapping roles; the connection will drop for a moment.")
                        : tr("The earbuds are swapping roles."));
    if (isStatic)
        m_handoverDelayMs = (delaySeconds + 2) * 1000;
}

void MainWindow::scheduleReconnect(int delayMs)
{
    if (m_reconnectAttempts >= 3) {
        showStatus(tr("Could not reconnect. Press Connect to try again."));
        return;
    }
    ++m_reconnectAttempts;
    m_reconnectTimer->start(delayMs);
    showStatus(tr("Reconnecting in %1 s (attempt %2 of 3)…").arg(delayMs / 1000).arg(m_reconnectAttempts));
}

void MainWindow::setConnection(Connection state, const QString &text)
{
    m_connection = state;
    switch (state) {
    case Connection::Disconnected:
        m_connectButton->setText(tr("Connect"));
        m_connectionLabel->setText(text.isEmpty() ? tr("Not connected.") : text);
        break;
    case Connection::Connecting:
        m_connectButton->setText(tr("Cancel"));
        m_connectionLabel->setText(text);
        break;
    case Connection::Connected: {
        m_connectButton->setText(tr("Disconnect"));
        const auto &info = m_session.deviceInfo();
        m_connectionLabel->setText(info.firmwareVersion.isEmpty()
                                       ? tr("Connected to %1.").arg(m_connectedName)
                                       : tr("Connected to %1, firmware %2.").arg(m_connectedName, info.firmwareVersion));
        break;
    }
    }
    m_deviceCombo->setEnabled(state == Connection::Disconnected);
}

void MainWindow::setPagesReady(bool ready)
{
    m_touchPage->setSessionReady(ready);
    m_eqPage->setSessionReady(ready);
    m_tabs->setTabEnabled(1, ready && m_session.hasFeature(gaia::Feature::Earbud));
    m_tabs->setTabEnabled(2, ready && m_session.hasFeature(gaia::Feature::MusicProcessing));
    if (!ready)
        refreshOverview();
}

void MainWindow::refreshOverview()
{
    const bool ready = m_session.state() == gaia::Session::State::Ready;
    const auto &info = m_session.deviceInfo();
    auto orDash = [](const QString &s) { return s.isEmpty() ? QStringLiteral("—") : s; };

    m_infoName->setText(ready ? orDash(m_connectedName) : QStringLiteral("—"));
    m_infoAddress->setText(ready ? orDash(m_connectedAddress) : QStringLiteral("—"));
    m_infoLink->setText(!ready ? QStringLiteral("—")
                        : m_options.demo ? tr("Simulated device")
                                         : tr("RFCOMM, %1").arg(m_link.serviceDescription()));
    m_infoFirmware->setText(ready ? orDash(info.firmwareVersion) : QStringLiteral("—"));
    m_infoSerial->setText(ready ? orDash(info.serialNumber) : QStringLiteral("—"));
    m_infoVariant->setText(ready ? orDash(info.variantName) : QStringLiteral("—"));
    QString primary = QStringLiteral("—");
    if (ready && m_session.primaryEarbud() == gaia::Session::EarbudPosition::Left)
        primary = tr("Left");
    else if (ready && m_session.primaryEarbud() == gaia::Session::EarbudPosition::Right)
        primary = tr("Right");
    m_infoPrimary->setText(primary);
    m_infoApi->setText(ready ? tr("v%1.%2 (device reports GAIA version %3)")
                                   .arg(info.apiMajor)
                                   .arg(info.apiMinor)
                                   .arg(info.gaiaVersion >= 0 ? QString::number(info.gaiaVersion) : tr("unknown"))
                             : QStringLiteral("—"));
    m_infoFrame->setText(ready ? tr("version %1").arg(m_session.frameVersion()) : QStringLiteral("—"));
    QStringList features;
    for (const gaia::FeatureInfo &f : info.features)
        features << tr("%1 v%2").arg(gaia::featureName(f.id)).arg(f.version);
    m_infoFeatures->setText(ready && !features.isEmpty() ? features.join(QStringLiteral(", ")) : QStringLiteral("—"));

    if (m_connection == Connection::Connected)
        setConnection(Connection::Connected);
}

void MainWindow::appendLog(bool outgoing, const QByteArray &raw, const QString &summary)
{
    QString line = QStringLiteral("%1 %2 %3")
                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                            outgoing ? QStringLiteral("→") : QStringLiteral("←"), summary);
    if (m_logRaw->isChecked())
        line += QStringLiteral("\n               [%1]").arg(gaia::toHex(raw));
    m_log->appendPlainText(line);
}

void MainWindow::saveLog()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save traffic log"),
                                                      QStringLiteral("moondrop-gaia-log.txt"),
                                                      tr("Text files (*.txt)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save traffic log"), file.errorString());
        return;
    }
    QTextStream(&file) << m_log->toPlainText() << '\n';
    showStatus(tr("Saved %1").arg(path));
}

void MainWindow::chooseChannel()
{
    QSettings settings;
    bool ok = false;
    const int channel = QInputDialog::getInt(
        this, tr("RFCOMM channel"),
        tr("RFCOMM channel to use. 0 finds the GAIA service automatically, which is right for almost "
           "everyone."),
        settings.value(QStringLiteral("channel"), 0).toInt(), 0, 30, 1, &ok);
    if (ok)
        settings.setValue(QStringLiteral("channel"), channel);
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About"),
        tr("<p><b>Moondrop Link (unofficial)</b> %1</p>"
           "<p>Controls Moondrop Space Travel earbuds from Linux: touch controls, EQ presets, custom EQ and "
           "device information.</p>"
           "<p>It speaks Qualcomm GAIA v3 over Bluetooth RFCOMM, following the protocol used by the "
           "MOONDROP Link Android app 1.3.12. Not affiliated with or endorsed by Moondrop.</p>")
            .arg(QApplication::applicationVersion()));
}

void MainWindow::showStatus(const QString &message)
{
    statusBar()->showMessage(message, 8000);
}
