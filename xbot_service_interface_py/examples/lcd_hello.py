"""
Counter display on a 1602 LCD via RemoteGPIOService (id=10).

GPIO6 = button → count down
GPIO7 = button → count up
Line 0: counter value
Line 1: custom icon strip (CGRAM slots 0-7)
"""
import time
import logging
import argparse
from pathlib import Path

import heatshrink2

from xbot_service_interface import XbotServiceIo, ServiceInterface

logging.basicConfig(level=logging.WARNING)

BIND_IP   = '0.0.0.0'
GPIO_JSON = Path(__file__).parent / 'gpio.json'
GPIO_DOWN = 6
GPIO_UP   = 7


# ── LcdDisplay ────────────────────────────────────────────────────────────────

class LcdDisplay:
    """HD44780 1602 LCD over PCF8574 I2C expander via RemoteGPIOService.

    PCF8574 pin mapping:
      P7-P4 → D7-D4  (high nibble)
      P3    → Backlight
      P2    → Enable
      P1    → R/W (always 0 = write)
      P0    → RS  (0=command, 1=data)

    Call configure() each time the service connects; it initialises the
    controller and uploads all custom characters to CGRAM.
    """

    # PCF8574 control bits
    _BL = 0x08
    _EN = 0x04
    _RS = 0x01

    # Typical PCF8574 addresses: 0x27 (A0-A2 high) or 0x3F (A0-A2 low)
    DEFAULT_ADDR    = 0x27
    DEFAULT_I2C_BUS = 0      # matches "id": 0 in gpio.json

    # Custom CGRAM characters (slots 0-7)
    CUSTOM_CHARS = [
        [0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x00, 0x0E, 0x0E],  # 0: emergency
        [0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F, 0x00],  # 1: battery empty
        [0x0E, 0x11, 0x11, 0x11, 0x1F, 0x1F, 0x1F, 0x00],  # 2: battery 50%
        [0x0E, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x00],  # 3: battery full
        [0x0E, 0x1B, 0x17, 0x11, 0x1D, 0x1B, 0x1F, 0x00],  # 4: battery charging
        [0x00, 0x0E, 0x19, 0x15, 0x13, 0x0E, 0x00, 0x00],  # 5: gps no rtk
        [0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00],  # 6: gps rtk float
        [0x00, 0x0E, 0x1F, 0x1B, 0x1F, 0x0E, 0x00, 0x00],  # 7: gps rtk fixed
    ]

    # Icon strip: emergency, gps no-rtk, gps float, gps fixed,
    #             bat empty, bat 50%, bat full, bat charging
    ICON_STRIP = [0, 5, 6, 7, 1, 2, 3, 4]

    def __init__(self, svc: ServiceInterface,
                 i2c_bus: int = DEFAULT_I2C_BUS,
                 addr: int = DEFAULT_ADDR):
        self._svc  = svc
        self._bus  = i2c_bus
        self._addr = addr

    # ── low-level helpers ─────────────────────────────────────────────────────

    def _tx(self, data: bytes) -> None:
        self._svc.call_i2c_transmit(self._bus, self._addr, data, timeout_ms=1500)

    def _nibble(self, nibble: int, flags: int) -> bytes:
        b = (nibble & 0xF0) | flags | self._BL
        return bytes([b | self._EN, b & ~self._EN])

    def _encode(self, value: int, rs: int) -> bytes:
        return (self._nibble(value & 0xF0, rs) +
                self._nibble((value << 4) & 0xF0, rs))

    def _cmd(self, cmd: int) -> None:
        self._tx(self._encode(cmd, 0))

    def _data(self, value: int) -> None:
        self._tx(self._encode(value, self._RS))

    # ── public API ────────────────────────────────────────────────────────────

    def configure(self) -> None:
        """Initialise controller and upload custom characters. Call on connect."""
        self._cmd(0x33)
        self._cmd(0x32)
        self._cmd(0x06)
        self._cmd(0x0C)
        self._cmd(0x28)
        self._cmd(0x01)
        time.sleep(0.002)  # HD44780 clear-display requires ≥1.52 ms
        for slot, charmap in enumerate(self.CUSTOM_CHARS):
            self._cmd(0x40 | (slot << 3))
            for byte in charmap:
                self._data(byte)

    def write(self, text: str, line: int = 0) -> None:
        """Write up to 16 chars of text to line 0 or 1."""
        self._cmd(0x80 if line == 0 else 0xC0)
        for ch in text[:16]:
            self._data(ord(ch))

    def write_icons(self, slots: list[int], line: int = 1) -> None:
        """Write CGRAM slot numbers as characters (custom icons)."""
        self._cmd(0x80 if line == 0 else 0xC0)
        for slot in slots[:16]:
            self._data(slot)


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='LCD counter via RemoteGPIOService')
    parser.add_argument('--bind', '-b', default=BIND_IP, metavar='IP',
                        help='local interface IP to bind (default: 0.0.0.0)')
    args = parser.parse_args()

    xbot = XbotServiceIo(bind_ip=args.bind)
    svc  = ServiceInterface(service_id=10)
    xbot.register(svc)

    gpio_blob = heatshrink2.compress(
        GPIO_JSON.read_bytes(), window_sz2=9, lookahead_sz2=5)

    svc.registers['gpio_configs'] = gpio_blob
    svc.registers['periodic_update_interval'] = 1000

    lcd     = LcdDisplay(svc)
    counter = 0

    @svc.on_gpio_event_changed
    def gpio_event(value, _ts):
        nonlocal counter
        gpio_id = value[0]
        # value[1] = level (1 = pressed for active-high), value[2] = flags
        if value[1] == 0:
            return
        if gpio_id == GPIO_DOWN:
            counter -= 1
        elif gpio_id == GPIO_UP:
            counter += 1
        try:
            lcd.write(f"Count: {counter:<9}", line=0)
        except Exception as e:
            print(f"LCD update error: {e}")

    @svc.on_configured
    def configured():
        print("RemoteGPIOService connected — initialising LCD…")
        try:
            lcd.configure()
            lcd.write(f"Count: {counter:<9}", line=0)
            lcd.write_icons(LcdDisplay.ICON_STRIP, line=1)
            print("LCD ready.")
        except Exception as e:
            print(f"LCD init error: {e}")
            return
        try:
            svc.call_subscribe_gpio(GPIO_DOWN, 0)
            svc.call_subscribe_gpio(GPIO_UP,   0)
            print(f"Subscribed to GPIO{GPIO_DOWN} (down) and GPIO{GPIO_UP} (up).")
        except Exception as e:
            print(f"GPIO subscribe error: {e}")

    @svc.on_disconnected
    def disconnected():
        print("RemoteGPIOService disconnected.")

    xbot.start()
    local_ip, local_port = xbot._io.get_endpoint()
    print(f"Listening on {local_ip}:{local_port} — waiting for RemoteGPIOService (id=10)…")

    try:
        while xbot.ok():
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    finally:
        xbot.stop()


if __name__ == '__main__':
    main()
