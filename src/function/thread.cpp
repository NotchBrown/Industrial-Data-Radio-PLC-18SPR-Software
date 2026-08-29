#include "function/thread.h"
#include "function/serial.h"

ThreadManager::ThreadManager(QObject *parent)
    : QObject(parent)
{
}

ThreadManager::~ThreadManager()
{
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        // m_worker is deleted via QThread::finished -> deleteLater
    }
}

void ThreadManager::start()
{
    if (m_thread)
        return;
    m_thread = new QThread(this);
    m_worker = new SerialWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}
