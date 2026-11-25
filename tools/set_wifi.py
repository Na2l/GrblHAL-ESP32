#!/usr/bin/env python3
"""Join a WiFi network on grblHAL (MKS DLC32 MAX / ESP32-S3 driver).

Mirrors what the settings do:
    $73=3 (AP+Station)  $74=SSID  $75=password  $REBOOT

    python set_wifi.py --ssid MyNet --password secret123
    python set_wifi.py --ap-only                        # back to AP-only mode
    python set_wifi.py --status                         # show mode / IPs

Requires pyserial.  Default port /dev/ttyUSB0, 115200.
"""

import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: pip install pyserial")


def open_port(port, baud=115200):
    ser = serial.Serial(port, baud, timeout=0.5)
    time.sleep(0.4)
    return ser


def send(ser, cmd, wait=1.0):
    ser.write(cmd.encode() + b"\r\n")
    time.sleep(wait)
    return ser.read(16384).decode("utf-8", "replace").strip()


def info(ser):
    return send(ser, "$I", 1.5)


def report(ser):
    txt = info(ser)
    print(txt)
    for line in txt.splitlines():
        if line.startswith(("[STA IP:", "[AP IP:", "[WIFI STA MAC:")):
            print("  ->", line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--ssid")
    ap.add_argument("--password", default="")
    ap.add_argument("--station-only", action="store_true", help="$73=1 instead of 3 (AP goes away)")
    ap.add_argument("--ap-only", action="store_true", help="back to AP mode, clear STA credentials")
    ap.add_argument("--status", action="store_true", help="only print $I / settings")
    args = ap.parse_args()

    ser = open_port(args.port, args.baud)

    if args.status:
        print("mode:", send(ser, "$73"))
        print("ssid:", send(ser, "$74"))
        report(ser)
        return

    if args.ap_only:
        print(send(ser, "$73=2"))
        print(send(ser, "$74="))
        print(send(ser, "$75="))
        print("reboot:", send(ser, "$REBOOT", 0.3))
        ser.close()
        time.sleep(5)
        ser = open_port(args.port, args.baud)
        report(ser)
        return

    if not args.ssid:
        sys.exit("need --ssid (or --status / --ap-only)")

    mode = 1 if args.station_only else 3
    print("mode:", send(ser, "$73=%d" % mode))
    print("ssid:", send(ser, "$74=%s" % args.ssid))
    print("pass:", send(ser, "$75=%s" % args.password))
    print("reboot:", send(ser, "$REBOOT", 0.3))

    ser.close()
    time.sleep(5)
    ser = open_port(args.port, args.baud)

    for attempt in range(15):
        txt = info(ser)
        m = re.search(r"\[STA IP:([0-9]{1,3}(?:\.[0-9]{1,3}){3})\]", txt)
        if m:
            print("\nconnected — WebUI: http://%s/   (AP still on 192.168.5.1)" % m.group(1))
            report(ser)
            return
        time.sleep(2)

    print("\nno STA IP yet. Check 2.4 GHz-only SSID, spelling, WPA2 (not WPA3-only).")
    report(ser)


if __name__ == "__main__":
    main()
