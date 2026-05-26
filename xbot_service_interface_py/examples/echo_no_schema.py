"""
Echo service example — Mode 2 (no schema provided).

The schema is discovered automatically from the service advertisement.
No service.json file required. All registers, inputs and outputs are
still available — the full JSON is embedded in the advertisement CBOR.
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


def main():
    xbot = XbotServiceIo(bind_ip='0.0.0.0')

    # No schema — type is accepted as-is, schema comes from advertisement
    echo = ServiceInterface(service_id=1)
    xbot.register(echo)

    @echo.on_connected
    def connected():
        schema = echo._active_schema
        print(f"Connected to {schema.type!r} v{schema.version}")
        print(f"  Inputs:    {[i['name'] for i in schema.inputs]}")
        print(f"  Outputs:   {[o['name'] for o in schema.outputs]}")
        print(f"  Registers: {[r['name'] for r in schema.registers]}")
        print(f"  Functions: {[f['name'] for f in schema.functions]}")

        echo.registers['Prefix']    = "no-schema: "
        echo.registers['EchoCount'] = 1

    @echo.on_disconnected
    def disconnected():
        print("EchoService disconnected")

    # Callbacks registered by name — work the same as Mode 1
    @echo.on_echo_changed
    def got_echo(value: str, timestamp: int):
        print(f"  echo: {value!r}")

    @echo.on_message_count_changed
    def got_count(value: int, timestamp: int):
        pass  # Suppress, just show echo

    xbot.start()
    print("Waiting for EchoService (service_id=1, any type) …")


    i = 0
    try:
        while xbot.ok():
            if echo._connected:
                echo.send_input_text(f"request {i}")
                i += 1
                if i == 10:
                    echo.registers['EchoCount'] = 5
            time.sleep(1.0)
    except KeyboardInterrupt:
        pass
    finally:
        xbot.stop()


if __name__ == '__main__':
    main()
