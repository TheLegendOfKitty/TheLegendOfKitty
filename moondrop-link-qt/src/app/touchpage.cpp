#include "app/touchpage.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

using touch::Function;
using touch::Gesture;
using touch::Side;

namespace {

// Same order as the official app's picker.
constexpr Gesture kGestureOrder[] = {Gesture::SingleTap, Gesture::DoubleTap, Gesture::TripleTap,
                                     Gesture::LongPress1s, Gesture::LongPress2s, Gesture::None};

QLabel *headerLabel(const QString &text)
{
    auto *label = new QLabel(text);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    return label;
}

} // namespace

TouchPage::TouchPage(gaia::Session *session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(tr("Choose which tap or press triggers each function. Track skips use one earbud "
                                "each; everything else works on either earbud."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_enabledLabel = new QLabel;
    layout->addWidget(m_enabledLabel);

    auto makeGroup = [this](const QString &title, std::initializer_list<Function> functions) {
        auto *box = new QGroupBox(title);
        auto *grid = new QGridLayout(box);
        grid->addWidget(headerLabel(tr("Function")), 0, 0);
        grid->addWidget(headerLabel(tr("Gesture")), 0, 1);
        grid->addWidget(headerLabel(tr("Earbud")), 0, 2);
        grid->addWidget(headerLabel(tr("Factory default")), 0, 3);
        grid->setColumnStretch(3, 1);
        int row = 1;
        for (Function f : functions)
            buildRow(grid, row++, f);
        return box;
    };

    layout->addWidget(makeGroup(tr("Media and voice"),
                                {Function::PlayPause, Function::PreviousTrack, Function::NextTrack,
                                 Function::StartVoiceAssistant, Function::CancelVoiceAssistant, Function::AncOn,
                                 Function::AncOff}));
    layout->addWidget(makeGroup(tr("Calls"), {Function::AnswerCall, Function::HangUpCall}));

    auto *fixedBox = new QGroupBox(tr("Not editable (kept as they are)"));
    auto *fixedGrid = new QGridLayout(fixedBox);
    m_volumeUp = new QLabel;
    m_volumeDown = new QLabel;
    fixedGrid->addWidget(new QLabel(touch::functionName(Function::VolumeUp)), 0, 0);
    fixedGrid->addWidget(m_volumeUp, 0, 1);
    fixedGrid->addWidget(new QLabel(touch::functionName(Function::VolumeDown)), 1, 0);
    fixedGrid->addWidget(m_volumeDown, 1, 1);
    fixedGrid->setColumnStretch(1, 1);
    layout->addWidget(fixedBox);

    m_validation = new QLabel;
    m_validation->setWordWrap(true);
    m_validation->setStyleSheet(QStringLiteral("color: #c0392b;"));
    layout->addWidget(m_validation);

    auto *buttons = new QHBoxLayout;
    m_reload = new QPushButton(tr("Reload"));
    m_restore = new QPushButton(tr("Restore defaults"));
    m_apply = new QPushButton(tr("Apply"));
    m_apply->setDefault(true);
    buttons->addWidget(m_reload);
    buttons->addStretch();
    buttons->addWidget(m_restore);
    buttons->addWidget(m_apply);
    layout->addLayout(buttons);
    layout->addStretch();

    connect(m_reload, &QPushButton::clicked, this, &TouchPage::reload);
    connect(m_restore, &QPushButton::clicked, this, &TouchPage::restoreDefaults);
    connect(m_apply, &QPushButton::clicked, this, &TouchPage::apply);
    connect(m_session, &gaia::Session::touchCurrentChanged, this, &TouchPage::onCurrent);
    connect(m_session, &gaia::Session::touchDefaultsChanged, this, &TouchPage::onDefaults);
    connect(m_session, &gaia::Session::touchApplyFinished, this, &TouchPage::onApplyFinished);

    updateState();
}

void TouchPage::buildRow(QGridLayout *grid, int row, Function f)
{
    Row &r = m_rows[static_cast<int>(f)];
    r.name = new QLabel(touch::functionName(f));
    r.gesture = new QComboBox;
    for (Gesture g : kGestureOrder)
        r.gesture->addItem(touch::gestureName(g), static_cast<int>(g));
    r.defaultValue = new QLabel;
    r.defaultValue->setStyleSheet(QStringLiteral("color: palette(mid);"));

    grid->addWidget(r.name, row, 0);
    grid->addWidget(r.gesture, row, 1);
    if (touch::Editor::hasSideChoice(f)) {
        r.side = new QComboBox;
        r.side->addItem(tr("Left"), static_cast<int>(Side::Left));
        r.side->addItem(tr("Right"), static_cast<int>(Side::Right));
        grid->addWidget(r.side, row, 2);
        connect(r.side, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, f, &r](int) {
            if (m_updating)
                return;
            m_editor.setSide(f, static_cast<Side>(r.side->currentData().toInt()));
            refreshWidgets();
        });
    } else {
        r.fixedSide = new QLabel(tr("Either"));
        grid->addWidget(r.fixedSide, row, 2);
    }
    grid->addWidget(r.defaultValue, row, 3);

    connect(r.gesture, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, f, &r](int) {
        if (m_updating)
            return;
        m_editor.setGesture(f, static_cast<Gesture>(r.gesture->currentData().toInt()));
        refreshWidgets();
    });
}

void TouchPage::setSessionReady(bool ready)
{
    m_ready = ready;
    m_busy = false;
    if (!ready) {
        m_editor = touch::Editor();
        m_enabledLabel->clear();
        m_validation->clear();
        m_volumeUp->clear();
        m_volumeDown->clear();
        for (Row &r : m_rows) {
            if (r.defaultValue)
                r.defaultValue->clear();
        }
    } else {
        reload();
    }
    updateState();
}

void TouchPage::reload()
{
    if (!m_ready || !m_session->hasFeature(gaia::Feature::Earbud))
        return;
    m_session->requestTouchMapping();
}

void TouchPage::onCurrent(const touch::CurrentMapping &current)
{
    m_editor.load(current.mapping);
    m_enabledLabel->setText(current.enabled ? tr("Custom touch mapping is active on the earbuds.")
                                            : tr("The earbuds report custom touch mapping as off. "
                                                 "Applying a mapping turns it on."));
    m_volumeUp->setText(touch::slotDescription(current.mapping[Function::VolumeUp]));
    m_volumeDown->setText(touch::slotDescription(current.mapping[Function::VolumeDown]));
    refreshWidgets();
}

void TouchPage::onDefaults(const touch::Mapping &defaults)
{
    for (int i = 0; i < touch::kFunctionCount; ++i) {
        if (m_rows[i].defaultValue)
            m_rows[i].defaultValue->setText(touch::slotDescription(defaults.entries[i]));
    }
    updateState();
}

void TouchPage::refreshWidgets()
{
    m_updating = true;
    const auto current = m_session->currentTouchMapping();
    const touch::Mapping edited = m_editor.toMapping();
    for (int i = 0; i < touch::kFunctionCount; ++i) {
        const auto f = static_cast<Function>(i);
        Row &r = m_rows[i];
        if (!r.gesture)
            continue;
        r.gesture->setCurrentIndex(r.gesture->findData(static_cast<int>(m_editor.gesture(f))));
        if (r.side)
            r.side->setCurrentIndex(r.side->findData(static_cast<int>(m_editor.side(f))));
        const bool changed = current && edited.entries[i] != current->mapping.entries[i];
        QFont font = r.name->font();
        font.setBold(changed);
        r.name->setFont(font);
    }
    m_updating = false;
    updateState();
}

void TouchPage::updateState()
{
    const bool loaded = m_ready && m_editor.isLoaded();
    const QString problem = loaded ? m_editor.validate() : QString();
    m_validation->setText(problem);
    for (Row &r : m_rows) {
        if (r.gesture)
            r.gesture->setEnabled(loaded && !m_busy);
        if (r.side)
            r.side->setEnabled(loaded && !m_busy);
    }
    m_reload->setEnabled(m_ready && !m_busy);
    m_restore->setEnabled(loaded && !m_busy && m_session->defaultTouchMapping().has_value());
    m_apply->setEnabled(loaded && !m_busy && problem.isEmpty());
}

void TouchPage::apply()
{
    if (!m_editor.validate().isEmpty())
        return;
    m_busy = true;
    updateState();
    emit statusMessage(tr("Applying touch controls…"));
    m_session->applyTouchMapping(m_editor.toMapping());
}

void TouchPage::restoreDefaults()
{
    if (QMessageBox::question(this, tr("Restore defaults"),
                              tr("Restore the factory touch controls on the earbuds?"))
        != QMessageBox::Yes)
        return;
    m_busy = true;
    updateState();
    emit statusMessage(tr("Restoring default touch controls…"));
    m_session->restoreDefaultTouchMapping();
}

void TouchPage::onApplyFinished(bool ok, const QString &message)
{
    m_busy = false;
    updateState();
    emit statusMessage(message);
    if (!ok)
        QMessageBox::warning(this, tr("Touch controls"), message);
}
