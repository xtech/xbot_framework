// Command discover lists the services that advertise on this network.
//
//	go run ./examples/discover
//	go run ./examples/discover -bind 192.168.1.10 -wait 20s
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"time"

	"github.com/xtech/xbot_framework/xbot_service_interface_go/xbot"
)

func main() {
	bindIP := flag.String("bind", "", "local IPv4 address of the interface to listen on (default: all)")
	wait := flag.Duration("wait", 12*time.Second,
		"how long to listen; a service that is claimed advertises only every 10 s")
	flag.Parse()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()
	ctx, cancel := context.WithTimeout(ctx, *wait)
	defer cancel()

	services, err := xbot.Discover(ctx, *bindIP)
	if err != nil && !errors.Is(err, context.DeadlineExceeded) {
		log.Fatalf("discover: %v", err)
	}
	if len(services) == 0 {
		fmt.Printf("no service advertised within %s\n", *wait)
		return
	}
	fmt.Printf("%-3s %-30s %-3s %s\n", "SID", "TYPE", "VER", "ENDPOINT")
	for _, s := range services {
		fmt.Println(s)
	}
}
