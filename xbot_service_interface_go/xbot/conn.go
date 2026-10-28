package xbot

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"net"
	"strconv"
	"sync"
	"sync/atomic"
	"time"
)

// DefaultCallTimeout bounds a Conn.Call whose context carries no deadline.
const DefaultCallTimeout = 2 * time.Second

// Param is one RPC parameter: the descriptor id from the service json plus the
// serialized value. The wire layout is one DataDescriptor followed by the raw
// bytes of that value, in the order the parameters are given.
type Param struct {
	ID   uint16
	Data []byte
}

// Bytes returns a parameter with raw bytes (also used for blobs, arrays and
// fixed size char arrays that the caller padded itself).
func Bytes(id uint16, b []byte) Param { return Param{ID: id, Data: b} }

// String returns a parameter holding the bytes of s, without a terminator: the
// service reads the length from the descriptor. Fixed size char arrays are
// accepted with or without trailing zero bytes (see the service json).
func String(id uint16, s string) Param { return Param{ID: id, Data: []byte(s)} }

// Bool returns a parameter as the uint8 the service json uses for flags.
func Bool(id uint16, v bool) Param {
	if v {
		return Param{ID: id, Data: []byte{1}}
	}
	return Param{ID: id, Data: []byte{0}}
}

// Uint8 returns an 8 bit parameter.
func Uint8(id uint16, v uint8) Param { return Param{ID: id, Data: []byte{v}} }

// Uint16 returns a little endian 16 bit parameter.
func Uint16(id uint16, v uint16) Param {
	b := make([]byte, 2)
	binary.LittleEndian.PutUint16(b, v)
	return Param{ID: id, Data: b}
}

// Uint32 returns a little endian 32 bit parameter.
func Uint32(id uint16, v uint32) Param {
	b := make([]byte, 4)
	binary.LittleEndian.PutUint32(b, v)
	return Param{ID: id, Data: b}
}

// Options configure Dial.
type Options struct {
	// ServiceID is the id to connect to (services/service_ids.h). Required.
	ServiceID uint16
	// BindIP restricts the socket and the discovery listener to the interface
	// that owns this IPv4 address. "" or "0.0.0.0" means all interfaces.
	BindIP string
	// Heartbeat is how often the service expects a heartbeat while the client
	// keeps the service alive. 0 means DefaultHeartbeat.
	Heartbeat time.Duration
	// NoClaim connects without claiming. Use it for a service that is owned by
	// someone else (on a robot: the high level system) and only receives data
	// and RPCs from this client: claiming would restart the service and
	// re-target its responses.
	NoClaim bool
	// Endpoint ("ip:port") skips discovery and connects straight to a known
	// service. Implies NoClaim.
	Endpoint string
	// CallTimeout bounds Call calls whose context has no deadline. 0 means
	// DefaultCallTimeout.
	CallTimeout time.Duration
}

// Conn is a unicast connection to one service. Calls and sends are safe for
// concurrent use; a Call serialises with other calls because the responses
// arrive on the shared socket.
type Conn struct {
	udp             *net.UDPConn
	svcIP           string
	svcPort         int
	serviceID       uint16
	heartbeatMicros uint32
	callTimeout     time.Duration

	callMu sync.Mutex
	seq    atomic.Uint32
	callID atomic.Uint32
}

// Dial discovers the service (or uses opts.Endpoint) and claims it, unless
// opts.NoClaim is set. The context bounds the discovery wait and the claim
// handshake; a service that is unclaimed and older than DiscoveryTimeout only
// advertises every AdvertisementInterval, so allow a few 10 s here.
func Dial(ctx context.Context, opts Options) (*Conn, error) {
	if opts.ServiceID == 0 {
		return nil, errors.New("xbot: Options.ServiceID is required")
	}
	heartbeat := opts.Heartbeat
	if heartbeat <= 0 {
		heartbeat = DefaultHeartbeat
	}
	callTimeout := opts.CallTimeout
	if callTimeout <= 0 {
		callTimeout = DefaultCallTimeout
	}

	udp, err := listenUDP(opts.BindIP)
	if err != nil {
		return nil, err
	}
	c := &Conn{
		udp:             udp,
		serviceID:       opts.ServiceID,
		heartbeatMicros: uint32(heartbeat / time.Microsecond),
		callTimeout:     callTimeout,
	}

	if opts.Endpoint != "" {
		host, portStr, err := net.SplitHostPort(opts.Endpoint)
		if err != nil {
			_ = udp.Close()
			return nil, fmt.Errorf("xbot: Options.Endpoint %q: %w", opts.Endpoint, err)
		}
		port, err := strconv.Atoi(portStr)
		if err != nil {
			_ = udp.Close()
			return nil, fmt.Errorf("xbot: Options.Endpoint %q: %w", opts.Endpoint, err)
		}
		if net.ParseIP(host) == nil {
			_ = udp.Close()
			return nil, fmt.Errorf("xbot: Options.Endpoint %q: not an IPv4 address", opts.Endpoint)
		}
		c.svcIP, c.svcPort = host, port
		return c, nil
	}

	adv, err := find(ctx, opts.BindIP, opts.ServiceID)
	if err != nil {
		_ = udp.Close()
		return nil, err
	}
	c.svcIP, c.svcPort = adv.IP, adv.Port

	if !opts.NoClaim {
		localIP := opts.BindIP
		if localIP == "" || localIP == "0.0.0.0" {
			localIP = PrimaryIP()
		}
		if err := c.claim(ctx, localIP); err != nil {
			_ = udp.Close()
			return nil, err
		}
	}
	return c, nil
}

// listenUDP opens a UDP socket bound to bindIP ("" or "0.0.0.0" = any interface).
func listenUDP(bindIP string) (*net.UDPConn, error) {
	udp, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.ParseIP(bindIP)})
	if err != nil {
		return nil, fmt.Errorf("xbot: udp bind %q: %w", bindIP, err)
	}
	return udp, nil
}

// Close closes the socket. The service notices that the heartbeats stop.
func (c *Conn) Close() error { return c.udp.Close() }

// Endpoint returns the unicast address of the service ("ip:port").
func (c *Conn) Endpoint() string {
	return net.JoinHostPort(c.svcIP, strconv.Itoa(c.svcPort))
}

// ServiceID returns the id of the connected service.
func (c *Conn) ServiceID() uint16 { return c.serviceID }

// claim sends CLAIM and waits for the acknowledgement, retrying until the
// service answers or ctx is done.
func (c *Conn) claim(ctx context.Context, localIP string) error {
	ip := net.ParseIP(localIP).To4()
	if ip == nil {
		return fmt.Errorf("xbot: local address %q is not an IPv4 address", localIP)
	}
	localPort := c.udp.LocalAddr().(*net.UDPAddr).Port
	payload := PackClaimPayload(binary.BigEndian.Uint32(ip), uint16(localPort), c.heartbeatMicros)

	for {
		if err := ctx.Err(); err != nil {
			return fmt.Errorf("xbot: claim service %d: %w", c.serviceID, err)
		}
		if err := c.send(MsgClaim, 0, 0, payload); err != nil {
			return err
		}
		hdr, _, err := c.readPacket(time.Second)
		if err == nil && hdr.MessageType == MsgClaim && hdr.Arg1 == 1 {
			return nil
		}
		if err != nil && !isTimeout(err) {
			return err
		}
	}
}

// Call sends an RPC call and waits for its response. It returns the raw return
// value (nil for a void function) or an *RPCError when the service answered
// busy/error. Without a deadline in ctx, Options.CallTimeout applies.
func (c *Conn) Call(ctx context.Context, functionID uint8, params ...Param) ([]byte, error) {
	timeout := c.callTimeout
	if deadline, ok := ctx.Deadline(); ok {
		timeout = time.Until(deadline)
	}

	c.callMu.Lock()
	defer c.callMu.Unlock()

	callID, err := c.startCall(functionID, params)
	if err != nil {
		return nil, err
	}

	deadline := time.Now().Add(timeout)
	for {
		if err := ctx.Err(); err != nil {
			return nil, fmt.Errorf("xbot: rpc function %d: %w", functionID, err)
		}
		hdr, payload, err := c.readPacket(time.Until(deadline))
		if err != nil {
			if isTimeout(err) {
				if ctxErr := ctx.Err(); ctxErr != nil {
					return nil, fmt.Errorf("xbot: rpc function %d: %w", functionID, ctxErr)
				}
				return nil, fmt.Errorf("xbot: rpc function %d: no response within %s: %w",
					functionID, timeout, context.DeadlineExceeded)
			}
			return nil, err
		}
		if hdr.MessageType == MsgRPCResponse && hdr.Arg2 == callID {
			if hdr.Arg1 != RPCStatusSuccess {
				return nil, &RPCError{FunctionID: functionID, Status: hdr.Arg1}
			}
			return payload, nil
		}
		// Heartbeats and other unrelated packets are ignored.
	}
}

// SendRPC sends a call without waiting for the response ("fire and forget").
//
// Needed when another client owns (claimed) the service: the firmware still
// executes the call, only the response goes to the owner.
func (c *Conn) SendRPC(functionID uint8, params ...Param) error {
	_, err := c.startCall(functionID, params)
	return err
}

// SendData sends raw bytes to an id of the service: an input, a register, or
// anything else the service json names. The caller is responsible for the
// layout (see the schema) — e.g. HeatshrinkEncode for a compressed blob
// register.
func (c *Conn) SendData(targetID uint16, data []byte) error {
	return c.send(MsgData, 0, targetID, data)
}

// buildRPCPayload serialises an RPC body: one descriptor per parameter, each
// followed by that parameter's bytes.
func buildRPCPayload(params []Param) []byte {
	var body []byte
	for _, p := range params {
		body = append(body, PackDescriptor(p.ID, uint32(len(p.Data)))...)
		body = append(body, p.Data...)
	}
	return body
}

// startCall sends an RPC and returns the call id it used.
func (c *Conn) startCall(functionID uint8, params []Param) (uint16, error) {
	callID := uint16(c.callID.Add(1) & 0xFFFF)
	return callID, c.send(MsgRPCCall, functionID, callID, buildRPCPayload(params))
}

// send writes one header+payload packet to the service.
func (c *Conn) send(msgType MessageType, arg1 uint8, arg2 uint16, payload []byte) error {
	hdr := Header{
		ProtocolVersion: ProtocolVersion,
		MessageType:     msgType,
		ServiceID:       c.serviceID,
		Arg1:            arg1,
		Arg2:            arg2,
		SequenceNo:      uint16(c.seq.Add(1) & 0xFFFF),
		Timestamp:       nowMicros(),
		PayloadSize:     uint32(len(payload)),
	}
	pkt := make([]byte, 0, HeaderSize+len(payload))
	pkt = append(pkt, hdr.Marshal()...)
	pkt = append(pkt, payload...)
	_, err := c.udp.WriteToUDP(pkt, &net.UDPAddr{IP: net.ParseIP(c.svcIP), Port: c.svcPort})
	return err
}

// readPacket reads one packet and splits it into header and payload.
func (c *Conn) readPacket(timeout time.Duration) (Header, []byte, error) {
	_ = c.udp.SetReadDeadline(time.Now().Add(timeout))
	buf := make([]byte, MaxPacketSize)
	n, _, err := c.udp.ReadFromUDP(buf)
	if err != nil {
		return Header{}, nil, err
	}
	data := buf[:n]
	hdr, err := UnmarshalHeader(data)
	if err != nil {
		return hdr, nil, err
	}
	if len(data) != HeaderSize+int(hdr.PayloadSize) {
		return hdr, nil, errPacketMismatch
	}
	return hdr, data[HeaderSize:], nil
}

// isTimeout reports whether err is a read deadline expiry.
func isTimeout(err error) bool {
	var ne net.Error
	return errors.As(err, &ne) && ne.Timeout()
}
