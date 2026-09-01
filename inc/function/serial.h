#ifndef DRUPPC_SERIAL_H
#define DRUPPC_SERIAL_H

#include <QObject>
#include <QQueue>
#include <QTimer>

#include <QSerialPort>

namespace Proto {

constexpr quint8 HEAD_READ = 0x36;
constexpr quint8 HEAD_WRITE = 0x37;

constexpr quint8 tailOf(quint8 head) { return static_cast<quint8>(~head); }

// CRC-8, poly 0x07, init 0x00, covers head..data
quint8 crc8(const quint8 *data, int len);

enum Addr : quint8 {
    ADDR_MCU_ID0 = 0x00,
    ADDR_MCU_ID1 = 0x01,
    ADDR_MCU_ID2 = 0x02,
    ADDR_RTC_MS = 0x03,
    ADDR_RTC_SEC = 0x04,
    ADDR_RTC_MIN = 0x05,
    ADDR_RTC_HOUR = 0x06,
    ADDR_RTC_DAY = 0x07,
    ADDR_RTC_MON = 0x08,
    ADDR_RTC_YEAR = 0x09,
    ADDR_DI_LO = 0x0A,
    ADDR_DI_HI = 0x0B,
    ADDR_DO_LO = 0x0C,
    ADDR_DO_HI = 0x0D,
    ADDR_AI0 = 0x0E,
    ADDR_AI1 = 0x0F,
    ADDR_AI2 = 0x10,
    ADDR_AI3 = 0x11,
    ADDR_AO0 = 0x12,
    ADDR_AO1 = 0x13,
    ADDR_AO2 = 0x14,
    ADDR_AO3 = 0x15,
    ADDR_LOCAL_ADDR = 0x16,
    ADDR_PEER_ADDR = 0x17,
    ADDR_ROLE = 0x19,
    ADDR_485_BAUD = 0x1A,
    ADDR_485_BUF = 0x1B,
    ADDR_485_TIMEOUT = 0x1C,
    ADDR_485_ENABLE = 0x1D,
    ADDR_SAVE = 0x1E,
    ADDR_FACTORY_RESET = 0x1F,
    ADDR_RF_TEST = 0x20,
    ADDR_RX_COUNT = 0x21,
    ADDR_CRC_ERR_COUNT = 0x22,
    ADDR_TX_OVERFLOW_COUNT = 0x23,
    ADDR_RSSI = 0x24,
    ADDR_SNR = 0x25,
    ADDR_CAL_TRIGGER = 0x26,
    ADDR_CAL_VALUE = 0x27,
    ADDR_CAL_SWITCH = 0x28,
    ADDR_APPLY_RF = 0x29,       // write 0x0001: re-apply RF params (0x30~0x38) immediately
    ADDR_FREQ_LO = 0x30,
    ADDR_FREQ_HI = 0x31,
    ADDR_SF = 0x32,
    ADDR_BW = 0x33,
    ADDR_CR = 0x34,
    ADDR_POWER = 0x35,
    ADDR_PREAMBLE = 0x36,
    ADDR_SYNCWORD = 0x37,
    ADDR_LNA = 0x38,
    ADDR_RADIO = 0x2F,       // modem: 0 = LoRa, 1 = FSK (a write also applies RF)
    ADDR_LONG_RANGE = 0x3F, // 1 = long-range mode (firmware scales RF timeouts; EEPROM)
    // FSK physical params live on the 0x60..0x7F SX1278 direct-write page:
    //   +2/+3 BitRateMSB/LSB(0x02/03), +4/+5 FdevMSB/LSB(0x04/05), +0x12 RxBw, +0x13 AfcBw
    ADDR_FSK_PA    = 0x69,   // PA config (0x09)
    ADDR_FSK_LNA   = 0x6C,   // LNA (0x0C)
    ADDR_FSK_RXCFG = 0x6D,   // RxConfig (0x0D)
    ADDR_FSK_RXBW  = 0x72,   // RxBw (0x12)
    ADDR_FSK_AFCBW = 0x73,   // AfcBw (0x13)
    // FSK packet-format direct regs (0x39..0x3E -> SX1278 0x30/31/32/28):
    ADDR_FSK_PKT1  = 0x39,   // PACKETCONFIG1 (0x30)
    ADDR_FSK_PKT2  = 0x3A,   // PACKETCONFIG2 (0x31)
    ADDR_FSK_PAYLOAD = 0x3B, // PAYLOADLENGTH (0x32)
    ADDR_FSK_SYNC  = 0x3E,   // SYNCVALUE1 (0x28)
    ADDR_REG_BASE = 0x60,
    ADDR_TASK_BASE = 0x80
};

inline quint8 taskCi1(int n) { return static_cast<quint8>(ADDR_TASK_BASE + n * 4); }
inline quint8 taskEna(int n) { return static_cast<quint8>(taskCi1(n) + 1); }
inline quint8 taskPeriodLo(int n) { return static_cast<quint8>(taskCi1(n) + 2); }
inline quint8 taskPeriodHi(int n) { return static_cast<quint8>(taskCi1(n) + 3); }
inline quint8 taskCi2(int n) { return static_cast<quint8>(0x40 + n); }

} // namespace Proto

// Runs strictly inside its worker thread (see ThreadManager).
// Serializes requests: one outstanding frame at a time, replies (or timeout)
// release the next queued request. Keeps the 0x1E save semantics safe.
class SerialWorker : public QObject
{
    Q_OBJECT

public:
    explicit SerialWorker(QObject *parent = nullptr);

public slots:
    void openPort(const QString &portName);
    void closePort();
    // Optional per-request reply timeout: 0 (default) uses m_replyTimeoutMs.
    void sendFrame(quint8 head, quint8 addr, quint16 data, int timeoutMs = 0);
    void setReplyTimeout(int ms) { m_replyTimeoutMs = ms; }

signals:
    void opened(const QString &portName);
    void closed();
    void errorOccurred(const QString &message);
    void replyReceived(quint8 head, quint8 addr, quint16 data);
    void countersChanged(qint64 tx, qint64 rx);
    void statusMessage(const QString &message);
    // True while a frame is in flight or requests remain queued.
    void busyChanged(bool busy);

private slots:
    void onReadyRead();
    void onReplyTimeout();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    struct Request
    {
        quint8 head;
        quint8 addr;
        quint16 data;
        int timeoutMs = 0; // 0 => use m_replyTimeoutMs
    };

    void transmit(const Request &req);
    void pump();
    void processBuffer();
    void updateBusy();

    QSerialPort *m_port = nullptr;
    QQueue<Request> m_queue;
    QTimer m_replyTimer;
    bool m_awaitingReply = false;
    bool m_busy = false;
    int m_replyTimeoutMs = 1000;
    QByteArray m_rxBuffer;
    qint64 m_txFrames = 0;
    qint64 m_rxFrames = 0;
};

#endif // DRUPPC_SERIAL_H
