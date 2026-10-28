# xBot Framework

## A Lightweight Alternative to ROS at the Hardware Boundary

ROS is excellent middleware for high-level robotics components, but it's the wrong tool at the hardware boundary.
Solutions like microROS pull a full ROS dependency onto the microcontroller, which bloats firmware, complicates
debugging, and tightly couples low-level drivers to application-specific code.

**xBot Framework** solves this with a small, independent protocol for talking to sensors and actuators — no ROS
dependency required on the device. It's the hardware layer powering **[OpenMower](https://github.com/ClemensElflein/OpenMower)**,
running in production on real mowers today.

### Features

- **Service-Based Architecture:** Low-level features (sensors, actuators) are implemented as services, described by a
  JSON interface and implemented as C++ classes. Services run identically on a microcontroller or on Linux — the
  same code, the same protocol, no porting layer.
- **Hardware Communication Simplified:** Define a service in JSON and the code generator produces the C++ boilerplate
  for talking to ESCs, IMUs, GPIOs, and other hardware.
- **Lightweight and Portable:** Minimal dependencies, zero dynamic memory allocation. Built for microcontrollers,
  runs just as well on Linux.
- **Service Discovery:** Services advertise themselves automatically over UDP multicast — no manual wiring to connect
  low-level services to high-level application code.
- **Runtime:**
    - **REST API:** Discover services programmatically. Stable. Port 18080, optional — pass `start_rest_api=true` to `Start()`.
    - **Firmware Update:** Flash firmware directly through the built-in Ethernet bootloader. Stable, used in production.
    - **Web UI:** Visually explore devices, monitor data, and test actuators. Planned.
- **Performant Serialization:** Data is transmitted schemaless and binary-packed for low traffic and fast
  serialization.

## Repository Structure

This repository contains all parts of the xbot_framework.

### /libxbot-service

Use this library to provide a service to the system. An example would be publishing IMU data or providing motor control
services. The library will take care of advertising your service and connecting to the runtime.

### /libxbot-service-interface

Use **libxbot-service-interface** to connect to a specific service. For example if there is an IMU service on your
network, and you want to receive its data (or bridge to ROS), include **libxbot-service-interface** in your project to
use your services.

### /include

Files in this directory are needed on both sides (service and service interface). E.g. message header definitions.

### /codegen

This folder contains the code generation part of the **xbot_framework**. Code generation is done from `service.json`
files which describe inputs, outputs, registers and enums of services.

The code generator will generate callbacks for inputs and sending methods for the outputs. It will also generate all
code necessary for service discovery and configuration.

### /ext

All dependencies are included here as submodules. Not every dependency is needed by every part of the software.

### /xbot_service_interface_py

Python service interface — connect to xBot services from Python without any code generation. See
[Python Interface](#python-interface) below for a summary, or the
[package README](xbot_service_interface_py/README.md) for full details.

### /xbot_service_interface_go

Go service interface — discovery, claim, RPC calls and DATA messages from Go, without any code generation. See
[Go Interface](#go-interface) below for a summary, or the
[package README](xbot_service_interface_go/README.md) for full details.

---

## Wire Protocol

All communication uses UDP. Every packet begins with a fixed 24-byte header (`XbotHeader`), followed by a
message-specific payload.

### XbotHeader (24 bytes, packed)

| Offset | Size | Field            | Notes                                                   |
|--------|------|------------------|---------------------------------------------------------|
| 0      | 1    | protocol_version | Currently `1`                                           |
| 1      | 1    | message_type     | See message type table below                            |
| 2      | 1    | flags            | Bit 0: reboot flag (set on boot, cleared on seq rollover) |
| 3      | 1    | reserved1        |                                                         |
| 4      | 2    | service_id       | Identifies the service                                  |
| 6      | 1    | arg1             | Message-specific (see table below)                      |
| 7      | 1    | reserved2        |                                                         |
| 8      | 2    | arg2             | Message-specific (see table below)                      |
| 10     | 2    | sequence_no      | Increments per message; rollover clears reboot flag     |
| 12     | 8    | timestamp        | Unix timestamp in **microseconds**                      |
| 20     | 4    | payload_size     | Byte length of payload following the header             |

### Message Types

| Value  | Name                    | arg1                        | arg2        | Payload                                |
|--------|-------------------------|-----------------------------|-------------|----------------------------------------|
| `0x00` | UNKNOWN                 | —                           | —           | —                                      |
| `0x01` | DATA                    | —                           | `target_id` | Raw value bytes                        |
| `0x02` | CONFIGURATION_REQUEST   | —                           | —           | Empty                                  |
| `0x03` | CLAIM                   | `0`=request, `1`=ack        | —           | `ClaimPayload` (request) or empty (ack) |
| `0x04` | HEARTBEAT               | —                           | —           | Empty                                  |
| `0x05` | TRANSACTION             | `0`=data, `1`=configuration | —           | Sequence of `DataDescriptor`+payload   |
| `0x7F` | LOG                     | log level (1–7)             | —           | UTF-8 string (max 255 bytes)           |
| `0x80` | SERVICE_ADVERTISEMENT   | —                           | —           | CBOR-encoded service JSON              |
| `0x81` | SERVICE_QUERY           | —                           | —           | —                                      |

### TRANSACTION payload format

A TRANSACTION payload is a sequence of chunks, each preceded by a `DataDescriptor`:

**DataDescriptor (8 bytes, packed):**

| Offset | Size | Field        |
|--------|------|--------------|
| 0      | 2    | target_id    |
| 2      | 2    | reserved     |
| 4      | 4    | payload_size |

Chunks are read sequentially until `payload_size` bytes of the transaction are consumed.

### ClaimPayload (10 bytes, packed)

Sent by the interface as the payload of a `CLAIM` message:

| Offset | Size | Field             | Notes                          |
|--------|------|-------------------|--------------------------------|
| 0      | 4    | target_ip         | Interface IP (network byte order) |
| 4      | 2    | target_port       | Interface UDP port             |
| 6      | 4    | heartbeat_micros  | Requested heartbeat interval   |

---

## Service Discovery

Service discovery uses **UDP multicast**.

| Parameter         | Value           |
|-------------------|-----------------|
| Multicast address | `233.255.255.0` |
| Port              | `4242`          |
| Message type      | `SERVICE_ADVERTISEMENT` (0x80) |

**Advertisement payload:** CBOR-encoded JSON with the following structure:

```json
{
  "sid": 1,
  "endpoint": { "ip": "192.168.1.10", "port": 12345 },
  "desc": {
    "type": "EchoService",
    "version": 1,
    "inputs":  [{ "id": 0, "name": "InputText",  "type": "char[100]" }],
    "outputs": [{ "id": 0, "name": "Echo",        "type": "char[100]" }]
  }
}
```

**Advertisement rate:**
- Fast: every **1 second** while unclaimed
- Slow: every **10 seconds** after being claimed

**Discovery flow:**
1. Service broadcasts `SERVICE_ADVERTISEMENT` on `233.255.255.0:4242`.
2. Interface listens on that multicast group and fires `OnServiceDiscovered`.
3. Interface sends unicast `CLAIM` (with `ClaimPayload`) to the service's reported endpoint.
4. Service stops, loads defaults, sends `CLAIM` ack (`arg1=1`, empty payload).
5. If the service has registers, it sends `CONFIGURATION_REQUEST` every second until it receives configuration.
6. Interface responds with a `TRANSACTION` (`arg1=1`) containing register values as `DataDescriptor`-framed chunks.
7. Service validates all required registers and calls `OnStart()`.
8. Service sends `HEARTBEAT` at half the requested heartbeat interval. Interface drops the service after `heartbeat_micros + 100ms` without a heartbeat.

---

## Remote Logging

Remote logging uses **UDP multicast**.

| Parameter         | Value             |
|-------------------|-------------------|
| Multicast address | `233.255.255.1`   |
| Port              | `4242`            |
| Message type      | `LOG` (0x7F)      |

**Payload:** UTF-8 string, max 255 bytes. When a `service_id` context is available the message is prefixed as `[ID=X] message`.

**Log levels (arg1):**

| arg1 | Level    |
|------|----------|
| 1    | TRACE    |
| 2    | DEBUG    |
| 3    | INFO     |
| 4    | WARNING  |
| 5    | ERROR    |
| 6    | CRITICAL |
| 7    | ALWAYS   |

Enable on the service side by calling `xbot::service::startRemoteLogging(level)`. The interface-side `RemoteLoggingReceiverImpl` joins the multicast group automatically when `Start()` is called.

---

## Service Definition (service.json)

Services are defined in a JSON file. The code generator (`codegen/`) reads this file and generates a `{ServiceName}Base` C++ class.

```json
{
  "type": "EchoService",
  "version": 1,
  "inputs": [
    { "id": 0, "name": "InputText", "type": "char[100]" }
  ],
  "outputs": [
    { "id": 0, "name": "Echo",         "type": "char[100]" },
    { "id": 1, "name": "MessageCount", "type": "uint32_t"  }
  ],
  "registers": [
    { "id": 0, "name": "Prefix",    "type": "char[42]",  "default": "hello", "default_length": 5 },
    { "id": 1, "name": "EchoCount", "type": "uint32_t",  "default": 0 },
    { "id": 2, "name": "BlobData",  "type": "blob" },
    { "id": 3, "name": "Optional",  "type": "uint32_t",  "optional": true }
  ],
  "enums": [
    {
      "id": "MyEnum", "base_type": "uint8_t",
      "values": { "A": 0, "B": 1 }
    },
    {
      "id": "MyFlags", "base_type": "uint8_t", "bitmask": true,
      "values": { "FLAG_A": 0, "FLAG_B": 1 }
    }
  ]
}
```

**Valid types:** `char`, `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, `int8_t`, `int16_t`, `int32_t`, `int64_t`, `float`, `double`, `blob` (registers only), or fixed-length arrays of any scalar type as `type[N]`.

**IDs** must be unique within each section (`inputs`, `outputs`, `registers`).

**Generated API (service side):**
- `void On{Name}Changed(const T* value, uint32_t length)` — called when an input arrives (array types)
- `void On{Name}Changed(const T& value)` — called when an input arrives (scalar types)
- `bool Send{Name}(const T* data, uint32_t length)` — send an output (array types)
- `bool Send{Name}(const T& data)` — send an output (scalar types)
- `bool OnRegister{Name}Changed(const void* data, size_t length)` — called when a register is set (blob registers)
- Struct `{Name}` with `value`, `length` (arrays), and `valid` fields — for non-blob registers

**Generated API (interface side — same JSON, different template):**
- Inputs and outputs swap roles: outputs become callbacks, inputs become send methods.

**CMake integration:**
```cmake
# Service side
include(${XBOT_CODEGEN_PATH}/cmake/AddService.cmake)
target_add_service(MyTarget MyServiceName path/to/service.json)

# Interface side
include(${XBOT_CODEGEN_PATH}/cmake/AddServiceInterface.cmake)
target_add_service_interface(MyTarget MyInterfaceName path/to/service.json)
```

Then inherit from the generated `MyServiceNameBase` or `MyInterfaceNameBase` class.

---

## Python Interface

`xbot_service_interface_py` is a pure-Python client for xBot services — no codegen step required. It talks directly
to the wire protocol described above, so it's a good fit for scripting, quick prototyping, ROS bridges, or driving
services from Jupyter/IPython.

Two ways to use a service:

- **Schema provided:** pass a `service.json` file (or dict) to `ServiceInterface`. The schema is validated against
  the service's advertisement on connect, and an `IncompatibleServiceError` aborts the connection on a mismatch.
- **Schema-free:** omit the schema entirely. The full service description is embedded in the advertisement itself, so
  inputs, outputs, registers, and RPC functions are all available immediately after discovery.

```python
from xbot_service_interface import XbotServiceIo, ServiceInterface

xbot = XbotServiceIo(bind_ip='0.0.0.0')
echo = ServiceInterface(service_id=1, schema='echo_service.json')
xbot.register(echo)

@echo.on_connected
def connected():
    echo.registers['Prefix'] = "py: "

@echo.on_echo_changed
def got_echo(value: str, timestamp: int):
    print(f"echo: {value!r}")

xbot.start()
```

Beyond the core send/receive/register API, the package includes:

- **RPC calls** — synchronous `call_{name}(*args, timeout_ms=...)` with typed exceptions (`RpcTimeoutError`,
  `RpcBusyError`, `RpcError`).
- **Atomic transactions** — bundle multiple sends into one UDP packet via `with iface.transaction(): ...`.
- **`xbot-shell`** — an IPython-based interactive shell (`pip install "xbot-service-interface-py[shell]"`) with
  tab-completable service proxies, a `services()` discovery table, live output streaming (`svc.watch_all()`), and an
  interactive register configuration wizard.
- **`xbot-logs`** — a CLI remote log viewer for the `LOG` multicast stream, filterable by level.

Install from a tagged release:

```bash
pip install "xbot-service-interface-py @ git+https://github.com/xtech/xbot_framework.git@v1.0.0#subdirectory=xbot_service_interface_py"
```

Requires Python 3.10+. Full API reference, type mapping table, and shell walkthrough in the
[package README](xbot_service_interface_py/README.md).

---

## Go Interface

`xbot_service_interface_go` is a Go client for the same protocol — no code generation, no cgo, one dependency
(`fxamacker/cbor` for the discovery payloads). It covers what a small tool or bridge needs: discovery, claim,
RPC calls and DATA messages.

```bash
# in your module
go get github.com/xtech/xbot_framework/xbot_service_interface_go@v0.1.0
```

```go
import "github.com/xtech/xbot_framework/xbot_service_interface_go/xbot"

// Discover (a service that is claimed advertises every 10 s, so allow a window)
ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
defer cancel()
services, err := xbot.Discover(ctx, "")

// Or connect straight away; RPC against the EchoService example
conn, err := xbot.Dial(ctx, xbot.Options{ServiceID: 1})
defer conn.Close()
reply, err := conn.Call(ctx, 2, xbot.Bytes(0, text), xbot.Uint32(1, 3))
if errors.Is(err, xbot.ErrBusy) { /* the service is still executing another call */ }
```

Services are addressed as in the C++/Python side: a `service.json` names the function, input and register ids, and
the Go client sends those ids with typed helpers (`xbot.String`, `xbot.Uint32`, `xbot.Bool`, `xbot.Bytes`) or raw
payloads. Heatshrink-compressed blob registers use `xbot.HeatshrinkEncode`.

Install a specific version by tag (`xbot_service_interface_go/v0.1.0`) or the latest commit with `@main`. Full API
reference and examples in the [package README](xbot_service_interface_go/README.md).

---

## Client Support Matrix

What each client implements of the wire protocol above (`o` = only with an optional dependency, `–` = not
implemented). A missing feature is not a protocol limitation: the services accept it, the client just cannot send
or receive it yet.

| Feature                                | C++ interface | Python | Go       |
| -------------------------------------- | ------------- | ------ | -------- |
| Discovery (multicast)                  | ✓             | ✓      | ✓        |
| Claim / release                        | ✓             | ✓      | ✓        |
| Heartbeat + disconnect detection       | ✓             | ✓      | ✓        |
| RPC call (synchronous, status → error) | ✓             | ✓      | ✓        |
| RPC fire-and-forget (without waiting)  | ✓             | ✓      | ✓        |
| Inputs / outputs (`DATA`)              | ✓             | ✓      | send     |
| Registers (typed)                      | via codegen   | ✓      | raw DATA |
| Transactions (atomic bundles)          | ✓             | ✓      | –        |
| Schema from `service.json` (validated) | via codegen   | ✓      | –        |
| Schema-free from the advertisement     | ✓             | ✓      | –        |
| Heatshrink encode / decode             | encode        | o      | encode   |
| Remote log stream (`LOG`)              | service side  | ✓      | –        |
| Direct endpoint, no discovery          | ✓             | ✓      | ✓        |
| Reconnect handling                     | ✓             | ✓      | –        |
| Interactive shell / CLI tools          | –             | ✓      | –        |
| Code generation needed                 | yes           | no     | no       |

---

## Status and Contributions

xBot Framework is stable and running in production as the hardware layer for **[OpenMower](https://github.com/ClemensElflein/OpenMower)**.
The wire protocol, service discovery, and codegen are solid; the Web UI is the one piece still on the roadmap.

Contributions and feedback are welcome — check the issue tracker to get started.
