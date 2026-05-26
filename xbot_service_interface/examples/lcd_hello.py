"""
Write "hello world" to a 1602 LCD connected via PCF8574 I2C expander.

Uses RemoteGPIOService (id=10). Configures with gpio.json (heatshrink-compressed)
and 1000 ms periodic update interval.

Typical PCF8574 addresses: 0x27 (A0-A2 high) or 0x3F (A0-A2 low).
"""
import sys
import time
import logging
from pathlib import Path

import heatshrink2

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from xbot_service_interface import XbotServiceIo, ServiceInterface

logging.basicConfig(level=logging.WARNING)

# ── Config ────────────────────────────────────────────────────────────────────

BIND_IP   = '172.16.78.151'
GPIO_JSON = Path(__file__).parent / 'gpio.json'
LCD_ADDR  = 0x27   # PCF8574 I2C address — change to 0x3F if needed
I2C_BUS   = 0      # matches "id": 0 in gpio.json

# ── HD44780 via PCF8574 ───────────────────────────────────────────────────────
# PCF8574 pin mapping:
#   P7-P4 → D7-D4  (high nibble)
#   P3    → Backlight
#   P2    → Enable
#   P1    → R/W (always 0 = write)
#   P0    → RS  (0=command, 1=data)

_BL = 0x08
_EN = 0x04
_RS = 0x01


def _nibble_bytes(nibble: int, flags: int) -> bytes:
    """Pulse Enable for one 4-bit nibble. Returns 2 I2C bytes."""
    b = (nibble & 0xF0) | flags | _BL
    return bytes([b | _EN, b & ~_EN])


def _byte_bytes(value: int, rs: int) -> bytes:
    """Encode a full byte as two nibble pulses (4 I2C bytes)."""
    return (_nibble_bytes(value & 0xF0, rs) +
            _nibble_bytes((value << 4) & 0xF0, rs))


def _tx(svc, data: bytes):
    svc.call_i2_ctransmit(I2C_BUS, LCD_ADDR, data, timeout_ms=1500)


def lcd_cmd(svc, cmd: int):
    _tx(svc, _byte_bytes(cmd, 0))


def lcd_char(svc, ch: str):
    _tx(svc, _byte_bytes(ord(ch), _RS))


def lcd_init(svc):
    lcd_cmd(svc, 0x33)
    lcd_cmd(svc, 0x32)
    lcd_cmd(svc, 0x06)
    lcd_cmd(svc, 0x0C)
    lcd_cmd(svc, 0x28)
    lcd_cmd(svc, 0x01)
    time.sleep(0.0005)


def lcd_write(svc, text: str, line: int = 0):
    addr = 0x80 if line == 0 else 0xC0
    lcd_cmd(svc, addr)
    for ch in text[:16]:
        lcd_char(svc, ch)


def create_char(svc, location, charmap):
    """Write custom char to CGRAM"""
    location &= 0x7  # Only 8 slots (0–7)
    lcd_cmd(svc, 0x40 | (location << 3))
    for byte in charmap:
        _tx(svc, _byte_bytes(byte, _RS))


def define_custom_characters(svc):
    # emergency
    create_char(svc, 0, [0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x00, 0x0E, 0x0E])
    # battery empty
    create_char(svc, 1, [0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F, 0x00])
    # battery 50%
    create_char(svc, 2, [0x0E, 0x11, 0x11, 0x11, 0x1F, 0x1F, 0x1F, 0x00])
    # battery full
    create_char(svc, 3, [0x0E, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x00])
    # battery charging
    create_char(svc, 4, [0x0E, 0x1B, 0x17, 0x11, 0x1D, 0x1B, 0x1F, 0x00])
    # gps no rtk
    create_char(svc, 5, [0x00, 0x0E, 0x19, 0x15, 0x13, 0x0E, 0x00, 0x00])
    # gps rtk float
    create_char(svc, 6, [0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00])
    # gps rtk fixed
    create_char(svc, 7, [0x00, 0x0E, 0x1F, 0x1B, 0x1F, 0x0E, 0x00, 0x00])


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    xbot = XbotServiceIo(bind_ip=BIND_IP)
    svc  = ServiceInterface(service_id=10)
    xbot.register(svc)

    gpio_blob = heatshrink2.compress(
        GPIO_JSON.read_bytes(), window_sz2=9, lookahead_sz2=5)

    @svc.on_connected
    def connected():
        print("RemoteGPIOService connected — initialising LCD…")
        svc.registers['gpio_configs'] = gpio_blob
        svc.registers['periodic_update_interval'] = 1000
        try:
            lcd_init(svc)
            define_custom_characters(svc)
            print("Done.")
        except Exception as e:
            print(f"LCD error: {e}")

    @svc.on_disconnected
    def disconnected():
        print("RemoteGPIOService disconnected.")



    xbot.start()
    print("Waiting for RemoteGPIOService (id=10)…")

    try:
        counter = 0
        while xbot.ok():
            if svc.connected:
                try:
                    lcd_write(svc, f"Counter: {counter}", line=0)
                except Exception as e:
                    print(f"Update error: {e}")
                counter += 1

            # time.sleep(0.01)
    except KeyboardInterrupt:
        pass
    finally:
        xbot.stop()


if __name__ == '__main__':
    main()
