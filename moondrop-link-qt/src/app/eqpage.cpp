#include "app/eqpage.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

using gaia::EqBand;

namespace {

enum Column { ColFilter = 0, ColFrequency, ColQ, ColGain, ColumnCount };

QString settingsKey(const QString &deviceKey, int presetId)
{
    QString key = deviceKey;
    key.remove(QLatin1Char(':'));
    return QStringLiteral("presetNames/%1/%2").arg(key.isEmpty() ? QStringLiteral("any") : key).arg(presetId);
}

} // namespace

EqPage::EqPage(gaia::Session *session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    auto *layout = new QVBoxLayout(this);

    auto *presetBox = new QGroupBox(tr("Sound presets"));
    auto *presetLayout = new QVBoxLayout(presetBox);
    m_eqState = new QLabel;
    m_eqState->setWordWrap(true);
    presetLayout->addWidget(m_eqState);
    m_presetList = new QListWidget;
    m_presetList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_presetList->setMinimumHeight(120);
    presetLayout->addWidget(m_presetList);
    auto *presetHint = new QLabel(tr("The earbuds only report preset numbers. Moondrop's app gets the names from "
                                     "its server, so use Rename to label them yourself."));
    presetHint->setWordWrap(true);
    presetHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    presetLayout->addWidget(presetHint);
    auto *presetButtons = new QHBoxLayout;
    m_rename = new QPushButton(tr("Rename…"));
    m_applyDefault = new QPushButton(tr("Apply default"));
    m_applyPreset = new QPushButton(tr("Apply preset"));
    presetButtons->addWidget(m_rename);
    presetButtons->addStretch();
    presetButtons->addWidget(m_applyDefault);
    presetButtons->addWidget(m_applyPreset);
    presetLayout->addLayout(presetButtons);
    layout->addWidget(presetBox);

    m_customBox = new QGroupBox(tr("Custom EQ"));
    auto *customLayout = new QVBoxLayout(m_customBox);
    auto *customNote = new QLabel(tr("The earbuds report a user EQ preset. Moondrop's app does not offer this for "
                                     "Space Travel, so the firmware may ignore or reject changes."));
    customNote->setWordWrap(true);
    customLayout->addWidget(customNote);
    m_customState = new QLabel;
    customLayout->addWidget(m_customState);
    m_bandTable = new QTableWidget(0, ColumnCount);
    m_bandTable->setHorizontalHeaderLabels({tr("Filter"), tr("Frequency"), tr("Q"), tr("Gain")});
    m_bandTable->horizontalHeader()->setSectionResizeMode(ColFilter, QHeaderView::Stretch);
    m_bandTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_bandTable->setMinimumHeight(180);
    customLayout->addWidget(m_bandTable);
    auto *bandButtons = new QHBoxLayout;
    m_reloadBands = new QPushButton(tr("Reload"));
    m_flatten = new QPushButton(tr("Set gains to 0 dB"));
    m_useCustom = new QPushButton(tr("Use custom EQ"));
    m_applyBands = new QPushButton(tr("Apply bands"));
    bandButtons->addWidget(m_reloadBands);
    bandButtons->addWidget(m_flatten);
    bandButtons->addStretch();
    bandButtons->addWidget(m_useCustom);
    bandButtons->addWidget(m_applyBands);
    customLayout->addLayout(bandButtons);
    layout->addWidget(m_customBox);
    layout->addStretch();

    connect(m_presetList, &QListWidget::itemSelectionChanged, this, &EqPage::updateState);
    connect(m_presetList, &QListWidget::itemDoubleClicked, this, &EqPage::applySelectedPreset);
    connect(m_rename, &QPushButton::clicked, this, &EqPage::renamePreset);
    connect(m_applyDefault, &QPushButton::clicked, this, &EqPage::applyDefaultPreset);
    connect(m_applyPreset, &QPushButton::clicked, this, &EqPage::applySelectedPreset);
    connect(m_reloadBands, &QPushButton::clicked, m_session, &gaia::Session::requestUserBands);
    connect(m_flatten, &QPushButton::clicked, this, &EqPage::flattenBands);
    connect(m_useCustom, &QPushButton::clicked, this, &EqPage::useCustomEq);
    connect(m_applyBands, &QPushButton::clicked, this, &EqPage::applyBands);

    connect(m_session, &gaia::Session::eqStateChanged, this, &EqPage::updateState);
    connect(m_session, &gaia::Session::presetsChanged, this, &EqPage::rebuildPresets);
    connect(m_session, &gaia::Session::selectedPresetChanged, this, &EqPage::rebuildPresets);
    connect(m_session, &gaia::Session::userBandCountChanged, this, &EqPage::updateState);
    connect(m_session, &gaia::Session::userBandsChanged, this, &EqPage::rebuildBands);
    connect(m_session, &gaia::Session::presetSelectFinished, this, [this](bool ok, const QString &message) {
        m_presetBusy = false;
        updateState();
        emit statusMessage(message);
        if (!ok)
            QMessageBox::warning(this, tr("Equaliser"), message);
    });
    connect(m_session, &gaia::Session::userBandsApplyFinished, this, [this](bool ok, const QString &message) {
        m_bandsBusy = false;
        updateState();
        emit statusMessage(message);
        if (!ok)
            QMessageBox::warning(this, tr("Custom EQ"), message);
    });

    updateState();
}

void EqPage::setDeviceKey(const QString &key)
{
    m_deviceKey = key;
    rebuildPresets();
}

void EqPage::setSessionReady(bool ready)
{
    m_ready = ready;
    m_presetBusy = false;
    m_bandsBusy = false;
    if (ready && m_session->hasFeature(gaia::Feature::MusicProcessing)) {
        m_session->requestEqInfo();
    } else {
        m_presetList->clear();
        m_bandTable->setRowCount(0);
    }
    updateState();
}

QString EqPage::presetLabel(int presetId) const
{
    const QString custom = QSettings().value(settingsKey(m_deviceKey, presetId)).toString();
    if (!custom.isEmpty())
        return custom;
    return tr("%1 (preset %2)").arg(gaia::defaultPresetName(presetId)).arg(presetId);
}

void EqPage::rebuildPresets()
{
    const int previouslySelected =
        m_presetList->currentItem() ? m_presetList->currentItem()->data(Qt::UserRole).toInt() : -1;
    const int active = m_session->selectedPreset();
    m_presetList->clear();
    for (int id : m_session->presets()) {
        if (id == gaia::music::kUserPreset)
            continue;  // shown as "Custom EQ" instead, like the official app
        auto *item = new QListWidgetItem(presetLabel(id));
        item->setData(Qt::UserRole, id);
        if (id == active) {
            item->setText(tr("%1 — active").arg(item->text()));
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
        m_presetList->addItem(item);
        if (id == (previouslySelected >= 0 ? previouslySelected : active))
            m_presetList->setCurrentItem(item);
    }
    updateState();
}

void EqPage::rebuildBands()
{
    const QVector<EqBand> bands = m_session->userBands();
    m_bandTable->setRowCount(bands.size());
    QStringList rowLabels;
    for (int row = 0; row < bands.size(); ++row) {
        const EqBand &band = bands[row];
        rowLabels << tr("Band %1").arg(band.index + 1);

        auto *filter = new QComboBox;
        for (int t = 0; t < gaia::kFilterTypeCount; ++t)
            filter->addItem(gaia::filterTypeName(t), t);
        if (filter->findData(band.filterType) < 0)
            filter->addItem(gaia::filterTypeName(band.filterType), band.filterType);
        filter->setCurrentIndex(filter->findData(band.filterType));
        m_bandTable->setCellWidget(row, ColFilter, filter);

        auto *freq = new QSpinBox;
        freq->setRange(1, 65535);
        freq->setSuffix(tr(" Hz"));
        freq->setValue(band.frequencyHz);
        m_bandTable->setCellWidget(row, ColFrequency, freq);

        auto *q = new QDoubleSpinBox;
        q->setDecimals(2);
        q->setRange(0.01, 15.99);
        q->setSingleStep(0.05);
        q->setValue(band.q);
        m_bandTable->setCellWidget(row, ColQ, q);

        auto *gain = new QDoubleSpinBox;
        gain->setDecimals(2);
        gain->setRange(-30.0, 30.0);
        gain->setSingleStep(0.5);
        gain->setSuffix(tr(" dB"));
        gain->setValue(band.gainDb);
        m_bandTable->setCellWidget(row, ColGain, gain);
    }
    m_bandTable->setVerticalHeaderLabels(rowLabels);
    m_bandTable->resizeColumnsToContents();
    m_bandTable->horizontalHeader()->setSectionResizeMode(ColFilter, QHeaderView::Stretch);
    updateState();
}

QVector<EqBand> EqPage::bandsFromTable() const
{
    QVector<EqBand> bands = m_session->userBands();
    for (int row = 0; row < bands.size() && row < m_bandTable->rowCount(); ++row) {
        EqBand &band = bands[row];
        if (auto *filter = qobject_cast<QComboBox *>(m_bandTable->cellWidget(row, ColFilter)))
            band.filterType = filter->currentData().toInt();
        if (auto *freq = qobject_cast<QSpinBox *>(m_bandTable->cellWidget(row, ColFrequency)))
            band.frequencyHz = freq->value();
        if (auto *q = qobject_cast<QDoubleSpinBox *>(m_bandTable->cellWidget(row, ColQ)))
            band.q = q->value();
        if (auto *gain = qobject_cast<QDoubleSpinBox *>(m_bandTable->cellWidget(row, ColGain)))
            band.gainDb = gain->value();
    }
    return bands;
}

void EqPage::updateState()
{
    const bool hasEq = m_ready && m_session->hasFeature(gaia::Feature::MusicProcessing);
    if (!m_ready)
        m_eqState->setText(tr("Connect to the earbuds to change the sound."));
    else if (!hasEq)
        m_eqState->setText(tr("These earbuds do not offer EQ control."));
    else
        m_eqState->setText(m_session->eqPresent() ? tr("Pick a preset and press Apply.")
                                                  : tr("The earbuds report their EQ as not present."));

    const bool haveSelection = m_presetList->currentItem() != nullptr;
    m_presetList->setEnabled(hasEq && !m_presetBusy);
    m_rename->setEnabled(hasEq && haveSelection);
    m_applyDefault->setEnabled(hasEq && !m_presetBusy && m_presetList->count() > 0);
    m_applyPreset->setEnabled(hasEq && !m_presetBusy && haveSelection);

    const bool userEq = hasEq && m_session->presets().contains(gaia::music::kUserPreset)
                        && m_session->userBandCount() > 0;
    m_customBox->setVisible(userEq);
    const bool customActive = m_session->selectedPreset() == gaia::music::kUserPreset;
    m_customState->setText(customActive ? tr("Custom EQ is the active preset.")
                                        : tr("Custom EQ is not active. Press \"Use custom EQ\" to switch to it."));
    const bool bandsLoaded = !m_session->userBands().isEmpty();
    m_bandTable->setEnabled(userEq && !m_bandsBusy);
    m_reloadBands->setEnabled(userEq && !m_bandsBusy);
    m_flatten->setEnabled(userEq && bandsLoaded && !m_bandsBusy);
    m_useCustom->setEnabled(userEq && !customActive && !m_presetBusy);
    m_applyBands->setEnabled(userEq && bandsLoaded && !m_bandsBusy);
}

void EqPage::applySelectedPreset()
{
    QListWidgetItem *item = m_presetList->currentItem();
    if (!item || m_presetBusy)
        return;
    m_presetBusy = true;
    updateState();
    emit statusMessage(tr("Applying %1…").arg(presetLabel(item->data(Qt::UserRole).toInt())));
    m_session->selectPreset(item->data(Qt::UserRole).toInt());
}

void EqPage::applyDefaultPreset()
{
    // The official app's "apply default" picks the first preset the earbuds list.
    if (m_presetList->count() == 0 || m_presetBusy)
        return;
    if (QMessageBox::question(this, tr("Apply default"), tr("Switch the earbuds to their default preset?"))
        != QMessageBox::Yes)
        return;
    m_presetList->setCurrentRow(0);
    applySelectedPreset();
}

void EqPage::renamePreset()
{
    QListWidgetItem *item = m_presetList->currentItem();
    if (!item)
        return;
    const int id = item->data(Qt::UserRole).toInt();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename preset"),
                                               tr("Name for preset %1 (leave empty to reset):").arg(id),
                                               QLineEdit::Normal,
                                               QSettings().value(settingsKey(m_deviceKey, id)).toString(), &ok);
    if (!ok)
        return;
    QSettings settings;
    if (name.trimmed().isEmpty())
        settings.remove(settingsKey(m_deviceKey, id));
    else
        settings.setValue(settingsKey(m_deviceKey, id), name.trimmed());
    rebuildPresets();
}

void EqPage::useCustomEq()
{
    if (m_presetBusy)
        return;
    m_presetBusy = true;
    updateState();
    emit statusMessage(tr("Switching to custom EQ…"));
    m_session->selectPreset(gaia::music::kUserPreset);
}

void EqPage::applyBands()
{
    if (m_bandsBusy)
        return;
    m_bandsBusy = true;
    updateState();
    emit statusMessage(tr("Applying custom EQ…"));
    m_session->setUserBands(bandsFromTable());
}

void EqPage::flattenBands()
{
    for (int row = 0; row < m_bandTable->rowCount(); ++row) {
        if (auto *gain = qobject_cast<QDoubleSpinBox *>(m_bandTable->cellWidget(row, ColGain)))
            gain->setValue(0.0);
    }
}
