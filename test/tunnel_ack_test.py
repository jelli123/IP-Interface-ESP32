#!/usr/bin/env python3
"""
On-device test of the tunnel acknowledgement watch (patch 12) and the tunnel
close log (patch 29).

Two KNXnet/IP tunnels to the device: one sends group telegrams, the stack
hands each of them to the other tunnel as L_Data.ind. That second tunnel
can stop acknowledging. The device log, read over /api/log, has to show
exactly one warning for it, a line once it acknowledges again, and a close
line with slot, channel, address and reason. Only the Python standard
library is needed; nothing but the device has to be on the network.

    test/tunnel_ack_test.py --gateway 192.168.179.105

The test writes values to a few group addresses (--group, +0 .. +2), and the
frames also go out on TP. With --heartbeat it additionally waits for the
stack to drop a tunnel without heartbeat, which takes over two minutes.
"""

import argparse
import queue
import socket
import struct
import sys
import threading
import time
import urllib.request

CONNECT_REQUEST          = 0x0205
CONNECT_RESPONSE         = 0x0206
DISCONNECT_REQUEST       = 0x0209
DISCONNECT_RESPONSE      = 0x020A
TUNNELING_REQUEST        = 0x0420
TUNNELING_ACK            = 0x0421

L_DATA_REQ = 0x11
L_DATA_IND = 0x29

WARN_AFTER = 10  # SBIP_TUNNEL_UNACKED_WARN in patch 12


def parse_ga(text):
    main, middle, sub = (int(x) for x in text.split("/"))
    return (main << 11) | (middle << 8) | sub


def fmt_pa(a):
    return f"{a >> 12}.{(a >> 8) & 15}.{a & 0xFF}"


class Tunnel:
    """KNXnet/IP tunnelling client whose acknowledgements can be switched off."""

    def __init__(self, gateway):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect(gateway)
        self.ip, self.port = self.sock.getsockname()
        self.sock.settimeout(0.05)
        self.replies = queue.Queue()
        self.running = True
        self.ack = True
        self.seq = 0
        self.inds = 0
        threading.Thread(target=self._receiver, daemon=True).start()

        self.sock.send(self._header(CONNECT_REQUEST, self._hpai() + self._hpai() + bytes([4, 4, 2, 0])))
        r = self._wait(CONNECT_RESPONSE, 3)
        if r is None:
            raise RuntimeError("no CONNECT_RESPONSE")
        if r[1] != 0:
            raise RuntimeError(f"CONNECT_RESPONSE status 0x{r[1]:02X}")
        self.channel = r[0]
        self.address = struct.unpack(">H", r[-2:])[0]

    def _hpai(self):
        return struct.pack(">BB4sH", 8, 1, socket.inet_aton(self.ip), self.port)

    @staticmethod
    def _header(service, body):
        return struct.pack(">BBHH", 6, 0x10, service, 6 + len(body)) + body

    def _receiver(self):
        while self.running:
            try:
                d = self.sock.recv(1024)
            except socket.timeout:
                continue
            except OSError:
                break
            service = struct.unpack(">H", d[2:4])[0]
            body = d[6:]
            if service != TUNNELING_REQUEST:
                self.replies.put((service, body))
                continue
            if self.ack:
                self.sock.send(self._header(TUNNELING_ACK, bytes([4, body[1], body[2], 0])))
            if body[4] == L_DATA_IND:
                self.inds += 1

    def _wait(self, service, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                r = self.replies.get(timeout=max(0.001, end - time.time()))
            except queue.Empty:
                return None
            if r[0] == service:
                return r[1]
        return None

    def write(self, ga, value):
        """A_GroupValue_Write with one data octet. @return status of the TUNNELING_ACK"""
        cemi = bytes([L_DATA_REQ, 0x00, 0xBC, 0xE0]) + struct.pack(">HH", 0, ga) + bytes([2, 0x00, 0x80, value])
        self.sock.send(self._header(TUNNELING_REQUEST, bytes([4, self.channel, self.seq, 0]) + cemi))
        r = self._wait(TUNNELING_ACK, 1.0)
        self.seq = (self.seq + 1) & 0xFF
        return None if r is None else r[3]

    def close(self):
        self.sock.send(self._header(DISCONNECT_REQUEST, bytes([self.channel, 0]) + self._hpai()))
        self._wait(DISCONNECT_RESPONSE, 1)
        self.running = False
        self.sock.close()


class DeviceLog:
    def __init__(self, host):
        self.url = f"http://{host}/api/log"
        self.mark()

    def _lines(self):
        return urllib.request.urlopen(self.url, timeout=5).read().decode(errors="replace").splitlines()

    def mark(self):
        self.start = len(self._lines())

    def new(self, *needles):
        """Lines since the last mark() that contain all needles."""
        return [l for l in self._lines()[self.start:] if all(n in l for n in needles)]


class Results:
    def __init__(self):
        self.items = []

    def check(self, name, ok, info=""):
        self.items.append((name, ok))
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({info})" if info else ""))

    def summary(self):
        passed = sum(1 for _, ok in self.items if ok)
        print(f"\n== Ergebnis: {passed}/{len(self.items)} bestanden ==")
        return passed == len(self.items)


def send(sender, ga, count, start=0):
    for i in range(count):
        sender.write(ga, (start + i) & 0xFF)
        time.sleep(0.05)
    time.sleep(0.5)


def tunnel_name(t):
    return f"channel 0x{t.channel:X}, {fmt_pa(t.address)}"


def main():
    p = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    p.add_argument("--gateway", default="192.168.179.105")
    p.add_argument("--gateway-port", type=int, default=3671)
    p.add_argument("--group", default="15/7/40", help="first group address, +0 .. +2 are used")
    p.add_argument("--heartbeat", action="store_true", help="also wait for a close without heartbeat (> 2 min)")
    args = p.parse_args()

    gw = (args.gateway, args.gateway_port)
    ga = parse_ga(args.group)
    res = Results()
    log = DeviceLog(args.gateway)

    sender = Tunnel(gw)
    receiver = Tunnel(gw)
    print(f"Sender: {tunnel_name(sender)}, Empfänger: {tunnel_name(receiver)}")

    print("\n== 1. Beide quittieren ==")
    log.mark()
    send(sender, ga, 15)
    res.check("15 Rahmen kommen an, keine Warnung",
              receiver.inds >= 15 and not log.new("tunnel client"), f"{receiver.inds} empfangen")

    print("\n== 2. Empfänger quittiert nicht ==")
    receiver.ack = False
    log.mark()
    send(sender, ga + 1, WARN_AFTER - 1)
    res.check(f"nach {WARN_AFTER - 1} Rahmen keine Warnung", not log.new("tunnel client"))
    send(sender, ga + 1, 6, WARN_AFTER - 1)
    warn = log.new("is not acknowledging")
    res.check(f"nach {WARN_AFTER + 5} Rahmen genau eine Warnung, für den Empfänger",
              len(warn) == 1 and tunnel_name(receiver) in warn[0], warn[0] if warn else "keine")

    print("\n== 3. Empfänger quittiert wieder ==")
    receiver.ack = True
    log.mark()
    send(sender, ga + 2, 3)
    again = log.new(f"channel 0x{receiver.channel:X} acknowledges again")
    res.check("Meldung 'acknowledges again'", len(again) == 1 and not log.new("is not acknowledging"),
              again[0] if again else "keine")

    print("\n== 4. Abmeldung ==")
    opened = [l for l in log._lines() if f"Channel: 0x{receiver.channel:X} PA: {fmt_pa(receiver.address)}" in l
              and "New Tunnel-Connection[" in l]
    slot = opened[-1].split("New Tunnel-Connection[")[1].split("]")[0] if opened else "?"
    log.mark()
    channel, address = receiver.channel, receiver.address
    receiver.close()
    time.sleep(0.3)
    closed = log.new("Close Tunnel-Connection[")
    expected = f"Close Tunnel-Connection[{slot}], Channel: 0x{channel:X} PA: {fmt_pa(address)} (disconnect request)"
    res.check("Close-Zeile mit Slot, Kanal, PA und Grund",
              len(closed) == 1 and expected in closed[0], closed[0] if closed else "keine")

    print("\n== 5. Neue Verbindung beginnt bei null ==")
    receiver = Tunnel(gw)
    print(f"  Empfänger neu: {tunnel_name(receiver)}")
    log.mark()
    send(sender, ga, 15, 100)
    res.check("15 quittierte Rahmen, keine Warnung",
              receiver.inds >= 15 and not log.new("tunnel client"), f"{receiver.inds} empfangen")

    if args.heartbeat:
        print("\n== 6. Kein Heartbeat ==")
        channel, address = receiver.channel, receiver.address
        receiver.running = False  # neither acks nor heartbeats from here on
        log.mark()
        print("  warte 130 s ...")
        time.sleep(130)
        closed = log.new("Close Tunnel-Connection[", f"Channel: 0x{channel:X} PA: {fmt_pa(address)}")
        res.check("Close-Zeile mit Grund 'no heartbeat for 120 s'",
                  len(closed) == 1 and "(no heartbeat for 120 s)" in closed[0], closed[0] if closed else "keine")
        receiver.sock.close()
        # The sender sent nothing either and is gone by now as well.
        sender.running = False
        sender.sock.close()
    else:
        receiver.close()
        sender.close()

    return 0 if res.summary() else 1


if __name__ == "__main__":
    sys.exit(main())
