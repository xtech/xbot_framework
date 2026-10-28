package xbot

import (
	"strings"
	"testing"

	"github.com/fxamacker/cbor/v2"
)

// packAdvertisement builds a SERVICE_ADVERTISEMENT exactly the way the framework
// serialises ServiceInfo (sid / endpoint / desc).
func packAdvertisement(t *testing.T, sid int, ip string, port int, typ string, version int) []byte {
	t.Helper()
	payload, err := cbor.Marshal(map[string]any{
		"sid":      sid,
		"endpoint": map[string]any{"ip": ip, "port": port},
		"desc":     map[string]any{"type": typ, "version": version},
	})
	if err != nil {
		t.Fatal(err)
	}
	hdr := Header{
		ProtocolVersion: ProtocolVersion,
		MessageType:     MsgServiceAdvertisement,
		ServiceID:       uint16(sid),
		PayloadSize:     uint32(len(payload)),
	}
	return append(hdr.Marshal(), payload...)
}

func TestDecodeAdvertisement(t *testing.T) {
	adv, ok := decodeAdvertisement(packAdvertisement(t, 1, "172.16.78.150", 49153, "EchoService", 3))
	if !ok {
		t.Fatal("advertisement rejected")
	}
	if adv.ServiceID != 1 || adv.Type != "EchoService" || adv.Version != 3 {
		t.Errorf("decoded %+v, want SID 1 / EchoService / v3", adv)
	}
	if got, want := adv.Endpoint(), "172.16.78.150:49153"; got != want {
		t.Errorf("Endpoint() = %q, want %q", got, want)
	}
	if got := adv.String(); !strings.HasPrefix(got, "1   EchoService") {
		t.Errorf("String() = %q, want it to start with the id and type", got)
	}
	if !strings.Contains(adv.String(), "172.16.78.150:49153") {
		t.Errorf("String() = %q, want it to contain the endpoint", adv.String())
	}
}

// TestDecodeAdvertisementRejects pins that packets the framework would ignore
// are ignored here as well, instead of producing a bogus service entry.
func TestDecodeAdvertisementRejects(t *testing.T) {
	valid := packAdvertisement(t, 1, "10.0.0.2", 4242, "EchoService", 1)

	cases := map[string][]byte{
		"empty":            nil,
		"header only":      valid[:HeaderSize],
		"truncated header": valid[:HeaderSize-1],
	}
	// Wrong message type.
	wrongType := append([]byte(nil), valid...)
	wrongType[1] = byte(MsgRPCCall)
	cases["wrong message type"] = wrongType
	// Payload size that does not match the packet.
	shortPayload := append([]byte(nil), valid...)
	shortPayload[20] = byte(int(shortPayload[20]) + 1)
	cases["payload size mismatch"] = shortPayload
	// Undecodable CBOR.
	badCBOR := append(append([]byte(nil), valid[:HeaderSize]...), 0xFF, 0xFF, 0xFF)
	cases["broken cbor"] = badCBOR
	// Valid CBOR but no endpoint.
	noEndpoint, err := cbor.Marshal(map[string]any{"sid": 3, "desc": map[string]any{"type": "X", "version": 1}})
	if err != nil {
		t.Fatal(err)
	}
	hdr := Header{ProtocolVersion: ProtocolVersion, MessageType: MsgServiceAdvertisement, PayloadSize: uint32(len(noEndpoint))}
	cases["no endpoint"] = append(hdr.Marshal(), noEndpoint...)

	for name, data := range cases {
		if adv, ok := decodeAdvertisement(data); ok {
			t.Errorf("%s: decoded %+v, want rejection", name, adv)
		}
	}
}

func TestSummarizeSIDs(t *testing.T) {
	for _, c := range []struct {
		name string
		seen []Advertisement
		want string
	}{
		{"nothing", nil, "none"},
		{"one", []Advertisement{{ServiceID: 7}}, "7"},
		{"sorted and deduplicated", []Advertisement{
			{ServiceID: 13}, {ServiceID: 7}, {ServiceID: 13},
		}, "7,13"},
	} {
		if got := summarizeSIDs(c.seen); got != c.want {
			t.Errorf("%s: summarizeSIDs() = %q, want %q", c.name, got, c.want)
		}
	}
}
