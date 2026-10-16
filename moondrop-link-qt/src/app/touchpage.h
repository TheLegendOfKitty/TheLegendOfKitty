#pragma once

#include "gaia/gaiasession.h"
#include "gaia/touchmapping.h"

#include <QWidget>

#include <array>

class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;

class TouchPage : public QWidget
{
    Q_OBJECT

public:
    explicit TouchPage(gaia::Session *session, QWidget *parent = nullptr);

    // Called when the session becomes ready or goes away.
    void setSessionReady(bool ready);

signals:
    void statusMessage(const QString &message);

private:
    struct Row
    {
        QLabel *name = nullptr;
        QComboBox *gesture = nullptr;
        QComboBox *side = nullptr;
        QLabel *fixedSide = nullptr;
        QLabel *defaultValue = nullptr;
    };

    void buildRow(QGridLayout *grid, int row, touch::Function f);
    void onCurrent(const touch::CurrentMapping &current);
    void onDefaults(const touch::Mapping &defaults);
    void onApplyFinished(bool ok, const QString &message);
    void refreshWidgets();
    void updateState();
    void apply();
    void restoreDefaults();
    void reload();

    gaia::Session *m_session;
    touch::Editor m_editor;
    std::array<Row, touch::kFunctionCount> m_rows;
    bool m_ready = false;
    bool m_busy = false;
    bool m_updating = false;

    QLabel *m_enabledLabel = nullptr;
    QLabel *m_validation = nullptr;
    QLabel *m_volumeUp = nullptr;
    QLabel *m_volumeDown = nullptr;
    QPushButton *m_reload = nullptr;
    QPushButton *m_restore = nullptr;
    QPushButton *m_apply = nullptr;
};
