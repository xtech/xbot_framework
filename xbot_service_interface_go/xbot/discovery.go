package xbot

import (
	"context"
	"fmt"
	"net"
	"sort"
	"strconv"
	"strings"

	"github.com/fxamacker/cbor/v2"
)

// Advertisement is one service as announced on the discovery multicast group.
type Advertisement struct {
	// ServiceID is the id the service is addressed with (services/service_ids.h).
	ServiceID uint16
	// Type is the service type, e.g. "FileService".
	Type string
	// Version is the version of the service definition the service implements.
	Version int
	// IP and Port are the unicast endpoint the service listens on.
	IP   string
	Port int
}

// Endpoint renders the unicast address as "ip:port".
func (a Advertisement) Endpoint() string { return net.JoinHostPort(a.IP, strconv.Itoa(a.Port)) }

// String renders one line of a service table.
func (a Advertisement) String() string {
	return fmt.Sprintf("%-3d %-30s %-3d %s", a.ServiceID, a.Type, a.Version, a.Endpoint())
}

// wireAdvertisement is the CBOR payload of a SERVICE_ADVERTISEMENT
// (ServiceInfo::to_json on the service side).
type wireAdvertisement struct {
	SID      int `cbor:"sid"`
	Endpoint struct {
		IP   string `cbor:"ip"`
		Port int    `cbor:"port"`
	} `cbor:"endpoint"`
	Desc struct {
		Type    string `cbor:"type"`
		Version int    `cbor:"version"`
	} `cbor:"desc"`
}

// decodeAdvertisement decodes one discovery packet. It reports false for
// anything unusable — another message type, a payload that does not match the
// header, undecodable CBOR or a missing endpoint — which is what the framework
// does as well.
func decodeAdvertisement(data []byte) (Advertisement, bool) {
	hdr, err := UnmarshalHeader(data)
	if err != nil || hdr.MessageType != MsgServiceAdvertisement {
		return Advertisement{}, false
	}
	if len(data) != HeaderSize+int(hdr.PayloadSize) {
		return Advertisement{}, false
	}
	var adv wireAdvertisement
	if err := cbor.Unmarshal(data[HeaderSize:], &adv); err != nil {
		return Advertisement{}, false
	}
	if adv.SID <= 0 || adv.SID > 0xFFFF || adv.Endpoint.IP == "" || adv.Endpoint.Port == 0 {
		return Advertisement{}, false
	}
	return Advertisement{
		ServiceID: uint16(adv.SID),
		Type:      adv.Desc.Type,
		Version:   adv.Desc.Version,
		IP:        adv.Endpoint.IP,
		Port:      adv.Endpoint.Port,
	}, true
}

// listen joins the discovery group and hands every usable advertisement to
// visit until visit returns true or ctx is done. It returns everything it saw in
// arrival order, so a timed-out caller can report what was there.
func listen(ctx context.Context, bindIP string, visit func(Advertisement) bool) ([]Advertisement, error) {
	maddr, err := net.ResolveUDPAddr("udp4", net.JoinHostPort(SDMulticastAddress, strconv.Itoa(MulticastPort)))
	if err != nil {
		return nil, err
	}
	var ifi *net.Interface
	if bindIP != "" && bindIP != "0.0.0.0" {
		ifi = interfaceByIP(bindIP)
	}
	conn, err := net.ListenMulticastUDP("udp4", ifi, maddr)
	if err != nil {
		return nil, fmt.Errorf("xbot: multicast listen on %s: %w", maddr, err)
	}
	defer conn.Close()

	// Close the socket when the context is cancelled - a blocked ReadFromUDP
	// would otherwise outlive the caller.
	done := make(chan struct{})
	defer close(done)
	go func() {
		select {
		case <-ctx.Done():
			_ = conn.Close()
		case <-done:
		}
	}()

	var seen []Advertisement
	buf := make([]byte, MaxPacketSize)
	for {
		n, _, err := conn.ReadFromUDP(buf)
		if err != nil {
			if ctx.Err() != nil {
				return seen, ctx.Err()
			}
			if ne, ok := err.(net.Error); ok && ne.Timeout() {
				continue
			}
			return seen, err
		}
		adv, ok := decodeAdvertisement(buf[:n])
		if !ok {
			continue
		}
		seen = append(seen, adv)
		if visit != nil && visit(adv) {
			return seen, nil
		}
	}
}

// Discover lists the services advertised on the discovery group until ctx is
// done: one entry per service id, sorted by id. A window of a few advertisement
// intervals is needed to see every service — AdvertisementIntervalFast (1 s)
// only applies while a service is unclaimed and younger than DiscoveryTimeout,
// afterwards it advertises every AdvertisementInterval (10 s).
func Discover(ctx context.Context, bindIP string) ([]Advertisement, error) {
	seen, err := listen(ctx, bindIP, nil)
	if err != nil && len(seen) == 0 {
		return nil, err
	}
	byID := make(map[uint16]Advertisement, len(seen))
	for _, adv := range seen {
		byID[adv.ServiceID] = adv
	}
	out := make([]Advertisement, 0, len(byID))
	for _, adv := range byID {
		out = append(out, adv)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ServiceID < out[j].ServiceID })
	return out, nil
}

// find waits until the service with the given id is advertised and returns it.
// On timeout the error wraps ErrNotAdvertised and lists the ids that were seen:
// that distinguishes "nothing on this interface" from "this firmware has no such
// service".
func find(ctx context.Context, bindIP string, serviceID uint16) (Advertisement, error) {
	var found *Advertisement
	seen, err := listen(ctx, bindIP, func(adv Advertisement) bool {
		if adv.ServiceID != serviceID {
			return false
		}
		found = &adv
		return true
	})
	if found != nil {
		return *found, nil
	}
	if err == nil {
		// Every way out of listen() that is not a hit carries an error; guard
		// anyway so the message below stays meaningful.
		err = context.DeadlineExceeded
	}
	return Advertisement{}, fmt.Errorf("%w: service %d (advertised: %s): %w",
		ErrNotAdvertised, serviceID, summarizeSIDs(seen), err)
}

// summarizeSIDs lists the distinct service ids of the seen advertisements for
// error messages ("none" when the bus was silent).
func summarizeSIDs(seen []Advertisement) string {
	ids := make(map[uint16]bool, len(seen))
	for _, adv := range seen {
		ids[adv.ServiceID] = true
	}
	if len(ids) == 0 {
		return "none"
	}
	sorted := make([]int, 0, len(ids))
	for id := range ids {
		sorted = append(sorted, int(id))
	}
	sort.Ints(sorted)
	parts := make([]string, len(sorted))
	for i, id := range sorted {
		parts[i] = strconv.Itoa(id)
	}
	return strings.Join(parts, ",")
}

// interfaceByIP returns the interface that owns the given IPv4 address, or nil
// (= all interfaces) when the address is not local.
func interfaceByIP(ipStr string) *net.Interface {
	ip := net.ParseIP(ipStr)
	if ip == nil {
		return nil
	}
	ifaces, err := net.Interfaces()
	if err != nil {
		return nil
	}
	for i := range ifaces {
		addrs, err := ifaces[i].Addrs()
		if err != nil {
			continue
		}
		for _, a := range addrs {
			if ipn, ok := a.(*net.IPNet); ok && ipn.IP.Equal(ip) {
				return &ifaces[i]
			}
		}
	}
	return nil
}

// PrimaryIP returns the first non-loopback IPv4 address of a physical
// interface, skipping docker/veth/tunnel devices. A service needs it as the
// target for heartbeats and RPC responses when the client binds to 0.0.0.0.
func PrimaryIP() string {
	ifaces, err := net.Interfaces()
	if err != nil {
		return "127.0.0.1"
	}
	skip := []string{"docker", "veth", "virbr", "br-", "wg", "tun", "tap", "lo"}
	for i := range ifaces {
		name := ifaces[i].Name
		skipped := false
		for _, p := range skip {
			if strings.HasPrefix(name, p) {
				skipped = true
				break
			}
		}
		if skipped {
			continue
		}
		addrs, err := ifaces[i].Addrs()
		if err != nil {
			continue
		}
		for _, a := range addrs {
			if ipn, ok := a.(*net.IPNet); ok {
				if ip4 := ipn.IP.To4(); ip4 != nil && !ip4.IsLoopback() {
					return ip4.String()
				}
			}
		}
	}
	return "127.0.0.1"
}
