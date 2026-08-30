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
    void sendFrame(quint8 head, quint8 addr, quint16 data);
    void sendRead(quint8 addr) { sendFrame(Proto::HEAD_READ, addr, 0); }
    void sendWrite(quint8 addr, quint16 data) { sendFrame(Proto::HEAD_WRITE, addr, data); }
    void registerHandler(quint8 addr, Handler fn) { m_handlers.insert(addr, std::move(fn)); }
    void updatePeriodItem(int n);

    Ui::ConfigPage *ui;
    std::unique_ptr<PagesDef> m_pg;
    ThreadManager *m_threads = nullptr;
    QHash<quint8, Handler> m_handlers;
    QTimer *m_autoTimer = nullptr;
    bool m_isMaster;
    bool m_connected = false;
    bool m_busy = false;
    QString m_portName;
    QString m_filePath;
    quint16 m_mcuId[3] = {0, 0, 0};
    quint16 m_freqLo = 0;
    quint16 m_freqHi = 0;
    quint8 m_periodLo[32] = {0};
    quint8 m_periodHi[32] = {0};
    // Guards the task-table editor while a row is being loaded into it, so the
    // live editor->row binding does not echo values back into the same row.
    bool m_taskLoading = false;
};

#endif // DRUPPC_CONFIG_PAGE_H
