// Command echo talks to the EchoService example of this repository: it calls
// RpcEchoTest and prints the answer.
//
// Start the service first (see examples/services/EchoService), then:
//
//	go run ./examples/echo
//	go run ./examples/echo -addr 192.168.1.50:4242 -text hello
package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"strings"
	"time"

	"github.com/xtech/xbot_framework/xbot_service_interface_go/xbot"
)

// Ids from examples/services/EchoService/service.json.
const (
	echoServiceID = 1
	fnRpcEchoTest = 2
)

func main() {
	addr := flag.String("addr", "", "service endpoint ip:port (skips discovery)")
	text := flag.String("text", "hello", "text to echo (padded to char[10])")
	count := flag.Uint("count", 3, "how often the service shall echo it")
	wait := flag.Duration("wait", 30*time.Second, "discovery and claim budget")
	flag.Parse()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()
	ctx, cancel := context.WithTimeout(ctx, *wait)
	defer cancel()

	opts := xbot.Options{ServiceID: echoServiceID}
	if *addr != "" {
		opts.Endpoint = *addr
	}
	conn, err := xbot.Dial(ctx, opts)
	if err != nil {
		log.Fatalf("dial: %v", err)
	}
	defer func() { _ = conn.Close() }()
	fmt.Printf("connected to service %d at %s\n", conn.ServiceID(), conn.Endpoint())

	reply, err := conn.Call(ctx, fnRpcEchoTest,
		xbot.Bytes(0, fixedString(*text, 10)), // char[10]
		xbot.Uint32(1, uint32(*count)),        // uint32_t
	)
	if err != nil {
		log.Fatalf("RpcEchoTest: %v", err)
	}
	fmt.Printf("RpcEchoTest -> %q\n", fixedStringReply(reply))
}

// fixedString pads s with NUL to the size of a char[N] parameter.
func fixedString(s string, size int) []byte {
	b := make([]byte, size)
	copy(b, s)
	return b
}

// fixedStringReply turns a char[N] return value back into a string.
func fixedStringReply(b []byte) string { return strings.TrimRight(string(b), "\x00") }
