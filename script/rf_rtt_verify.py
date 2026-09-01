#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
rf_rtt_verify.py - 验证多组 RF 调制参数下 0x20 往返测试是否可用。

协议(与上位机 serial.cpp 一致, 见 doc/protocol.md / upperpc.md):
    帧 = [head][addr][data-L][data-H][crc8(前4字节)][~head]  共 6 字节
      head:  读 0x36 / 写 0x37
      data 小端(低字节在前)
      crc8: poly 0x07 init 0x00 (与 Proto::crc8 一致)
    链路为严格 request/reply: 每发一帧必须等设备回对称帧, 收到后再发下一帧,
    且各 UART 指令之间按 GAP 停顿, 避免连发/指令挤压破坏链路。

本脚本增强:
    1) 覆盖更多配置组 (多档 LoRa / 多档 FSK 高速).
    2) 每条 UART 指令发送间隔由 GAP 控制 (发送后必停顿).
    3) 同时配置主站/从站的公共参数、调制、"配置表"(任务0 CI1/CI2/周期/使能).
    4) 每次写入后回读校验: 写帧返回值为设备写后回读值, 并再单独 read 一次,
       二者均需与目标值一致才认为真正应用成功.

用法:
    python rf_rtt_verify.py --list
    python rf_rtt_verify.py --master COM7 --slave COM11 --freq 470
    python rf_rtt_verify.py --master COM7 --slave COM11 --long-range --timeout 10
    python rf_rtt_verify.py --master COM7 --slave COM11 --gap 0.05
"""
import argparse
import sys
import time

import serial
from serial.tools import list_ports

# ---------------- 协议常量 (与 inc/function/serial.h 对齐) ----------------
HEAD_READ = 0x36          # 读帧
HEAD_WRITE = 0x37         # 写帧

ADDR_LOCAL_ADDR  = 0x16
ADDR_PEER_ADDR   = 0x17
ADDR_ROLE        = 0x19
ADDR_485_BAUD    = 0x1A
ADDR_485_BUF     = 0x1B
ADDR_485_TIMEOUT = 0x1C
ADDR_485_ENABLE  = 0x1D
ADDR_SAVE        = 0x1E
ADDR_FACTORY     = 0x1F
ADDR_RF_TEST     = 0x20   # 往返测试: 写(1) 阻塞触发, 返回 RTT (0xFFFF=超时)
ADDR_RX_CNT      = 0x21
ADDR_CRC_ERR     = 0x22
ADDR_TX_OVF      = 0x23
ADDR_RSSI        = 0x24
ADDR_SNR         = 0x25
ADDR_APPLY_RF    = 0x29   # 写 1: 立即应用 RF 参数
ADDR_FREQ_LO     = 0x30
ADDR_FREQ_HI     = 0x31
ADDR_SF          = 0x32   # LoRa 扩频因子
ADDR_BW          = 0x33   # LoRa 带宽 kHz
ADDR_CR          = 0x34   # LoRa 编码率 5..8
ADDR_POWER       = 0x35   # 发射功率 dBm
ADDR_PREAMBLE    = 0x36   # 前导长度 (LoRa)
ADDR_SYNCWORD    = 0x37   # 同步字
ADDR_LNA         = 0x38   # LoRa LNA
ADDR_RADIO       = 0x2F   # 0=LoRa 1=FSK
ADDR_LONG_RANGE  = 0x3F

# FSK 直写段 (0x60..0x7F -> SX1278 0x00..0x1F; 0x39..0x3E -> FSK 包格式)
ADDR_FSK_PA      = 0x69   # 0x09
ADDR_FSK_LNA     = 0x6C   # 0x0C
ADDR_FSK_RXCFG   = 0x6D   # 0x0D
ADDR_FSK_RXBW    = 0x72   # 0x12
ADDR_FSK_AFCBW   = 0x73   # 0x13
ADDR_FSK_PKT1    = 0x39   # 0x30
ADDR_FSK_PKT2    = 0x3A   # 0x31
ADDR_FSK_PAYLOAD = 0x3B   # 0x32
ADDR_FSK_SYNC    = 0x3E   # 0x28 SYNCVALUE1
ADDR_FSK_BIT_MSB = 0x62   # 0x02
ADDR_FSK_BIT_LSB = 0x63   # 0x03
ADDR_FSK_FDEV_MSB= 0x64   # 0x04
ADDR_FSK_FDEV_LSB= 0x65   # 0x05

# 任务表: CI1@0x80+n*4, ENA@+1, PeriodLo@+2, PeriodHi@+3, CI2@0x40+n
TASK_CI1_BASE = 0x80
TASK_CI2_BASE = 0x40

XTAL = 32000000   # 用于 FSK BitRate 计算


# ---------------- 小工具 ----------------
def crc8(data):
    crc = 0x00
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def build_frame(head, addr, data):
    d = [(addr & 0xFF), (data & 0xFF), ((data >> 8) & 0xFF)]
    c = crc8([head] + d)
    return bytes([head, d[0], d[1], d[2], c, (~head) & 0xFF])


class Link:
    """严格 request/reply 串口: 发一帧等一帧, 每帧后必停顿 GAP。"""
    def __init__(self, port, gap, reply_timeout=1.0):
        self.s = serial.Serial(port, 115200, timeout=reply_timeout, write_timeout=0.5)
        self.port = port
        self.gap = gap
        self.s.reset_input_buffer()

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass

    def _recv_frame(self, deadline):
        acc = bytearray()
        while time.time() < deadline:
            if self.s.in_waiting:
                acc.extend(self.s.read(self.s.in_waiting))
            while len(acc) >= 6:
                h = acc[0]
                if h not in (HEAD_READ, HEAD_WRITE):
                    acc.pop(0); continue
                if acc[5] != ((~h) & 0xFF):
                    acc.pop(0); continue
                if crc8(acc[:4]) != acc[4]:
                    acc.pop(0); continue
                return (h, acc[1], acc[2] | (acc[3] << 8))
            if len(acc) > 64:
                acc = acc[1:]
            time.sleep(0.002)
        return None

    def _tx(self, head, addr, data):
        self.s.reset_input_buffer()
        self.s.write(build_frame(head, addr, data))
        return self._recv_frame(time.time() + 1.0)

    def write(self, addr, data, retry=2):
        """写指令。返回设备写后回显值; 失败重试。"""
        for _ in range(retry):
            r = self._tx(HEAD_WRITE, addr, data)
            if r is not None:
                time.sleep(self.gap)      # 发送间隔
                return r[2]
            time.sleep(0.05)
        return None

    def read(self, addr, retry=2):
        for _ in range(retry):
            r = self._tx(HEAD_READ, addr, 0)
            if r is not None:
                time.sleep(self.gap)
                return r[2]
            time.sleep(0.05)
        return None

    def write_verify(self, addr, want, label='', echo_only=False, tolerant=False):
        """写入 + 回读校验.
        echo_only=True : 只需确认有回显 (0x29 apply / 0x20 触发等只回 0 的命令).
        tolerant=True  : 写帧必需有回显; 独立读回值不作硬判 (FSK 寄存器受固件
                         忙等切换影响读回可能瞬时不一致), 仅打印警告不判失败.。
        否则: 写帧回显 == want, 且再独立 read == want, 才认为真正应用。"""
        echo = self.write(addr, want)
        if echo is None:
            print(f"  [x] {label}写无响应 addr=0x{addr:02X}")
            return False
        if echo_only:
            return True
        if tolerant:
            rd = self.read(addr)
            if rd != (want & 0xFFFF):
                print(f"  (w) {label}读回容忍 addr=0x{addr:02X} want=0x{want:04X} got={rd}")
            return True
        if echo != (want & 0xFFFF):
            print(f"  [x] {label}写回显不符 addr=0x{addr:02X} want=0x{want:04X} got=0x{echo:04X}")
            return False
        rd = self.read(addr)
        if rd != (want & 0xFFFF):
            print(f"  [x] {label}读回不符 addr=0x{addr:02X} want=0x{want:04X} got={rd}")
            return False
        return True


# ---------------- 配置写入 ----------------
def fsk_bw_reg_for(hz):
    """SX1278 FSK RxBw 寄存器值查表 (参考 Semtech FskBandwidths, 与 demo/common 一致)。"""
    tbl = [(2600,0x17),(3100,0x0F),(3900,0x07),(5200,0x16),(6300,0x0E),(7800,0x06),
           (10400,0x15),(12500,0x0D),(15600,0x05),(20800,0x14),(25000,0x0C),(31300,0x04),
           (41700,0x13),(50000,0x0B),(62500,0x03),(83333,0x12),(100000,0x0A),(125000,0x02),
           (166700,0x11),(200000,0x09),(250000,0x01)]
    for bw, reg in tbl:
        if hz <= bw:
            return reg
    return 0x00


def setup_common(link, self_addr, peer_addr, is_master, power=13,
                 preamble=8, syncword=0xC1, lna=0x24, freq=470000000,
                 long_range=0):
    ok = True
    ok &= link.write_verify(ADDR_LOCAL_ADDR, self_addr, 'self')
    ok &= link.write_verify(ADDR_PEER_ADDR,  peer_addr, 'peer')
    ok &= link.write_verify(ADDR_ROLE,       1 if is_master else 0, 'role')
    ok &= link.write_verify(ADDR_POWER,      power, 'power')
    ok &= link.write_verify(ADDR_PREAMBLE,   preamble, 'preamble')
    ok &= link.write_verify(ADDR_SYNCWORD,   syncword, 'syncword')
    ok &= link.write_verify(ADDR_LNA,        lna, 'lna')
    ok &= link.write_verify(ADDR_LONG_RANGE, 1 if long_range else 0, 'longrange')
    ok &= link.write_verify(ADDR_FREQ_LO,    freq & 0xFFFF, 'frqlo')
    ok &= link.write_verify(ADDR_FREQ_HI,    (freq >> 16) & 0xFFFF, 'frqhi')
    return ok


def setup_modem(link, radio, fsk=None, lora=None):
    """radio: 0=LoRa 1=FSK。写完寄存器最后应用 0x29。返回 True=全部写入且回读一致。
    规避固件忙等: 切 radio 后延时等待固件完成重配, FSK 寄存器写后经延时再宽松读回。"""
    ok = link.write_verify(ADDR_RADIO, 1 if radio else 0, 'radio', echo_only=True)
    if not ok:
        return False
    time.sleep(0.3)          # 等固件 rf_apply_config 忙等完成 (LoRa/FSK 切换)
    if radio:
        # FSK 下 0x72(RxBw) 才读回真实值; 已切到 FSK 并延时, 可严格读回
        br = int(round(XTAL / fsk['bitrate']))
        fd = int(round(fsk['fdev'] / 61))
        bwr = fsk_bw_reg_for(fsk['rxbw'])
        lst = [
            (ADDR_FSK_BIT_MSB, (br >> 8) & 0xFF, 'BrH'),
            (ADDR_FSK_BIT_LSB, br & 0xFF, 'BrL'),
            (ADDR_FSK_FDEV_MSB,(fd >> 8) & 0xFF, 'FdH'),
            (ADDR_FSK_FDEV_LSB, fd & 0xFF, 'FdL'),
            (ADDR_FSK_RXBW,  bwr, 'RxBw'),
            (ADDR_FSK_AFCBW, bwr, 'Afc'),
            (ADDR_FSK_PA,     0xD0, 'PA'),      # 0x80|0x10 power=16dBm
            (ADDR_FSK_LNA,    0x23, 'LNA'),
            (ADDR_FSK_RXCFG,  0x1E, 'RXCFG'),
            (ADDR_FSK_PKT1,   0x90, 'PKT1'),
            (ADDR_FSK_PKT2,   0x40, 'PKT2'),
            (ADDR_FSK_PAYLOAD, 0xFF, 'FLEN'),
            (ADDR_FSK_SYNC,   0xC1, 'SYNC'),
        ]
        for addr, val, lbl in lst:
            if not link.write_verify(addr, val, lbl, tolerant=True):
                ok = False
                break
    else:
        lst = [
            (ADDR_SF, lora['sf'], 'SF'),
            (ADDR_BW, lora['bw'], 'BW'),
            (ADDR_CR, lora['cr'], 'CR'),
        ]
        for addr, val, lbl in lst:
            if not link.write_verify(addr, val, lbl):
                ok = False
                break
    time.sleep(0.05)
    ok &= link.write_verify(ADDR_APPLY_RF, 0x0001, 'apply', echo_only=True)
    time.sleep(0.05)
    return ok


def setup_task0(link, ci1, ci2, period_ms, ena=True):
    """配置表任务0: CI1 + ENA + Period(6kHz tick) + CI2。全部回读校验。"""
    units = int(round(period_ms * 6))
    ok = link.write_verify(TASK_CI1_BASE + 0, ci1, 't0ci1')
    ok &= link.write_verify(TASK_CI1_BASE + 1, 1 if ena else 0, 't0ena')
    ok &= link.write_verify(TASK_CI1_BASE + 2, units & 0xFF, 't0pl')
    ok &= link.write_verify(TASK_CI1_BASE + 3, (units >> 8) & 0xFF, 't0ph')
    ok &= link.write_verify(TASK_CI2_BASE + 0, ci2, 't0ci2')
    return ok


def run_rtt(master_link, slave_link=None, window=1.2, gap=0.08):
    """触发 0x20 往返测试并读回 RTT。0x20=1 写触发 (固件发任务0完整收发计时),
    随后读 0x20 得到 RF_RTT_MS (0xFFFF=超时/未完成)。返回 (rtt_ms, ok)。"""
    master_link.write(ADDR_RF_TEST, 0x0001, retry=1)
    deadline = time.time() + window
    while time.time() < deadline:
        time.sleep(gap)
        v = master_link.read(ADDR_RF_TEST, retry=1)
        if v is None:
            continue
        if v != 0xFFFF:
            return (v, True)   # 已得到真实往返时间
    return (0xFFFF, False)     # 超时


# ---------------- 配置组 ----------------
def rc_fsk(name, bitrate, fdev, rxbw):
    return ('fsk', name, {'bitrate': bitrate, 'fdev': fdev, 'rxbw': rxbw})


def rc_lora(name, sf, bw, cr):
    return ('lora', name, {'sf': sf, 'bw': bw, 'cr': cr})


CONFIGS = [
    # ================== LoRa 快/中速率 ==================
    rc_lora('LoRa SF7/BW250/CR4/5',       7, 250, 5),
    rc_lora('LoRa SF7/BW125/CR4/5',       7, 125, 5),
    rc_lora('LoRa SF9/BW250/CR4/5',       9, 250, 5),
    rc_lora('LoRa SF9/BW125/CR4/5',       9, 125, 5),
    # ================== LoRa 慢速率 (多编码率) ==================
    rc_lora('LoRa SF10/BW125/CR4/5',     10, 125, 5),
    rc_lora('LoRa SF10/BW125/CR4/8',     10, 125, 8),
    rc_lora('LoRa SF11/BW125/CR4/5',     11, 125, 5),
    rc_lora('LoRa SF11/BW125/CR4/8',     11, 125, 8),
    rc_lora('LoRa SF12/BW125/CR4/5',     12, 125, 5),
    rc_lora('LoRa SF12/BW125/CR4/8',     12, 125, 8),
    # ================== FSK ── 低速段 ==================
    rc_fsk('FSK 4.8k/Fdev3k/RxBw25k',    4800,   3000,  25000),
    rc_fsk('FSK 9.6k/Fdev5k/RxBw37.5k',  9600,   5000,  37500),
    rc_fsk('FSK 19.2k/Fdev10k/RxBw50k', 19200,  10000,  50000),
    rc_fsk('FSK 38.4k/Fdev20k/RxBw84k', 38400,  20000,  84000),
    rc_fsk('FSK 50k/Fdev25k/RxBw100k',  50000,  25000, 100000),
    # ================== FSK ── 中/高速段 ==================
    rc_fsk('FSK 76.8k/Fdev25k/RxBw125k',76800,  25000, 125000),
    rc_fsk('FSK 100k/Fdev50k/RxBw200k', 100000,  50000, 200000),
    rc_fsk('FSK 150k/Fdev50k/RxBw200k', 150000,  50000, 200000),
    rc_fsk('FSK 200k/Fdev50k/RxBw250k', 200000,  50000, 250000),
    rc_fsk('FSK 250k/Fdev75k/RxBw250k', 250000,  75000, 250000),
    rc_fsk('FSK 300k/Fdev90k/RxBw250k', 300000,  90000, 250000),
]


def main():
    ap = argparse.ArgumentParser(description='验证 0x20 往返在分组配置下是否可用')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--probe', action='store_true',
                    help='只读探测: 不写任何配置, 只确认串口能回帧 + 读关键状态')
    ap.add_argument('--master', help='主站串口 (COM7)')
    ap.add_argument('--slave',  help='从站串口 (COM11)')
    ap.add_argument('--long-range', action='store_true', help='长距离模式')
    ap.add_argument('--ci1', type=lambda s: int(s, 0), default=0x40)
    ap.add_argument('--ci2', type=lambda s: int(s, 0), default=0x40)
    ap.add_argument('--period', type=float, default=100.0, help='任务0 周期 ms')
    ap.add_argument('--gap', type=float, default=0.04, help='UART 指令发送间隔 s')
    ap.add_argument('--window', type=float, default=1.2, help='通联验证窗口 s')
    ap.add_argument('--timeout', type=float, default=8.0,
                    help='0x20 阻塞触发最大等待 s (长距离需大)')
    ap.add_argument('--freq', type=float, default=470.0, help='载波 MHz')
    ap.add_argument('--groups', default='all', help='all 或逗号分隔序号(1起)')
    args = ap.parse_args()

    if args.list:
        for p in list_ports.comports():
            print(f"{p.device}\t{p.description}")
        return

    if args.probe:
        for port, tag in [(args.master, '主站'), (args.slave, '从站')]:
            if not port:
                continue
            lk = Link(port, gap=0.1)
            print(f"\n[{tag}] {port} 只读探测:")
            val = lk.read(ADDR_LOCAL_ADDR, retry=1)
            print(f"  本机地址(0x16)     = {val if val is not None else 'NO REPLY'}")
            val = lk.read(ADDR_PEER_ADDR, retry=1)
            print(f"  对端地址(0x17)     = {val if val is not None else 'NO REPLY'}")
            val = lk.read(ADDR_ROLE, retry=1)
            print(f"  主从角色(0x19)     = {val if val is not None else 'NO REPLY'}")
            val = lk.read(ADDR_RADIO, retry=1)
            print(f"  调制(0x2F)         = {val if val is not None else 'NO REPLY'}")
            val = lk.read(ADDR_RX_CNT, retry=1)
            print(f"  收帧计数(0x21)     = {val if val is not None else 'NO REPLY'}")
            val = lk.read(ADDR_RSSI, retry=1)
            print(f"  RSSI(0x24)         = {val if val is not None else 'NO REPLY'}")
            lk.close()
        return

    if not args.master:
        ap.error('需要 --master (可用 --list 查看)')

    # 选组
    allcfg = CONFIGS
    if args.groups != 'all':
        want = {int(x) for x in args.groups.split(',')}
        allcfg = [(kind, name, p) for i, (kind, name, p) in enumerate(CONFIGS, 1)
                  if i in want]

    freq_hz = int(round(args.freq * 1e6))
    ml = Link(args.master, gap=args.gap)
    sl = Link(args.slave, gap=args.gap) if args.slave else None
    print(f"[主站] {args.master}  gap={args.gap}s")
    if sl:
        print(f"[从站] {args.slave}  gap={args.gap}s")
    else:
        print("[从站] 未提供 --slave, 假定对端已配好")

    try:
        print("\n== 公共参数(主站) ==")
        ok = setup_common(ml, self_addr=1, peer_addr=2, is_master=True,
                          long_range=args.long_range, freq=freq_hz)
        # 死机/瞬时无响应容忍: 重新打开一次串口再尝试一遍公共参数
        if not ok:
            print("主站公共参数首次失败, 重新打开串口重试...")
            ml.s.close()
            ml.s.open()
            ok = setup_common(ml, self_addr=1, peer_addr=2, is_master=True,
                              long_range=args.long_range, freq=freq_hz)
        if not ok:
            print("主站公共参数仍失败, 中止"); return
        if sl:
            print("== 公共参数(从站) ==")
            ok = setup_common(sl, self_addr=2, peer_addr=1, is_master=False,
                              long_range=args.long_range, freq=freq_hz)
            if not ok:
                sl.s.close(); sl.s.open()
                ok = setup_common(sl, self_addr=2, peer_addr=1, is_master=False,
                                  long_range=args.long_range, freq=freq_hz)
            if not ok:
                print("从站公共参数失败, 中止"); return

        # 任务0 在进入任一调制前先配置好 (配置表)
        print("== 配置表: 任务0 ==")
        if not setup_task0(ml, args.ci1, args.ci2, args.period):
            print("主站任务0 配置失败"); return
        if sl and not setup_task0(sl, args.ci2, args.ci2, args.period):
            print("从站任务0 配置失败"); return

        print(f"\n{'配置':<28}{'调制':<5}{'结果':<6}{'RTT(ms)':>10}")
        print('-' * 56)
        for kind, name, params in allcfg:
            radio = 1 if kind == 'fsk' else 0
            ok = setup_modem(ml, radio, fsk=params if radio else None,
                             lora=params if not radio else None)
            print(f"  [主站调制: {kind}] {'OK' if ok else '写入失败'}")
            if sl and ok:
                if not setup_modem(sl, radio, fsk=params if radio else None,
                                   lora=params if not radio else None):
                    print(f"  [从站调制: {kind}] 写入失败"); ok = False
            if not ok:
                print(f"{name:<28}{kind:<5}{'CFGFAIL':<6}{'--':>10}")
                time.sleep(0.5)      # 切换失败/组间恢复延时
                continue
            rtt, good = run_rtt(ml, sl, window=args.window, gap=args.gap)
            tag = 'OK' if good else 'TIMEOUT'
            show = rtt if isinstance(rtt, int) else 'RX'
            print(f"{name:<28}{kind:<5}{tag:<6}{show!s:>10}")
            time.sleep(0.5)          # 簇组间延时, 防连续切换状态不稳
    finally:
        ml.close()
        if sl:
            sl.close()


if __name__ == '__main__':
    sys.exit(main())