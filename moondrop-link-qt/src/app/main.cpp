#include "app/mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("moondrop-link-qt"));
    QApplication::setApplicationName(QStringLiteral("moondrop-link-qt"));
    QApplication::setApplicationDisplayName(QStringLiteral("Moondrop Link (unofficial)"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("moondrop-link"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Control Moondrop Space Travel earbuds over Bluetooth (GAIA v3)."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption demo(QStringLiteral("demo"), QStringLiteral("Use simulated earbuds instead of Bluetooth."));
    QCommandLineOption address(QStringList{QStringLiteral("a"), QStringLiteral("address")},
                               QStringLiteral("Connect to the earbuds at <address> on start."),
                               QStringLiteral("address"));
    QCommandLineOption channel(QStringList{QStringLiteral("c"), QStringLiteral("channel")},
                               QStringLiteral("Use RFCOMM <channel> instead of looking up the service."),
                               QStringLiteral("channel"));
    parser.addOption(demo);
    parser.addOption(address);
    parser.addOption(channel);
    parser.process(app);

    MainWindow::Options options;
    options.demo = parser.isSet(demo);
    options.address = parser.value(address);
    if (parser.isSet(channel)) {
        bool ok = false;
        options.channel = parser.value(channel).toInt(&ok);
        if (!ok || options.channel < 0 || options.channel > 30)
            parser.showHelp(1);
    }

    MainWindow window(options);
    window.show();
    return app.exec();
}
