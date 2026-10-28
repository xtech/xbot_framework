// Package xbot is a Go client for the xBot Framework service protocol: it
// discovers services over the UDP multicast group, claims them, and talks to
// them with RPC calls and DATA messages.
//
// The wire format is defined by the C++ headers of this repository
// (include/xbot/datatypes/XbotHeader.hpp, include/xbot/config.hpp); the
// constants in this package are checked against them by spec_test.go.
//
// Scope: discovery, claim, heartbeat, synchronous and fire-and-forget RPC,
// sending DATA (inputs and registers as raw bytes) and heatshrink encoding of
// blob registers. A service specific client is a thin wrapper around Conn that
// knows the function and register ids from a service.json — see the README for
// the support matrix of this client compared to the C++ and Python ones.
//
// Typical use:
//
//	conn, err := xbot.Dial(ctx, xbot.Options{ServiceID: 13})
//	if err != nil {
//		return err
//	}
//	defer conn.Close()
//
//	reply, err := conn.Call(ctx, 3, xbot.String(0, "zz_ping.mp3"), xbot.Bool(1, true))
package xbot
