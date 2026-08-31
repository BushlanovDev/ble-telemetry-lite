#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Read and change ble-telemetry-lite settings over Bluetooth Low Energy.

Talks to the firmware's configuration GATT service FFF1:
  FFF1 — baudrate, uint32 little-endian, 1200..5250000
  FFF2 — device name (BLE name and access-point SSID), 1..20 printable ASCII characters
  FFF3 — operating mode: 0 = BLE, 1 = WEB (the device reboots after the write)
  FFF4 — UART diagnostics status, uint8: 1 = OK, 2 = NO SIGNAL, 3 = BAD DATA (notifies on change)

The device must be in BLE mode (in WEB mode Bluetooth is off).
If no client connects within 120 seconds, the device goes to deep sleep —
power-cycle it and run the script again.

Requires the bleak library:

  pip install bleak

Examples:

  python tools/ble_config.py scan
  python tools/ble_config.py read
  python tools/ble_config.py baud 420000
  python tools/ble_config.py domain "My Device"
  python tools/ble_config.py mode web
  python tools/ble_config.py uart --watch 30
"""

import argparse
import asyncio
import struct
import sys

try:
    from bleak import BleakClient, BleakScanner
    from bleak.exc import BleakError
except ImportError:  # bleak not installed: --help still works, the rest does not
    BleakClient = BleakScanner = BleakError = None

# Values are synchronized with src/main.h
DEFAULT_DEVICE_NAME = "BLE Telemetry Lite"  # DEFAULT_DOMAIN_NAME
UUID_SERVICE_EXCHANGE = "0000fff0-0000-1000-8000-00805f9b34fb"  # FFF0 — telemetry exchange
UUID_SERVICE_CONFIG = "0000fff1-0000-1000-8000-00805f9b34fb"  # FFF1 — configuration
UUID_CHAR_VENDOR = "00002a29-0000-1000-8000-00805f9b34fb"  # 180A/2A29 — manufacturer
UUID_CHAR_MODEL = "00002a24-0000-1000-8000-00805f9b34fb"  # 180A/2A24 — model
UUID_CHAR_FIRMWARE = "00002a26-0000-1000-8000-00805f9b34fb"  # 180A/2A26 — firmware version
UUID_CHAR_BAUDRATE = "0000fff1-0000-1000-8000-00805f9b34fb"  # uint32 little-endian
UUID_CHAR_DOMAIN = "0000fff2-0000-1000-8000-00805f9b34fb"  # ASCII string
UUID_CHAR_MODE = "0000fff3-0000-1000-8000-00805f9b34fb"  # 0 = BLE, 1 = WEB
UUID_CHAR_UART_STATUS = "0000fff4-0000-1000-8000-00805f9b34fb"  # 1 = OK, 2 = NO SIGNAL, 3 = BAD DATA

BAUDRATE_MIN = 1200  # SERIAL_BAUDRATE_MIN
BAUDRATE_MAX = 5250000  # SERIAL_BAUDRATE_MAX
DOMAIN_MAX_LENGTH = 20  # DOMAIN_NAME_MAX_LENGTH
MODE_BLE = 0
MODE_WEB = 1
UART_STATUS_OK = 1  # UART_DIAG_OK
UART_STATUS_NO_SIGNAL = 2  # UART_DIAG_NO_SIGNAL
UART_STATUS_BAD_DATA = 3  # UART_DIAG_BAD_DATA
CONNECT_TIMEOUT_S = 15.0

MODE_NAMES = {MODE_BLE: "BLE", MODE_WEB: "WEB"}
UART_STATUS_NAMES = {
    UART_STATUS_OK: "OK",
    UART_STATUS_NO_SIGNAL: "NO SIGNAL",
    UART_STATUS_BAD_DATA: "BAD DATA",
}
UART_STATUS_HINTS = {
    UART_STATUS_NO_SIGNAL: "no data on the UART line — check wiring (TX->RX, GND) and transmitter power",
    UART_STATUS_BAD_DATA: "bytes arrive but no valid CRSF frames — most likely a baudrate mismatch",
}


def parse_mode(text):
    value = text.strip().lower()
    if value in ("0", "ble"):
        return MODE_BLE
    if value in ("1", "web"):
        return MODE_WEB
    raise argparse.ArgumentTypeError("mode must be ble/0 or web/1")


def find_candidates(scan_results, wanted_name):
    """Pick matching devices: first by services FFF0/FFF1, then by name."""
    by_service, by_name = [], []
    for dev, adv in scan_results.values():
        uuids = {u.lower() for u in adv.service_uuids}
        if UUID_SERVICE_CONFIG in uuids or UUID_SERVICE_EXCHANGE in uuids:
            by_service.append((dev, adv))
        elif dev.name is not None and dev.name == wanted_name:
            by_name.append((dev, adv))
    return by_service or by_name


def print_not_found_hints():
    print("Device not found. Check that:", file=sys.stderr)
    print("  * the device must be in BLE mode (in WEB mode Bluetooth is off);", file=sys.stderr)
    print("  * it must not be connected to another client — otherwise it does not advertise;", file=sys.stderr)
    print("  * with no connections the device goes to deep sleep after 120 s — power-cycle it and retry.", file=sys.stderr)


async def read_uart_status(client):
    """Read FFF4; returns None on older firmware that has no such characteristic."""
    try:
        return bytes(await client.read_gatt_char(UUID_CHAR_UART_STATUS))[0]
    except (BleakError, OSError):
        return None


def print_uart_status(value):
    name = UART_STATUS_NAMES.get(value, "unknown")
    print(f"uart       : {value} ({name})")
    hint = UART_STATUS_HINTS.get(value, "unexpected status value")
    print(f"             {hint}")


async def cmd_scan(args):
    print(f"Scanning for {args.timeout:g} s...")
    results = await BleakScanner.discover(timeout=args.timeout, return_adv=True)
    found = find_candidates(results, args.name)
    if not found:
        print_not_found_hints()
        return 1
    print(f"Devices found: {len(found)}")
    for dev, adv in found:
        print(f"  {dev.address}  RSSI {adv.rssi:>4}  {dev.name or '(no name)'}")
    return 0


async def find_device(args):
    """Return the device address or None (with --address scanning is skipped)."""
    if args.address:
        return args.address
    print(f"Searching for the device (services FFF0/FFF1 or name {args.name!r}), {args.timeout:g} s...")
    results = await BleakScanner.discover(timeout=args.timeout, return_adv=True)
    found = find_candidates(results, args.name)
    if not found:
        print_not_found_hints()
        return None
    if len(found) > 1:
        print("Several matching devices found, disambiguate with --address:", file=sys.stderr)
        for dev, adv in found:
            print(f"  {dev.address}  RSSI {adv.rssi:>4}  {dev.name or '(no name)'}", file=sys.stderr)
        return None
    dev, _ = found[0]
    print(f"Device found: {dev.name or '(no name)'} [{dev.address}]")
    return dev.address


async def cmd_read(client, _args):
    vendor = bytes(await client.read_gatt_char(UUID_CHAR_VENDOR)).decode("ascii", "replace")
    model = bytes(await client.read_gatt_char(UUID_CHAR_MODEL)).decode("ascii", "replace")
    firmware = bytes(await client.read_gatt_char(UUID_CHAR_FIRMWARE)).decode("ascii", "replace")
    baud = struct.unpack("<I", bytes(await client.read_gatt_char(UUID_CHAR_BAUDRATE))[:4])[0]
    domain = bytes(await client.read_gatt_char(UUID_CHAR_DOMAIN)).decode("ascii", "replace")
    mode = bytes(await client.read_gatt_char(UUID_CHAR_MODE))[0]
    uart = await read_uart_status(client)
    print(f"Device     : {vendor} / {model} (firmware {firmware})")
    print(f"baudrate   : {baud}")
    print(f"domain     : {domain}")
    print(f"mode       : {mode} ({MODE_NAMES.get(mode, 'unknown')})")
    if uart is None:
        print("uart       : n/a (older firmware)")
    else:
        print(f"uart       : {uart} ({UART_STATUS_NAMES.get(uart, 'unknown')})")
    return 0


async def cmd_uart(client, args):
    status = await read_uart_status(client)
    if status is None:
        print("This firmware has no UART diagnostics characteristic (FFF4).", file=sys.stderr)
        return 1
    print_uart_status(status)
    if args.watch > 0:
        print(f"Following status notifications for {args.watch:g} s (Ctrl+C to abort)...")
        loop = asyncio.get_running_loop()
        started = loop.time()

        def on_notify(_sender, data):
            value = bytes(data)[0] if data else 0
            elapsed = loop.time() - started
            name = UART_STATUS_NAMES.get(value, "unknown")
            print(f"[{elapsed:7.1f} s] uart: {value} ({name})")
            hint = UART_STATUS_HINTS.get(value)
            if hint:
                print(f"             {hint}")

        await client.start_notify(UUID_CHAR_UART_STATUS, on_notify)
        try:
            await asyncio.sleep(args.watch)
        finally:
            await client.stop_notify(UUID_CHAR_UART_STATUS)
    return 0


async def cmd_baud(client, args):
    if not BAUDRATE_MIN <= args.value <= BAUDRATE_MAX:
        print(f"Baudrate must be within {BAUDRATE_MIN}..{BAUDRATE_MAX}", file=sys.stderr)
        return 1
    await client.write_gatt_char(UUID_CHAR_BAUDRATE, struct.pack("<I", args.value), response=False)
    baud = struct.unpack("<I", bytes(await client.read_gatt_char(UUID_CHAR_BAUDRATE))[:4])[0]
    print(f"Done, the device reports baudrate = {baud}")
    return 0


async def cmd_domain(client, args):
    try:
        data = args.value.encode("ascii")
    except UnicodeEncodeError:
        print("The name must consist of ASCII characters only", file=sys.stderr)
        return 1
    if not 1 <= len(data) <= DOMAIN_MAX_LENGTH or any(not 0x20 <= b <= 0x7E for b in data):
        print(f"The name must be 1..{DOMAIN_MAX_LENGTH} printable ASCII characters", file=sys.stderr)
        return 1
    await client.write_gatt_char(UUID_CHAR_DOMAIN, data, response=False)
    name = bytes(await client.read_gatt_char(UUID_CHAR_DOMAIN)).decode("ascii", "replace")
    print(f"Done, the device now answers to the name {name!r}.")
    print("Use --name to find it next time (or connect via --address).")
    return 0


async def cmd_mode(client, args):
    await client.write_gatt_char(UUID_CHAR_MODE, bytes([args.value]), response=False)
    print(f"Done, the device is rebooting into {MODE_NAMES[args.value]} mode.")
    if args.value == MODE_WEB:
        print("In WEB mode Bluetooth is off: switch back to BLE with the BOOT button (C3)")
        print("or via the web UI at http://192.168.4.1.")
    return 0


def build_parser():
    parser = argparse.ArgumentParser(
        description="Read and change ble-telemetry-lite settings over Bluetooth Low Energy (service FFF1).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="The device must be in BLE mode and not connected to another client.",
    )
    parser.add_argument("--name", default=DEFAULT_DEVICE_NAME, help="device name (default: %(default)s)")
    parser.add_argument("--address", help="Bluetooth device address, skips scanning")
    parser.add_argument("--timeout", type=float, default=10.0,
                        help="scan timeout, seconds (default: %(default)s)")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("scan", help="list found devices and exit")
    p.set_defaults(func=cmd_scan)

    p = sub.add_parser("read", help="show current settings")
    p.set_defaults(func=cmd_read)

    p = sub.add_parser("uart", help="show the UART diagnostics status (wiring / baudrate check)")
    p.add_argument("--watch", type=float, default=0.0, metavar="SECONDS",
                   help="follow status notifications for SECONDS seconds (default: %(default)s)")
    p.set_defaults(func=cmd_uart)

    p = sub.add_parser("baud", help="change the UART baudrate")
    p.add_argument("value", type=int, metavar="BAUD", help=f"baud rate, {BAUDRATE_MIN}..{BAUDRATE_MAX}")
    p.set_defaults(func=cmd_baud)

    p = sub.add_parser("domain", help="change the device name (BLE name and access-point SSID)")
    p.add_argument("value", metavar="NAME", help=f"1..{DOMAIN_MAX_LENGTH} printable ASCII characters")
    p.set_defaults(func=cmd_domain)

    p = sub.add_parser("mode", help="switch the operating mode (the device reboots)")
    p.add_argument("value", type=parse_mode, metavar="MODE", help="ble/0 or web/1")
    p.set_defaults(func=cmd_mode)

    return parser


async def main(argv=None):
    args = build_parser().parse_args(argv)
    if BleakClient is None:
        print("The bleak library is not installed — install it with: pip install bleak", file=sys.stderr)
        return 1

    if args.command == "scan":
        return await args.func(args)

    address = await find_device(args)
    if address is None:
        return 1
    result = None
    try:
        async with BleakClient(address, timeout=CONNECT_TIMEOUT_S) as client:
            result = await args.func(client, args)
    except (BleakError, asyncio.TimeoutError, OSError) as exc:
        if result is None:
            print(f"Communication error: {exc}", file=sys.stderr)
            return 1
        # The device disconnected after the command already succeeded (reboot on mode
        # switch) — not an error.
    return result


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
