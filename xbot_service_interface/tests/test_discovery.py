"""Tests for discovery.py — advertisement parsing, listener callbacks, service tracking."""
import threading
from unittest.mock import MagicMock, call
import pytest
import cbor2

from xbot_service_interface.discovery import ServiceDiscovery
from xbot_service_interface.datatypes import (
    pack_header, HEADER_SIZE, MessageType,
)
from xbot_service_interface.schema import ServiceSchema
from tests.conftest import ECHO_DESC, make_advertisement


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_discovery():
    """Return a ServiceDiscovery with mock socket, not started."""
    sd = ServiceDiscovery.__new__(ServiceDiscovery)
    sd._bind_ip  = '0.0.0.0'
    sd._sock     = MagicMock()
    sd._thread   = None
    sd._running  = False
    sd._lock     = threading.Lock()
    sd._services = {}
    sd._listeners = []
    return sd


def make_listener():
    l = MagicMock()
    l.on_service_found = MagicMock()
    return l


# ---------------------------------------------------------------------------
# Advertisement packet handling
# ---------------------------------------------------------------------------

class TestHandleAdvertisement:
    def test_new_service_fires_listener(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        pkt = make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC)
        sd._handle_packet(pkt)
        listener.on_service_found.assert_called_once()
        args = listener.on_service_found.call_args[0]
        assert args[0] == 1            # service_id
        assert args[1] == '10.0.0.5'  # ip
        assert args[2] == 9000         # port

    def test_schema_passed_to_listener(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        pkt = make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC)
        sd._handle_packet(pkt)
        schema = listener.on_service_found.call_args[0][3]
        assert isinstance(schema, ServiceSchema)
        assert schema.type    == 'EchoService'
        assert schema.version == 1

    def test_advertised_schema_has_registers(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        pkt = make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC)
        sd._handle_packet(pkt)
        schema = listener.on_service_found.call_args[0][3]
        assert len(schema.registers) == 2

    def test_same_service_same_endpoint_no_duplicate(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        pkt = make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC)
        sd._handle_packet(pkt)
        sd._handle_packet(pkt)  # same packet again
        assert listener.on_service_found.call_count == 1

    def test_same_service_new_endpoint_fires_again(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        sd._handle_packet(make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC))
        sd._handle_packet(make_advertisement(1, '10.0.0.6', 9001, ECHO_DESC))
        assert listener.on_service_found.call_count == 2
        last_ip   = listener.on_service_found.call_args_list[-1][0][1]
        last_port = listener.on_service_found.call_args_list[-1][0][2]
        assert last_ip   == '10.0.0.6'
        assert last_port == 9001

    def test_multiple_services(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        sd._handle_packet(make_advertisement(1, '10.0.0.1', 9000, ECHO_DESC))
        sd._handle_packet(make_advertisement(2, '10.0.0.2', 9001, ECHO_DESC))
        assert listener.on_service_found.call_count == 2
        ids = {c[0][0] for c in listener.on_service_found.call_args_list}
        assert ids == {1, 2}

    def test_no_listeners_no_error(self):
        sd = make_discovery()
        pkt = make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC)
        sd._handle_packet(pkt)  # no crash

    def test_service_stored_in_cache(self):
        sd = make_discovery()
        pkt = make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC)
        sd._handle_packet(pkt)
        info = sd.get_service_info(1)
        assert info is not None
        assert info['ip']   == '10.0.0.5'
        assert info['port'] == 9000

    def test_get_service_info_returns_copy(self):
        sd = make_discovery()
        sd._handle_packet(make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC))
        info = sd.get_service_info(1)
        info['ip'] = '0.0.0.0'  # mutate copy
        assert sd._services[1]['ip'] == '10.0.0.5'  # original unchanged

    def test_unknown_service_returns_none(self):
        sd = make_discovery()
        assert sd.get_service_info(99) is None


# ---------------------------------------------------------------------------
# Malformed / ignored packets
# ---------------------------------------------------------------------------

class TestMalformedPackets:
    def test_too_short_ignored(self):
        sd = make_discovery()
        sd._handle_packet(b'\x00\x01\x02')

    def test_wrong_message_type_ignored(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        pkt = pack_header(MessageType.DATA, service_id=1, arg1=0, arg2=0,
                           sequence_no=0, timestamp=0, payload_size=0)
        sd._handle_packet(pkt)
        listener.on_service_found.assert_not_called()

    def test_malformed_cbor_ignored(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        garbage = b'\xFF\xFF\xFF\xFF\xFF'
        hdr = pack_header(MessageType.SERVICE_ADVERTISEMENT, service_id=0, arg1=0, arg2=0,
                           sequence_no=0, timestamp=0, payload_size=len(garbage))
        sd._handle_packet(hdr + garbage)
        listener.on_service_found.assert_not_called()

    def test_missing_endpoint_ip_ignored(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        bad = {'sid': 1, 'endpoint': {'ip': '', 'port': 9000}, 'desc': ECHO_DESC}
        payload = cbor2.dumps(bad)
        hdr = pack_header(MessageType.SERVICE_ADVERTISEMENT, service_id=0, arg1=0, arg2=0,
                           sequence_no=0, timestamp=0, payload_size=len(payload))
        sd._handle_packet(hdr + payload)
        listener.on_service_found.assert_not_called()

    def test_missing_endpoint_port_ignored(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        bad = {'sid': 1, 'endpoint': {'ip': '10.0.0.1', 'port': 0}, 'desc': ECHO_DESC}
        payload = cbor2.dumps(bad)
        hdr = pack_header(MessageType.SERVICE_ADVERTISEMENT, service_id=0, arg1=0, arg2=0,
                           sequence_no=0, timestamp=0, payload_size=len(payload))
        sd._handle_packet(hdr + payload)
        listener.on_service_found.assert_not_called()

    def test_missing_sid_ignored(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        bad = {'endpoint': {'ip': '10.0.0.1', 'port': 9000}, 'desc': ECHO_DESC}
        payload = cbor2.dumps(bad)
        hdr = pack_header(MessageType.SERVICE_ADVERTISEMENT, service_id=0, arg1=0, arg2=0,
                           sequence_no=0, timestamp=0, payload_size=len(payload))
        sd._handle_packet(hdr + payload)
        listener.on_service_found.assert_not_called()

    def test_size_mismatch_ignored(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        pkt = make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC)
        # Truncate payload
        sd._handle_packet(pkt[:-5])
        listener.on_service_found.assert_not_called()


# ---------------------------------------------------------------------------
# Listener management
# ---------------------------------------------------------------------------

class TestListenerManagement:
    def test_register_listener(self):
        sd = make_discovery()
        l = make_listener()
        sd.register_listener(l)
        assert l in sd._listeners

    def test_register_same_listener_twice(self):
        sd = make_discovery()
        l = make_listener()
        sd.register_listener(l)
        sd.register_listener(l)
        assert sd._listeners.count(l) == 1

    def test_unregister_listener(self):
        sd = make_discovery()
        l = make_listener()
        sd.register_listener(l)
        sd.unregister_listener(l)
        assert l not in sd._listeners

    def test_register_replays_existing_services(self):
        sd = make_discovery()
        sd._handle_packet(make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC))
        l = make_listener()
        sd.register_listener(l)
        # Should immediately receive already-known service
        l.on_service_found.assert_called_once()
        assert l.on_service_found.call_args[0][0] == 1

    def test_multiple_listeners_all_notified(self):
        sd = make_discovery()
        l1, l2 = make_listener(), make_listener()
        sd.register_listener(l1)
        sd.register_listener(l2)
        sd._handle_packet(make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC))
        l1.on_service_found.assert_called_once()
        l2.on_service_found.assert_called_once()

    def test_unregistered_listener_not_notified(self):
        sd = make_discovery()
        l = make_listener()
        sd.register_listener(l)
        sd.unregister_listener(l)
        sd._handle_packet(make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC))
        l.on_service_found.assert_not_called()

    def test_listener_exception_does_not_block_others(self):
        sd = make_discovery()
        bad = MagicMock()
        bad.on_service_found.side_effect = RuntimeError("boom")
        good = make_listener()
        sd.register_listener(bad)
        sd.register_listener(good)
        sd._handle_packet(make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC))
        good.on_service_found.assert_called_once()


# ---------------------------------------------------------------------------
# drop_service
# ---------------------------------------------------------------------------

class TestDropService:
    def test_drop_removes_from_cache(self):
        sd = make_discovery()
        sd._handle_packet(make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC))
        sd.drop_service(1)
        assert sd.get_service_info(1) is None

    def test_drop_allows_rediscovery(self):
        sd = make_discovery()
        listener = make_listener()
        sd.register_listener(listener)
        sd._handle_packet(make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC))
        sd.drop_service(1)
        sd._handle_packet(make_advertisement(1, '10.0.0.5', 9000, ECHO_DESC))
        assert listener.on_service_found.call_count == 2

    def test_drop_nonexistent_no_error(self):
        sd = make_discovery()
        sd.drop_service(99)  # must not raise
