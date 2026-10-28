package xbot

import (
	"encoding/binary"
	"net"
	"testing"
)

func TestHeaderRoundTrip(t *testing.T) {
	h := Header{
		ProtocolVersion: ProtocolVersion,
		MessageType:     MsgRPCCall,
		ServiceID:       12,
		Arg1:            2,
		Arg2:            42,
		SequenceNo:      7,
		Timestamp:       123456789,
		PayloadSize:     512,
	}
	b := h.Marshal()
	if len(b) != HeaderSize {
		t.Fatalf("header size = %d, want %d", len(b), HeaderSize)
	}
	got, err := UnmarshalHeader(b)
	if err != nil {
		t.Fatal(err)
	}
	if got != h {
		t.Fatalf("round trip mismatch:\n got %+v\nwant %+v", got, h)
	}
}

// TestWireSizes pins the packed sizes against the C++ structs and the Python
// reference (datatypes.py: header '<BBBBHBBHHQI', descriptor '<HHI', claim
// '<IHI').
func TestWireSizes(t *testing.T) {
	if HeaderSize != 24 {
		t.Fatalf("HeaderSize = %d, want 24", HeaderSize)
	}
	if DescriptorSize != 8 {
		t.Fatalf("DescriptorSize = %d, want 8", DescriptorSize)
	}
	if ClaimSize != 10 {
		t.Fatalf("ClaimSize = %d, want 10", ClaimSize)
	}
}

func TestHeaderRejectsShortPacket(t *testing.T) {
	if _, err := UnmarshalHeader(make([]byte, HeaderSize-1)); err == nil {
		t.Fatal("expected an error for a truncated header")
	}
}

func TestClaimPayloadWire(t *testing.T) {
	// Python: ip_int = struct.unpack('!I', inet_aton(ip))[0]; pack('<IHI', ...).
	ip := "172.16.78.1"
	ipInt := binary.BigEndian.Uint32(net.ParseIP(ip).To4())
	p := PackClaimPayload(ipInt, 4242, 10_000_000)
	if len(p) != ClaimSize {
		t.Fatalf("claim size = %d, want %d", len(p), ClaimSize)
	}
	if got := binary.LittleEndian.Uint32(p[0:4]); got != ipInt {
		t.Fatalf("target_ip = %#x, want %#x", got, ipInt)
	}
	if got := binary.LittleEndian.Uint16(p[4:6]); got != 4242 {
		t.Fatalf("port = %d, want 4242", got)
	}
	if got := binary.LittleEndian.Uint32(p[6:10]); got != 10_000_000 {
		t.Fatalf("heartbeat = %d, want 10000000", got)
	}
}

func TestPackDescriptor(t *testing.T) {
	p := PackDescriptor(0, 256)
	if len(p) != DescriptorSize {
		t.Fatalf("descriptor size = %d, want %d", len(p), DescriptorSize)
	}
	if got := binary.LittleEndian.Uint16(p[0:2]); got != 0 {
		t.Fatalf("target_id = %d, want 0", got)
	}
	if got := binary.LittleEndian.Uint32(p[4:8]); got != 256 {
		t.Fatalf("payload_size = %d, want 256", got)
	}
}

func TestMessageTypeString(t *testing.T) {
	for _, c := range []struct {
		mt   MessageType
		want string
	}{
		{MsgRPCCall, "RPC_CALL"},
		{MsgServiceAdvertisement, "SERVICE_ADVERTISEMENT"},
		{MsgUnknown, "UNKNOWN"},
		{MessageType(0x42), "MessageType(0x42)"},
	} {
		if got := c.mt.String(); got != c.want {
			t.Errorf("MessageType(%#x).String() = %q, want %q", uint8(c.mt), got, c.want)
		}
	}
}
