#include "function/serial.h"

#include <QSerialPort>

namespace Proto {

quint8 crc8(const quint8 *data, int len)
{
    quint8 crc = 0x00;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? static_cast<quint8>((crc << 1) ^ 0x07)
                               : static_cast<quint8>(crc << 1);
    }
    return crc;
}

} // namespace Proto

SerialWorker::SerialWorker(QObject *parent)
    : QObject(parent)
{
    // Must be a child so that moveToThread() takes the timer into the worker
    // thread; starting a QTimer from a foreign thread is not allowed.
    m_replyTimer.setParent(this);
    m_replyTimer.setSingleShot(true);
    connect(&m_replyTimer, &QTimer::timeout, this, &SerialWorker::onReplyTimeout);
}

void SerialWorker::openPort(const QString &portName)
{
    closePort();

    m_port = new QSerialPort(this);
    m_port->setPortName(portName);
    m_port->setBaudRate(QSerialPort::Baud115200);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        const QString msg = tr("Cannot open %1: %2").arg(portName, m_port->errorString());
        m_port->deleteLater();
        m_port = nullptr;
        emit errorOccurred(msg);
        return;
    }

    connect(m_port, &QSerialPort::readyRead, this, &SerialWorker::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialWorker::onSerialError);
    emit opened(portName);
}

void SerialWorker::closePort()
{
    if (!m_port)
        return;
    m_port->close();
    m_port->deleteLater();
    m_port = nullptr;
    m_queue.clear();
    m_awaitingReply = false;
    m_replyTimer.stop();
    updateBusy();
    emit closed();
}

void SerialWorker::sendFrame(quint8 head, quint8 addr, quint16 data, int timeoutMs)
{
    m_queue.enqueue({head, addr, data, timeoutMs});
    pump();
    updateBusy();
}

void SerialWorker::transmit(const Request &req)
{
    quint8 buf[6];
    buf[0] = req.head;
    buf[1] = req.addr;
    buf[2] = static_cast<quint8>(req.data & 0xFF);
    buf[3] = static_cast<quint8>((req.data >> 8) & 0xFF);
    buf[4] = Proto::crc8(buf, 4);
    buf[5] = Proto::tailOf(req.head);
    m_port->write(reinterpret_cast<const char *>(buf), 6);
    ++m_txFrames;
    emit countersChanged(m_txFrames, m_rxFrames);
}

void SerialWorker::pump()
{
    if (m_awaitingReply || m_queue.isEmpty() || !m_port || !m_port->isOpen())
        return;
    const Request &req = m_queue.head();
    transmit(req);
    m_awaitingReply = true;
    m_replyTimer.start(req.timeoutMs > 0 ? req.timeoutMs : m_replyTimeoutMs);
}

void SerialWorker::onReadyRead()
{
    if (!m_port)
        return;
    m_rxBuffer.append(m_port->readAll());
    processBuffer();
}

void SerialWorker::processBuffer()
{
    for (;;) {
        // Drop garbage before the first candidate head byte.
        int i = 0;
        while (i < m_rxBuffer.size()) {
            const quint8 b = static_cast<quint8>(m_rxBuffer.at(i));
            if (b == Proto::HEAD_READ || b == Proto::HEAD_WRITE)
                break;
            ++i;
        }
        if (i > 0)
            m_rxBuffer.remove(0, i);
        if (m_rxBuffer.size() < 6)
            return;

        const quint8 head = static_cast<quint8>(m_rxBuffer.at(0));
        const quint8 tail = static_cast<quint8>(m_rxBuffer.at(5));
        const quint8 crc = Proto::crc8(reinterpret_cast<const quint8 *>(m_rxBuffer.constData()), 4);

        if (tail != Proto::tailOf(head) || crc != static_cast<quint8>(m_rxBuffer.at(4))) {
            m_rxBuffer.remove(0, 1); // resync
            continue;
        }

        const quint8 addr = static_cast<quint8>(m_rxBuffer.at(1));
        const quint16 data = static_cast<quint8>(m_rxBuffer.at(2))
                | (static_cast<quint16>(static_cast<quint8>(m_rxBuffer.at(3))) << 8);
        m_rxBuffer.remove(0, 6);

        ++m_rxFrames;
        m_awaitingReply = false;
        m_replyTimer.stop();
        emit replyReceived(head, addr, data);
        emit countersChanged(m_txFrames, m_rxFrames);
        // The UART3 link is strictly request/reply: any valid frame releases the
        // outstanding request so the queue always advances (never deadlocks), even
        // if the device echoes an unexpected address.
        if (!m_queue.isEmpty())
            m_queue.dequeue();
        pump();
        updateBusy();
    }
}

void SerialWorker::onReplyTimeout()
{
    if (!m_awaitingReply)
        return;
    m_awaitingReply = false;
    if (!m_queue.isEmpty())
        m_queue.dequeue(); // drop the unanswered request
    emit statusMessage(tr("Reply timeout, request dropped."));
    pump();
    updateBusy();
}

void SerialWorker::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_port)
        return;
    // The cable was pulled / the port became unusable mid-session: tear the
    // link down and let the GUI notice, instead of failing silently forever.
    const QString msg = m_port->errorString();
    closePort(); // clears the queue and emits closed()
    emit errorOccurred(tr("Connection lost: %1").arg(msg));
}

void SerialWorker::updateBusy()
{
    const bool busy = m_awaitingReply || !m_queue.isEmpty();
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged(busy);
    }
}
