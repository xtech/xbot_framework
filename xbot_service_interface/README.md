# xbot-service-interface

Python interface library for the [xBot Framework](https://github.com/xtech/xbot_framework).
Connect to xBot services running anywhere on your network — no code generation required.

## Installation

```bash
pip install xbot-service-interface
```

## Quick start

```python
from xbot_service_interface import XbotServiceIo, ServiceInterface

xbot = XbotServiceIo(bind_ip='0.0.0.0')

# Mode 1: provide schema for type/version validation
echo = ServiceInterface(service_id=1, schema='echo_service.json')

# Mode 2: discover schema automatically from advertisement
echo = ServiceInterface(service_id=1)

xbot.register(echo)

@echo.on_connected
def connected():
    echo.registers['Prefix']    = "py: "
    echo.registers['EchoCount'] = 2

@echo.on_echo_changed
def got_echo(value: str, timestamp: int):
    print(f"echo: {value}")

xbot.start()

echo.send_input_text("hello")

# Atomic transaction
with echo.transaction():
    echo.send_input_text("hello")
    echo.send_other_input(42)

xbot.stop()
```

## Concepts

### Two modes

| Mode | Usage | Behaviour |
|------|-------|-----------|
| **Mode 1** — schema provided | `ServiceInterface(service_id=1, schema='svc.json')` | Validates advertised `type` + `version` against your schema on connect. Raises `IncompatibleServiceError` on mismatch. |
| **Mode 2** — no schema | `ServiceInterface(service_id=1)` | Accepts the first service with matching ID. Uses the full schema embedded in the advertisement (inputs, outputs, registers, enums all available). |

### Dynamic API

Channels from the service JSON are available as Python attributes at runtime:

```python
# Inputs (interface → service): send_{snake_name}(value)
echo.send_input_text("hello")
echo.send_input[0]("hello")          # by id

# Outputs (service → interface): on_{snake_name}_changed
echo.on_echo_changed = my_callback   # assignment
@echo.on_echo_changed                # decorator
def my_callback(value, timestamp): ...
echo.on_output[0] = my_callback      # by id
```

Name mapping: `"Input Text"` → `send_input_text`, `"Message Count"` → `on_message_count_changed`.

### Registers

```python
echo.registers['Prefix']    = "hello: "   # sent on connect / immediately if connected
echo.registers['EchoCount'] = 2
val = echo.registers['Prefix']
```

All registers are sent as a single configuration transaction (required by the xBot protocol).

## Requirements

- Python 3.10+
- Linux (uses `SIOCGIFADDR` for IP detection, UDP multicast for discovery)
- [`cbor2`](https://pypi.org/project/cbor2/)
