# xbot-service-interface

Python interface library for the [xBot Framework](https://github.com/xtech/xbot_framework).
Connect to xBot services running anywhere on your network — no code generation required.

## Installation

```bash
# Library only
pip install xbot-service-interface

# Library + interactive shell (IPython + Rich)
pip install "xbot-service-interface[shell]"
```

### From source

```bash
git clone https://github.com/xtech/xbot_framework
cd xbot_framework/xbot_service_interface

# Library only
pip install -e .

# With interactive shell
pip install -e ".[shell]"
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

## Interactive shell

`xbot-shell` is an IPython-based REPL for exploring and testing services live.
Requires the `[shell]` extras (`ipython`, `rich`).

```bash
xbot-shell               # bind all interfaces
xbot-shell --bind 192.168.1.x   # specific interface
```

On startup the shell listens for service advertisements on the multicast group
and prints each service as it appears:

```
→ Discovered: EchoService (id=1) at 192.168.1.5:4242 — connect(1)
```

### Shell commands

```python
services()               # list all discovered services
svc = connect(1)         # connect by service ID
svc = connect("EchoService")  # or by type name

svc.wait_connected()     # block until the service is claimed (default 10 s)
svc.info()               # print schema — inputs, outputs, registers, RPC functions
svc.watch_all()          # stream every output value to the console
svc.watch('echo')        # stream a specific output
```

IPython magic equivalents:

```
%services
%connect EchoService
%connect 1 as svc
```

### Tab completion

All service channels are tab-completable once connected:

```
svc.send_<TAB>              → send_input_text(value)
svc.call_<TAB>              → call_rpc_echo_test(text, echo_count)
svc.on_<TAB>                → on_echo_changed, on_message_count_changed
svc.registers.<TAB>         → prefix, echo_count
```

Pressing `?` on a method shows the full parameter signature:

```python
svc.call_rpc_echo_test?
# RpcEchoTest(text: char[10], echo_count: uint32_t) -> char[30]
```

### Example session

```python
In [1]: services()
# ┌────┬─────────────┬──────────────────┬─────┬─────┬────┐
# │ ID │ Type        │ Endpoint         │ In  │ Out │ Fn │
# ├────┼─────────────┼──────────────────┼─────┼─────┼────┤
# │  1 │ EchoService │ 192.168.1.5:4242 │  1  │  2  │  3 │
# └────┴─────────────┴──────────────────┴─────┴─────┴────┘

In [2]: svc = connect(1)
In [3]: svc.wait_connected()
# ✓ Connected to EchoService

In [4]: svc.registers.prefix = "py: "
In [5]: svc.registers.echo_count = 2

In [6]: svc.watch_all()
# Watching: echo, message_count

In [7]: svc.send_input_text("hello")
#    1234567ms echo = 'py: hello'
#    1234567ms message_count = 1

In [8]: svc.call_rpc_echo_test("hi", 3)
# 'py: hihihi'
```

## Requirements

- Python 3.10+
- Linux (uses `SIOCGIFADDR` for IP detection, UDP multicast for discovery)
- [`cbor2`](https://pypi.org/project/cbor2/)
- [`ipython`](https://pypi.org/project/ipython/) ≥ 9 (shell only)
- [`rich`](https://pypi.org/project/rich/) ≥ 15 (shell only)
