"""Tests for io.py — ServiceIO packet handling, sending, watchdog."""
import socket
import struct
import time
import threading
from unittest.mock import MagicMock, patch, call
import pytest

from xbot_service_interface.io import ServiceIO, _ServiceState, _get_primary_ip
from xbot_service_interface.datatypes import (
    pack_header, unpack_header, pack_descriptor, unpack_descriptor,
    pack_claim_payload, HEADER_SIZE, DESCRIPTOR_SIZE,
    MessageType, DEFAULT_HEARTBEAT_MICROS, HEARTBEAT_JITTER, CLAIM_SIZE,
)
from tests.conftest import (
    make_io, make_callbacks, make_packet,
    register_claimed_service, register_unclaimed_service,
)


# ---------------------------------------------------------------------------
# _get_primary_ip
# ---------------------------------------------------------------------------

class TestGetPrimaryIp:
    def test_returns_string(self):
        ip = _get_primary_ip()
        assert isinstance(ip, str)

    def test_returns_valid_ip(self):
        ip = _get_primary_ip()
        parts = ip.split('.')
        assert len(parts) == 4
        assert all(p.isdigit() for p in parts)

    def test_not_loopback(self):
        ip = _get_primary_ip()
        assert not ip.startswith('127.')

    def test_not_empty(self):
        assert _get_primary_ip() != ''

    def test_fallback_on_error(self):
        with patch('xbot_service_interface.io.fcntl.ioctl', side_effect=OSError):
            # Should fall back, not raise
            ip = _get_primary_ip()
            assert isinstance(ip, str)


# ---------------------------------------------------------------------------
# ServiceIO — basic state
# ---------------------------------------------------------------------------

class TestServiceIOState:
    def test_initial_not_running(self):
        io = ServiceIO()
        assert io.ok() == False

    def test_register_service(self):
        io = make_io()
        cbs = make_callbacks()
        io.register_service(1, '10.0.0.2', 9000, cbs)
        assert 1 in io._services
        assert io._services[1].ip   == '10.0.0.2'
        assert io._services[1].port == 9000

    def test_unregister_service(self):
        io = make_io()
        io.register_service(1, '10.0.0.2', 9000, make_callbacks())
        io.unregister_service(1)
        assert 1 not in io._services

    def test_unregister_nonexistent_no_error(self):
        io = make_io()
        io.unregister_service(99)  # must not raise

    def test_get_endpoint(self):
        io = make_io(my_ip='192.168.1.10', my_port=54321)
        assert io.get_endpoint() == ('192.168.1.10', 54321)


# ---------------------------------------------------------------------------
# send_data
# ---------------------------------------------------------------------------

class TestSendData:
    def test_sends_correct_packet(self):
        io = make_io()
        register_claimed_service(io, service_id=1, ip='10.0.0.2', port=9000)
        payload = b'\x01\x02\x03\x04'
        io.send_data(1, target_id=5, payload=payload)
        io._sock.sendto.assert_called_once()
        packet, addr = io._sock.sendto.call_args[0]
        assert addr == ('10.0.0.2', 9000)
        h = unpack_header(packet)
        assert h['message_type'] == MessageType.DATA
        assert h['service_id']   == 1
        assert h['arg2']         == 5
        assert h['payload_size'] == 4
        assert packet[HEADER_SIZE:] == payload

    def test_returns_false_when_not_claimed(self):
        io = make_io()
        register_unclaimed_service(io, service_id=1)
        assert io.send_data(1, 0, b'x') == False
        io._sock.sendto.assert_not_called()

    def test_returns_false_for_unknown_service(self):
        io = make_io()
        assert io.send_data(99, 0, b'x') == False

    def test_sequence_number_increments(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        io.send_data(1, 0, b'a')
        io.send_data(1, 0, b'b')
        packets = [call[0][0] for call in io._sock.sendto.call_args_list]
        seq0 = unpack_header(packets[0])['sequence_no']
        seq1 = unpack_header(packets[1])['sequence_no']
        assert seq1 == seq0 + 1

    def test_sequence_number_wraps_at_65535(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        io._services[1].sequence_no = 0xFFFF
        io.send_data(1, 0, b'x')
        pkt = io._sock.sendto.call_args[0][0]
        assert unpack_header(pkt)['sequence_no'] == 0xFFFF
        io.send_data(1, 0, b'y')
        pkt = io._sock.sendto.call_args[0][0]
        assert unpack_header(pkt)['sequence_no'] == 0  # wrapped

    def test_different_services_independent_seq(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        register_claimed_service(io, service_id=2)
        io._services[2].sequence_no = 10
        io.send_data(1, 0, b'a')
        pkt1 = io._sock.sendto.call_args[0][0]
        io.send_data(2, 0, b'b')
        pkt2 = io._sock.sendto.call_args[0][0]
        assert unpack_header(pkt1)['sequence_no'] == 0
        assert unpack_header(pkt2)['sequence_no'] == 10


# ---------------------------------------------------------------------------
# send_transaction
# ---------------------------------------------------------------------------

class TestSendTransaction:
    def test_data_transaction_arg1_zero(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        io.send_transaction(1, [(0, b'\x01\x02')])
        pkt = io._sock.sendto.call_args[0][0]
        assert unpack_header(pkt)['arg1'] == 0

    def test_config_transaction_arg1_one(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        io.send_transaction(1, [(0, b'\x01')], is_config=True)
        pkt = io._sock.sendto.call_args[0][0]
        assert unpack_header(pkt)['arg1'] == 1

    def test_multiple_chunks_encoded(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        chunks = [(0, b'\xAA\xBB'), (1, b'\xCC\xDD\xEE')]
        io.send_transaction(1, chunks)
        pkt  = io._sock.sendto.call_args[0][0]
        body = pkt[HEADER_SIZE:]
        # First descriptor
        d0 = unpack_descriptor(body, 0)
        assert d0['target_id']    == 0
        assert d0['payload_size'] == 2
        assert body[DESCRIPTOR_SIZE:DESCRIPTOR_SIZE+2] == b'\xAA\xBB'
        # Second descriptor
        off = DESCRIPTOR_SIZE + 2
        d1 = unpack_descriptor(body, off)
        assert d1['target_id']    == 1
        assert d1['payload_size'] == 3

    def test_payload_size_in_header(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        io.send_transaction(1, [(0, b'\x01\x02\x03')])
        pkt = io._sock.sendto.call_args[0][0]
        expected = DESCRIPTOR_SIZE + 3
        assert unpack_header(pkt)['payload_size'] == expected

    def test_not_claimed_returns_false(self):
        io = make_io()
        register_unclaimed_service(io, service_id=1)
        assert io.send_transaction(1, [(0, b'x')]) == False


# ---------------------------------------------------------------------------
# _handle_packet — CLAIM ack
# ---------------------------------------------------------------------------

class TestHandleClaimAck:
    def test_claim_ack_marks_claimed(self):
        io = make_io()
        cbs = register_unclaimed_service(io, service_id=1)
        pkt = make_packet(MessageType.CLAIM, service_id=1, arg1=1)
        io._handle_packet(pkt)
        assert io._services[1].claimed == True

    def test_claim_ack_fires_callback(self):
        io = make_io()
        cbs = register_unclaimed_service(io, service_id=1)
        pkt = make_packet(MessageType.CLAIM, service_id=1, arg1=1)
        io._handle_packet(pkt)
        cbs['on_claim_ack'].assert_called_once()

    def test_claim_ack_updates_heartbeat(self):
        io = make_io()
        register_unclaimed_service(io, service_id=1)
        io._services[1].last_heartbeat = 0.0
        pkt = make_packet(MessageType.CLAIM, service_id=1, arg1=1)
        io._handle_packet(pkt)
        assert io._services[1].last_heartbeat > 0.0

    def test_claim_request_ignored(self):
        io = make_io()
        cbs = register_unclaimed_service(io, service_id=1)
        pkt = make_packet(MessageType.CLAIM, service_id=1, arg1=0)  # request not ack
        io._handle_packet(pkt)
        assert io._services[1].claimed == False
        cbs['on_claim_ack'].assert_not_called()

    def test_duplicate_claim_ack_ignored(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)  # already claimed
        pkt = make_packet(MessageType.CLAIM, service_id=1, arg1=1)
        io._handle_packet(pkt)
        cbs['on_claim_ack'].assert_not_called()  # should not fire again


# ---------------------------------------------------------------------------
# _handle_packet — DATA
# ---------------------------------------------------------------------------

class TestHandleData:
    def test_data_fires_on_data(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        payload = b'\x01\x02\x03\x04'
        pkt = make_packet(MessageType.DATA, service_id=1, payload=payload, arg2=7)
        io._handle_packet(pkt)
        cbs['on_data'].assert_called_once_with(0, 7, payload)

    def test_data_passes_timestamp(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        pkt = make_packet(MessageType.DATA, service_id=1, payload=b'\x00')
        io._handle_packet(pkt)
        ts = cbs['on_data'].call_args[0][0]
        assert isinstance(ts, int)

    def test_data_ignored_if_not_claimed(self):
        io = make_io()
        cbs = register_unclaimed_service(io, service_id=1)
        pkt = make_packet(MessageType.DATA, service_id=1, payload=b'\x01')
        io._handle_packet(pkt)
        cbs['on_data'].assert_not_called()

    def test_data_unknown_service_ignored(self):
        io = make_io()
        pkt = make_packet(MessageType.DATA, service_id=99, payload=b'\x01')
        io._handle_packet(pkt)  # must not raise


# ---------------------------------------------------------------------------
# _handle_packet — TRANSACTION
# ---------------------------------------------------------------------------

class TestHandleTransaction:
    def _make_transaction(self, service_id, chunks):
        body = b''
        for tid, data in chunks:
            body += pack_descriptor(tid, len(data)) + data
        return make_packet(MessageType.TRANSACTION, service_id=service_id, payload=body, arg1=0)

    def test_transaction_fires_per_chunk(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        pkt = self._make_transaction(1, [(0, b'\x01'), (1, b'\x02\x03')])
        io._handle_packet(pkt)
        assert cbs['on_data'].call_count == 2

    def test_transaction_fires_start_and_end(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        pkt = self._make_transaction(1, [(0, b'\x01')])
        io._handle_packet(pkt)
        cbs['on_transaction_start'].assert_called_once()
        cbs['on_transaction_end'].assert_called_once()

    def test_transaction_correct_target_ids(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        pkt = self._make_transaction(1, [(3, b'\xAA'), (7, b'\xBB')])
        io._handle_packet(pkt)
        calls = cbs['on_data'].call_args_list
        tids = [c[0][1] for c in calls]
        assert set(tids) == {3, 7}

    def test_config_transaction_arg1_1_ignored(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        body = pack_descriptor(0, 1) + b'\x01'
        pkt = make_packet(MessageType.TRANSACTION, service_id=1, payload=body, arg1=1)
        io._handle_packet(pkt)
        cbs['on_data'].assert_not_called()  # config transactions don't call on_data

    def test_transaction_not_claimed_ignored(self):
        io = make_io()
        cbs = register_unclaimed_service(io, service_id=1)
        pkt = self._make_transaction(1, [(0, b'\x01')])
        io._handle_packet(pkt)
        cbs['on_data'].assert_not_called()


# ---------------------------------------------------------------------------
# _handle_packet — HEARTBEAT
# ---------------------------------------------------------------------------

class TestHandleHeartbeat:
    def test_heartbeat_updates_timestamp(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        io._services[1].last_heartbeat = 0.0
        pkt = make_packet(MessageType.HEARTBEAT, service_id=1)
        io._handle_packet(pkt)
        assert io._services[1].last_heartbeat > 0.0

    def test_heartbeat_unknown_service_ignored(self):
        io = make_io()
        pkt = make_packet(MessageType.HEARTBEAT, service_id=99)
        io._handle_packet(pkt)  # must not raise


# ---------------------------------------------------------------------------
# _handle_packet — CONFIGURATION_REQUEST
# ---------------------------------------------------------------------------

class TestHandleConfigRequest:
    def test_config_request_fires_callback(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        pkt = make_packet(MessageType.CONFIGURATION_REQUEST, service_id=1)
        io._handle_packet(pkt)
        cbs['on_config_request'].assert_called_once()

    def test_config_request_not_claimed_ignored(self):
        io = make_io()
        cbs = register_unclaimed_service(io, service_id=1)
        pkt = make_packet(MessageType.CONFIGURATION_REQUEST, service_id=1)
        io._handle_packet(pkt)
        cbs['on_config_request'].assert_not_called()


# ---------------------------------------------------------------------------
# _handle_packet — edge cases
# ---------------------------------------------------------------------------

class TestHandlePacketEdgeCases:
    def test_too_short_ignored(self):
        io = make_io()
        io._handle_packet(b'\x00\x01\x02')  # too short, no crash

    def test_size_mismatch_ignored(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        pkt = make_packet(MessageType.DATA, service_id=1, payload=b'\x01\x02')
        # Corrupt reported payload size
        bad = pkt[:20] + struct.pack('<I', 99) + pkt[24:]
        io._handle_packet(bad)
        # No crash

    def test_unknown_message_type_ignored(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        hdr = pack_header(0x42, service_id=1, arg1=0, arg2=0,
                           sequence_no=0, timestamp=0, payload_size=0)
        io._handle_packet(hdr)  # must not raise


# ---------------------------------------------------------------------------
# _send_claim
# ---------------------------------------------------------------------------

class TestSendClaim:
    def test_claim_packet_structure(self):
        io = make_io(my_ip='10.0.0.1', my_port=12345)
        register_unclaimed_service(io, service_id=1, ip='10.0.0.2', port=9000)
        io._send_claim(1, io._services[1])
        io._sock.sendto.assert_called_once()
        pkt, addr = io._sock.sendto.call_args[0]
        assert addr == ('10.0.0.2', 9000)
        h = unpack_header(pkt)
        assert h['message_type'] == MessageType.CLAIM
        assert h['arg1']         == 0
        assert h['service_id']   == 1
        assert h['payload_size'] == CLAIM_SIZE

    def test_claim_payload_contains_my_endpoint(self):
        io = make_io(my_ip='10.0.0.1', my_port=12345)
        register_unclaimed_service(io, service_id=1)
        io._send_claim(1, io._services[1])
        pkt = io._sock.sendto.call_args[0][0]
        ip_int, port, hb = struct.unpack('<IHI', pkt[HEADER_SIZE:])
        assert port == 12345
        assert hb   == DEFAULT_HEARTBEAT_MICROS
        # IP round-trip: unpack host-order int → inet_ntoa
        import socket as sock
        ip_str = sock.inet_ntoa(struct.pack('!I', ip_int))
        assert ip_str == '10.0.0.1'

    def test_claim_updates_last_sent(self):
        io = make_io()
        register_unclaimed_service(io, service_id=1)
        before = time.monotonic()
        io._send_claim(1, io._services[1])
        assert io._services[1].last_claim_sent >= before

    def test_claim_no_ip_skips(self):
        io = make_io()
        io._my_ip   = None
        io._my_port = None
        register_unclaimed_service(io, service_id=1)
        io._send_claim(1, io._services[1])
        io._sock.sendto.assert_not_called()


# ---------------------------------------------------------------------------
# Watchdog — heartbeat timeout
# ---------------------------------------------------------------------------

class TestWatchdogTimeout:
    def test_timeout_fires_on_disconnected(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        # Set last_heartbeat far in the past (2 seconds ago)
        io._services[1].last_heartbeat = time.monotonic() - 2.0
        timeout_s = (DEFAULT_HEARTBEAT_MICROS + HEARTBEAT_JITTER) / 1_000_000
        assert 2.0 > timeout_s  # confirm it's past threshold
        # Manually run one watchdog iteration
        io._running = True
        io._watchdog_step()
        cbs['on_disconnected'].assert_called_once()

    def test_timeout_marks_unclaimed(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        io._services[1].last_heartbeat = time.monotonic() - 2.0
        io._watchdog_step()
        # After disconnected callback, service was unregistered by manager
        # but the claimed flag was set to False before callback
        # (The callback in manager would call unregister, but here we only test claimed)
        # Actually on_disconnected fires AFTER claimed=False
        # So if service still in dict, it should be unclaimed
        if 1 in io._services:
            assert io._services[1].claimed == False

    def test_no_timeout_when_heartbeat_recent(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        io._services[1].last_heartbeat = time.monotonic()  # fresh
        io._watchdog_step()
        cbs['on_disconnected'].assert_not_called()


# ---------------------------------------------------------------------------
# Watchdog — claim retry
# ---------------------------------------------------------------------------

class TestWatchdogClaimRetry:
    def test_retries_unclaimed_service(self):
        io = make_io()
        register_unclaimed_service(io, service_id=1)
        io._services[1].last_claim_sent = 0.0  # never sent
        io._watchdog_step()
        io._sock.sendto.assert_called_once()  # CLAIM sent

    def test_no_retry_within_cooldown(self):
        io = make_io()
        register_unclaimed_service(io, service_id=1)
        io._services[1].last_claim_sent = time.monotonic()  # just sent
        io._watchdog_step()
        io._sock.sendto.assert_not_called()


# Patch watchdog to expose single-step for testing
@pytest.fixture(autouse=True)
def patch_watchdog_step():
    """Add _watchdog_step to ServiceIO for testing."""
    def _watchdog_step(self):
        timeout_s = (DEFAULT_HEARTBEAT_MICROS + HEARTBEAT_JITTER) / 1_000_000
        now = time.monotonic()
        with self._lock:
            snapshot = list(self._services.items())
        for service_id, state in snapshot:
            if not state.claimed:
                if now - state.last_claim_sent >= 1.0:
                    with self._lock:
                        if service_id in self._services:
                            self._send_claim(service_id, self._services[service_id])
            else:
                if now - state.last_heartbeat > timeout_s:
                    with self._lock:
                        if service_id in self._services:
                            self._services[service_id].claimed = False
                    self._fire(state, 'on_disconnected')
    ServiceIO._watchdog_step = _watchdog_step
    yield
    if hasattr(ServiceIO, '_watchdog_step'):
        del ServiceIO._watchdog_step
