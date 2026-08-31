#include "widget/config_page/config_page.h"

#include "ui_config_page.h"
#include "ui_mcu_id_page.h"
#include "ui_rtc_page.h"
#include "ui_storage_page.h"
#include "ui_digital_page.h"
#include "ui_analog_page.h"
#include "ui_address_page.h"
#include "ui_role_page.h"
#include "ui_frequency_page.h"
#include "ui_modulation_page.h"
#include "ui_power_page.h"
#include "ui_task_table_page.h"
#include "ui_rs485_page.h"
#include "ui_counters_page.h"
#include "ui_rssi_page.h"
#include "ui_calibration_page.h"
#include "ui_register_page.h"

#include "function/thread.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QDomDocument>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QTreeWidgetItem>

namespace {

QString hex8(quint16 v)
{
    return QString("%1").arg(v & 0xFF, 2, 16, QChar('0')).toUpper();
}

QString hex16(quint16 v)
{
    return QString("%1").arg(v, 4, 16, QChar('0')).toUpper();
}

bool parseHex(const QString &text, int maxVal, quint16 &out)
{
    bool ok = false;
    const int v = text.trimmed().toInt(&ok, 16);
    if (!ok || v < 0 || v > maxVal)
        return false;
    out = static_cast<quint16>(v);
    return true;
}

template <typename UiT>
QWidget *makePage(UiT &ui)
{
    auto *w = new QWidget;
    ui.setupUi(w);
    return w;
}

// The index-tree group/leaf labels travel through QStringLiteral variables into
// addPage()/tr(qPrintable(...)), which lupdate cannot scan (it only picks up
// tr() with literal arguments). Declaring them with QT_TRANSLATE_NOOP marks them
// as active translatable strings so lupdate keeps them in the .ts instead of
// marking them "vanished", which lrelease would then drop (leaving the tree in
// English at runtime).
const char *const kTreeLabels[] = {
    QT_TRANSLATE_NOOP("ConfigPage", "System"),
    QT_TRANSLATE_NOOP("ConfigPage", "MCU ID"),
    QT_TRANSLATE_NOOP("ConfigPage", "RTC Clock"),
    QT_TRANSLATE_NOOP("ConfigPage", "Storage"),
    QT_TRANSLATE_NOOP("ConfigPage", "Realtime I/O"),
    QT_TRANSLATE_NOOP("ConfigPage", "Digital DI/DO"),
    QT_TRANSLATE_NOOP("ConfigPage", "Analog AI/AO"),
    QT_TRANSLATE_NOOP("ConfigPage", "Network & Role"),
    QT_TRANSLATE_NOOP("ConfigPage", "Local / Peer Address"),
    QT_TRANSLATE_NOOP("ConfigPage", "Master-Slave Role"),
    QT_TRANSLATE_NOOP("ConfigPage", "RF Parameters"),
    QT_TRANSLATE_NOOP("ConfigPage", "Carrier Frequency"),
    QT_TRANSLATE_NOOP("ConfigPage", "Modulation"),
    QT_TRANSLATE_NOOP("ConfigPage", "Power & Preamble"),
    QT_TRANSLATE_NOOP("ConfigPage", "Schedule"),
    QT_TRANSLATE_NOOP("ConfigPage", "Task Table"),
    QT_TRANSLATE_NOOP("ConfigPage", "RS-485 Passthrough"),
    QT_TRANSLATE_NOOP("ConfigPage", "Debug & Statistics"),
    QT_TRANSLATE_NOOP("ConfigPage", "Counters & Test"),
    QT_TRANSLATE_NOOP("ConfigPage", "RSSI / SNR"),
    QT_TRANSLATE_NOOP("ConfigPage", "Frequency Calibration"),
    QT_TRANSLATE_NOOP("ConfigPage", "Register Access"),
    QT_TRANSLATE_NOOP("ConfigPage", "SX1278 Register"),
};

} // namespace

// All page Ui objects and their widgets, kept out of the header.
struct PagesDef
{
    Ui::McuIdPage mcuId;
    QWidget *mcuIdW = nullptr;
    Ui::RtcPage rtc;
    QWidget *rtcW = nullptr;
    Ui::StoragePage storage;
    QWidget *storageW = nullptr;
    Ui::DigitalPage digital;
    QWidget *digitalW = nullptr;
    Ui::AnalogPage analog;
    QWidget *analogW = nullptr;
    Ui::AddressPage address;
    QWidget *addressW = nullptr;
    Ui::RolePage role;
    QWidget *roleW = nullptr;
    Ui::FrequencyPage frequency;
    QWidget *frequencyW = nullptr;
    Ui::ModulationPage modulation;
    QWidget *modulationW = nullptr;
    Ui::PowerPage power;
    QWidget *powerW = nullptr;
    Ui::TaskTablePage tasks;
    QWidget *tasksW = nullptr;
    Ui::Rs485Page rs485;
    QWidget *rs485W = nullptr;
    Ui::CountersPage counters;
    QWidget *countersW = nullptr;
    Ui::RssiPage rssi;
    QWidget *rssiW = nullptr;
    Ui::CalibrationPage calibration;
    QWidget *calibrationW = nullptr;
    Ui::RegisterPage regPage;
    QWidget *regW = nullptr;
};

ConfigPage::ConfigPage(bool master, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ConfigPage)
    , m_isMaster(master)
{
    ui->setupUi(this);
    // Let the stacked pages shrink instead of growing the tab (and pushing the
    // status bar off the window). Tall pages (Analog, Task Table) otherwise
    // force the main window taller than the screen.
    ui->stackedPages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);

    m_pg.reset(new PagesDef);
    m_pg->mcuIdW = makePage(m_pg->mcuId);
    m_pg->rtcW = makePage(m_pg->rtc);
    m_pg->storageW = makePage(m_pg->storage);
    m_pg->digitalW = makePage(m_pg->digital);
    m_pg->analogW = makePage(m_pg->analog);
    m_pg->addressW = makePage(m_pg->address);
    m_pg->roleW = makePage(m_pg->role);
    m_pg->frequencyW = makePage(m_pg->frequency);
    m_pg->modulationW = makePage(m_pg->modulation);
    m_pg->powerW = makePage(m_pg->power);
    if (m_isMaster)
        m_pg->tasksW = makePage(m_pg->tasks);
    m_pg->rs485W = makePage(m_pg->rs485);
    m_pg->countersW = makePage(m_pg->counters);
    m_pg->rssiW = makePage(m_pg->rssi);
    m_pg->calibrationW = makePage(m_pg->calibration);
    m_pg->regW = makePage(m_pg->regPage);

    createPages();
    wirePages();
    refreshPorts();

    m_rfTestTimer = new QTimer(this);
    m_rfTestTimer->setSingleShot(true);

    connect(ui->btnPortRefresh, &QToolButton::clicked, this, &ConfigPage::refreshPorts);
    connect(ui->btnConnect, &QPushButton::clicked, this, &ConfigPage::toggleConnection);
    connect(ui->treeIndex, &QTreeWidget::itemClicked, this, &ConfigPage::onTreeClicked);
    connect(ui->treeIndex, &QTreeWidget::currentItemChanged, this, &ConfigPage::onTreeCurrentChanged);
}

ConfigPage::~ConfigPage()
{
    delete ui;
}

void ConfigPage::retranslate()
{
    ui->retranslateUi(this);
    // Refresh the index tree from the stored English sources.
    for (int i = 0; i < ui->treeIndex->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = ui->treeIndex->topLevelItem(i);
        const QString gsrc = top->data(0, Qt::UserRole + 1).toString();
        if (!gsrc.isEmpty())
            top->setText(0, tr(qPrintable(gsrc)));
        for (int j = 0; j < top->childCount(); ++j) {
            QTreeWidgetItem *ch = top->child(j);
            const QString lsrc = ch->data(0, Qt::UserRole + 1).toString();
            if (!lsrc.isEmpty())
                ch->setText(0, tr(qPrintable(lsrc)));
        }
    }
    m_pg->mcuId.retranslateUi(m_pg->mcuIdW);
    m_pg->rtc.retranslateUi(m_pg->rtcW);
    m_pg->storage.retranslateUi(m_pg->storageW);
    m_pg->digital.retranslateUi(m_pg->digitalW);
    m_pg->analog.retranslateUi(m_pg->analogW);
    m_pg->address.retranslateUi(m_pg->addressW);
    m_pg->role.retranslateUi(m_pg->roleW);
    m_pg->frequency.retranslateUi(m_pg->frequencyW);
    m_pg->modulation.retranslateUi(m_pg->modulationW);
    m_pg->power.retranslateUi(m_pg->powerW);
    if (m_pg->tasksW)
        m_pg->tasks.retranslateUi(m_pg->tasksW);
    m_pg->rs485.retranslateUi(m_pg->rs485W);
    m_pg->counters.retranslateUi(m_pg->countersW);
    m_pg->rssi.retranslateUi(m_pg->rssiW);
    m_pg->calibration.retranslateUi(m_pg->calibrationW);
    m_pg->regPage.retranslateUi(m_pg->regW);
    setConnected(m_connected); // re-apply Connect/Disconnect button text
}

void ConfigPage::addPage(const QString &groupSrc, const QString &leafSrc, QWidget *page)
{
    // Translate the English sources for display, but store the ENGLISH sources
    // in UserRole+1. tr(qPrintable()) of a translated (Chinese) string garbles
    // when switching back to English on a Chinese locale, because qPrintable()
    // re-encodes to the local 8-bit codepage (GBK) while tr() expects UTF-8.
    const QString group = tr(qPrintable(groupSrc));
    const QString leaf = tr(qPrintable(leafSrc));
    QTreeWidgetItem *top = nullptr;
    for (int i = 0; i < ui->treeIndex->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = ui->treeIndex->topLevelItem(i);
        if (it->text(0) == group) {
            top = it;
            break;
        }
    }
    if (!top) {
        top = new QTreeWidgetItem(ui->treeIndex, QStringList(group));
        top->setFlags(Qt::ItemIsEnabled);
        top->setData(0, Qt::UserRole + 1, groupSrc); // English source for retranslate
    }
    auto *child = new QTreeWidgetItem(top, QStringList(leaf));
    child->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<QWidget *>(page)));
    child->setData(0, Qt::UserRole + 1, leafSrc); // English source for retranslate
    ui->stackedPages->addWidget(page);
}

void ConfigPage::createPages()
{
    Q_UNUSED(kTreeLabels); // keep the QT_TRANSLATE_NOOP declarations referenced
    addPage(QStringLiteral("System"), QStringLiteral("MCU ID"), m_pg->mcuIdW);
    addPage(QStringLiteral("System"), QStringLiteral("RTC Clock"), m_pg->rtcW);
    addPage(QStringLiteral("System"), QStringLiteral("Storage"), m_pg->storageW);
    addPage(QStringLiteral("Realtime I/O"), QStringLiteral("Digital DI/DO"), m_pg->digitalW);
    addPage(QStringLiteral("Realtime I/O"), QStringLiteral("Analog AI/AO"), m_pg->analogW);
    addPage(QStringLiteral("Network & Role"), QStringLiteral("Local / Peer Address"), m_pg->addressW);
    addPage(QStringLiteral("Network & Role"), QStringLiteral("Master-Slave Role"), m_pg->roleW);
    addPage(QStringLiteral("RF Parameters"), QStringLiteral("Carrier Frequency"), m_pg->frequencyW);
    addPage(QStringLiteral("RF Parameters"), QStringLiteral("Modulation"), m_pg->modulationW);
    addPage(QStringLiteral("RF Parameters"), QStringLiteral("Power & Preamble"), m_pg->powerW);
    if (m_isMaster)
        addPage(QStringLiteral("Schedule"), QStringLiteral("Task Table"), m_pg->tasksW);
    addPage(QStringLiteral("Schedule"), QStringLiteral("RS-485 Passthrough"), m_pg->rs485W);
    addPage(QStringLiteral("Debug & Statistics"), QStringLiteral("Counters & Test"), m_pg->countersW);
    addPage(QStringLiteral("Debug & Statistics"), QStringLiteral("RSSI / SNR"), m_pg->rssiW);
    addPage(QStringLiteral("Debug & Statistics"), QStringLiteral("Frequency Calibration"), m_pg->calibrationW);
    addPage(QStringLiteral("Register Access"), QStringLiteral("SX1278 Register"), m_pg->regW);

    ui->treeIndex->expandAll();
    if (ui->treeIndex->topLevelItemCount() > 0) {
        QTreeWidgetItem *first = ui->treeIndex->topLevelItem(0)->child(0);
        if (first)
            ui->treeIndex->setCurrentItem(first);
    }
    // Resizable left index panel: 220 px default, never smaller than its minimum.
    ui->splitterMain->setStretchFactor(0, 0);
    ui->splitterMain->setStretchFactor(1, 1);
    ui->splitterMain->setSizes(QList<int>() << 220 << 1);
}

void ConfigPage::wirePages()
{
    // ---- System / MCU ID ---------------------------------------------------
    connect(m_pg->mcuId.btnReadMcuId, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < 3; ++i)
            sendRead(static_cast<quint8>(Proto::ADDR_MCU_ID0 + i));
    });
    for (int i = 0; i < 3; ++i) {
        registerHandler(static_cast<quint8>(Proto::ADDR_MCU_ID0 + i), [this, i](quint8, quint16 data) {
            m_mcuId[i] = data;
            m_pg->mcuId.lblMcuIdValue->setText(
                    QString("%1 %2 %3").arg(hex16(m_mcuId[0]), hex16(m_mcuId[1]), hex16(m_mcuId[2])));
        });
    }

    // ---- System / RTC Clock ------------------------------------------------
    {
        const auto refreshClock = [this] {
            m_pg->rtc.lcdDate->display(QString("%1-%2-%3")
                                               .arg(m_pg->rtc.spinRtcYear->value(), 4, 10, QChar('0'))
                                               .arg(m_pg->rtc.spinRtcMon->value(), 2, 10, QChar('0'))
                                               .arg(m_pg->rtc.spinRtcDay->value(), 2, 10, QChar('0')));
            m_pg->rtc.lcdClock->display(QString("%1:%2:%3")
                                                .arg(m_pg->rtc.spinRtcHour->value(), 2, 10, QChar('0'))
                                                .arg(m_pg->rtc.spinRtcMin->value(), 2, 10, QChar('0'))
                                                .arg(m_pg->rtc.spinRtcSec->value(), 2, 10, QChar('0')));
        };
        const QList<QPair<quint8, QSpinBox *>> rows = {
                {Proto::ADDR_RTC_MS, m_pg->rtc.spinRtcMs},
                {Proto::ADDR_RTC_SEC, m_pg->rtc.spinRtcSec},
                {Proto::ADDR_RTC_MIN, m_pg->rtc.spinRtcMin},
                {Proto::ADDR_RTC_HOUR, m_pg->rtc.spinRtcHour},
                {Proto::ADDR_RTC_DAY, m_pg->rtc.spinRtcDay},
                {Proto::ADDR_RTC_MON, m_pg->rtc.spinRtcMon},
                {Proto::ADDR_RTC_YEAR, m_pg->rtc.spinRtcYear}};
        connect(m_pg->rtc.btnRtcRead, &QPushButton::clicked, this, [this, rows] {
            for (const auto &r : rows)
                sendRead(r.first);
        });
        // Write the current field values; year is stored as 2000 + value.
        const auto applyFields = [this, rows] {
            for (const auto &r : rows) {
                int v = r.second->value();
                if (r.first == Proto::ADDR_RTC_YEAR)
                    v -= 2000;
                sendWrite(r.first, static_cast<quint16>(qBound(0, v, 255)));
            }
        };
        connect(m_pg->rtc.btnRtcWrite, &QPushButton::clicked, this, applyFields);
        // Set from PC: fill every field from the host clock, then write.
        connect(m_pg->rtc.btnRtcSetPc, &QPushButton::clicked, this, [this, applyFields] {
            const QDateTime now = QDateTime::currentDateTime();
            m_pg->rtc.spinRtcYear->setValue(now.date().year());
            m_pg->rtc.spinRtcMon->setValue(now.date().month());
            m_pg->rtc.spinRtcDay->setValue(now.date().day());
            m_pg->rtc.spinRtcHour->setValue(now.time().hour());
            m_pg->rtc.spinRtcMin->setValue(now.time().minute());
            m_pg->rtc.spinRtcSec->setValue(now.time().second());
            m_pg->rtc.spinRtcMs->setValue(now.time().msec());
            m_pg->rtc.lcdClock->display(now.toString(QStringLiteral("HH:mm:ss")));
            applyFields();
        });
        for (const auto &r : rows) {
            registerHandler(r.first, [this, box = r.second, refreshClock, isYear = r.first
                             == Proto::ADDR_RTC_YEAR](quint8, quint16 data) {
                const int v = data & 0xFF;
                box->setValue(isYear ? 2000 + v : v);
                refreshClock();
            });
        }
        registerHandler(Proto::ADDR_RTC_MS, [this](quint8, quint16 data) {
            m_pg->rtc.spinRtcMs->setValue(data & 0xFFFF);
        });
    }

    // ---- System / Storage --------------------------------------------------
    connect(m_pg->storage.btnReadConfigValid, &QPushButton::clicked, this,
            [this] { sendRead(Proto::ADDR_SAVE); });
    // Save Config writes / applies every field from the loaded configuration,
    // then persists it to EEPROM. This is the one-click "apply all" path the
    // bundled example configurations rely on.
    connect(m_pg->storage.btnSaveConfig, &QPushButton::clicked, this, [this] {
        writeAllSettings();
        sendWrite(Proto::ADDR_APPLY_RF, 0x0001);
        sendWriteTimeout(Proto::ADDR_SAVE, 0x0001);
    });
    connect(m_pg->storage.btnFactoryReset, &QPushButton::clicked, this, [this] {
        const auto ret = QMessageBox::question(this, tr("Factory Reset"),
                                               tr("Reset the device to factory settings?"));
        if (ret == QMessageBox::Yes)
            sendWriteTimeout(Proto::ADDR_FACTORY_RESET, 0x0001);
    });
    registerHandler(Proto::ADDR_SAVE, [this](quint8 head, quint16 data) {
        m_pg->storage.lblConfigValid->setText(data ? tr("Valid") : tr("Invalid"));
        if (head == Proto::HEAD_WRITE)
            emit statusMessage(tr("Config saved to EEPROM."));
    });
    registerHandler(Proto::ADDR_FACTORY_RESET, [this](quint8 head, quint16) {
        if (head == Proto::HEAD_WRITE)
            emit statusMessage(tr("Factory reset done."));
    });

    // ---- Realtime I/O / Digital DI/DO --------------------------------------
    {
        QList<QLabel *> ledDi = {m_pg->digital.ledDi0, m_pg->digital.ledDi1, m_pg->digital.ledDi2,
                                 m_pg->digital.ledDi3, m_pg->digital.ledDi4, m_pg->digital.ledDi5,
                                 m_pg->digital.ledDi6, m_pg->digital.ledDi7, m_pg->digital.ledDi8,
                                 m_pg->digital.ledDi9, m_pg->digital.ledDi10, m_pg->digital.ledDi11,
                                 m_pg->digital.ledDi12, m_pg->digital.ledDi13, m_pg->digital.ledDi14,
                                 m_pg->digital.ledDi15};
        QList<QLabel *> ledDo = {m_pg->digital.ledDo0, m_pg->digital.ledDo1, m_pg->digital.ledDo2,
                                 m_pg->digital.ledDo3, m_pg->digital.ledDo4, m_pg->digital.ledDo5,
                                 m_pg->digital.ledDo6, m_pg->digital.ledDo7, m_pg->digital.ledDo8,
                                 m_pg->digital.ledDo9, m_pg->digital.ledDo10, m_pg->digital.ledDo11,
                                 m_pg->digital.ledDo12, m_pg->digital.ledDo13, m_pg->digital.ledDo14,
                                 m_pg->digital.ledDo15};
        QList<QCheckBox *> chkDo = {m_pg->digital.chkDo0, m_pg->digital.chkDo1, m_pg->digital.chkDo2,
                                    m_pg->digital.chkDo3, m_pg->digital.chkDo4, m_pg->digital.chkDo5,
                                    m_pg->digital.chkDo6, m_pg->digital.chkDo7, m_pg->digital.chkDo8,
                                    m_pg->digital.chkDo9, m_pg->digital.chkDo10, m_pg->digital.chkDo11,
                                    m_pg->digital.chkDo12, m_pg->digital.chkDo13, m_pg->digital.chkDo14,
                                    m_pg->digital.chkDo15};
        const auto setLed = [](QLabel *lbl, bool on) {
            lbl->setStyleSheet(on ? QStringLiteral(
                                    "background:#35c135;border:1px solid #278127;border-radius:8px;")
                                  : QStringLiteral(
                                    "background:#707070;border:1px solid #4a4a4a;border-radius:8px;"));
        };
        for (QLabel *lbl : ledDi)
            setLed(lbl, false);
        for (QLabel *lbl : ledDo)
            setLed(lbl, false);
        connect(m_pg->digital.btnDoRead, &QPushButton::clicked, this, [this] {
            sendRead(Proto::ADDR_DI_LO);
            sendRead(Proto::ADDR_DI_HI);
            sendRead(Proto::ADDR_DO_LO);
            sendRead(Proto::ADDR_DO_HI);
        });
        connect(m_pg->digital.btnDoWrite, &QPushButton::clicked, this, [this, chkDo] {
            quint16 v = 0xFFFF; // active-low: checked switch -> bit 0
            for (int i = 0; i < 16; ++i)
                if (chkDo.at(i)->isChecked())
                    v &= ~(1u << i);
            sendWrite(Proto::ADDR_DO_LO, static_cast<quint16>(v & 0xFF));
            sendWrite(Proto::ADDR_DO_HI, static_cast<quint16>((v >> 8) & 0xFF));
        });
        // DI lamps: position p shows data bit 15-p (reversed), lamp lit when the
        // bit is LOW. DI_HI (D15..D8) fills positions 0..7, DI_LO fills 8..15.
        const auto applyDi = [setLed](quint8 base, QList<QLabel *> leds, quint16 data) {
            for (int o = 0; o < 8; ++o)
                setLed(leds.at(base + o), (data & (1u << (7 - o))) == 0);
        };
        registerHandler(Proto::ADDR_DI_HI, [applyDi, ledDi](quint8, quint16 data) {
            applyDi(0, ledDi, data);
        });
        registerHandler(Proto::ADDR_DI_LO, [applyDi, ledDi](quint8, quint16 data) {
            applyDi(8, ledDi, data);
        });
        // DO lamps: active-low, reversed order too; bit clear = conducting = lit.
        const auto applyDo = [setLed, chkDo](quint8 base, QList<QLabel *> leds, quint16 data) {
            for (int o = 0; o < 8; ++o) {
                const int n = base + o;
                const bool conducting = (data & (1u << (7 - o))) == 0;
                setLed(leds.at(n), conducting);
                const QSignalBlocker b(chkDo.at(n));
                chkDo.at(n)->setChecked(conducting);
            }
        };
        registerHandler(Proto::ADDR_DO_HI, [applyDo, ledDo](quint8, quint16 data) {
            applyDo(0, ledDo, data);
        });
        registerHandler(Proto::ADDR_DO_LO, [applyDo, ledDo](quint8, quint16 data) {
            applyDo(8, ledDo, data);
        });
    }

    // ---- Realtime I/O / Analog AI/AO --------------------------------------
    {
        // 10-bit raw -> 0..10 V and 0..20 mA (linear over full scale).
        const auto showAnalog = [](QLCDNumber *v, QLCDNumber *ma, int raw) {
            v->display(QString::number(raw * 10.0 / 1023.0, 'f', 2));
            ma->display(QString::number(raw * 20.0 / 1023.0, 'f', 2));
        };
        const QList<QPair<int, QLCDNumber *>> aiV = {
                {0, m_pg->analog.lcdAi0V}, {1, m_pg->analog.lcdAi1V},
                {2, m_pg->analog.lcdAi2V}, {3, m_pg->analog.lcdAi3V}};
        const QList<QPair<int, QLCDNumber *>> aimA = {
                {0, m_pg->analog.lcdAi0mA}, {1, m_pg->analog.lcdAi1mA},
                {2, m_pg->analog.lcdAi2mA}, {3, m_pg->analog.lcdAi3mA}};
        // AI: column n shows channel n (natural order). AO stays reversed.
        for (int i = 0; i < 4; ++i) {
            registerHandler(static_cast<quint8>(Proto::ADDR_AI0 + i),
                            [showAnalog, v = aiV.at(i).second, ma = aimA.at(i).second](
                                    quint8, quint16 data) {
                                showAnalog(v, ma, data & 0x03FF);
                            });
        }
        // AO channels: Read readout (from radio) + Write readout (pending, driven by dial).
        struct AoRow { QDial *dial; QLCDNumber *readV; QLCDNumber *readMa; QLCDNumber *writeV; QLCDNumber *writeMa; };
        const QList<AoRow> aoRows = {
                {m_pg->analog.dialAo0, m_pg->analog.lcdAo0V, m_pg->analog.lcdAo0mA,
                 m_pg->analog.lcdAo0WV, m_pg->analog.lcdAo0WmA},
                {m_pg->analog.dialAo1, m_pg->analog.lcdAo1V, m_pg->analog.lcdAo1mA,
                 m_pg->analog.lcdAo1WV, m_pg->analog.lcdAo1WmA},
                {m_pg->analog.dialAo2, m_pg->analog.lcdAo2V, m_pg->analog.lcdAo2mA,
                 m_pg->analog.lcdAo2WV, m_pg->analog.lcdAo2WmA},
                {m_pg->analog.dialAo3, m_pg->analog.lcdAo3V, m_pg->analog.lcdAo3mA,
                 m_pg->analog.lcdAo3WV, m_pg->analog.lcdAo3WmA}};
        for (int i = 0; i < aoRows.size(); ++i) {
            const int ch = 3 - i; // UI column i <-> channel ch (reversed)
            const auto row = aoRows.at(i);
            const auto refreshWrite = [showAnalog, row]() {
                showAnalog(row.writeV, row.writeMa, row.dial->value());
            };
            connect(row.dial, qOverload<int>(&QDial::valueChanged), this,
                    [refreshWrite](int) { refreshWrite(); });
            refreshWrite(); // show the pending value for the initial dial position
            registerHandler(static_cast<quint8>(Proto::ADDR_AO0 + ch),
                            [showAnalog, row](quint8, quint16 data) {
                                showAnalog(row.readV, row.readMa, data & 0x03FF);
                            });
        }
        // Single Read/Apply pair: Read fetches AI and AO, Apply writes AO.
        connect(m_pg->analog.btnAoRead, &QPushButton::clicked, this, [this] {
            for (int i = 0; i < 4; ++i) {
                sendRead(static_cast<quint8>(Proto::ADDR_AI0 + i));
                sendRead(static_cast<quint8>(Proto::ADDR_AO0 + i));
            }
        });
        connect(m_pg->analog.btnAoWrite, &QPushButton::clicked, this, [this, aoRows] {
            for (int i = 0; i < aoRows.size(); ++i)
                sendWrite(static_cast<quint8>(Proto::ADDR_AO0 + (3 - i)),
                          static_cast<quint16>(aoRows.at(i).dial->value()));
        });
    }

    // ---- Network & Role ----------------------------------------------------
    connect(m_pg->address.btnAddrRead, &QPushButton::clicked, this, [this] {
        sendRead(Proto::ADDR_LOCAL_ADDR);
        sendRead(Proto::ADDR_PEER_ADDR);
    });
    connect(m_pg->address.btnAddrWrite, &QPushButton::clicked, this, [this] {
        sendWrite(Proto::ADDR_LOCAL_ADDR, static_cast<quint16>(m_pg->address.spinSelfAddr->value()));
        sendWrite(Proto::ADDR_PEER_ADDR, static_cast<quint16>(m_pg->address.spinPeerAddr->value()));
    });
    registerHandler(Proto::ADDR_LOCAL_ADDR, [this](quint8, quint16 data) {
        m_pg->address.spinSelfAddr->setValue(data & 0xFF);
    });
    registerHandler(Proto::ADDR_PEER_ADDR, [this](quint8, quint16 data) {
        m_pg->address.spinPeerAddr->setValue(data & 0xFF);
    });
    // ---- Network & Role / Master-Slave Role --------------------------------
    // The role is fixed by the configuration file type (master/slave), so this
    // page only writes the chosen role to the device.
    m_pg->role.comboRole->setCurrentIndex(m_isMaster ? 1 : 0);
    m_pg->role.comboRole->setEnabled(false);
    connect(m_pg->role.btnRoleWrite, &QPushButton::clicked, this, [this] {
        sendWrite(Proto::ADDR_ROLE, static_cast<quint16>(m_pg->role.comboRole->currentIndex()));
    });

    // ---- RF Parameters / Carrier Frequency ---------------------------------
    connect(m_pg->frequency.btnFreqRead, &QPushButton::clicked, this, [this] {
        sendRead(Proto::ADDR_FREQ_LO);
        sendRead(Proto::ADDR_FREQ_HI);
    });
    // Write: store the frequency to the data registers only (pending).
    const auto writeFreq = [this]() -> bool { return writeFrequency(); };
    connect(m_pg->frequency.btnFreqWrite, &QPushButton::clicked, this, [writeFreq] { writeFreq(); });
    // Apply: store the data registers, then trigger 0x29 so the RF bank takes
    // effect immediately. Self-contained (no prior Write required).
    connect(m_pg->frequency.btnFreqApply, &QPushButton::clicked, this, [this, writeFreq] {
        if (writeFreq())
            sendWrite(Proto::ADDR_APPLY_RF, 0x0001);
    });
    const auto showFrequency = [this] {
        m_pg->frequency.comboFreqUnit->setCurrentIndex(0); // display in MHz
        m_pg->frequency.spinFreq->setValue(
                static_cast<double>(m_freqLo | (static_cast<quint32>(m_freqHi) << 16)) / 1e6);
    };
    registerHandler(Proto::ADDR_FREQ_LO, [this, showFrequency](quint8, quint16 data) {
        m_freqLo = data;
        showFrequency();
    });
    registerHandler(Proto::ADDR_FREQ_HI, [this, showFrequency](quint8, quint16 data) {
        m_freqHi = data;
        showFrequency();
    });

    // ---- RF Parameters / Modulation (LoRa or FSK) ---------------------------
    // FSK RxBw raw register value -> Hz (subset, matches SX1278 FskBandwidths).
    const auto fskBwHz = [](int i) {
        static const int t[4] = {125000, 166700, 200000, 250000};
        return t[qBound(0, i, 3)];
    };
    const auto fskBwReg = [](int hz) {
        // raw SX1278 RxBw value for {125,166.7,200,250} kHz
        if (hz <= 125000) return 0x02;
        if (hz <= 166700) return 0x11;
        if (hz <= 200000) return 0x09;
        return 0x01;
    };
    const auto fskBwIndex = [fskBwHz](uint16_t raw) {
        switch (raw) { case 0x02: return 0; case 0x11: return 1; case 0x09: return 2; default: return 3; }
    };
    // Effective data rate: LoRa = SF*BW/2^SF*(4/CR); FSK = bit rate (bps).
    const auto loraDataRate = [](int sf, int bwHz, int cr) {
        const qlonglong ch = static_cast<qlonglong>(sf) * bwHz / (1LL << sf);
        return ch * 4 / cr;
    };
    const auto updateDataRate = [this, loraDataRate] {
        if (m_pg->modulation.comboRadio->currentIndex() == 1) {
            const qlonglong dr = m_pg->modulation.spinFskBitrate->value();
            m_pg->modulation.lblDataRate->setText(
                dr >= 1000 ? tr("%1 kbps").arg(dr / 1000.0, 0, 'f', 1) : tr("%1 bps").arg(dr));
        } else {
            static const int bwHz[3] = {125000, 250000, 500000};
            const int sf = 6 + m_pg->modulation.comboSf->currentIndex();
            const int bw = bwHz[m_pg->modulation.comboBw->currentIndex()];
            const int cr = 5 + m_pg->modulation.comboCr->currentIndex();
            const qlonglong dr = loraDataRate(sf, bw, cr);
            m_pg->modulation.lblDataRate->setText(tr("%1 bps").arg(dr));
        }
    };
    const auto isFsk = [this] { return m_pg->modulation.comboRadio->currentIndex() == 1; };
    // Toggle the visible parameter group for the selected modulation.
    const auto updateModemUi = [this, isFsk, updateDataRate] {
        // The two mode parameter sets live in separate stacked pages (pageLoRa
        // / pageFsk), so switching the stack keeps every field aligned with no
        // empty rows left behind.
        m_pg->modulation.stackModem->setCurrentIndex(isFsk() ? 1 : 0);
        updateDataRate();
    };
    connect(m_pg->modulation.comboRadio, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [updateModemUi](int) { updateModemUi(); });
    connect(m_pg->modulation.comboSf, qOverload<int>(&QComboBox::currentIndexChanged), this, updateDataRate);
    connect(m_pg->modulation.comboBw, qOverload<int>(&QComboBox::currentIndexChanged), this, updateDataRate);
    connect(m_pg->modulation.comboCr, qOverload<int>(&QComboBox::currentIndexChanged), this, updateDataRate);
    connect(m_pg->modulation.spinFskBitrate, qOverload<int>(&QSpinBox::valueChanged), this, updateDataRate);
    connect(m_pg->modulation.spinFskFdev, qOverload<int>(&QSpinBox::valueChanged), this, updateDataRate);
    connect(m_pg->modulation.comboFskBw, qOverload<int>(&QComboBox::currentIndexChanged), this, updateDataRate);
    // SX1278 FSK bounds: BitRate 1.2..300 kbps, Fdev 0.6..200 kHz. The receiver
    // filter must satisfy RxBw >= Fdev + BitRate/2 (single-sided IF bandwidth),
    // so the maximum Fdev is clamped live from the selected RxBw and bit rate.
    m_pg->modulation.spinFskBitrate->setRange(1200, 300000);
    m_pg->modulation.spinFskFdev->setRange(600, 200000);
    const auto updateFskFdevMax = [this] {
        static const int bwHz[4] = {125000, 166700, 200000, 250000};
        const int bitrate = m_pg->modulation.spinFskBitrate->value();
        const int rxbw = bwHz[qBound(0, m_pg->modulation.comboFskBw->currentIndex(), 3)];
        const int maxFdev = qMax(600, qMin(200000, rxbw - bitrate / 2));
        m_pg->modulation.spinFskFdev->setMaximum(maxFdev);
    };
    updateFskFdevMax();
    connect(m_pg->modulation.spinFskBitrate, qOverload<int>(&QSpinBox::valueChanged), this,
            [updateFskFdevMax](int) { updateFskFdevMax(); });
    connect(m_pg->modulation.comboFskBw, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [updateFskFdevMax](int) { updateFskFdevMax(); });
    updateModemUi();

    connect(m_pg->modulation.btnModemRead, &QPushButton::clicked, this, [this] {
        sendRead(Proto::ADDR_RADIO);
    });
    // Write: store the modem params for the selected modulation (pending).
    const auto writeModem = [this] { writeModemParams(); };
    connect(m_pg->modulation.btnModemWrite, &QPushButton::clicked, this, [writeModem] { writeModem(); });
    // Apply: store then trigger 0x29 so the RF bank takes effect immediately.
    connect(m_pg->modulation.btnModemApply, &QPushButton::clicked, this, [this, writeModem] {
        writeModem();
        sendWrite(Proto::ADDR_APPLY_RF, 0x0001);
    });
    // Test: trigger the RF test frame (0x20); the device measures one link
    // exchange and replies with its duration in ms. A one-shot timer guards
    // against a missing reply so the label never sticks on "Testing...".
    connect(m_pg->modulation.btnModemTest, &QPushButton::clicked, this, [this] {
        m_pg->modulation.lblRoundTripValue->setText(tr("Testing..."));
        m_rfTestTimer->start(2500);
        sendWriteTimeout(Proto::ADDR_RF_TEST, 0x0001);
    });
    registerHandler(Proto::ADDR_RF_TEST, [this](quint8 head, quint16 data) {
        m_rfTestTimer->stop();
        if (head == Proto::HEAD_WRITE)
            m_pg->modulation.lblRoundTripValue->setText(tr("%1 ms").arg(data));
    });
    connect(m_rfTestTimer, &QTimer::timeout, this, [this] {
        m_pg->modulation.lblRoundTripValue->setText(QStringLiteral("--"));
        emit statusMessage(tr("RF round-trip test timed out."));
    });
    // The round-trip probe is a master-only operation; hide it on a slave page.
    if (!m_isMaster) {
        m_pg->modulation.btnModemTest->setVisible(false);
        m_pg->modulation.lblRoundTrip->setVisible(false);
        m_pg->modulation.lblRoundTripValue->setVisible(false);
    }
    registerHandler(Proto::ADDR_RADIO, [this, updateModemUi](quint8, quint16 data) {
        const bool fsk = (data & 0x01) != 0;
        m_pg->modulation.comboRadio->setCurrentIndex(fsk ? 1 : 0);
        updateModemUi();
        if (fsk) {
            m_fskReadCount = 0;
            sendRead(static_cast<quint8>(0x62));
            sendRead(static_cast<quint8>(0x63));
            sendRead(static_cast<quint8>(0x64));
            sendRead(static_cast<quint8>(0x65));
            sendRead(Proto::ADDR_FSK_RXBW);
        } else {
            sendRead(Proto::ADDR_SF);
            sendRead(Proto::ADDR_BW);
            sendRead(Proto::ADDR_CR);
        }
    });
    registerHandler(Proto::ADDR_SF, [this](quint8, quint16 data) {
        const int sf = qBound(6, static_cast<int>(data & 0xFF), 12);
        m_pg->modulation.comboSf->setCurrentIndex(sf - 6);
    });
    registerHandler(Proto::ADDR_BW, [this](quint8, quint16 data) {
        int idx = 0;
        if (data == 250)
            idx = 1;
        else if (data == 500)
            idx = 2;
        m_pg->modulation.comboBw->setCurrentIndex(idx);
    });
    registerHandler(Proto::ADDR_CR, [this](quint8, quint16 data) {
        const int cr = qBound(5, static_cast<int>(data & 0xFF), 8);
        m_pg->modulation.comboCr->setCurrentIndex(cr - 5);
    });
    // FSK 读回(0x62~0x73, register 页段)在 onReply 中协调; 此处无需额外 handler。

    // ---- RF Parameters / Power & Preamble ----------------------------------
    // Live-show the equivalent dBm for the power code (PA_BOOST: ~17-(15-code) dBm).
    const auto updatePowerDbm = [this](int code) {
        m_pg->power.lblPowerDbm->setText(tr("%1 dBm").arg(17 - (15 - code)));
    };
    connect(m_pg->power.spinPower, qOverload<int>(&QSpinBox::valueChanged), this,
            [updatePowerDbm](int v) { updatePowerDbm(v); });
    updatePowerDbm(m_pg->power.spinPower->value());
    connect(m_pg->power.btnRfCfgRead, &QPushButton::clicked, this, [this] {
        sendRead(Proto::ADDR_POWER);
        sendRead(Proto::ADDR_PREAMBLE);
        sendRead(Proto::ADDR_SYNCWORD);
        sendRead(Proto::ADDR_LNA);
    });
    connect(m_pg->power.btnRfCfgWrite, &QPushButton::clicked, this, [this] {
        sendWrite(Proto::ADDR_POWER, static_cast<quint16>(m_pg->power.spinPower->value()));
        sendWrite(Proto::ADDR_PREAMBLE, static_cast<quint16>(m_pg->power.spinPreamble->value()));
        sendWrite(Proto::ADDR_SYNCWORD, static_cast<quint16>(m_pg->power.spinSyncword->value()));
        sendWrite(Proto::ADDR_LNA, static_cast<quint16>(m_pg->power.spinLna->value()));
    });
    registerHandler(Proto::ADDR_POWER, [this](quint8, quint16 data) {
        m_pg->power.spinPower->setValue(data & 0xFF);
    });
    // 0x29: re-apply RF params (0x30~0x38) immediately without reboot. UI lives on
    // the Power & Preamble page but applies the whole RF bank.
    // Apply: write the power-bank data registers from the current UI values,
    // then trigger 0x29 so the RF bank takes effect immediately. Self-contained,
    // so it works even if the user pressed Apply without a prior Write.
    connect(m_pg->power.btnRfApply, &QPushButton::clicked, this, [this] {
        sendWrite(Proto::ADDR_POWER, static_cast<quint16>(m_pg->power.spinPower->value()));
        sendWrite(Proto::ADDR_PREAMBLE, static_cast<quint16>(m_pg->power.spinPreamble->value()));
        sendWrite(Proto::ADDR_SYNCWORD, static_cast<quint16>(m_pg->power.spinSyncword->value()));
        sendWrite(Proto::ADDR_LNA, static_cast<quint16>(m_pg->power.spinLna->value()));
        sendWrite(Proto::ADDR_APPLY_RF, 0x0001);
    });
    registerHandler(Proto::ADDR_APPLY_RF, [this](quint8 head, quint16) {
        if (head == Proto::HEAD_WRITE)
            emit statusMessage(tr("RF configuration applied (0x29)."));
    });
    registerHandler(Proto::ADDR_PREAMBLE, [this](quint8, quint16 data) {
        m_pg->power.spinPreamble->setValue(data & 0xFFFF);
    });
    registerHandler(Proto::ADDR_SYNCWORD, [this](quint8, quint16 data) {
        m_pg->power.spinSyncword->setValue(data & 0xFF);
    });
    registerHandler(Proto::ADDR_LNA, [this](quint8, quint16 data) {
        m_pg->power.spinLna->setValue(data & 0xFF);
    });

    // ---- TX Task Table (master only) ---------------------------------------
    if (m_isMaster) {
        QTableWidget *t = m_pg->tasks.tableTasks;
        t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch); // Name
        t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        t->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch); // Interval
        t->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        t->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        t->verticalHeader()->setVisible(false);
        // Rows are dynamic (Add/Remove, 1..32). Task number = row index.
        const auto ensureTaskRows = [t](int count) {
            const int cur = t->rowCount();
            if (count > cur)
                t->setRowCount(count);
            for (int n = cur; n < count; ++n) {
                auto *it0 = new QTableWidgetItem(QString::number(n));
                it0->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                auto *it1 = new QTableWidgetItem(QString());
                it1->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); // edit via editor only
                auto *it2 = new QTableWidgetItem;
                it2->setFlags((it2->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
                it2->setCheckState(Qt::Unchecked);
                auto *it3 = new QTableWidgetItem(QStringLiteral("0"));
                it3->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                auto *it4 = new QTableWidgetItem(QStringLiteral("00"));
                it4->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                auto *it5 = new QTableWidgetItem(QStringLiteral("00"));
                it5->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                t->setItem(n, 0, it0);
                t->setItem(n, 1, it1);
                t->setItem(n, 2, it2);
                t->setItem(n, 3, it3);
                t->setItem(n, 4, it4);
                t->setItem(n, 5, it5);
            }
        };
        const auto refreshTaskNumbers = [t] {
            for (int n = 0; n < t->rowCount(); ++n)
                t->item(n, 0)->setText(QString::number(n));
        };
        ensureTaskRows(1);
        // CI byte bits (see frame.md): DH DL A3 A2 A1 A0 word compand.
        // 10-bit (word bit = 1) and A-law compand are mutually exclusive.
        const auto packCi = [this](QCheckBox *dh, QCheckBox *dl, QCheckBox *a3, QCheckBox *a2,
                                   QCheckBox *a1, QCheckBox *a0, QComboBox *word, QCheckBox *comp) {
            quint8 v = 0;
            v |= dh->isChecked() ? 0x80 : 0;
            v |= dl->isChecked() ? 0x40 : 0;
            v |= a3->isChecked() ? 0x20 : 0;
            v |= a2->isChecked() ? 0x10 : 0;
            v |= a1->isChecked() ? 0x08 : 0;
            v |= a0->isChecked() ? 0x04 : 0;
            const bool is10Bit = word->currentIndex() == 0; // "10-bit Length"
            v |= is10Bit ? 0x02 : 0;
            v |= (!is10Bit && comp->isChecked()) ? 0x01 : 0;
            return v;
        };
        const auto packCi1 = [this, packCi] {
            return packCi(m_pg->tasks.chk1Dh, m_pg->tasks.chk1Dl, m_pg->tasks.chk1A3,
                          m_pg->tasks.chk1A2, m_pg->tasks.chk1A1, m_pg->tasks.chk1A0,
                          m_pg->tasks.combo1Word, m_pg->tasks.chk1Comp);
        };
        const auto packCi2 = [this, packCi] {
            return packCi(m_pg->tasks.chk2Dh, m_pg->tasks.chk2Dl, m_pg->tasks.chk2A3,
                          m_pg->tasks.chk2A2, m_pg->tasks.chk2A1, m_pg->tasks.chk2A0,
                          m_pg->tasks.combo2Word, m_pg->tasks.chk2Comp);
        };
        const auto applyCiToEditor = [this](quint8 v, QCheckBox *dh, QCheckBox *dl, QCheckBox *a3,
                                            QCheckBox *a2, QCheckBox *a1, QCheckBox *a0,
                                            QComboBox *word, QCheckBox *comp) {
            dh->setChecked(v & 0x80);
            dl->setChecked(v & 0x40);
            a3->setChecked(v & 0x20);
            a2->setChecked(v & 0x10);
            a1->setChecked(v & 0x08);
            a0->setChecked(v & 0x04);
            word->setCurrentIndex((v & 0x02) ? 0 : 1);
            comp->setChecked(v & 0x01);
        };
        // 10-bit word length disables the A-law compand checkbox (frame.md).
        const auto updateCompandState = [this] {
            const bool c1 = m_pg->tasks.combo1Word->currentIndex() == 0;
            m_pg->tasks.chk1Comp->setEnabled(!c1);
            if (c1)
                m_pg->tasks.chk1Comp->setChecked(false);
            const bool c2 = m_pg->tasks.combo2Word->currentIndex() == 0;
            m_pg->tasks.chk2Comp->setEnabled(!c2);
            if (c2)
                m_pg->tasks.chk2Comp->setChecked(false);
        };
        // Live binding: write the editor state into the selected row.
        const auto syncRowFromEditor = [this, t, packCi1, packCi2](int n) {
            if (n < 0 || n >= t->rowCount())
                return;
            t->item(n, 1)->setText(m_pg->tasks.editTaskName->text());
            t->item(n, 2)->setCheckState(m_pg->tasks.chkTaskEna->isChecked() ? Qt::Checked
                                                                             : Qt::Unchecked);
            t->item(n, 3)->setText(
                    QString::number(m_pg->tasks.spinTaskInterval->value(), 'f', 2));
            t->item(n, 4)->setText(hex8(packCi1()));
            t->item(n, 5)->setText(hex8(packCi2()));
        };
        const auto syncCurrentRow = [this, t, syncRowFromEditor] {
            if (!m_taskLoading)
                syncRowFromEditor(t->currentRow());
        };
        // Load the selected row into the editor.
        const auto loadRowToEditor = [this, t, applyCiToEditor, updateCompandState] {
            const int n = t->currentRow();
            if (n < 0)
                return;
            m_taskLoading = true;
            m_pg->tasks.editTaskName->setText(t->item(n, 1)->text());
            m_pg->tasks.chkTaskEna->setChecked(t->item(n, 2)->checkState() == Qt::Checked);
            m_pg->tasks.spinTaskInterval->setValue(t->item(n, 3)->text().toDouble());
            quint16 ci1 = 0, ci2 = 0;
            parseHex(t->item(n, 4)->text(), 0xFF, ci1);
            parseHex(t->item(n, 5)->text(), 0xFF, ci2);
            applyCiToEditor(static_cast<quint8>(ci1), m_pg->tasks.chk1Dh, m_pg->tasks.chk1Dl,
                            m_pg->tasks.chk1A3, m_pg->tasks.chk1A2, m_pg->tasks.chk1A1,
                            m_pg->tasks.chk1A0, m_pg->tasks.combo1Word, m_pg->tasks.chk1Comp);
            applyCiToEditor(static_cast<quint8>(ci2), m_pg->tasks.chk2Dh, m_pg->tasks.chk2Dl,
                            m_pg->tasks.chk2A3, m_pg->tasks.chk2A2, m_pg->tasks.chk2A1,
                            m_pg->tasks.chk2A0, m_pg->tasks.combo2Word, m_pg->tasks.chk2Comp);
            m_taskLoading = false;
            updateCompandState();
        };
        connect(t, &QTableWidget::itemSelectionChanged, this, loadRowToEditor);
        t->selectRow(0); // select the first task so the editor is usable immediately

        // The table is read-only; every editor control updates the selected row
        // immediately (no separate Apply-row button).
        connect(m_pg->tasks.editTaskName, &QLineEdit::textEdited, this, syncCurrentRow);
        connect(m_pg->tasks.chkTaskEna, &QCheckBox::toggled, this, syncCurrentRow);
        connect(m_pg->tasks.spinTaskInterval, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, syncCurrentRow);
        const QList<QCheckBox *> ci1Boxes = {m_pg->tasks.chk1Dh, m_pg->tasks.chk1Dl,
                                             m_pg->tasks.chk1A3, m_pg->tasks.chk1A2,
                                             m_pg->tasks.chk1A1, m_pg->tasks.chk1A0,
                                             m_pg->tasks.chk1Comp};
        for (QCheckBox *cb : ci1Boxes)
            connect(cb, &QCheckBox::toggled, this, syncCurrentRow);
        const QList<QCheckBox *> ci2Boxes = {m_pg->tasks.chk2Dh, m_pg->tasks.chk2Dl,
                                             m_pg->tasks.chk2A3, m_pg->tasks.chk2A2,
                                             m_pg->tasks.chk2A1, m_pg->tasks.chk2A0,
                                             m_pg->tasks.chk2Comp};
        for (QCheckBox *cb : ci2Boxes)
            connect(cb, &QCheckBox::toggled, this, syncCurrentRow);
        connect(m_pg->tasks.combo1Word, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, updateCompandState, syncCurrentRow](int) {
                    updateCompandState();
                    syncCurrentRow();
                });
        connect(m_pg->tasks.combo2Word, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, updateCompandState, syncCurrentRow](int) {
                    updateCompandState();
                    syncCurrentRow();
                });
        updateCompandState();
        connect(m_pg->tasks.btnTaskAdd, &QPushButton::clicked, this, [this, t, ensureTaskRows,
                                                                      refreshTaskNumbers] {
            if (t->rowCount() >= 32) {
                QMessageBox::warning(this, tr("Task Table"),
                                     tr("Task table is full (32 tasks)."));
                return;
            }
            ensureTaskRows(t->rowCount() + 1);
            refreshTaskNumbers();
            t->setCurrentCell(t->rowCount() - 1, 0); // select the new row for editing
        });
        connect(m_pg->tasks.btnTaskDelete, &QPushButton::clicked, this,
                [this, t, refreshTaskNumbers] {
                    if (t->rowCount() <= 1) {
                        QMessageBox::warning(this, tr("Task Table"),
                                             tr("At least one task is required."));
                        return;
                    }
                    const int n = t->currentRow();
                    if (n < 0) {
                        QMessageBox::warning(this, tr("Task Table"),
                                             tr("Select a task row to delete."));
                        return;
                    }
                    t->removeRow(n);
                    refreshTaskNumbers();
                    t->setCurrentCell(qMin(n, t->rowCount() - 1), 0);
                });
        connect(m_pg->tasks.btnTaskRead, &QPushButton::clicked, this, [this, ensureTaskRows] {
            ensureTaskRows(32); // expand so every slot has a cell to fill
            for (int n = 0; n < 32; ++n) {
                sendRead(Proto::taskCi1(n));
                sendRead(Proto::taskEna(n));
                sendRead(Proto::taskPeriodLo(n));
                sendRead(Proto::taskPeriodHi(n));
                sendRead(Proto::taskCi2(n));
            }
        });
        connect(m_pg->tasks.btnTaskApply, &QPushButton::clicked, this, [this] {
            QTableWidget *table = m_pg->tasks.tableTasks;
            if (table->rowCount() < 1) {
                QMessageBox::warning(this, tr("Task Table"),
                                     tr("At least one task is required before applying."));
                return;
            }
            // Validate every visible row first, so a bad row aborts before any
            // partial write reaches the device.
            for (int n = 0; n < table->rowCount(); ++n) {
                quint16 c1 = 0, c2 = 0;
                if (!parseHex(table->item(n, 4)->text(), 0xFF, c1)
                        || !parseHex(table->item(n, 5)->text(), 0xFF, c2)) {
                    QMessageBox::warning(this, tr("Task Table"),
                                         tr("Task %1: CI1/CI2 must be hex 00~FF.").arg(n));
                    return;
                }
                bool ok = false;
                const double periodMs = table->item(n, 3)->text().toDouble(&ok);
                if (!ok || periodMs < 1.0 / 6.0) {
                    QMessageBox::warning(this, tr("Task Table"),
                                         tr("Task %1: interval must be at least 1/6 ms.")
                                                 .arg(n));
                    return;
                }
                const qint64 units = qRound64(periodMs * 6.0);
                if (units < 1 || units > 0xFFFF) {
                    QMessageBox::warning(this, tr("Task Table"),
                                         tr("Task %1: interval exceeds ~10.9 s limit.")
                                                 .arg(n));
                    return;
                }
            }
            writeAllTasks();
            emit statusMessage(tr("Applying task table..."));
        });
        // After a device reply updates a cell, refresh the editor if that row is
        // currently selected, so the editor always mirrors the device data.
        const auto maybeReloadEditor = [t, loadRowToEditor](int n) {
            if (t->currentRow() == n)
                loadRowToEditor();
        };
        for (int n = 0; n < 32; ++n) {
            registerHandler(Proto::taskCi1(n),
                            [this, n, ensureTaskRows, maybeReloadEditor](quint8, quint16 data) {
                                ensureTaskRows(n + 1);
                                m_pg->tasks.tableTasks->item(n, 4)->setText(hex8(data));
                                maybeReloadEditor(n);
                            });
            registerHandler(Proto::taskEna(n),
                            [this, n, ensureTaskRows, maybeReloadEditor](quint8, quint16 data) {
                                ensureTaskRows(n + 1);
                                m_pg->tasks.tableTasks->item(n, 2)->setCheckState(
                                        (data & 0x01) ? Qt::Checked : Qt::Unchecked);
                                maybeReloadEditor(n);
                            });
            registerHandler(Proto::taskPeriodLo(n),
                            [this, n, ensureTaskRows, maybeReloadEditor](quint8, quint16 data) {
                                ensureTaskRows(n + 1);
                                m_periodLo[n] = static_cast<quint8>(data & 0xFF);
                                updatePeriodItem(n);
                                maybeReloadEditor(n);
                            });
            registerHandler(Proto::taskPeriodHi(n),
                            [this, n, ensureTaskRows, maybeReloadEditor](quint8, quint16 data) {
                                ensureTaskRows(n + 1);
                                m_periodHi[n] = static_cast<quint8>(data & 0xFF);
                                updatePeriodItem(n);
                                maybeReloadEditor(n);
                            });
            registerHandler(Proto::taskCi2(n),
                            [this, n, ensureTaskRows, maybeReloadEditor](quint8, quint16 data) {
                                ensureTaskRows(n + 1);
                                m_pg->tasks.tableTasks->item(n, 5)->setText(hex8(data));
                                maybeReloadEditor(n);
                            });
        }
    } else {
        // Slave (no task table): CI2 slot 0 (0x40) is the content the slave
        // returns to the master; used by the bundled example configurations.
        registerHandler(static_cast<quint8>(0x40), [this](quint8, quint16 data) {
            m_slaveCi2 = data & 0xFF;
        });
    }

    // ---- RS-485 ------------------------------------------------------------
    connect(m_pg->rs485.btn485Read, &QPushButton::clicked, this, [this] {
        sendRead(Proto::ADDR_485_BAUD);
        sendRead(Proto::ADDR_485_BUF);
        sendRead(Proto::ADDR_485_TIMEOUT);
        sendRead(Proto::ADDR_485_ENABLE);
    });
    connect(m_pg->rs485.btn485Write, &QPushButton::clicked, this, [this] {
        static const int baudTable[8] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
        sendWrite(Proto::ADDR_485_BAUD,
                  static_cast<quint16>(baudTable[m_pg->rs485.combo485Baud->currentIndex()]));
        sendWrite(Proto::ADDR_485_BUF, static_cast<quint16>(m_pg->rs485.spin485Buf->value()));
        sendWrite(Proto::ADDR_485_TIMEOUT, static_cast<quint16>(m_pg->rs485.spin485Timeout->value()));
        sendWrite(Proto::ADDR_485_ENABLE, m_pg->rs485.chk485Enable->isChecked() ? 1 : 0);
    });
    registerHandler(Proto::ADDR_485_BAUD, [this](quint8, quint16 data) {
        static const int baudTable[8] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
        int idx = 7; // default 115200
        for (int i = 0; i < 8; ++i) {
            if (baudTable[i] == static_cast<int>(data)) {
                idx = i;
                break;
            }
        }
        m_pg->rs485.combo485Baud->setCurrentIndex(idx);
    });
    registerHandler(Proto::ADDR_485_BUF, [this](quint8, quint16 data) {
        m_pg->rs485.spin485Buf->setValue(data & 0xFF);
    });
    registerHandler(Proto::ADDR_485_TIMEOUT, [this](quint8, quint16 data) {
        m_pg->rs485.spin485Timeout->setValue(data & 0xFFFF);
    });
    registerHandler(Proto::ADDR_485_ENABLE, [this](quint8, quint16 data) {
        m_pg->rs485.chk485Enable->setChecked((data & 0x01) != 0);
    });

    // ---- Debug & Statistics / Counters & Test ------------------------------
    connect(m_pg->counters.btnCountersRead, &QPushButton::clicked, this, [this] {
        sendRead(Proto::ADDR_RX_COUNT);
        sendRead(Proto::ADDR_CRC_ERR_COUNT);
        sendRead(Proto::ADDR_TX_OVERFLOW_COUNT);
    });
    connect(m_pg->counters.btnClearRx, &QPushButton::clicked, this,
            [this] { sendWrite(Proto::ADDR_RX_COUNT, 0); });
    connect(m_pg->counters.btnClearCrc, &QPushButton::clicked, this,
            [this] { sendWrite(Proto::ADDR_CRC_ERR_COUNT, 0); });
    connect(m_pg->counters.btnClearOvf, &QPushButton::clicked, this,
            [this] { sendWrite(Proto::ADDR_TX_OVERFLOW_COUNT, 0); });
    // RF test (0x20 master status frame) only makes sense on the master; hide it
    // on a slave profile page.
    m_pg->counters.btnRfTest->setVisible(m_isMaster);
    connect(m_pg->counters.btnRfTest, &QPushButton::clicked, this,
            [this] { sendWrite(Proto::ADDR_RF_TEST, 0x0001); });
    registerHandler(Proto::ADDR_RX_COUNT, [this](quint8, quint16 data) {
        m_pg->counters.editRxCount->setText(QString::number(data));
    });
    registerHandler(Proto::ADDR_CRC_ERR_COUNT, [this](quint8, quint16 data) {
        m_pg->counters.editCrcErr->setText(QString::number(data));
    });
    registerHandler(Proto::ADDR_TX_OVERFLOW_COUNT, [this](quint8, quint16 data) {
        m_pg->counters.editTxOverflow->setText(QString::number(data));
    });

    // ---- Debug & Statistics / RSSI / SNR -----------------------------------
    connect(m_pg->rssi.btnRssiRead, &QPushButton::clicked, this, [this] {
        sendRead(Proto::ADDR_RSSI);
        sendRead(Proto::ADDR_SNR);
    });
    registerHandler(Proto::ADDR_RSSI, [this](quint8, quint16 data) {
        m_pg->rssi.editRssi->setText(tr("%1 dBm").arg(static_cast<qint16>(data)));
    });
    registerHandler(Proto::ADDR_SNR, [this](quint8, quint16 data) {
        m_pg->rssi.editSnr->setText(tr("%1 dB").arg(static_cast<qint16>(data)));
    });

    // ---- Debug & Statistics / Frequency Calibration -------------------------
    connect(m_pg->calibration.btnCalRead, &QPushButton::clicked, this, [this] {
        sendRead(Proto::ADDR_CAL_TRIGGER);
        sendRead(Proto::ADDR_CAL_VALUE);
        sendRead(Proto::ADDR_CAL_SWITCH);
    });
    connect(m_pg->calibration.btnCalWrite, &QPushButton::clicked, this, [this] {
        const int value = m_pg->calibration.spinCalValue->value();
        sendWrite(Proto::ADDR_CAL_VALUE, static_cast<quint16>(static_cast<quint16>(value)));
        sendWrite(Proto::ADDR_CAL_SWITCH, m_pg->calibration.chkCalEnable->isChecked() ? 1 : 0);
    });
    connect(m_pg->calibration.btnCalTrigger, &QPushButton::clicked, this, [this] {
        if (!m_pg->calibration.chkCalEnable->isChecked()) {
            QMessageBox::warning(this, tr("Frequency Calibration"),
                                 tr("Enable the calibration switch first."));
            return;
        }
        sendWrite(Proto::ADDR_CAL_TRIGGER, 0x0001);
    });
    registerHandler(Proto::ADDR_CAL_TRIGGER, [this](quint8, quint16 data) {
        m_pg->calibration.lblCalStatus->setText(QString::number(data & 0xFF));
    });
    registerHandler(Proto::ADDR_CAL_VALUE, [this](quint8, quint16 data) {
        m_pg->calibration.spinCalValue->setValue(static_cast<qint16>(data));
    });
    registerHandler(Proto::ADDR_CAL_SWITCH, [this](quint8, quint16 data) {
        m_pg->calibration.chkCalEnable->setChecked((data & 0x01) != 0);
    });

    // ---- Register Access ----------------------------------------------------
    {
        static const char *const regDesc[32] = {
                "Transceiver FIFO buffer",
                "Operating mode / LoRa vs FSK, sleep, standby",
                "Reserved",
                "Reserved",
                "Reserved",
                "Reserved",
                "Carrier frequency (MSB)",
                "Carrier frequency (mid)",
                "Carrier frequency (LSB)",
                "PA selection & output power",
                "PA ramping / low-power Tx",
                "Over-current protection",
                "LNA gain & boost",
                "FIFO read/write pointer",
                "FIFO Tx base address",
                "FIFO Rx base address",
                "FIFO current Rx address",
                "IRQ mask (0=disabled)",
                "IRQ flags (status)",
                "Received payload byte count",
                "RSSI of last packet",
                "SNR of last packet",
                "Est. frequency error of last packet",
                "Packet counter (MSB)",
                "Packet counter (LSB)",
                "Frequency error indicator (MSB)",
                "Frequency error indicator (mid)",
                "Frequency error indicator (LSB)",
                "Wideband RSSI",
                "Modem config 1: BW / CR / implicit header",
                "Modem config 2: SF / CRC / Tx mode",
                "LoRa symbol timeout (LSB)"};
        QTableWidget *t = m_pg->regPage.tableRegisters;
        // Cards are built with setItem(); QTableWidget does NOT auto-add rows,
        // so the rows must exist first or the table renders empty.
        t->setRowCount(32);
        t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        t->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        t->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        t->verticalHeader()->setVisible(false);
        for (int r = 0; r < 32; ++r) {
            auto *itId = new QTableWidgetItem(hex8(r)); // ID
            itId->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            auto *itAddr = new QTableWidgetItem(QStringLiteral("0x%1")
                                                        .arg(0x60 | r, 2, 16, QChar('0'))
                                                        .toUpper());
            itAddr->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            auto *itRd = new QTableWidgetItem(QStringLiteral("--")); // Read Value
            itRd->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            auto *itWr = new QTableWidgetItem(QStringLiteral("00")); // Write Value (hex, editable)
            auto *itDesc = new QTableWidgetItem(
                    QCoreApplication::translate("RegisterPage", regDesc[r]));
            itDesc->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            t->setItem(r, 0, itId);
            t->setItem(r, 1, itAddr);
            t->setItem(r, 2, itRd);
            t->setItem(r, 3, itWr);
            t->setItem(r, 4, itDesc);
        }
        // Validate the editable Write Value column as hex 00~FF.
        const auto validateWriteValue = [t](int row) {
            quint16 v = 0;
            return row >= 0 && parseHex(t->item(row, 3)->text(), 0xFF, v);
        };
        // Read the selected register into the Read Value column.
        connect(m_pg->regPage.btnRegRead, &QPushButton::clicked, this, [this, t] {
            const int row = t->currentRow();
            if (row < 0) {
                QMessageBox::warning(this, tr("Register Access"),
                                     tr("Select a register row first."));
                return;
            }
            sendRead(static_cast<quint8>(Proto::ADDR_REG_BASE | (row & 0x1F)));
        });
        // Read All: fetch every register into the Read Value column.
        connect(m_pg->regPage.btnRegReadAll, &QPushButton::clicked, this,
                [this] {
                    for (int r = 0; r < 32; ++r)
                        sendRead(static_cast<quint8>(Proto::ADDR_REG_BASE | (r & 0x1F)));
                    emit statusMessage(tr("Reading all registers..."));
                });
        // Apply the selected row's Write Value.
        connect(m_pg->regPage.btnRegApply, &QPushButton::clicked, this,
                [this, t, validateWriteValue] {
                    const int row = t->currentRow();
                    if (row < 0) {
                        QMessageBox::warning(this, tr("Register Access"),
                                             tr("Select a register row first."));
                        return;
                    }
                    if (!validateWriteValue(row)) {
                        QMessageBox::warning(this, tr("Register Access"),
                                             tr("Write Value must be hex 00~FF."));
                        return;
                    }
                    quint16 v = 0;
                    parseHex(t->item(row, 3)->text(), 0xFF, v);
                    sendWrite(static_cast<quint8>(Proto::ADDR_REG_BASE | (row & 0x1F)),
                              static_cast<quint16>(v & 0xFF));
                });
        // Write: write the selected row's Write Value (hex validated).
        connect(m_pg->regPage.btnRegWrite, &QPushButton::clicked, this,
                [this, t, validateWriteValue] {
                    const int row = t->currentRow();
                    if (row < 0) {
                        QMessageBox::warning(this, tr("Register Access"),
                                             tr("Select a register row first."));
                        return;
                    }
                    if (!validateWriteValue(row)) {
                        QMessageBox::warning(this, tr("Register Access"),
                                             tr("Write Value must be hex 00~FF."));
                        return;
                    }
                    quint16 v = 0;
                    parseHex(t->item(row, 3)->text(), 0xFF, v);
                    sendWrite(static_cast<quint8>(Proto::ADDR_REG_BASE | (row & 0x1F)),
                              static_cast<quint16>(v & 0xFF));
                });
        // Write All: write every row's Write Value (hex validated).
        connect(m_pg->regPage.btnRegWriteAll, &QPushButton::clicked, this,
                [this, t, validateWriteValue] {
                    for (int r = 0; r < 32; ++r) {
                        if (!validateWriteValue(r)) {
                            QMessageBox::warning(
                                    this, tr("Register Access"),
                                    tr("Row %1: Write Value must be hex 00~FF.").arg(r));
                            return;
                        }
                        quint16 v = 0;
                        parseHex(t->item(r, 3)->text(), 0xFF, v);
                        sendWrite(static_cast<quint8>(Proto::ADDR_REG_BASE | (r & 0x1F)),
                                  static_cast<quint16>(v & 0xFF));
                    }
                    emit statusMessage(tr("Writing all register values..."));
                });
    }
}

void ConfigPage::doFskReadUpdate()
{
    const quint16 br = static_cast<quint16>((m_fskBitMsb << 8) | m_fskBitLsb);
    if (br)
        m_pg->modulation.spinFskBitrate->setValue(static_cast<int>(32000000 / br));
    const quint16 fd = static_cast<quint16>((m_fskFdevMsb << 8) | m_fskFdevLsb);
    m_pg->modulation.spinFskFdev->setValue(fd * 61);
    // spinFskBitrate/spinFskFdev valueChanged already refresh the data-rate label.
}

void ConfigPage::updatePeriodItem(int n)
{
    const quint32 units = m_periodLo[n] | (static_cast<quint32>(m_periodHi[n]) << 8);
    // Show the stored TIM4 6kHz-tick field (166.7us each) as milliseconds: ms = units / 6.
    m_pg->tasks.tableTasks->item(n, 3)->setText(QString::number(units / 6.0, 'f', 2));
}

void ConfigPage::refreshPorts()
{
    ui->comboPort->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports)
        ui->comboPort->addItem(
                QString("%1 - %2").arg(info.portName(),
                                       info.description().isEmpty() ? tr("Unknown port")
                                                                    : info.description()),
                info.portName());
}

void ConfigPage::toggleConnection()
{
    if (m_connected) {
        QMetaObject::invokeMethod(m_threads->worker(), "closePort");
        return;
    }
    const QString portName = ui->comboPort->currentData().toString();
    if (portName.isEmpty()) {
        QMessageBox::warning(this, tr("Connection"), tr("No serial port selected."));
        return;
    }
    ensureThreads();
    m_portName = portName;
    emit statusMessage(tr("Opening %1...").arg(portName));
    QMetaObject::invokeMethod(m_threads->worker(), "openPort", Q_ARG(QString, portName));
}

void ConfigPage::onTreeClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (item && item->childCount() == 0)
        ui->treeIndex->setCurrentItem(item);
}

void ConfigPage::onTreeCurrentChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    Q_UNUSED(previous)
    if (!current || current->childCount() != 0)
        return;
    QWidget *page = current->data(0, Qt::UserRole).value<QWidget *>();
    if (page)
        ui->stackedPages->setCurrentWidget(page);
}

void ConfigPage::ensureThreads()
{
    if (m_threads)
        return;
    m_threads = new ThreadManager(this);
    m_threads->start();
    SerialWorker *w = m_threads->worker();
    connect(w, &SerialWorker::opened, this, &ConfigPage::onWorkerOpened);
    connect(w, &SerialWorker::closed, this, &ConfigPage::onWorkerClosed);
    connect(w, &SerialWorker::errorOccurred, this, &ConfigPage::onWorkerError);
    connect(w, &SerialWorker::replyReceived, this, &ConfigPage::onReply);
    connect(w, &SerialWorker::busyChanged, this, &ConfigPage::setBusy);
    connect(w, &SerialWorker::countersChanged, this, &ConfigPage::countersChanged);
    connect(w, &SerialWorker::statusMessage, this, &ConfigPage::statusMessage);
    applyConnectionSettings();
}

void ConfigPage::applyConnectionSettings()
{
    QSettings s;
    const int ms = qBound(100, s.value("connection/timeoutMs", 1000).toInt(), 10000);
    if (m_threads)
        QMetaObject::invokeMethod(m_threads->worker(), "setReplyTimeout", Q_ARG(int, ms));
}

void ConfigPage::setAutoRefreshInterval(int ms)
{
    if (!m_autoTimer) {
        m_autoTimer = new QTimer(this);
        m_autoTimer->setSingleShot(false);
        connect(m_autoTimer, &QTimer::timeout, this, &ConfigPage::doAutoRefresh);
    }
    m_autoTimer->stop();
    if (ms > 0)
        m_autoTimer->start(ms);
}

void ConfigPage::doAutoRefresh()
{
    // Skip while a request queue is still draining or we are not connected, so
    // the auto refresh never interleaves with a manual write (e.g. task table
    // Apply All). Requests are already serialized by the worker queue.
    if (!m_connected || m_busy)
        return;
    for (int i = 0; i < 7; ++i) // RTC 0x03..0x09
        sendRead(static_cast<quint8>(Proto::ADDR_RTC_MS + i));
    sendRead(Proto::ADDR_DI_LO); // DI/DO
    sendRead(Proto::ADDR_DI_HI);
    sendRead(Proto::ADDR_DO_LO);
    sendRead(Proto::ADDR_DO_HI);
    for (int i = 0; i < 4; ++i) // AI0..3
        sendRead(static_cast<quint8>(Proto::ADDR_AI0 + i));
    sendRead(Proto::ADDR_RX_COUNT); // counters
    sendRead(Proto::ADDR_CRC_ERR_COUNT);
    sendRead(Proto::ADDR_TX_OVERFLOW_COUNT);
    sendRead(Proto::ADDR_RSSI); // RSSI/SNR
    sendRead(Proto::ADDR_SNR);
}

void ConfigPage::setConnected(bool connected)
{
    ui->comboPort->setEnabled(!connected);
    ui->btnPortRefresh->setEnabled(!connected);
    ui->btnConnect->setText(connected ? tr("Disconnect") : tr("Connect"));
}

void ConfigPage::setBusy(bool busy)
{
    m_busy = busy;
    // Freeze every button on the config pages (connection row is on the left
    // panel, outside stackedPages, and stays clickable) until the queue drains.
    const auto buttons = ui->stackedPages->findChildren<QAbstractButton *>();
    for (QAbstractButton *b : buttons)
        b->setEnabled(!busy);
    emit busyChanged(busy);
}

void ConfigPage::onWorkerOpened(const QString &portName)
{
    m_connected = true;
    m_disconnectWarned = false;
    setConnected(true);
    emit connectionChanged(true);
    emit statusMessage(tr("Connected to %1.").arg(portName));
}

void ConfigPage::onWorkerClosed()
{
    m_connected = false;
    setConnected(false);
    emit connectionChanged(false);
    emit statusMessage(tr("Disconnected."));
}

void ConfigPage::onWorkerError(const QString &message)
{
    emit statusMessage(message);
    // Serial error (e.g. the USB cable was pulled while connected) => warn once.
    // A normal Disconnect goes through onWorkerClosed without this signal.
    if (m_connected && !m_disconnectWarned) {
        m_disconnectWarned = true;
        QMessageBox::warning(this, tr("Connection Lost"), message);
    }
}

void ConfigPage::onReply(quint8 head, quint8 addr, quint16 data)
{
    if (addr >= Proto::ADDR_REG_BASE) {
        const int reg = addr - Proto::ADDR_REG_BASE;
        QTableWidget *t = m_pg->regPage.tableRegisters;
        if (reg >= 0 && reg < t->rowCount())
            t->item(reg, 2)->setText(hex8(data)); // Read Value column
        // FSK modem readbacks that live on this SX1278 direct-write page:
        switch (addr) {
        case 0x62: m_fskBitMsb = static_cast<quint8>(data & 0xFF); break;
        case 0x63: m_fskBitLsb = static_cast<quint8>(data & 0xFF); break;
        case 0x64: m_fskFdevMsb = static_cast<quint8>(data & 0xFF); break;
        case 0x65: m_fskFdevLsb = static_cast<quint8>(data & 0xFF); break;
        case Proto::ADDR_FSK_RXBW: {
            int idx = 0;
            if ((data & 0xFF) == 0x11) idx = 1;
            else if ((data & 0xFF) == 0x09) idx = 2;
            else if ((data & 0xFF) == 0x01) idx = 3;
            m_pg->modulation.comboFskBw->setCurrentIndex(idx);
            break;
        }
        default: break;
        }
        if (addr == 0x62 || addr == 0x63 || addr == 0x64 || addr == 0x65
                || addr == Proto::ADDR_FSK_RXBW) {
            if (++m_fskReadCount >= 5)
                doFskReadUpdate();
        }
        emit statusMessage(tr("Reg 0x%1 = 0x%2.")
                                   .arg(reg, 2, 16, QChar('0'))
                                   .arg(data & 0xFF, 2, 16, QChar('0')));
        return;
    }
    const auto it = m_handlers.constFind(addr);
    if (it != m_handlers.constEnd() && *it)
        (*it)(head, data);
}

void ConfigPage::sendFrame(quint8 head, quint8 addr, quint16 data, int timeoutMs)
{
    if (!m_threads || !m_connected) {
        emit statusMessage(tr("Not connected."));
        return;
    }
    QMetaObject::invokeMethod(m_threads->worker(), "sendFrame", Q_ARG(quint8, head),
                              Q_ARG(quint8, addr), Q_ARG(quint16, data),
                              Q_ARG(int, timeoutMs));
}

QString ConfigPage::connectionText() const
{
    // English on purpose: shown in the status bar, which is not translated.
    return m_connected ? QStringLiteral("Connected: %1").arg(m_portName)
                       : QStringLiteral("Port: Not connected");
}

/* ========================= Bulk device operations ========================= */
bool ConfigPage::writeFrequency()
{
    const bool mhz = m_pg->frequency.comboFreqUnit->currentIndex() == 0;
    const double val = m_pg->frequency.spinFreq->value();
    const quint64 hz = static_cast<quint64>(mhz ? val * 1e6 : val * 1e3);
    if (hz < 137000000ull || hz > 525000000ull) {
        QMessageBox::warning(this, tr("Carrier Frequency"),
                             tr("Frequency out of range: 137.000 ~ 525.000 MHz."));
        return false;
    }
    sendWrite(Proto::ADDR_FREQ_LO, static_cast<quint16>(hz & 0xFFFF));
    sendWrite(Proto::ADDR_FREQ_HI, static_cast<quint16>((hz >> 16) & 0xFFFF));
    return true;
}

void ConfigPage::writePowerBank()
{
    sendWrite(Proto::ADDR_POWER, static_cast<quint16>(m_pg->power.spinPower->value()));
    sendWrite(Proto::ADDR_PREAMBLE, static_cast<quint16>(m_pg->power.spinPreamble->value()));
    sendWrite(Proto::ADDR_SYNCWORD, static_cast<quint16>(m_pg->power.spinSyncword->value()));
    sendWrite(Proto::ADDR_LNA, static_cast<quint16>(m_pg->power.spinLna->value()));
}

void ConfigPage::writeModemParams()
{
    const bool fsk = m_pg->modulation.comboRadio->currentIndex() == 1;
    sendWrite(Proto::ADDR_RADIO, static_cast<quint16>(fsk ? 1 : 0));
    if (fsk) {
        // SX1278 FSK: reject combinations the receiver cannot demodulate
        // (single-sided IF bandwidth must cover Fdev + BitRate/2 inside RxBw).
        static const int fskBwHz[4] = {125000, 166700, 200000, 250000};
        const int bitrate = m_pg->modulation.spinFskBitrate->value();
        const int fdev = m_pg->modulation.spinFskFdev->value();
        const int rxbw = fskBwHz[qBound(0, m_pg->modulation.comboFskBw->currentIndex(), 3)];
        if (fdev + bitrate / 2 > rxbw) {
            QMessageBox::warning(
                    this, tr("Modulation"),
                    tr("FSK: Fdev + BitRate/2 (=%1 Hz) exceeds RxBw (=%2 Hz).")
                            .arg(fdev + bitrate / 2)
                            .arg(rxbw));
            return;
        }
        const int br = static_cast<int>(32000000ULL / bitrate);
        const int fd = static_cast<int>(fdev / 61);
        const int bwr = (rxbw <= 125000) ? 0x02 : (rxbw <= 166700) ? 0x11
                                         : (rxbw <= 200000) ? 0x09 : 0x01;
        sendWrite(static_cast<quint8>(0x62), static_cast<quint16>((br >> 8) & 0xFF));
        sendWrite(static_cast<quint8>(0x63), static_cast<quint16>(br & 0xFF));
        sendWrite(static_cast<quint8>(0x64), static_cast<quint16>((fd >> 8) & 0xFF));
        sendWrite(static_cast<quint8>(0x65), static_cast<quint16>(fd & 0xFF));
        sendWrite(Proto::ADDR_FSK_RXBW, static_cast<quint16>(bwr));
        sendWrite(Proto::ADDR_FSK_AFCBW, static_cast<quint16>(bwr));
        sendWrite(Proto::ADDR_FSK_PA, 0x8F);      // PA enable + power=15 (matches demo)
        sendWrite(Proto::ADDR_FSK_LNA, 0x23);
        sendWrite(Proto::ADDR_FSK_RXCFG, 0x1E);
        sendWrite(Proto::ADDR_FSK_PKT1, 0x90);
        sendWrite(Proto::ADDR_FSK_PKT2, 0x40);
        sendWrite(Proto::ADDR_FSK_PAYLOAD, 0xFF);
        sendWrite(Proto::ADDR_FSK_SYNC, 0xC1);
    } else {
        static const int bwTable[3] = {125, 250, 500};
        sendWrite(Proto::ADDR_SF, static_cast<quint16>(6 + m_pg->modulation.comboSf->currentIndex()));
        sendWrite(Proto::ADDR_BW,
                  static_cast<quint16>(bwTable[m_pg->modulation.comboBw->currentIndex()]));
        sendWrite(Proto::ADDR_CR, static_cast<quint16>(5 + m_pg->modulation.comboCr->currentIndex()));
    }
}

void ConfigPage::writeAllTasks()
{
    QTableWidget *table = m_pg->tasks.tableTasks;
    for (int n = 0; n < 32; ++n) {
        quint16 ci1 = 0, ci2 = 0;
        int ena = 0;
        if (n < table->rowCount()) {
            // Reject invalid rows before writing any of the task slots (visible
            // warning, not a status-bar hint, so the user cannot miss it).
            if (!parseHex(table->item(n, 4)->text(), 0xFF, ci1)
                    || !parseHex(table->item(n, 5)->text(), 0xFF, ci2)) {
                QMessageBox::warning(this, tr("Task Table"),
                                     tr("Task %1: CI1/CI2 must be hex 00~FF.").arg(n));
                return;
            }
            bool ok = false;
            const double periodMs = table->item(n, 3)->text().toDouble(&ok);
            if (!ok || periodMs < 1.0 / 6.0) {
                QMessageBox::warning(this, tr("Task Table"),
                                     tr("Task %1: interval must be at least 1/6 ms.").arg(n));
                return;
            }
            const qint64 units = qRound64(periodMs * 6.0);
            if (units < 1 || units > 0xFFFF) {
                QMessageBox::warning(this, tr("Task Table"),
                                     tr("Task %1: interval exceeds ~10.9 s limit.").arg(n));
                return;
            }
            ena = table->item(n, 2)->checkState() == Qt::Checked ? 1 : 0;
            sendWrite(Proto::taskPeriodLo(n), static_cast<quint16>(units & 0xFF));
            sendWrite(Proto::taskPeriodHi(n), static_cast<quint16>((units >> 8) & 0xFF));
        }
        sendWrite(Proto::taskCi1(n), ci1);
        sendWrite(Proto::taskEna(n), static_cast<quint16>(ena));
        sendWrite(Proto::taskCi2(n), ci2);
    }
}

// Network/Role + RS-485 + RF bank + tasks (master) or slave CI2. Data-register
// writes only: nothing is applied (0x29) here.
void ConfigPage::writeAllSettings()
{
    sendWrite(Proto::ADDR_LOCAL_ADDR, static_cast<quint16>(m_pg->address.spinSelfAddr->value()));
    sendWrite(Proto::ADDR_PEER_ADDR, static_cast<quint16>(m_pg->address.spinPeerAddr->value()));
    sendWrite(Proto::ADDR_ROLE, static_cast<quint16>(m_isMaster ? 1 : 0));
    static const int baudTable[8] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
    sendWrite(Proto::ADDR_485_BAUD,
              static_cast<quint16>(baudTable[qBound(0, m_pg->rs485.combo485Baud->currentIndex(), 7)]));
    sendWrite(Proto::ADDR_485_BUF, static_cast<quint16>(m_pg->rs485.spin485Buf->value()));
    sendWrite(Proto::ADDR_485_TIMEOUT, static_cast<quint16>(m_pg->rs485.spin485Timeout->value()));
    sendWrite(Proto::ADDR_485_ENABLE, m_pg->rs485.chk485Enable->isChecked() ? 1 : 0);
    writeFrequency();
    writePowerBank();
    writeModemParams();
    if (m_isMaster)
        writeAllTasks();
    else
        sendWrite(static_cast<quint8>(0x40), static_cast<quint16>(m_slaveCi2));
}

void ConfigPage::readAll()
{
    for (int i = 0; i < 3; ++i)
        sendRead(static_cast<quint8>(Proto::ADDR_MCU_ID0 + i));
    sendRead(Proto::ADDR_LOCAL_ADDR);
    sendRead(Proto::ADDR_PEER_ADDR);
    sendRead(Proto::ADDR_ROLE);
    sendRead(Proto::ADDR_485_BAUD);
    sendRead(Proto::ADDR_485_BUF);
    sendRead(Proto::ADDR_485_TIMEOUT);
    sendRead(Proto::ADDR_485_ENABLE);
    sendRead(Proto::ADDR_SAVE);            // configuration validity
    sendRead(Proto::ADDR_FREQ_LO);
    sendRead(Proto::ADDR_FREQ_HI);
    sendRead(Proto::ADDR_RADIO);           // triggers the LoRa/FSK param reads
    sendRead(Proto::ADDR_POWER);
    sendRead(Proto::ADDR_PREAMBLE);
    sendRead(Proto::ADDR_SYNCWORD);
    sendRead(Proto::ADDR_LNA);
    sendRead(Proto::ADDR_RSSI);
    sendRead(Proto::ADDR_SNR);
    sendRead(Proto::ADDR_RX_COUNT);
    sendRead(Proto::ADDR_CRC_ERR_COUNT);
    sendRead(Proto::ADDR_TX_OVERFLOW_COUNT);
    if (m_isMaster) {
        for (int n = 0; n < 32; ++n) {
            sendRead(Proto::taskCi1(n));
            sendRead(Proto::taskEna(n));
            sendRead(Proto::taskPeriodLo(n));
            sendRead(Proto::taskPeriodHi(n));
            sendRead(Proto::taskCi2(n));
        }
    } else {
        sendRead(static_cast<quint8>(0x40));
    }
    emit statusMessage(tr("Reading all settings..."));
}

void ConfigPage::writeAll()
{
    writeAllSettings();
    emit statusMessage(tr("All settings written (Apply required)."));
}

void ConfigPage::applyAll()
{
    writeAllSettings();
    sendWrite(Proto::ADDR_APPLY_RF, 0x0001);
    // Persist so the applied configuration survives power cycles.
    sendWriteTimeout(Proto::ADDR_SAVE, 0x0001);
    emit statusMessage(tr("All settings written, applied and saved to EEPROM."));
}

/* ========================= .iml field serialization ======================= */
bool ConfigPage::toXml(QDomDocument &doc) const
{
    QDomElement root = doc.createElement(QStringLiteral("druppc"));
    root.setAttribute(QStringLiteral("type"), m_isMaster ? QStringLiteral("master")
                                                         : QStringLiteral("slave"));
    root.setAttribute(QStringLiteral("version"), QStringLiteral("1"));
    root.setAttribute(QStringLiteral("app"), QCoreApplication::applicationVersion());
    doc.appendChild(root);

    QDomElement address = doc.createElement(QStringLiteral("address"));
    address.setAttribute(QStringLiteral("local"), m_pg->address.spinSelfAddr->value());
    address.setAttribute(QStringLiteral("peer"), m_pg->address.spinPeerAddr->value());
    root.appendChild(address);

    QDomElement rf = doc.createElement(QStringLiteral("rf"));
    const bool mhz = m_pg->frequency.comboFreqUnit->currentIndex() == 0;
    const double val = m_pg->frequency.spinFreq->value();
    const qint64 hz = static_cast<qint64>(mhz ? val * 1e6 : val * 1e3);
    rf.setAttribute(QStringLiteral("freq"), QString::number(hz / 1e6, 'f', 3)); // MHz
    rf.setAttribute(QStringLiteral("modem"), m_pg->modulation.comboRadio->currentIndex());
    rf.setAttribute(QStringLiteral("sf"), 6 + m_pg->modulation.comboSf->currentIndex());
    static const int bwTable[3] = {125, 250, 500};
    rf.setAttribute(QStringLiteral("bw"), bwTable[m_pg->modulation.comboBw->currentIndex()]);
    rf.setAttribute(QStringLiteral("cr"), 5 + m_pg->modulation.comboCr->currentIndex());
    rf.setAttribute(QStringLiteral("fskbitrate"), m_pg->modulation.spinFskBitrate->value());
    rf.setAttribute(QStringLiteral("fskfdev"), m_pg->modulation.spinFskFdev->value());
    rf.setAttribute(QStringLiteral("fskbw"), m_pg->modulation.comboFskBw->currentIndex());
    root.appendChild(rf);

    QDomElement power = doc.createElement(QStringLiteral("power"));
    power.setAttribute(QStringLiteral("tx"), m_pg->power.spinPower->value());
    power.setAttribute(QStringLiteral("preamble"), m_pg->power.spinPreamble->value());
    power.setAttribute(QStringLiteral("sync"), m_pg->power.spinSyncword->value());
    power.setAttribute(QStringLiteral("lna"), m_pg->power.spinLna->value());
    root.appendChild(power);

    static const int baudTable[8] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
    QDomElement rs485 = doc.createElement(QStringLiteral("rs485"));
    rs485.setAttribute(QStringLiteral("baud"),
                       baudTable[qBound(0, m_pg->rs485.combo485Baud->currentIndex(), 7)]);
    rs485.setAttribute(QStringLiteral("buf"), m_pg->rs485.spin485Buf->value());
    rs485.setAttribute(QStringLiteral("timeout"), m_pg->rs485.spin485Timeout->value());
    rs485.setAttribute(QStringLiteral("enable"), m_pg->rs485.chk485Enable->isChecked() ? 1 : 0);
    root.appendChild(rs485);

    if (m_isMaster) {
        QTableWidget *table = m_pg->tasks.tableTasks;
        QDomElement tasks = doc.createElement(QStringLiteral("tasks"));
        tasks.setAttribute(QStringLiteral("count"), table->rowCount());
        for (int n = 0; n < table->rowCount(); ++n) {
            QDomElement task = doc.createElement(QStringLiteral("task"));
            task.setAttribute(QStringLiteral("ci1"), table->item(n, 4)->text());
            task.setAttribute(QStringLiteral("ci2"), table->item(n, 5)->text());
            task.setAttribute(QStringLiteral("ena"),
                             table->item(n, 2)->checkState() == Qt::Checked ? 1 : 0);
            task.setAttribute(QStringLiteral("period"), table->item(n, 3)->text());
            tasks.appendChild(task);
        }
        root.appendChild(tasks);
    } else {
        QDomElement slave = doc.createElement(QStringLiteral("slave"));
        slave.setAttribute(QStringLiteral("ci2"), hex8(m_slaveCi2));
        root.appendChild(slave);
    }
    return true;
}

bool ConfigPage::fromXml(const QDomDocument &doc)
{
    const QDomElement root = doc.firstChildElement(QStringLiteral("druppc"));
    if (root.isNull())
        return false;
    const QString type = root.attribute(QStringLiteral("type"));
    if (type != (m_isMaster ? QStringLiteral("master") : QStringLiteral("slave")))
        return false;

    const QDomElement address = root.firstChildElement(QStringLiteral("address"));
    if (!address.isNull()) {
        m_pg->address.spinSelfAddr->setValue(address.attribute(QStringLiteral("local")).toInt());
        m_pg->address.spinPeerAddr->setValue(address.attribute(QStringLiteral("peer")).toInt());
    }

    const QDomElement rf = root.firstChildElement(QStringLiteral("rf"));
    if (!rf.isNull()) {
        m_pg->frequency.comboFreqUnit->setCurrentIndex(0); // display in MHz
        m_pg->frequency.spinFreq->setValue(rf.attribute(QStringLiteral("freq")).toDouble());
        m_pg->modulation.comboRadio->setCurrentIndex(rf.attribute(QStringLiteral("modem")).toInt());
        const int sf = rf.attribute(QStringLiteral("sf"), "7").toInt();
        m_pg->modulation.comboSf->setCurrentIndex(qBound(6, sf, 12) - 6);
        const int bw = rf.attribute(QStringLiteral("bw"), "125").toInt();
        m_pg->modulation.comboBw->setCurrentIndex(bw >= 500 ? 2 : (bw >= 250 ? 1 : 0));
        const int cr = rf.attribute(QStringLiteral("cr"), "5").toInt();
        m_pg->modulation.comboCr->setCurrentIndex(qBound(5, cr, 8) - 5);
        m_pg->modulation.spinFskBitrate->setValue(
                rf.attribute(QStringLiteral("fskbitrate"), "100000").toInt());
        m_pg->modulation.spinFskFdev->setValue(
                rf.attribute(QStringLiteral("fskfdev"), "50000").toInt());
        m_pg->modulation.comboFskBw->setCurrentIndex(
                qBound(0, rf.attribute(QStringLiteral("fskbw"), "2").toInt(), 3));
    }

    const QDomElement power = root.firstChildElement(QStringLiteral("power"));
    if (!power.isNull()) {
        m_pg->power.spinPower->setValue(power.attribute(QStringLiteral("tx"), "13").toInt());
        m_pg->power.spinPreamble->setValue(power.attribute(QStringLiteral("preamble"), "8").toInt());
        m_pg->power.spinSyncword->setValue(power.attribute(QStringLiteral("sync"), "18").toInt());
        m_pg->power.spinLna->setValue(power.attribute(QStringLiteral("lna"), "35").toInt());
    }

    const QDomElement rs485 = root.firstChildElement(QStringLiteral("rs485"));
    if (!rs485.isNull()) {
        static const int baudTable[8] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
        const int baud = rs485.attribute(QStringLiteral("baud"), "115200").toInt();
        int idx = 7;
        for (int i = 0; i < 8; ++i) {
            if (baudTable[i] == baud) { idx = i; break; }
        }
        m_pg->rs485.combo485Baud->setCurrentIndex(idx);
        m_pg->rs485.spin485Buf->setValue(rs485.attribute(QStringLiteral("buf"), "64").toInt());
        m_pg->rs485.spin485Timeout->setValue(
                rs485.attribute(QStringLiteral("timeout"), "5").toInt());
        m_pg->rs485.chk485Enable->setChecked(rs485.attribute(QStringLiteral("enable"), "0").toInt());
    }

    if (m_isMaster) {
        QTableWidget *table = m_pg->tasks.tableTasks;
        const QDomElement tasks = root.firstChildElement(QStringLiteral("tasks"));
        const int count = qBound(1, tasks.isNull()
                                        ? 1 : tasks.attribute(QStringLiteral("count"), "1").toInt(),
                                 32);
        table->setRowCount(0);
        table->setRowCount(count); // grow before setItem so rows exist
        for (int n = 0; n < count; ++n) {
            auto *itn = new QTableWidgetItem(QString::number(n));
            itn->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            auto *name = new QTableWidgetItem(QString());
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            auto *cbox = new QTableWidgetItem;
            cbox->setFlags((cbox->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
            cbox->setCheckState(Qt::Unchecked);
            auto *per = new QTableWidgetItem(QStringLiteral("0"));
            per->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            auto *c1 = new QTableWidgetItem(QStringLiteral("00"));
            c1->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            auto *c2 = new QTableWidgetItem(QStringLiteral("00"));
            c2->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            table->setItem(n, 0, itn);
            table->setItem(n, 1, name);
            table->setItem(n, 2, cbox);
            table->setItem(n, 3, per);
            table->setItem(n, 4, c1);
            table->setItem(n, 5, c2);
        }
        QDomElement task = tasks.firstChildElement(QStringLiteral("task"));
        for (int n = 0; n < count && !task.isNull(); ++n,
             task = task.nextSiblingElement(QStringLiteral("task"))) {
            table->item(n, 4)->setText(task.attribute(QStringLiteral("ci1"), "00"));
            table->item(n, 5)->setText(task.attribute(QStringLiteral("ci2"), "00"));
            table->item(n, 2)->setCheckState(
                    task.attribute(QStringLiteral("ena"), "0").toInt() ? Qt::Checked : Qt::Unchecked);
            table->item(n, 3)->setText(task.attribute(QStringLiteral("period"), "0"));
        }
    } else {
        const QDomElement slave = root.firstChildElement(QStringLiteral("slave"));
        if (!slave.isNull()) {
            quint16 ci2 = 0;
            parseHex(slave.attribute(QStringLiteral("ci2"), "00"), 0xFF, ci2);
            m_slaveCi2 = static_cast<quint8>(ci2);
        }
    }
    return true;
}
