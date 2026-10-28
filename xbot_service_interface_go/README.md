# xbot-service-interface-go

Go client library for [xBot Framework](https://github.com/xtech/xbot_framework) services. Connects to xBot services
over UDP without code generation, for tools, bridges and tests (no cgo).

## Installation

```bash
go get github.com/xtech/xbot_framework/xbot_service_interface_go
```

Pin a release with its tag (`xbot_service_interface_go/v0.1.0`), or use the latest commit with `@main`. Requires
Go 1.23+. The only dependency is `github.com/fxamacker/cbor/v2`, which decodes the service advertisements.

## Architecture overview

```text
xbot.Conn        — one service: discovery (or a fixed endpoint), claim, RPC calls, DATA messages
    ├── Call()      — synchronous RPC, returns the value or an *RPCError
    ├── SendRPC()   — fire-and-forget RPC (needed when someone else claimed the service)
    └── SendData()  — raw DATA frames: inputs and registers
xbot.Discover()  — list the services that advertise on the discovery group
```

A service specific client is a thin wrapper around `Conn` that knows the ids from a `service.json` (function ids,
input/register ids, parameter layout). The [OpenMower](https://github.com/ClemensElflein/OpenMower) `soundctl` is
such a consumer.

## Quick start

```go
ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
defer cancel()

// 1. What is on the bus?
services, err := xbot.Discover(ctx, "") // "" = all interfaces
for _, s := range services {
	fmt.Printf("%d\t%s\tv%d\t%s\n", s.ServiceID, s.Type, s.Version, s.Endpoint())
}

// 2. Connect to one of them (claims it unless Options.NoClaim is set).
conn, err := xbot.Dial(ctx, xbot.Options{ServiceID: 1})
if err != nil {
	log.Fatal(err)
}
defer conn.Close()

// 3. RPC. The ids come from the service.json: function 2, parameters 0 and 1.
reply, err := conn.Call(ctx, 2, xbot.Bytes(0, []byte("hello")), xbot.Uint32(1, 3))
switch {
case errors.Is(err, xbot.ErrBusy):
	fmt.Println("service is busy, retry later")
case err != nil:
	log.Fatal(err)
}
fmt.Printf("%q\n", reply)
```

Pass a context with a deadline, or set `Options.CallTimeout` — otherwise `DefaultCallTimeout` (2 s) applies.

## API reference

### `xbot.Dial(ctx, xbot.Options{...}) (*xbot.Conn, error)`

| field         | meaning                                                                         |
| ------------- | ------------------------------------------------------------------------------- |
| `ServiceID`   | the id to connect to (required), see `service_ids.h`                            |
| `BindIP`      | IPv4 address of the interface to use; `""`/`0.0.0.0` = all                      |
| `Heartbeat`   | heartbeat interval; `0` = `xbot.DefaultHeartbeat` (1 s)                         |
| `NoClaim`     | connect without claiming: the owner keeps the responses, this client only sends |
| `Endpoint`    | `"ip:port"` — skip discovery (implies `NoClaim`)                                |
| `CallTimeout` | default timeout for `Call` without a context deadline; `0` = 2 s                |
| `ExpectType`, `ExpectVersion` | validate the discovered service against the schema the client was generated from |

Discovery needs a window of a few advertisement intervals: a service advertises every second only while it is
unclaimed and younger than 60 s, afterwards (and once claimed) every 10 s.

### `(*xbot.Conn)`

| method                                     | purpose                                                               |
| ------------------------------------------ | --------------------------------------------------------------------- |
| `Call(ctx, fn, params...) ([]byte, error)` | synchronous RPC: the return value, or `*RPCError`/context error       |
| `SendRPC(fn, params...) error`             | fire-and-forget RPC                                                   |
| `SendData(targetID, data) error`           | raw DATA frame (input or register)                                    |
| `SendTransaction(config, frames...) error` | several DATA frames in one packet; `config` marks a reconfiguration   |
| `Endpoint() string`, `ServiceID() uint16`  | where the connection points to                                        |
| `Close() error`                            | closes the socket; the service notices through the missing heartbeats |

`Call` is safe for concurrent use — calls are serialised because the responses share the socket.

### Parameters

`xbot.Param{ID, Data}` is one parameter: the descriptor id from the `service.json` plus its bytes. Helpers:
`xbot.Bytes`, `xbot.String`, `xbot.Bool`, `xbot.Uint8`, `xbot.Uint16`, `xbot.Uint32` (little endian). Fixed size
`char[N]` parameters may be sent with or without trailing NUL bytes — the service reads the length from the
descriptor.

### Errors

| error                   | meaning                                                            |
| ----------------------- | ------------------------------------------------------------------ |
| `xbot.ErrBusy`          | the service is still executing another call (`errors.Is`)          |
| `xbot.ErrRPC`           | the call failed inside the service                                 |
| `xbot.ErrNotAdvertised` | the service id was not advertised; the message lists what was seen |
| `*xbot.RPCError`        | carries `FunctionID` and `Status`; matches the two above via `Is`  |
| `xbot.ErrIncompatibleService` | the service announced a different type/version than the client expects (`*xbot.MismatchError`) |

### Discovery and helpers

- `xbot.Discover(ctx, bindIP)` → `[]xbot.Advertisement` (id, type, version, endpoint), sorted by id.
- `xbot.HeatshrinkEncode(data)` → the heatshrink stream the firmware decodes (window 9, lookahead 5), e.g. for a
  compressed blob register.
- `xbot.PrimaryIP()` → the first physical non-loopback IPv4 address (the claim target when binding to 0.0.0.0).

## Service clients (codegen)

`cmd/xbot-codegen` generates a typed client for **one** service from its `service.json`:

```bash
go run github.com/xtech/xbot_framework/xbot_service_interface_go/cmd/xbot-codegen \
    -service ../../services/file_service.json -service-id 12 -out client_generated.go
```

The generated file composes this package with the service definition - exactly the split the C++
side uses: `xbot.Conn` brings discovery, claim, RPC and DATA/configuration transactions, the
generated code adds the service id, the function ids, the enums, one method per RPC function, a
setter per register and a sender per input. `-service-id` is optional: without it `Dial` takes the
id as an argument.

The generated `Dial` sets `Options.ExpectType`/`ExpectVersion` from the schema, so a client generated
from an older `service.json` refuses a service that announces a newer version instead of sending
calls with the wrong parameter layout.

The output is `gofmt`ed and checked in (Go convention); a `go:generate` directive plus
`go generate ./... && git diff --exit-code` in CI keeps it in sync with the schema. Generator and
golden tests live in `codegen/`.

## Scope

The client implements what a tool like `soundctl` needs: discovery, claim, heartbeat, RPC (synchronous and
fire-and-forget), DATA and configuration transactions, and heatshrink encoding. It does **not** implement typed
registers and outputs, schema validation, the remote log stream or reconnect handling — the
[support matrix](../Readme.md#client-support-matrix) in the main README compares it with the C++ and Python
clients.

## Wire compatibility

The wire format is defined by the C++ headers of this repository, and two test files keep the two sides together:

- `xbot/spec_test.go` parses `include/xbot/config.hpp` and `include/xbot/datatypes/XbotHeader.hpp` and fails when a
  constant or a message type differs from this package. It skips when the headers are not next to the module (i.e.
  when the module is used from the module cache).
- `xbot/header_test.go`, `xbot/conn_test.go` and `xbot/discovery_test.go` pin the packed sizes, the claim/DATA/RPC
  frame layouts (against a fake service on the loopback interface) and the advertisement decoding.

## Examples

```bash
go run ./examples/discover                # list the services on the network
go run ./examples/echo                    # RPC against examples/services/EchoService
go run ./examples/echo -addr 10.0.0.5:4242 -text hello -count 5
```

## Development

```bash
cd xbot_service_interface_go
gofmt -l . && go vet ./... && go test ./... -race
```

## Releasing

This module lives in a subdirectory of a repository whose root tags belong to other packages (the C++ releases and
the Python interface). Go therefore expects **prefixed tags** — the tag must match the module directory relative to
the repository root:

```bash
git tag -a xbot_service_interface_go/v0.1.0 -m "xbot-service-interface-go v0.1.0"
git push origin xbot_service_interface_go/v0.1.0
```

- A bare `v0.1.6` (or the existing `v1.0.0`) addresses a module in the repository *root*; this package is not that
  module, so those tags are ignored here. Never tag this module with an unprefixed version.
- `git tag -l 'xbot_service_interface_go/*'` lists the Go releases;
  `go list -m -versions github.com/xtech/xbot_framework/xbot_service_interface_go` shows what consumers can fetch.
- Pre-releases work (`xbot_service_interface_go/v0.2.0-rc1`). A major version ≥ 2 additionally requires the module
  path to end in `/v2` (`…/xbot_service_interface_go/v2`, tag `xbot_service_interface_go/v2.0.0`).
- Until the first tag exists, `go get …@main` or `@<commit>` works as well.
