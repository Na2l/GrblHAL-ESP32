#!/usr/bin/env python3
"""Quick connectivity + selftest for grblHAL on MKS DLC32 MAX (ESP32-S3).

Read-only by default. Motion/spindle tests need explicit flags.

    python board_test.py                          # identity, settings, status
    python board_test.py --jog "X10" --yes        # move X +10 mm (motors powered!)
    python board_test.py --spindle 1000 --yes     # M3 S1000 for 3 s, then M5

Requires pyserial (pip install pyserial).  Default port /dev/ttyUSB0, 115200 8N1.
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: pip install pyserial")

BAUD = 115200


def send(ser, cmd, wait=0.6):
    ser.reset_input_buffer()
    ser.write(cmd.encode() + b"\r\n")
    time.sleep(wait)
    return ser.read(65536).decode("utf-8", "replace")


def status(ser):
    ser.reset_input_buffer()
    ser.write(b"?")
    time.sleep(0.25)
    return ser.read(4096).decode("utf-8", "replace").strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--jog", help='e.g. "X10 Y-5" (mm, G91/G0 per axis)')
    ap.add_argument("--feed", type=float, default=500.0, help="jog feed mm/min")
    ap.add_argument("--spindle", type=int, help="spindle/laser S value, e.g. 1000")
    ap.add_argument("--seconds", type=float, default=3.0)
    ap.add_argument("--yes", action="store_true", help="confirm motion/spindle test")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.4)
    ser.dtr = False
    ser.rts = True
    time.sleep(0.25)
    ser.rts = False
    time.sleep(1.0)
    banner = ser.read(4096).decode("utf-8", "replace").strip()
    print("banner:", banner or "(none)")

    info = send(ser, "$I")
    print(info.strip())

    ok = True
    if "AXS:4:XYZA" not in info:
        print("!! expected 4 axes (AXS:4:XYZA)")
        ok = False
    if "MKS DLC32 MAX" not in info:
        print("!! board is not reporting MKS DLC32 MAX")
        ok = False

    print("\nstatus:", status(ser))

    settings = send(ser, "$$", wait=1.5)
    key = ("$3=", "$4=", "$5=", "$6=", "$22=", "$100=", "$101=",
           "$102=", "$103=", "$110=", "$111=", "$112=", "$130=", "$131=")
    print("\nkey settings:")
    for line in settings.splitlines():
        if line.startswith(key):
            print("  " + line)

    if args.jog or args.spindle:
        if not args.yes:
            sys.exit("\nrefusing motion/spindle test without --yes")

    if args.jog:
        print("\nALARM check:", status(ser))
        print("unlock:", send(ser, "$X").strip())
        for move in args.jog.split():
            axis, dist = move[0], float(move[1:])
            cmd = "G91 G0 %s%.3f F%.1f" % (axis, dist, args.feed)
            print("send:", cmd, "->", send(ser, cmd).strip())
            print("status:", status(ser))
            print("back:", send(ser, "G90 G0 %s0" % axis).strip())
            time.sleep(0.3)

    if args.spindle:
        print("\nspindle:", send(ser, "M3 S%d" % args.spindle).strip())
        print("status:", status(ser))
        time.sleep(args.seconds)
        print("stop:", send(ser, "M5").strip())

    ser.close()
    print("\nRESULT:", "PASS" if ok else "CHECK ABOVE")


if __name__ == "__main__":
    main()
