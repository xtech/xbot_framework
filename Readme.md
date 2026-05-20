# xBot Framework

## Bridging the Gap in Robotics Development

Building robust and reusable robotics systems can be challenging. The Robot Operating System (ROS) provides powerful
middleware for abstracting communication between high-level components, but struggles at the hardware level, requiring
specialized firmware to interface with low-level devices. Existing solutions like microROS introduce a direct dependency
on ROS which complicates debugging, bloats the low-level firmware and leads to tight coupling between high-level
application specific code and low-level drivers.

The **xBot Framework** offers a lightweight, independent solution for interfacing directly with sensors and actuators
without adding heavy dependencies. This new framework helps integrating hardware components seamlessly into the broader
robotics ecosystem.

### Features

- **Service-Based Architecture:** Low level features (e.g. sensors or actuators) are implemented as a service,
  described by a JSON interface and implemented as C++ classes. This modular design allows for reusable hardware
  communication. Service implementations can be done either on a microcontroller or a Linux system, since the
  communication is abstracted from the actual operating system.
- **Hardware Communication Simplified:** Define services in JSON format to generate a C++ code template, allowing for
  easy communication with hardware components such as ESCs, IMUs, and GPIOs.
- **Lightweight and Portable:** The framework has minimal dependencies and avoids dynamic memory allocation, making it
  ideal for microcontrollers, but can also be used on Linux systems.
- **Service Discovery:** The framework includes automatic service discovery, making it easy to connect your low-level
  services to your application specific high-level code.
- **Runtime:**
    - **REST API:** Discover services programmatically. (Status: Working — port 18080, optional, pass `start_rest_api=true` to `Start()`)
    - **Web UI:** Visually explore devices, monitor data, and test actuators. (Status: Planned)
    - **Firmware Update:** Update the firmware on your board directly through the web interface using our included
      Ethernet bootloader. (Status: Working - stable)
- **Performant Serialization:** Data is transmitted schemaless and binary packed. This leads to less traffic and fast
  serialization times.

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

## Status and Contributions

The xBot Framework is currently a work in progress, and we welcome any input or feedback. If you'd like to contribute,
please read our contributing guidelines and check out our issue tracker.
