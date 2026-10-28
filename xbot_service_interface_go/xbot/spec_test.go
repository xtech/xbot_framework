package xbot

import (
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"
	"time"
)

// This file keeps the Go constants in sync with the C++ side. It parses the
// headers of this repository, so it only runs in a checkout: inside the module
// cache (go get) the files are not part of the module and the tests skip.

// readSpec returns the content of a header of the C++ side.
func readSpec(t *testing.T, rel string) string {
	t.Helper()
	path := filepath.Join("..", "..", rel)
	b, err := os.ReadFile(path)
	if err != nil {
		t.Skipf("%s not available (%v) - only checked in the repository", path, err)
	}
	return string(b)
}

// constValue pulls "<name> = <value>;" out of a C++ header and strips the digit
// separators C++ allows (10'000'000).
var constValue = regexp.MustCompile(`(?m)^[^/\n]*\b(\w+)\s*=\s*("[^"]*"|[0-9']+)\s*;`)

func cppConstants(t *testing.T, src string) map[string]string {
	t.Helper()
	out := make(map[string]string)
	for _, m := range constValue.FindAllStringSubmatch(src, -1) {
		out[m[1]] = strings.ReplaceAll(m[2], "'", "")
	}
	if len(out) == 0 {
		t.Fatal("no constants parsed - the header format changed")
	}
	return out
}

// TestConstantsMatchConfigHpp is the drift test between include/xbot/config.hpp
// and this package.
func TestConstantsMatchConfigHpp(t *testing.T) {
	c := cppConstants(t, readSpec(t, "include/xbot/config.hpp"))

	num := func(name string) int64 {
		t.Helper()
		raw, ok := c[name]
		if !ok {
			t.Fatalf("%s not found in config.hpp", name)
		}
		v, err := strconv.ParseInt(raw, 10, 64)
		if err != nil {
			t.Fatalf("%s = %q is not a number", name, raw)
		}
		return v
	}
	str := func(name string) string {
		t.Helper()
		raw, ok := c[name]
		if !ok {
			t.Fatalf("%s not found in config.hpp", name)
		}
		return strings.Trim(raw, `"`)
	}
	micros := func(name string) time.Duration {
		t.Helper()
		return time.Duration(num(name)) * time.Microsecond
	}

	if got := int(num("max_packet_size")); got != MaxPacketSize {
		t.Errorf("MaxPacketSize = %d, config.hpp says %d", MaxPacketSize, got)
	}
	if got := int(num("multicast_port")); got != MulticastPort {
		t.Errorf("MulticastPort = %d, config.hpp says %d", MulticastPort, got)
	}
	if got := str("sd_multicast_address"); got != SDMulticastAddress {
		t.Errorf("SDMulticastAddress = %q, config.hpp says %q", SDMulticastAddress, got)
	}
	if got := str("remote_log_multicast_address"); got != LogMulticastAddress {
		t.Errorf("LogMulticastAddress = %q, config.hpp says %q", LogMulticastAddress, got)
	}
	if got := micros("sd_advertisement_interval_micros"); got != AdvertisementInterval {
		t.Errorf("AdvertisementInterval = %s, config.hpp says %s", AdvertisementInterval, got)
	}
	if got := micros("sd_advertisement_interval_micros_fast"); got != AdvertisementIntervalFast {
		t.Errorf("AdvertisementIntervalFast = %s, config.hpp says %s", AdvertisementIntervalFast, got)
	}
	if got := micros("sd_discovery_timeout_micros"); got != DiscoveryTimeout {
		t.Errorf("DiscoveryTimeout = %s, config.hpp says %s", DiscoveryTimeout, got)
	}
	if got := micros("default_heartbeat_micros"); got != DefaultHeartbeat {
		t.Errorf("DefaultHeartbeat = %s, config.hpp says %s", DefaultHeartbeat, got)
	}
	if got := micros("heartbeat_jitter"); got != HeartbeatJitter {
		t.Errorf("HeartbeatJitter = %s, config.hpp says %s", HeartbeatJitter, got)
	}
}

// TestMessageTypesMatchHeaderHpp pins the MessageType enum against
// include/xbot/datatypes/XbotHeader.hpp.
func TestMessageTypesMatchHeaderHpp(t *testing.T) {
	src := readSpec(t, "include/xbot/datatypes/XbotHeader.hpp")

	enumBody := regexp.MustCompile(`(?s)enum class MessageType[^{]*\{(.*?)\}`).FindStringSubmatch(src)
	if enumBody == nil {
		t.Fatal("enum class MessageType not found - the header format changed")
	}
	entries := regexp.MustCompile(`(\w+)\s*=\s*(0x[0-9A-Fa-f]+)`).FindAllStringSubmatch(enumBody[1], -1)
	if len(entries) == 0 {
		t.Fatal("no enumerators parsed from XbotHeader.hpp")
	}

	ours := map[string]MessageType{
		"UNKNOWN":               MsgUnknown,
		"DATA":                  MsgData,
		"CONFIGURATION_REQUEST": MsgConfigurationRequest,
		"CLAIM":                 MsgClaim,
		"HEARTBEAT":             MsgHeartbeat,
		"TRANSACTION":           MsgTransaction,
		"RPC_CALL":              MsgRPCCall,
		"RPC_RESPONSE":          MsgRPCResponse,
		"LOG":                   MsgLog,
		"SERVICE_ADVERTISEMENT": MsgServiceAdvertisement,
		"SERVICE_QUERY":         MsgServiceQuery,
	}
	seen := make(map[string]bool, len(entries))
	for _, e := range entries {
		name, raw := e[1], e[2]
		want, ok := ours[name]
		if !ok {
			t.Errorf("MessageType %s exists in the header but not in Go", name)
			continue
		}
		seen[name] = true
		v, err := strconv.ParseUint(strings.TrimPrefix(raw, "0x"), 16, 8)
		if err != nil {
			t.Fatalf("%s = %q is not a hex byte", name, raw)
		}
		if uint8(want) != uint8(v) {
			t.Errorf("%s = %#x in Go, %#x in the header", name, uint8(want), v)
		}
	}
	for name := range ours {
		if !seen[name] {
			t.Errorf("MessageType %s is missing in XbotHeader.hpp", name)
		}
	}
}
