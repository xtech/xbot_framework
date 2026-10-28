package xbot

import (
	"encoding/binary"
	"errors"
	"fmt"
	"time"
)

// MessageType is the 8 bit message type of an XbotHeader
// (see include/xbot/datatypes/XbotHeader.hpp).
//
// The first bit of the value tells how the payload is encoded: 0 = raw binary
// (DATA, RPC, CLAIM, ...), 1 = CBOR (service discovery).
type MessageType uint8

// Message types. Not all of them are used by this client yet — the list
// documents the wire protocol.
const (
	MsgUnknown              MessageType = 0x00
	MsgData                 MessageType = 0x01
	MsgConfigurationRequest MessageType = 0x02
	MsgClaim                MessageType = 0x03
	MsgHeartbeat            MessageType = 0x04
	MsgTransaction          MessageType = 0x05
	MsgRPCCall              MessageType = 0x06
	MsgRPCResponse          MessageType = 0x07
	MsgLog                  MessageType = 0x7F
	MsgServiceAdvertisement MessageType = 0x80
	MsgServiceQuery         MessageType = 0x81
)

// String returns the symbol of the message type, e.g. "RPC_CALL".
func (m MessageType) String() string {
	switch m {
	case MsgData:
		return "DATA"
	case MsgConfigurationRequest:
		return "CONFIGURATION_REQUEST"
	case MsgClaim:
		return "CLAIM"
	case MsgHeartbeat:
		return "HEARTBEAT"
	case MsgTransaction:
		return "TRANSACTION"
	case MsgRPCCall:
		return "RPC_CALL"
	case MsgRPCResponse:
		return "RPC_RESPONSE"
	case MsgLog:
		return "LOG"
	case MsgServiceAdvertisement:
		return "SERVICE_ADVERTISEMENT"
	case MsgServiceQuery:
		return "SERVICE_QUERY"
	case MsgUnknown:
		return "UNKNOWN"
	default:
		return fmt.Sprintf("MessageType(0x%02x)", uint8(m))
	}
}

// RPC status codes (arg1 of an RPC_RESPONSE).
const (
	RPCStatusSuccess uint8 = 0
	RPCStatusBusy    uint8 = 1
	RPCStatusError   uint8 = 2
)

// Protocol constants, mirroring include/xbot/config.hpp. spec_test.go fails
// when the C++ side and this list drift apart.
const (
	// ProtocolVersion is the version this client speaks.
	ProtocolVersion uint8 = 1
	// HeaderSize is the size of the packed XbotHeader.
	HeaderSize = 24
	// DescriptorSize is the size of one DataDescriptor (target id + payload size).
	DescriptorSize = 8
	// ClaimSize is the size of a ClaimPayload.
	ClaimSize = 10
	// MaxPacketSize is the largest UDP payload a peer may send, sized so that a
	// full packet fits into one unfragmented Ethernet frame.
	MaxPacketSize = 1472

	// MulticastPort is used for discovery and remote logging.
	MulticastPort = 4242
	// SDMulticastAddress is the IPv4 group services advertise on.
	SDMulticastAddress = "233.255.255.0"
	// LogMulticastAddress carries the remote log stream.
	LogMulticastAddress = "233.255.255.1"

	// DefaultHeartbeat is the heartbeat interval a service assumes when the
	// client does not ask for another one.
	DefaultHeartbeat = 1 * time.Second
	// HeartbeatJitter is the tolerance on top of a heartbeat before a service
	// considers the client gone.
	HeartbeatJitter = 100 * time.Millisecond
)

// Discovery timing, mirroring include/xbot/config.hpp.
const (
	// AdvertisementInterval is how often a service advertises itself once it is
	// claimed or has stayed unclaimed for DiscoveryTimeout.
	AdvertisementInterval = 10 * time.Second
	// AdvertisementIntervalFast is used while a service is unclaimed and
	// younger than DiscoveryTimeout.
	AdvertisementIntervalFast = 1 * time.Second
	// DiscoveryTimeout is how long a service keeps the fast interval.
	DiscoveryTimeout = 60 * time.Second
)

var errPacketMismatch = errors.New("xbot: packet size does not match the header")

// Header is the 24-byte packed xbot protocol header.
type Header struct {
	ProtocolVersion uint8
	MessageType     MessageType
	// Flags: bit 0 = reboot (set after a service start, cleared once the
	// sequence number rolled over at least once).
	Flags uint8
	// Reserved1 is padding and must stay zero.
	Reserved1 uint8
	ServiceID uint16
	// Arg1 is message specific: the RPC function id for RPC_CALL, the status for
	// RPC_RESPONSE, the log level for LOG, the transaction kind for TRANSACTION.
	Arg1 uint8
	// Reserved2 is padding and must stay zero.
	Reserved2 uint8
	// Arg2 is message specific: the call id for RPC_CALL and RPC_RESPONSE, the
	// target id for DATA.
	Arg2 uint16
	// SequenceNo counts the messages of one sender, wraps at 16 bit.
	SequenceNo uint16
	// Timestamp is the send time in microseconds since the unix epoch.
	Timestamp uint64
	// PayloadSize is the length of the bytes that follow the header.
	PayloadSize uint32
}

// Marshal packs the header into its 24 wire bytes (little endian).
func (h Header) Marshal() []byte {
	b := make([]byte, HeaderSize)
	b[0] = h.ProtocolVersion
	b[1] = uint8(h.MessageType)
	b[2] = h.Flags
	b[3] = h.Reserved1
	binary.LittleEndian.PutUint16(b[4:6], h.ServiceID)
	b[6] = h.Arg1
	b[7] = h.Reserved2
	binary.LittleEndian.PutUint16(b[8:10], h.Arg2)
	binary.LittleEndian.PutUint16(b[10:12], h.SequenceNo)
	binary.LittleEndian.PutUint64(b[12:20], h.Timestamp)
	binary.LittleEndian.PutUint32(b[20:24], h.PayloadSize)
	return b
}

// UnmarshalHeader parses the 24 wire bytes of a header.
func UnmarshalHeader(data []byte) (Header, error) {
	var h Header
	if len(data) < HeaderSize {
		return h, fmt.Errorf("xbot: header too short: %d bytes", len(data))
	}
	h.ProtocolVersion = data[0]
	h.MessageType = MessageType(data[1])
	h.Flags = data[2]
	h.Reserved1 = data[3]
	h.ServiceID = binary.LittleEndian.Uint16(data[4:6])
	h.Arg1 = data[6]
	h.Reserved2 = data[7]
	h.Arg2 = binary.LittleEndian.Uint16(data[8:10])
	h.SequenceNo = binary.LittleEndian.Uint16(data[10:12])
	h.Timestamp = binary.LittleEndian.Uint64(data[12:20])
	h.PayloadSize = binary.LittleEndian.Uint32(data[20:24])
	return h, nil
}

// PackDescriptor packs an 8-byte DataDescriptor: the target id (a function
// parameter id, an input id or a register id) and the size of the data that
// follows it.
func PackDescriptor(targetID uint16, payloadSize uint32) []byte {
	b := make([]byte, DescriptorSize)
	binary.LittleEndian.PutUint16(b[0:2], targetID)
	binary.LittleEndian.PutUint32(b[4:8], payloadSize)
	return b
}

// PackClaimPayload packs a 10-byte ClaimPayload: where the service shall send
// its responses, and how often this client sends heartbeats.
//
// targetIP is the network order (big endian) uint32 of the local address, as
// produced by binary.BigEndian.Uint32(ip.To4()).
func PackClaimPayload(targetIP uint32, targetPort uint16, heartbeatMicros uint32) []byte {
	b := make([]byte, ClaimSize)
	binary.LittleEndian.PutUint32(b[0:4], targetIP)
	binary.LittleEndian.PutUint16(b[4:6], targetPort)
	binary.LittleEndian.PutUint32(b[6:10], heartbeatMicros)
	return b
}

func nowMicros() uint64 { return uint64(time.Now().UnixMicro()) }
