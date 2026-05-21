"""
Echo service example — Mode 1 (schema provided).

Equivalent to the C++ EchoServiceExample. Connects to an EchoService
(service_id=1) anywhere on the network, validates the service type+version,
sends periodic echo requests, and prints received echoes.

Run an EchoService first:
    ./build/Debug/examples/services/EchoService/EchoService

Then run this script.
"""
import sys
import time
import logging
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from xbot_service_interface import XbotServiceIo, ServiceInterface

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(name)s: %(message)s',
)

SCHEMA = Path(__file__).parent / 'echo_service.json'


def main():
    xbot = XbotServiceIo(bind_ip='0.0.0.0')
    echo = ServiceInterface(service_id=1, schema=SCHEMA)
    xbot.register(echo)

    # ---- Lifecycle callbacks ----

    @echo.on_connected
    def connected():
        print("EchoService connected — sending initial configuration")
        # Set registers (sent immediately because we're already connected)
        echo.registers['Prefix']    = "py: "
        echo.registers['EchoCount'] = 2

    @echo.on_disconnected
    def disconnected():
        print("EchoService disconnected")

    # ---- Output callbacks (service → us) ----

    @echo.on_echo_changed
    def got_echo(value: str, timestamp: int):
        print(f"  echo: {value!r}")

    @echo.on_message_count_changed
    def got_count(value: int, timestamp: int):
        print(f"  message_count: {value}")

    # ---- Start ----
    xbot.start()
    print(f"Waiting for EchoService (service_id=1) …")

    # ---- Main loop: send a request every second ----
    i = 0
    try:
        while xbot.ok():
            if echo._connected:
                msg = f"Echo request {i}"
                print(f"Sending: {msg!r}")
                echo.send_input_text(msg)
                i += 1
                if i == 10:
                    echo.registers['EchoCount'] = 1
            time.sleep(1.0)
    except KeyboardInterrupt:
        pass
    finally:
        xbot.stop()


if __name__ == '__main__':
    main()
