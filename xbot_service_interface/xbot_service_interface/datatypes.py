import struct
from enum import IntEnum

# Packed struct formats (little-endian, matching C++ __attribute__((packed)))
#
# XbotHeader (24 bytes):
#   u8  protocol_version
#   u8  message_type
#   u8  flags
#   u8  reserved1
#   u16 service_id
#   u8  arg1
#   u8  reserved2
#   u16 arg2
#   u16 sequence_no
#   u64 timestamp  (microseconds, monotonic)
#   u32 payload_size
HEADER_FORMAT = '<BBBBHBBHHQI'
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)   # 24

# DataDescriptor (8 bytes):
#   u16 target_id
#   u16 reserved
#   u32 payload_size
DESCRIPTOR_FORMAT = '<HHI'
DESCRIPTOR_SIZE = struct.calcsize(DESCRIPTOR_FORMAT)  # 8

# ClaimPayload (10 bytes):
#   u32 target_ip       (host byte order)
#   u16 target_port
#   u32 heartbeat_micros
CLAIM_FORMAT = '<IHI'
CLAIM_SIZE = struct.calcsize(CLAIM_FORMAT)     # 10

assert HEADER_SIZE == 24
assert DESCRIPTOR_SIZE == 8
assert CLAIM_SIZE == 10


class MessageType(IntEnum):
    UNKNOWN               = 0x00
    DATA                  = 0x01
    CONFIGURATION_REQUEST = 0x02
    CLAIM                 = 0x03
    HEARTBEAT             = 0x04
    TRANSACTION           = 0x05
    RPC_CALL              = 0x06
    RPC_RESPONSE          = 0x07
    LOG                   = 0x7F
    SERVICE_ADVERTISEMENT = 0x80
    SERVICE_QUERY         = 0x81


class RpcStatus(IntEnum):
    SUCCESS = 0
    BUSY    = 1
    ERROR   = 2


class LogLevel(IntEnum):
    TRACE    = 1
    DEBUG    = 2
    INFO     = 3
    WARNING  = 4
    ERROR    = 5
    CRITICAL = 6
    ALWAYS   = 7


# From xbot/config.hpp
MULTICAST_PORT          = 4242
SD_MULTICAST_ADDR       = '233.255.255.0'
LOG_MULTICAST_ADDR      = '233.255.255.1'
DEFAULT_HEARTBEAT_MICROS = 1_000_000
HEARTBEAT_JITTER        = 100_000
PROTOCOL_VERSION        = 1
MAX_PACKET_SIZE         = 1500


def unpack_header(data: bytes) -> dict:
    f = struct.unpack_from(HEADER_FORMAT, data)
    return {
        'protocol_version': f[0],
        'message_type':     f[1],
        'flags':            f[2],
        'service_id':       f[4],
        'arg1':             f[5],
        'arg2':             f[7],
        'sequence_no':      f[8],
        'timestamp':        f[9],
        'payload_size':     f[10],
    }


def pack_header(message_type: int, service_id: int, arg1: int, arg2: int,
                sequence_no: int, timestamp: int, payload_size: int,
                flags: int = 0) -> bytes:
    return struct.pack(
        HEADER_FORMAT,
        PROTOCOL_VERSION, int(message_type), flags, 0,
        service_id, arg1, 0, arg2,
        sequence_no, timestamp, payload_size,
    )


def unpack_descriptor(data: bytes, offset: int = 0) -> dict:
    f = struct.unpack_from(DESCRIPTOR_FORMAT, data, offset)
    return {'target_id': f[0], 'payload_size': f[2]}


def pack_descriptor(target_id: int, payload_size: int) -> bytes:
    return struct.pack(DESCRIPTOR_FORMAT, target_id, 0, payload_size)


def pack_claim_payload(target_ip_host: int, target_port: int,
                       heartbeat_micros: int) -> bytes:
    return struct.pack(CLAIM_FORMAT, target_ip_host, target_port, heartbeat_micros)
