#ifndef DRUPPC_CONFIG_PAGE_H
#define DRUPPC_CONFIG_PAGE_H

#include <QWidget>
#include <QHash>
#include <QVariant>
#include <functional>
#include <memory>

#include "function/serial.h"

class QTreeWidgetItem;
class ThreadManager;
struct PagesDef;

namespace Ui {
class ConfigPage;
}

// One tab = one configuration file (master or slave).
// Left: connection row + index tree; right: one page per tree leaf.
// The base class builds everything; the master flag only decides whether the
// TX task table group is created.
class ConfigPage : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigPage(bool master, QWidget *parent = nullptr);
    ~ConfigPage() override;

    bool isMaster() const { return m_isMaster; }
    QString connectionText() const;
    bool isBusy() const { return m_busy; }
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    // Re-run retranslateUi on every sub-page (used by runtime language switch).
    void retranslate();

    // Minimal .iml snapshot (type + version); field snapshot comes later.
    bool toXml(class QDomDocument &doc) const;
    bool fromXml(const class QDomDocument &doc);

public slots:
    void applyConnectionSettings();
    // 0 disables; otherwise refresh live read-only channels every ms.
    void setAutoRefreshInterval(int ms);
    // Bulk device operations (menu: Device > Read/Write/Apply All).
    void readAll();   // read every configuration field from the device
    void writeAll();  // write every field as data registers (no apply trigger)
    void applyAll();  // writeAll + trigger 0x29 apply + persist to EEPROM (0x1E)

signals:
    void statusMessage(const QString &message);
    void countersChanged(qint64 tx, qint64 rx);
    void connectionChanged(bool connected);
    void busyChanged(bool busy);

protected:
    void createPages();
    void addPage(const QString &group, const QString &leaf, QWidget *page);

private slots:
    void refreshPorts();
    void toggleConnection();
    void onTreeClicked(QTreeWidgetItem *item, int column);
    void onTreeCurrentChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void onWorkerOpened(const QString &portName);
    void onWorkerClosed();
    void onWorkerError(const QString &message);
    void onReply(quint8 head, quint8 addr, quint16 data);
    void doAutoRefresh();

private:
    using Handler = std::function<void(quint8 head, quint16 data)>;

    void ensureThreads();
    void wirePages();
    void setConnected(bool connected);
    void setBusy(bool busy);
    void sendFrame(quint8 head, quint8 addr, quint16 data, int timeoutMs = 0);
    void sendRead(quint8 addr) { sendFrame(Proto::HEAD_READ, addr, 0); }
    void sendWrite(quint8 addr, quint16 data) { sendFrame(Proto::HEAD_WRITE, addr, data); }
    // Long timeout for EEPROM writes (save/factory reset block ~1s+ on the device).
    void sendWriteTimeout(quint8 addr, quint16 data, int ms = 6000)
    {
        sendFrame(Proto::HEAD_WRITE, addr, data, ms);
    }
    void registerHandler(quint8 addr, Handler fn) { m_handlers.insert(addr, std::move(fn)); }
    void updatePeriodItem(int n);
    void doFskReadUpdate();
    // Bulk-update helpers (data register writes only; nothing applied here).
    bool writeFrequency();      // false if out of range
    void writePowerBank();
    void writeModemParams();    // LoRa or FSK physical params + 0x2F
    void writeAllTasks();       // all 32 task slots (master only)
    void writeAllSettings();    // address/role/rs485 + RF bank + tasks/slave CI2

    Ui::ConfigPage *ui;
    std::unique_ptr<PagesDef> m_pg;
    ThreadManager *m_threads = nullptr;
    QHash<quint8, Handler> m_handlers;
    QTimer *m_autoTimer = nullptr;
    QTimer *m_rfTestTimer = nullptr; // resets the round-trip label if no reply
    QTimer *m_rfPollTimer = nullptr; // polls 0x20 until the true RTT is ready
    bool m_isMaster;
    bool m_connected = false;
    bool m_busy = false;
    bool m_disconnectWarned = false; // warned once for an unexpected serial error
    QString m_portName;
    QString m_filePath;
    quint16 m_mcuId[3] = {0, 0, 0};
    quint16 m_freqLo = 0;
    quint16 m_freqHi = 0;
    quint8 m_periodLo[32] = {0};
    quint8 m_periodHi[32] = {0};
    // FSK read accumulation buffers.
    quint8 m_fskBitMsb = 0, m_fskBitLsb = 0, m_fskFdevMsb = 0, m_fskFdevLsb = 0;
    int m_fskReadCount = 0;
    // Slave instance: CI2 content (0x40) the slave returns to the master.
    quint8 m_slaveCi2 = 0;
    // Guards the task-table editor while a row is being loaded into it, so the
    // live editor->row binding does not echo values back into the same row.
    bool m_taskLoading = false;
};

#endif // DRUPPC_CONFIG_PAGE_H
