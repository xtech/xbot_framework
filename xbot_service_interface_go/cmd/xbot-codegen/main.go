// Command xbot-codegen generates a Go client for an xbot service from its
// service.json.
//
// Consumers run it from a //go:generate directive:
//
// //go:generate go run github.com/xtech/xbot_framework/xbot_service_interface_go/cmd/xbot-codegen -service ../../services/file_service.json -service-id 12 -out client_generated.go
//
// The generated file is checked in, so `go build` never needs this tool; the
// schema and the generated client stay in sync through CI
// (`go generate ./... && git diff --exit-code`).
package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/xtech/xbot_framework/xbot_service_interface_go/codegen"
)

func main() {
	service := flag.String("service", "", "path to the service.json (required)")
	out := flag.String("out", "", "output file (default: stdout)")
	serviceID := flag.Uint("service-id", 0, "service id from service_ids.h (0: Dial takes the id as an argument)")
	pkg := flag.String("package", "", "package name (default: derived from the service type)")
	flag.Parse()

	if *service == "" {
		fmt.Fprintln(os.Stderr, "xbot-codegen: -service is required")
		flag.Usage()
		os.Exit(2)
	}
	if *serviceID > 0xFFFF {
		fmt.Fprintf(os.Stderr, "xbot-codegen: -service-id %d does not fit into uint16\n", *serviceID)
		os.Exit(2)
	}

	schema, err := codegen.Load(*service)
	if err != nil {
		fmt.Fprintf(os.Stderr, "xbot-codegen: %v\n", err)
		os.Exit(1)
	}
	src, err := codegen.Generate(schema, codegen.Options{ServiceID: uint16(*serviceID), Package: *pkg})
	if err != nil {
		fmt.Fprintf(os.Stderr, "xbot-codegen: %v\n", err)
		os.Exit(1)
	}

	if *out == "" {
		if _, err := os.Stdout.Write(src); err != nil {
			fmt.Fprintf(os.Stderr, "xbot-codegen: %v\n", err)
			os.Exit(1)
		}
		return
	}
	if err := os.WriteFile(*out, src, 0o644); err != nil {
		fmt.Fprintf(os.Stderr, "xbot-codegen: %v\n", err)
		os.Exit(1)
	}
}
