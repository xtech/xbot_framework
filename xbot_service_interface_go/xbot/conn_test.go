package xbot

import (
	"bytes"
	"context"
	"errors"
	"net"
	"testing"
	"time"
)

// startFakeService runs a minimal xbot service on the loopback interface. respond
// returns the packet to send back (a zero Header means "answer nothing"); CLAIM
// is always acknowledged.
func startFakeService(t *testing.T, respond func(hdr Header, payload []byte) (Header, []byte)) (*net.UDPConn, string) {
	t.Helper()
	udp, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatalf("fake service: %v", err)
	}
	t.Cleanup(func() { _ = udp.Close() })

	go func() {
		buf := make([]byte, MaxPacketSize)
		for {
			n, addr, err := udp.ReadFromUDP(buf)
			if err != nil {
				return
			}
			hdr, err := UnmarshalHeader(buf[:n])
			if err != nil {
				continue
			}
			payload := buf[HeaderSize:n]
			if hdr.MessageType == MsgClaim {
				ack := Header{ProtocolVersion: ProtocolVersion, MessageType: MsgClaim,
					ServiceID: hdr.ServiceID, Arg1: 1}
				_, _ = udp.WriteToUDP(ack.Marshal(), addr)
				continue
			}
			outHdr, outPayload := respond(hdr, payload)
			if outHdr.MessageType == MsgUnknown {
				continue
			}
			// Fill what a test handler does not care about.
			outHdr.ProtocolVersion = ProtocolVersion
			if outHdr.ServiceID == 0 {
				outHdr.ServiceID = hdr.ServiceID
			}
			outHdr.PayloadSize = uint32(len(outPayload))
			pkt := append(outHdr.Marshal(), outPayload...)
			_, _ = udp.WriteToUDP(pkt, addr)
		}
	}()
	return udp, udp.LocalAddr().String()
}

// respondWith answers an RPC with the given status and return value.
func respondWith(status uint8, reply []byte) func(Header, []byte) (Header, []byte) {
	return func(hdr Header, _ []byte) (Header, []byte) {
		if hdr.MessageType != MsgRPCCall {
			return Header{}, nil
		}
		return Header{ProtocolVersion: ProtocolVersion, MessageType: MsgRPCResponse,
			ServiceID: hdr.ServiceID, Arg1: status, Arg2: hdr.Arg2}, reply
	}
}

func dialFake(t *testing.T, endpoint string) *Conn {
	t.Helper()
	conn, err := Dial(context.Background(), Options{ServiceID: 1, Endpoint: endpoint, CallTimeout: time.Second})
	if err != nil {
		t.Fatalf("Dial: %v", err)
	}
	t.Cleanup(func() { _ = conn.Close() })
	return conn
}

func TestDialRequiresServiceID(t *testing.T) {
	if _, err := Dial(context.Background(), Options{}); err == nil {
		t.Fatal("expected an error without Options.ServiceID")
	}
}

func TestDialRejectsBadEndpoint(t *testing.T) {
	for _, ep := range []string{"127.0.0.1", "localhost:4242", "127.0.0.1:http"} {
		if _, err := Dial(context.Background(), Options{ServiceID: 1, Endpoint: ep}); err == nil {
			t.Errorf("Endpoint %q accepted, want rejection", ep)
		}
	}
}

func TestCallReturnsPayload(t *testing.T) {
	_, ep := startFakeService(t, respondWith(RPCStatusSuccess, []byte("pong")))
	conn := dialFake(t, ep)

	reply, err := conn.Call(context.Background(), 2, String(0, "ping"))
	if err != nil {
		t.Fatalf("Call: %v", err)
	}
	if !bytes.Equal(reply, []byte("pong")) {
		t.Fatalf("reply = %q, want %q", reply, "pong")
	}
}

// TestCallMapsStatus pins the error contract: a busy answer matches ErrBusy, an
// error answer matches ErrRPC, and both carry the function id.
func TestCallMapsStatus(t *testing.T) {
	for _, c := range []struct {
		name    string
		status  uint8
		wantErr error
	}{
		{"busy", RPCStatusBusy, ErrBusy},
		{"error", RPCStatusError, ErrRPC},
	} {
		t.Run(c.name, func(t *testing.T) {
			_, ep := startFakeService(t, respondWith(c.status, nil))
			conn := dialFake(t, ep)

			_, err := conn.Call(context.Background(), 3)
			if !errors.Is(err, c.wantErr) {
				t.Fatalf("err = %v, want errors.Is(…, %v)", err, c.wantErr)
			}
			var rpcErr *RPCError
			if !errors.As(err, &rpcErr) {
				t.Fatalf("err = %v, want an *RPCError", err)
			}
			if rpcErr.FunctionID != 3 || rpcErr.Status != c.status {
				t.Fatalf("RPCError = %+v, want function 3 / status %d", rpcErr, c.status)
			}
		})
	}
}

func TestCallHonoursContextDeadline(t *testing.T) {
	// A service that only sends heartbeats never answers the call.
	_, ep := startFakeService(t, func(hdr Header, _ []byte) (Header, []byte) {
		return Header{ProtocolVersion: ProtocolVersion, MessageType: MsgHeartbeat, ServiceID: hdr.ServiceID}, nil
	})
	conn := dialFake(t, ep)

	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	start := time.Now()
	_, err := conn.Call(ctx, 1)
	if !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("err = %v, want context.DeadlineExceeded", err)
	}
	if elapsed := time.Since(start); elapsed > 2*time.Second {
		t.Fatalf("Call blocked for %s, expected the context deadline to end it", elapsed)
	}
}

// TestSendDataFrame checks the DATA layout: arg2 carries the target id (input or
// register id) and the payload is the raw value.
func TestSendDataFrame(t *testing.T) {
	got := make(chan Header, 1)
	var gotPayload []byte
	_, ep := startFakeService(t, func(hdr Header, payload []byte) (Header, []byte) {
		if hdr.MessageType == MsgData {
			gotPayload = append([]byte(nil), payload...)
			select {
			case got <- hdr:
			default:
			}
		}
		return Header{}, nil
	})
	conn := dialFake(t, ep)

	if err := conn.SendData(5, []byte{0xDE, 0xAD}); err != nil {
		t.Fatalf("SendData: %v", err)
	}
	select {
	case hdr := <-got:
		if hdr.Arg2 != 5 {
			t.Errorf("arg2 = %d, want the target id 5", hdr.Arg2)
		}
		if hdr.ServiceID != 1 {
			t.Errorf("service id = %d, want 1", hdr.ServiceID)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("no DATA frame arrived")
	}
	if !bytes.Equal(gotPayload, []byte{0xDE, 0xAD}) {
		t.Errorf("payload = %v, want [222 173]", gotPayload)
	}
}

// TestClaimHandshake drives the CLAIM exchange directly (Dial with an Endpoint
// skips it on purpose, and real discovery needs multicast).
func TestClaimHandshake(t *testing.T) {
	_, ep := startFakeService(t, respondWith(RPCStatusSuccess, nil))
	conn := dialFake(t, ep)

	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if err := conn.claim(ctx, "127.0.0.1"); err != nil {
		t.Fatalf("claim: %v", err)
	}
}

func TestRPCPayloadLayout(t *testing.T) {
	body := buildRPCPayload([]Param{String(0, "abc"), Uint32(1, 0x01020304)})
	want := []byte{
		0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c',
		0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01,
	}
	if !bytes.Equal(body, want) {
		t.Fatalf("payload =\n% x\nwant\n% x", body, want)
	}
}

func TestParamConstructors(t *testing.T) {
	if p := Bool(1, true); p.ID != 1 || !bytes.Equal(p.Data, []byte{1}) {
		t.Errorf("Bool(true) = %+v", p)
	}
	if p := Bool(1, false); !bytes.Equal(p.Data, []byte{0}) {
		t.Errorf("Bool(false) = %+v", p)
	}
	if p := Uint16(2, 0x0102); !bytes.Equal(p.Data, []byte{0x02, 0x01}) {
		t.Errorf("Uint16 = %v, want little endian", p.Data)
	}
}

// TestSendTransactionLayout pins the configuration transaction used for register
// writes: one descriptor per frame, and arg1 = 1 marks it as a reconfiguration.
func TestSendTransactionLayout(t *testing.T) {
	got := make(chan Header, 1)
	var gotPayload []byte
	_, ep := startFakeService(t, func(hdr Header, payload []byte) (Header, []byte) {
		if hdr.MessageType == MsgTransaction {
			gotPayload = append([]byte(nil), payload...)
			select {
			case got <- hdr:
			default:
			}
		}
		return Header{}, nil
	})
	conn := dialFake(t, ep)

	err := conn.SendTransaction(true,
		Data{ID: 0, Data: []byte("ab")},
		Data{ID: 7, Data: []byte{0xFF}},
	)
	if err != nil {
		t.Fatalf("SendTransaction: %v", err)
	}
	select {
	case hdr := <-got:
		if hdr.Arg1 != 1 {
			t.Errorf("arg1 = %d, want 1 (configuration transaction)", hdr.Arg1)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("no TRANSACTION frame arrived")
	}
	want := []byte{
		0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 'a', 'b',
		0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF,
	}
	if !bytes.Equal(gotPayload, want) {
		t.Fatalf("payload =\n% x\nwant\n% x", gotPayload, want)
	}
}
