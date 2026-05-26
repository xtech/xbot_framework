"""Tests for datatypes.py — struct layouts, enums, constants."""
import struct
import pytest

from xbot_service_interface.datatypes import (
    HEADER_FORMAT, HEADER_SIZE,
    DESCRIPTOR_FORMAT, DESCRIPTOR_SIZE,
    CLAIM_FORMAT, CLAIM_SIZE,
    pack_header, unpack_header,
    pack_descriptor, unpack_descriptor,
    pack_claim_payload,
    MessageType, LogLevel,
    MULTICAST_PORT, SD_MULTICAST_ADDR, LOG_MULTICAST_ADDR,
    DEFAULT_HEARTBEAT_MICROS, HEARTBEAT_JITTER, PROTOCOL_VERSION, MAX_PACKET_SIZE,
)


# ---------------------------------------------------------------------------
# Struct sizes — must match C++ packed structs exactly
# ---------------------------------------------------------------------------

class TestStructSizes:
    def test_header_size(self):
        assert HEADER_SIZE == 24

    def test_descriptor_size(self):
        assert DESCRIPTOR_SIZE == 8

    def test_claim_size(self):
        assert CLAIM_SIZE == 10

    def test_header_calcsize_matches(self):
        assert struct.calcsize(HEADER_FORMAT) == 24

    def test_descriptor_calcsize_matches(self):
        assert struct.calcsize(DESCRIPTOR_FORMAT) == 8

    def test_claim_calcsize_matches(self):
        assert struct.calcsize(CLAIM_FORMAT) == 10


# ---------------------------------------------------------------------------
# Header pack / unpack
# ---------------------------------------------------------------------------

class TestHeaderPackUnpack:
    def _pack(self, **kwargs):
        defaults = dict(message_type=MessageType.DATA, service_id=1,
                        arg1=0, arg2=0, sequence_no=0, timestamp=0, payload_size=0, flags=0)
        defaults.update(kwargs)
        return pack_header(**defaults)

    def test_round_trip_basic(self):
        raw = self._pack(message_type=MessageType.DATA, service_id=42, arg1=3,
                         arg2=7, sequence_no=100, timestamp=9999, payload_size=64)
        h = unpack_header(raw)
        assert h['message_type']  == MessageType.DATA
        assert h['service_id']    == 42
        assert h['arg1']          == 3
        assert h['arg2']          == 7
        assert h['sequence_no']   == 100
        assert h['timestamp']     == 9999
        assert h['payload_size']  == 64

    def test_protocol_version_always_one(self):
        raw = self._pack()
        h = unpack_header(raw)
        assert h['protocol_version'] == 1

    def test_flags_zero_by_default(self):
        raw = self._pack()
        assert raw[2] == 0  # flags byte at offset 2

    def test_flags_passed_through(self):
        raw = self._pack(flags=1)
        assert raw[2] == 1

    def test_max_service_id(self):
        raw = self._pack(service_id=0xFFFF)
        assert unpack_header(raw)['service_id'] == 0xFFFF

    def test_max_sequence_no(self):
        raw = self._pack(sequence_no=0xFFFF)
        assert unpack_header(raw)['sequence_no'] == 0xFFFF

    def test_max_payload_size(self):
        raw = self._pack(payload_size=0xFFFFFFFF)
        assert unpack_header(raw)['payload_size'] == 0xFFFFFFFF

    def test_max_timestamp(self):
        ts = (1 << 64) - 1
        raw = self._pack(timestamp=ts)
        assert unpack_header(raw)['timestamp'] == ts

    def test_all_message_types_encode(self):
        for mt in MessageType:
            raw = self._pack(message_type=mt)
            assert unpack_header(raw)['message_type'] == mt

    def test_output_is_bytes(self):
        assert isinstance(self._pack(), bytes)

    def test_output_length_is_header_size(self):
        assert len(self._pack()) == HEADER_SIZE

    def test_reserved_bytes_are_zero(self):
        raw = self._pack()
        # reserved1 at offset 3, reserved2 at offset 7
        assert raw[3] == 0
        assert raw[7] == 0


# ---------------------------------------------------------------------------
# Descriptor pack / unpack
# ---------------------------------------------------------------------------

class TestDescriptorPackUnpack:
    def test_round_trip(self):
        raw = pack_descriptor(target_id=5, payload_size=128)
        d = unpack_descriptor(raw)
        assert d['target_id']    == 5
        assert d['payload_size'] == 128

    def test_length(self):
        assert len(pack_descriptor(0, 0)) == DESCRIPTOR_SIZE

    def test_max_target_id(self):
        raw = pack_descriptor(0xFFFF, 0)
        assert unpack_descriptor(raw)['target_id'] == 0xFFFF

    def test_max_payload_size(self):
        raw = pack_descriptor(0, 0xFFFFFFFF)
        assert unpack_descriptor(raw)['payload_size'] == 0xFFFFFFFF

    def test_offset_parameter(self):
        prefix = b'\xAA\xBB'
        raw = prefix + pack_descriptor(7, 32)
        d = unpack_descriptor(prefix + pack_descriptor(7, 32), offset=2)
        assert d['target_id'] == 7
        assert d['payload_size'] == 32

    def test_reserved_is_zero(self):
        raw = pack_descriptor(1, 1)
        # reserved at bytes 2-3
        assert raw[2] == 0
        assert raw[3] == 0


# ---------------------------------------------------------------------------
# ClaimPayload pack
# ---------------------------------------------------------------------------

class TestClaimPayload:
    def test_length(self):
        assert len(pack_claim_payload(0, 0, 0)) == CLAIM_SIZE

    def test_fields_in_correct_positions(self):
        # ip=0x01020304 (host order), port=5678, heartbeat=1_000_000
        raw = pack_claim_payload(0x01020304, 5678, 1_000_000)
        ip, port, hb = struct.unpack('<IHI', raw)
        assert ip   == 0x01020304
        assert port == 5678
        assert hb   == 1_000_000

    def test_default_heartbeat_value(self):
        raw = pack_claim_payload(0, 0, DEFAULT_HEARTBEAT_MICROS)
        _, _, hb = struct.unpack('<IHI', raw)
        assert hb == 1_000_000


# ---------------------------------------------------------------------------
# MessageType enum values — must match C++ exactly
# ---------------------------------------------------------------------------

class TestMessageTypeValues:
    def test_unknown(self):             assert MessageType.UNKNOWN               == 0x00
    def test_data(self):                assert MessageType.DATA                  == 0x01
    def test_configuration_request(self): assert MessageType.CONFIGURATION_REQUEST == 0x02
    def test_claim(self):               assert MessageType.CLAIM                 == 0x03
    def test_heartbeat(self):           assert MessageType.HEARTBEAT             == 0x04
    def test_transaction(self):         assert MessageType.TRANSACTION           == 0x05
    def test_log(self):                 assert MessageType.LOG                   == 0x7F
    def test_service_advertisement(self): assert MessageType.SERVICE_ADVERTISEMENT == 0x80
    def test_service_query(self):       assert MessageType.SERVICE_QUERY         == 0x81


# ---------------------------------------------------------------------------
# LogLevel enum values
# ---------------------------------------------------------------------------

class TestLogLevelValues:
    def test_trace(self):    assert LogLevel.TRACE    == 1
    def test_debug(self):    assert LogLevel.DEBUG    == 2
    def test_info(self):     assert LogLevel.INFO     == 3
    def test_warning(self):  assert LogLevel.WARNING  == 4
    def test_error(self):    assert LogLevel.ERROR    == 5
    def test_critical(self): assert LogLevel.CRITICAL == 6
    def test_always(self):   assert LogLevel.ALWAYS   == 7


# ---------------------------------------------------------------------------
# Protocol constants — must match xbot/config.hpp
# ---------------------------------------------------------------------------

class TestConstants:
    def test_multicast_port(self):           assert MULTICAST_PORT           == 4242
    def test_sd_multicast_addr(self):        assert SD_MULTICAST_ADDR        == '233.255.255.0'
    def test_log_multicast_addr(self):       assert LOG_MULTICAST_ADDR       == '233.255.255.1'
    def test_default_heartbeat(self):        assert DEFAULT_HEARTBEAT_MICROS == 1_000_000
    def test_heartbeat_jitter(self):         assert HEARTBEAT_JITTER         == 100_000
    def test_protocol_version(self):         assert PROTOCOL_VERSION         == 1
    def test_max_packet_size(self):          assert MAX_PACKET_SIZE          == 1500
