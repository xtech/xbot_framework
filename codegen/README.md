# xbot_comms Codegen
This folder include the code generator for the service template.

In order to use it, declare the service in your CMakeLists.txt and inherit from the genrerated base class.

The code generator will generate:
- Callbacks for input channels
- Send methods for output channels
- All IO handling and threading including service discovery


Look at the EchoService for an example.

## Go client generator

`xbot_service_interface_go/cmd/xbot-codegen` generates a Go client from the same `service.json` :

```bash
go run github.com/xtech/xbot_framework/xbot_service_interface_go/cmd/xbot-codegen \
    -service services/file_service.json -service-id 12 -out client_generated.go
```

The generated package combines the generic transport of the Go module (`xbot.Conn`: discovery,
claim, RPC, DATA and configuration transactions) with the ids and typed calls of one service: the
service id, the function ids, the enums, one method per RPC function, a setter per register and a
sender per input. `-service-id` is optional - without it, `Dial` takes the id as an argument, which
is how the C++ interfaces get it.

The output is `gofmt`ed and checked in by the consumer; `go generate ./... && git diff --exit-code`
in CI keeps it in sync with the schema. See `xbot_service_interface_go/codegen` for the
implementation and the golden tests.

Not supported yet: outputs (DATA received from the service) need a receive loop in the Go client,
and arrays other than `char[n]`/`uint8_t[n]` are rejected while generating (loudly) instead of
producing broken code.
