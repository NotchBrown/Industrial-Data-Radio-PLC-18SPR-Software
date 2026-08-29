#ifndef DRUPPC_THREAD_H
#define DRUPPC_THREAD_H

#include <QObject>
#include <QThread>

class SerialWorker;

// Owns the worker QThread. The worker is moved into the thread; all QSerialPort
// operations stay inside it so the GUI never blocks on I/O.
class ThreadManager : public QObject
{
    Q_OBJECT

public:
    explicit ThreadManager(QObject *parent = nullptr);
    ~ThreadManager() override;

    void start();
    SerialWorker *worker() const { return m_worker; }

private:
    QThread *m_thread = nullptr;
    SerialWorker *m_worker = nullptr;
};

#endif // DRUPPC_THREAD_H
