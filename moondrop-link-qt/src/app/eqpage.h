#pragma once

#include "gaia/gaiasession.h"

#include <QWidget>

class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

class EqPage : public QWidget
{
    Q_OBJECT

public:
    explicit EqPage(gaia::Session *session, QWidget *parent = nullptr);

    void setSessionReady(bool ready);
    // Key used to remember preset names per pair of earbuds.
    void setDeviceKey(const QString &key);

signals:
    void statusMessage(const QString &message);

private:
    QString presetLabel(int presetId) const;
    void rebuildPresets();
    void rebuildBands();
    void updateState();
    void applySelectedPreset();
    void applyDefaultPreset();
    void renamePreset();
    void useCustomEq();
    void applyBands();
    void flattenBands();
    QVector<gaia::EqBand> bandsFromTable() const;

    gaia::Session *m_session;
    QString m_deviceKey;
    bool m_ready = false;
    bool m_presetBusy = false;
    bool m_bandsBusy = false;

    QLabel *m_eqState = nullptr;
    QListWidget *m_presetList = nullptr;
    QPushButton *m_rename = nullptr;
    QPushButton *m_applyDefault = nullptr;
    QPushButton *m_applyPreset = nullptr;

    QGroupBox *m_customBox = nullptr;
    QLabel *m_customState = nullptr;
    QTableWidget *m_bandTable = nullptr;
    QPushButton *m_reloadBands = nullptr;
    QPushButton *m_flatten = nullptr;
    QPushButton *m_useCustom = nullptr;
    QPushButton *m_applyBands = nullptr;
};
