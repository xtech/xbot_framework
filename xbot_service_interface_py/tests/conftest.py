"""Shared fixtures and helpers for xbot_service_interface tests."""
import struct
import threading
import sys
import os

import cbor2
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from xbot_service_interface.datatypes import (
    pack_header, HEADER_SIZE, MessageType,
)
from xbot_service_interface.schema import ServiceSchema
from xbot_service_interface.io import _ServiceState


# ---------------------------------------------------------------------------
# Shared schema dicts
# ---------------------------------------------------------------------------

ECHO_DESC = {
    'type': 'EchoService',
    'version': 1,
    'inputs':  [{'id': 0, 'name': 'Input Text',     'type': 'char[100]'}],
    'outputs': [{'id': 0, 'name': 'Echo',            'type': 'char[100]'},
                {'id': 1, 'name': 'Message Count',   'type': 'uint32_t'}],
    'registers': [{'id': 0, 'name': 'Prefix',    'type': 'char[42]'},
                  {'id': 1, 'name': 'EchoCount',  'type': 'uint32_t'}],
    'enums': [],
}

ENUM_DESC = {
    'type': 'EnumService',
    'version': 2,
    'inputs':  [{'id': 0, 'name': 'Mode', 'type': 'MotorMode'}],
    'outputs': [{'id': 0, 'name': 'State', 'type': 'MotorMode'}],
    'registers': [],
    'enums': [
        {
            'id': 'MotorMode',
            'base_type': 'uint8_t',
            'values': {'IDLE': 0, 'RUN': 1, 'BRAKE': 2},
            'bitmask': False,
        }
    ],
}

OPTIONAL_REG_DESC = {
    'type': 'OptService',
    'version': 1,
    'inputs': [],
    'outputs': [],
    'registers': [
        {'id': 0, 'name': 'Required', 'type': 'uint32_t'},
        {'id': 1, 'name': 'Optional', 'type': 'uint32_t', 'optional': True},
    ],
    'enums': [],
}


@pytest.fixture
def echo_schema():
    return ServiceSchema.from_dict(ECHO_DESC)


@pytest.fixture
def enum_schema():
    return ServiceSchema.from_dict(ENUM_DESC)


# ---------------------------------------------------------------------------
# Packet helpers
# ---------------------------------------------------------------------------

def make_packet(msg_type: MessageType, service_id: int, payload: bytes = b'',
                arg1: int = 0, arg2: int = 0, seq: int = 0) -> bytes:
    hdr = pack_header(msg_type, service_id=service_id, arg1=arg1, arg2=arg2,
                      sequence_no=seq, timestamp=0, payload_size=len(payload))
    return hdr + payload


def make_advertisement(service_id: int, ip: str, port: int, desc: dict) -> bytes:
    payload = cbor2.dumps({'sid': service_id,
                           'endpoint': {'ip': ip, 'port': port},
                           'desc': desc})
    return make_packet(MessageType.SERVICE_ADVERTISEMENT, service_id=0, payload=payload)


# ---------------------------------------------------------------------------
# Mock ServiceIO helper (no real sockets)
# ---------------------------------------------------------------------------

def make_io(my_ip='10.0.0.1', my_port=12345):
    """Return a ServiceIO instance with a mock socket, not started."""
    from unittest.mock import MagicMock
    from xbot_service_interface.io import ServiceIO

    io = ServiceIO.__new__(ServiceIO)
    io._bind_ip   = '0.0.0.0'
    io._sock      = MagicMock()
    io._my_ip     = my_ip
    io._my_port   = my_port
    io._running   = True
    io._lock      = threading.Lock()
    io._services  = {}
    io._recv_thread    = None
    io._watchdog_thread = None
    return io


def make_callbacks():
    from unittest.mock import MagicMock
    return {
        'on_claim_ack':        MagicMock(),
        'on_data':             MagicMock(),
        'on_transaction_start': MagicMock(),
        'on_transaction_end':  MagicMock(),
        'on_config_request':   MagicMock(),
        'on_disconnected':     MagicMock(),
    }


def register_claimed_service(io, service_id=1, ip='10.0.0.2', port=9000):
    """Register a pre-claimed service in io._services, return callbacks."""
    cbs = make_callbacks()
    state = _ServiceState(ip, port, cbs)
    state.claimed = True
    io._services[service_id] = state
    return cbs


def register_unclaimed_service(io, service_id=1, ip='10.0.0.2', port=9000):
    cbs = make_callbacks()
    state = _ServiceState(ip, port, cbs)
    state.claimed = False
    io._services[service_id] = state
    return cbs
