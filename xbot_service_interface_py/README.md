# xbot-service-interface-py

Python client library for [xBot Framework](https://github.com/xtech/xbot_framework) services. Connects to xBot services over UDP without code generation — schema is either loaded from a JSON file or received automatically from the service advertisement.

## Installation

Install directly from a tagged GitHub release (replace `v0.1.3` with the desired version):

```bash
pip install "xbot-service-interface-py @ git+https://github.com/xtech/xbot_framework.git@v0.1.3#subdirectory=xbot_service_interface_py"
```

With the optional IPython shell extras:

```bash
pip install "xbot-service-interface-py[shell] @ git+https://github.com/xtech/xbot_framework.git@v0.1.3#subdirectory=xbot_service_interface_py"
```

To install the latest unreleased version from `main`:

```bash
pip install "xbot-service-interface-py @ git+https://github.com/xtech/xbot_framework.git#subdirectory=xbot_service_interface_py"
```

> **PyPI:** `pip install xbot-service-interface-py` will be available once the package is published there.

Requires Python 3.10+.

---

## Architecture overview

```text
XbotServiceIo              — owns the UDP socket and discovery listener
  └── ServiceInterface     — represents one remote service
        ├── registers      — send/read configuration registers
        ├── send_*()       — send inputs to the service
        ├── on_*_changed   — receive outputs from the service
        └── call_*()       — synchronous RPC calls
```

`XbotServiceIo` runs two background threads: one for multicast service discovery (UDP 233.255.255.0:4242) and one for the data/control socket. Output and data callbacks (`on_*_changed`) are dispatched directly from those IO/discovery threads. Lifecycle callbacks (`on_connected`, `on_disconnected`) are dispatched on separate short-lived daemon threads so they can safely call RPC functions or block without deadlocking the IO thread.

---

## Quick start

### Mode 1 — schema provided (validated)

```python
from xbot_service_interface import XbotServiceIo, ServiceInterface

xbot = XbotServiceIo(bind_ip='0.0.0.0')
echo = ServiceInterface(service_id=1, schema='echo_service.json')
xbot.register(echo)

@echo.on_connected
def connected():
    echo.registers['Prefix']    = "py: "
    echo.registers['EchoCount'] = 2

@echo.on_echo_changed
def got_echo(value: str, timestamp: int):
    print(f"echo: {value!r}  (ts={timestamp}µs)")

xbot.start()

import time
while xbot.ok():
    if echo.connected:
        echo.send_input_text("hello")
    time.sleep(1.0)

xbot.stop()
```

The schema file is validated against the service advertisement on discovery. If the advertised type or version does not match, `IncompatibleServiceError` is raised, the connection attempt is aborted, and the interface will not connect.

### Mode 2 — schema-free (auto-discovered)

```python
echo = ServiceInterface(service_id=1)   # no schema= argument
xbot.register(echo)

@echo.on_connected
def connected():
    schema = echo._active_schema
    print(schema.type, schema.version)
    print([i['name'] for i in schema.inputs])
```

The full service description is embedded in the CBOR advertisement packet. All inputs, outputs, registers and RPC functions are available immediately after connection.

---

## API reference

### `XbotServiceIo`

```python
xbot = XbotServiceIo(bind_ip='0.0.0.0')
```

| Method | Description |
|--------|-------------|
| `register(iface)` | Register a `ServiceInterface`. Call before or after `start()`. |
| `start()` | Start IO and discovery background threads. |
| `stop()` | Stop all threads gracefully. |
| `ok() → bool` | `True` while the IO thread is running. Use as main-loop condition. |

`bind_ip` selects the network interface for the UDP socket and multicast group membership. Use `'0.0.0.0'` to listen on all interfaces.

---

### `ServiceInterface`

```python
iface = ServiceInterface(service_id: int, schema=None)
```

`schema` accepts: a file path (`str` or `Path`), a `dict`, a `ServiceSchema` instance, or `None` for schema-free mode.

#### Lifecycle

```python
@iface.on_connected
def handler():
    ...

@iface.on_disconnected
def handler():
    ...
```

Both can also be used as direct calls: `iface.on_connected(my_fn)`. Callbacks fire in a dedicated daemon thread so they can safely call RPC functions or block without deadlocking the IO thread.

```python
iface.connected  # bool property — True once CLAIM_ACK received
```

#### Sending inputs

Inputs are sent by calling `send_{snake_name}(value)`:

```python
iface.send_input_text("hello")   # char[] input named "InputText"
iface.send_target_speed(1.5)     # float input named "TargetSpeed"
```

The attribute is resolved dynamically against the active schema. Calling it before the service is connected raises `RuntimeError`.

#### Receiving outputs

Assign a callable to `on_{snake_name}_changed`:

```python
# Assignment style
iface.on_echo_changed = lambda value, ts: print(value)

# Decorator style
@iface.on_echo_changed
def handler(value, timestamp: int):
    ...
```

The callback receives `(value, timestamp)` where `timestamp` is in microseconds. Callbacks can be registered before discovery — they are wired automatically once the schema is known.

Output callbacks can also be registered by numeric channel ID before the schema is available:

```python
iface.on_output[3] = my_callback
```

#### Registers

Registers configure the service. They are sent as a transaction the moment one is written while connected; if not connected yet, they are stored and sent on the next `CONFIGURATION_REQUEST`.

```python
iface.registers['Prefix']    = "hello: "   # by original name
iface.registers['echo_count'] = 2          # by snake_case name
iface.registers['EchoCount']  = 2          # any capitalisation works
```

The service resets **all** registers to defaults before applying a configuration transaction, so the library always sends all stored registers together — not just the changed one.

#### Atomic transactions

Multiple inputs can be bundled into a single UDP packet:

```python
with iface.transaction():
    iface.send_x(1.0)
    iface.send_y(2.0)
    iface.send_z(3.0)
```

If an exception is raised inside the block the transaction is discarded. Only one transaction can be active at a time per interface.

#### RPC calls

RPC functions are called synchronously as `call_{snake_name}(*args, timeout_ms=1000)`:

```python
result = iface.call_rpc_echo_test("hi", 3)           # default 1000ms timeout
result = iface.call_rpc_echo_test("hi", 3, timeout_ms=500)
```

The call blocks until a response is received or the timeout expires. Raises:

| Exception | Cause |
|-----------|-------|
| `RpcTimeoutError` | No response within `timeout_ms` |
| `RpcBusyError` | Service reports another call is already in progress |
| `RpcError(status)` | Service returned a non-zero status code |
| `TypeError` | Wrong number of arguments |
| `RuntimeError` | Not connected, or call failed to send |

Only one in-flight RPC call per interface is allowed at a time.

#### Low-level by-ID access

```python
iface.send_input[channel_id](value)    # send by numeric channel id
iface.on_output[channel_id] = callback # register output by channel id
```

---

### `ServiceSchema`

Parsed service description. Normally created automatically from discovery or from the `schema=` argument.

```python
schema = ServiceSchema.from_file('echo_service.json')
schema = ServiceSchema.from_dict({"type": "EchoService", "version": 1, ...})

schema.type       # str
schema.version    # int
schema.inputs     # list of channel dicts
schema.outputs    # list of channel dicts
schema.registers  # list of register dicts
schema.functions  # list of function dicts
schema.enums_dict # dict of enum definitions

schema.get_input('InputText')    # by name, snake_name, or numeric id
schema.get_output(3)
schema.get_register('Prefix')
schema.get_function('RpcEchoTest')
schema.is_compatible(advertised_desc: dict) -> bool
```

Channel dicts contain: `id`, `name`, `snake_name`, `type_str`, `base_type`, `is_array`, `max_len`. Register dicts additionally have `optional` (bool) and optionally `default`.

#### Schema JSON format

```json
{
  "type": "EchoService",
  "version": 1,
  "inputs": [
    {"id": 1, "name": "InputText", "type": "char[100]"}
  ],
  "outputs": [
    {"id": 1, "name": "Echo",         "type": "char[100]"},
    {"id": 2, "name": "MessageCount", "type": "uint32_t"}
  ],
  "registers": [
    {"id": 1, "name": "Prefix",    "type": "char[20]", "optional": false},
    {"id": 2, "name": "EchoCount", "type": "uint32_t", "optional": true}
  ],
  "functions": [
    {
      "id": 1, "name": "RpcEchoTest",
      "return_type": "char[30]",
      "parameters": [
        {"id": 1, "name": "Text",      "type": "char[10]"},
        {"id": 2, "name": "EchoCount", "type": "uint32_t"}
      ]
    }
  ],
  "enums": [
    {
      "id": "GpioMode",
      "base_type": "uint8_t",
      "bitmask": false,
      "values": {"INPUT": 0, "OUTPUT": 1, "INPUT_PULLUP": 2}
    }
  ]
}
```

#### Type system

| Type string | Python type |
|-------------|-------------|
| `uint8_t` … `uint64_t` | `int` |
| `int8_t` … `int64_t` | `int` |
| `float`, `double` | `float` |
| `bool` | `bool` |
| `char[N]` | `str` |
| `uint8_t[N]` etc. | `list[int]` |
| `float[N]` / `double[N]` | `list[float]` |
| `blob` | `bytes` |
| enum name | `str` (value name) or `int` |

---

### Exceptions

```python
from xbot_service_interface.exceptions import (
    IncompatibleServiceError,  # schema type/version mismatch on discovery
    UnknownChannelError,       # unknown input/output/register/function name or id
    RpcError,                  # RPC non-zero status (.status attribute)
    RpcBusyError,              # RPC busy (status=1)
    RpcTimeoutError,           # RPC timeout (inherits TimeoutError)
)
```

---

### Logging

The library uses standard `logging` under the `xbot_service_interface` hierarchy. Enable with:

```python
import logging
logging.basicConfig(level=logging.INFO)
```

Use `logging.DEBUG` to see per-packet detail including RPC round-trips and discovery events.

---

## IPython shell

`xbot-shell` is an IPython-based interactive shell for exploring and controlling xBot services without writing any code. It requires the `[shell]` extras (`ipython`, `rich`).

```bash
xbot-shell                     # listen on all interfaces
xbot-shell --bind 192.168.1.5  # listen on a specific interface
```

On startup the shell prints a command reference and begins listening for service advertisements. Each new service is announced automatically:

```text
→ Discovered: EchoService (id=1) at 192.168.1.5:4242 — connect(1)
```

### Built-in functions

Two functions are injected into the IPython namespace.

#### `services()`

Print a rich table of all discovered services with endpoint, channel counts, connection and configuration status.

```python
In [1]: services()
```

```text
           Discovered Services
 ID  Type         Endpoint             In  Out  Fn  Status     Config
  1  EchoService  192.168.1.5:4242      1    2   1  connected  configured
  2  GpioService  192.168.1.6:4242      8    8   0
```

#### `connect(id_or_name) → ServiceProxy`

Connect to a service by numeric ID or type name. Returns a `ServiceProxy` immediately — connection handshake runs in the background.

```python
svc = connect(1)
svc = connect("EchoService")   # case-insensitive type name match
```

Call `wait_connected()` on the result to block until the service is ready before sending any data:

```python
svc.wait_connected()           # blocks up to 10 s (default)
svc.wait_connected(timeout=30) # custom timeout in seconds
```

Calling `connect()` a second time for the same service returns the existing proxy.

### Magic commands

IPython line-magic equivalents that also inject the proxy directly into the namespace:

```text
%services
%connect 1
%connect EchoService
%connect EchoService as echo
```

Without `as <name>`, the variable name is derived from the service type in lowercase (e.g. `echoservice`). With `as <name>` it uses that name exactly.

### `ServiceProxy`

The object returned by `connect()`. All attributes are tab-completable.

#### Introspection

```python
svc.info()    # print rich tables: inputs, outputs, registers, RPC functions
repr(svc)     # one-line summary with connection status
```

`svc.info()` example output:

```text
EchoService  v1  (id=1)  connected  configured

  Inputs
  method                      type
  send_input_text(value)      char[100]

  Outputs
  callback attribute          type
  on_echo_changed             char[100]
  on_message_count_changed    uint32_t

  Registers
  registers.name    type       value     req
  prefix            char[20]   'py: '    *
  echo_count        uint32_t   2

  RPC Functions
  method                      parameters                              returns
  call_rpc_echo_test(...)     text: char[10], echo_count: uint32_t   char[30]
```

`*` in the req column means the register is required (not optional).

#### Sending inputs

Tab-complete `svc.send_<TAB>` to see all inputs:

```python
svc.send_input_text("hello")
svc.send_target_speed(1.5)
```

Raises `RuntimeError` if not connected.

#### Receiving outputs

Tab-complete `svc.on_<TAB>` to see all output callbacks:

```python
svc.on_echo_changed = lambda value, ts: print(value)

@svc.on_echo_changed
def handler(value, timestamp):
    print(f"{value!r}  ts={timestamp}µs")
```

#### Live output streaming

```python
svc.watch_all()                          # stream every output to console
svc.watch('echo')                        # stream a single output
svc.watch('echo', 'message_count')       # stream multiple outputs
```

Console format:

```text
       12345ms echo = 'py: hello'
       12346ms message_count = 7
```

#### RPC calls

Tab-complete `svc.call_<TAB>` to list all RPC functions. Append `?` to see the full signature:

```python
svc.call_rpc_echo_test?
# RpcEchoTest(text: char[10], echo_count: uint32_t) -> char[30]
# RPC call. Optional keyword: timeout_ms (default 1000).

result = svc.call_rpc_echo_test("hi", 3)
result = svc.call_rpc_echo_test("hi", 3, timeout_ms=500)
```

#### Registers

Tab-complete `svc.registers.<TAB>` to see all register names:

```python
# Read
print(svc.registers.prefix)
print(svc.registers['Prefix'])

# Write — sent immediately if connected
svc.registers.prefix     = "hello: "
svc.registers.echo_count = 2
svc.registers['Prefix']  = "hello: "

# Pretty-print all registers with current values
print(svc.registers)
```

#### Interactive register wizard

```python
svc.configure_registers()
```

Walks through every register interactively, showing the current value, type, and an input hint. Rules:

- Press Enter to keep the current value unchanged.
- Type the new value and press Enter to update.
- Type `-` to clear an optional register.
- Required registers (marked `*`) cannot be cleared.
- Numeric arrays accept `1, 2, 3` or `[1, 2, 3]` notation.
- Enum registers show the valid names: `one of: INPUT, OUTPUT, INPUT_PULLUP`.
- `blob` registers accept a file path and optionally compress with heatshrink.

Example session:

```text
Configuring registers for EchoService (Enter = keep current, '-' = clear optional)

  prefix  char[20]  *
    current=<not set>  new value: py:
    ✓ set to 'py:'
  echo_count  uint32_t  (optional)
    current=<not set>  new value: 2
    ✓ set to 2

✓ Configuration sent.
```

For `blob` registers:

```text
  firmware  blob  *  hint: path to file (will ask about heatshrink compression)
    current=<not set>  file path: /tmp/firmware.bin
    heatshrink compress? [y/N]: y
    ✓ set to 4096 bytes
```

Heatshrink uses window=9, lookahead=5 — matching the C++ firmware defaults. Requires `heatshrink2` (`pip install "xbot-service-interface-py[shell]"` includes it).

#### Atomic transactions

```python
with svc.transaction():
    svc.send_x(1.0)
    svc.send_y(2.0)
    svc.send_z(3.0)
```

#### Connection status

```python
svc.connected                  # bool
svc.wait_connected(timeout=10) # blocks, returns bool, prints status
```

`wait_connected()` also warns if required registers are not yet configured:

```text
✓ Connected to EchoService
⚠ Required registers not set: prefix, echo_count — call configure_registers()
```

### Full shell session example

```python
In [1]: services()
# table shows EchoService id=1

In [2]: svc = connect(1)
# Connecting to EchoService (id=1)...  call wait_connected() on result to block until ready.

In [3]: svc.wait_connected()
# ✓ Connected to EchoService
# ⚠ Required registers not set: prefix, echo_count — call configure_registers()
Out[3]: True

In [4]: svc.configure_registers()
# ... interactive wizard ...
# ✓ Configuration sent.

In [5]: svc.watch_all()
# Watching: echo, message_count

In [6]: svc.send_input_text("hello")
#        12345ms echo = 'py: hello'
#        12345ms message_count = 1

In [7]: result = svc.call_rpc_echo_test("hi", 3)
In [8]: result
Out[8]: 'hi hi hi'
```

### Magic-command session

```text
%connect EchoService as svc
svc.wait_connected()
svc.configure_registers()
svc.watch_all()
```

---

## Remote log viewer

`xbot-logs` receives and displays remote log messages broadcast by xBot services. It is included in the base package — no extras required.

Services enable remote logging by calling `xbot::service::startRemoteLogging(level)` on the C++ side. Log messages are multicast to `233.255.255.1:4242`.

```bash
xbot-logs                          # listen on all interfaces, show debug and above
xbot-logs --bind 192.168.1.5       # listen on a specific interface
xbot-logs --level info             # filter: trace|debug|info|warning|error|critical
xbot-logs --no-colour              # disable ANSI colour output
```

Example output:

```text
12:34:56.789 [INF] 192.168.1.10 | [ID=1] Service started
12:34:57.001 [DBG] 192.168.1.10 | loop tick 42
12:34:57.500 [WRN] 192.168.1.10 | heartbeat late
```

---

## Programmatic usage notes

### Subscribing to outputs before discovery

```python
xbot = XbotServiceIo()
echo = ServiceInterface(service_id=1)
xbot.register(echo)

# Registered before start() — wired automatically on discovery
@echo.on_echo_changed
def got_echo(value, ts):
    print(value)

xbot.start()
```

### Reconnection

On disconnect, `XbotServiceIo` drops the service from its internal table. The next advertisement from the same service ID triggers a fresh connection flow, calling `on_connected` again. No reconnection logic is needed in application code.

```python
@echo.on_disconnected
def disconnected():
    print("Lost connection — will reconnect automatically on next advertisement")
```

### Multiple services

```python
xbot = XbotServiceIo()
echo = ServiceInterface(service_id=1, schema='echo_service.json')
gpio = ServiceInterface(service_id=2, schema='gpio_service.json')
xbot.register(echo)
xbot.register(gpio)
xbot.start()
```

Each `ServiceInterface` connects and disconnects independently.

---

## Development

```bash
cd xbot_service_interface_py
pip install -e ".[dev]"
pytest
```
