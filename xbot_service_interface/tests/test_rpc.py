"""Tests for RPC system: schema parsing, IO send/recv, ServiceInterface dispatch."""
import struct
import threading
import time
from unittest.mock import MagicMock

import pytest

from xbot_service_interface.datatypes import (
    pack_header, pack_descriptor, HEADER_SIZE, DESCRIPTOR_SIZE, MessageType,
)
from xbot_service_interface.schema import ServiceSchema
from xbot_service_interface.exceptions import (
    RpcError, RpcBusyError, RpcTimeoutError, UnknownChannelError,
)
from xbot_service_interface.serialization import pack_value, unpack_value
from xbot_service_interface.interface import ServiceInterface
from tests.conftest import make_io, register_claimed_service, register_unclaimed_service


# ---------------------------------------------------------------------------
# Shared service descriptions
# ---------------------------------------------------------------------------

RPC_DESC = {
    'type': 'RpcService',
    'version': 1,
    'inputs':    [],
    'outputs':   [],
    'registers': [],
    'enums':     [],
    'functions': [
        {
            'id': 0,
            'name': 'NoParamsVoid',
            'parameters': [],
            'return_type': 'void',
        },
        {
            'id': 1,
            'name': 'ScalarReturn',
            'parameters': [
                {'id': 0, 'name': 'Speed',  'type': 'float'},
                {'id': 1, 'name': 'Count',  'type': 'uint32_t'},
            ],
            'return_type': 'int32_t',
        },
        {
            'id': 2,
            'name': 'ArrayParam',
            'parameters': [
                {'id': 0, 'name': 'Label', 'type': 'char[32]'},
            ],
            'return_type': 'void',
        },
        {
            'id': 3,
            'name': 'BoolReturn',
            'parameters': [
                {'id': 0, 'name': 'Enable', 'type': 'bool'},
            ],
            'return_type': 'bool',
        },
    ],
}

NO_FUNC_DESC = {
    'type': 'Plain',
    'version': 1,
    'inputs': [], 'outputs': [], 'registers': [], 'enums': [],
}


# ---------------------------------------------------------------------------
# Schema: function parsing
# ---------------------------------------------------------------------------

class TestSchemaFunctions:
    def test_functions_parsed(self):
        s = ServiceSchema.from_dict(RPC_DESC)
        assert len(s.functions) == 4

    def test_no_functions_key(self):
        s = ServiceSchema.from_dict(NO_FUNC_DESC)
        assert s.functions == []

    def test_function_fields(self):
        s = ServiceSchema.from_dict(RPC_DESC)
        fn = s.get_function(0)
        assert fn['id'] == 0
        assert fn['name'] == 'NoParamsVoid'
        assert fn['snake_name'] == 'no_params_void'
        assert fn['return_type'] == 'void'
        assert fn['parameters'] == []

    def test_function_with_scalar_params(self):
        s = ServiceSchema.from_dict(RPC_DESC)
        fn = s.get_function(1)
        assert len(fn['parameters']) == 2
        p0 = fn['parameters'][0]
        assert p0['id'] == 0
        assert p0['name'] == 'Speed'
        assert p0['type_str'] == 'float'
        assert p0['is_array'] is False
        assert p0['max_len'] is None

    def test_function_with_array_param(self):
        s = ServiceSchema.from_dict(RPC_DESC)
        fn = s.get_function(2)
        p = fn['parameters'][0]
        assert p['type_str'] == 'char[32]'
        assert p['base_type'] == 'char'
        assert p['is_array'] is True
        assert p['max_len'] == 32

    def test_function_bool_return(self):
        s = ServiceSchema.from_dict(RPC_DESC)
        fn = s.get_function(3)
        assert fn['return_type'] == 'bool'

    def test_lookup_by_id(self):
        s = ServiceSchema.from_dict(RPC_DESC)
        assert s.get_function(2)['name'] == 'ArrayParam'

    def test_lookup_by_name(self):
        s = ServiceSchema.from_dict(RPC_DESC)
        assert s.get_function('ScalarReturn')['id'] == 1

    def test_lookup_by_snake_name(self):
        s = ServiceSchema.from_dict(RPC_DESC)
        assert s.get_function('scalar_return')['id'] == 1

    def test_lookup_unknown_raises(self):
        s = ServiceSchema.from_dict(RPC_DESC)
        with pytest.raises(UnknownChannelError):
            s.get_function('nonexistent')

    def test_functions_property_returns_list(self):
        s = ServiceSchema.from_dict(RPC_DESC)
        fns = s.functions
        assert isinstance(fns, list)
        assert all('id' in f for f in fns)


# ---------------------------------------------------------------------------
# Serialization: bool type
# ---------------------------------------------------------------------------

class TestBoolSerialization:
    def test_pack_true(self):
        assert pack_value('bool', True) == b'\x01'

    def test_pack_false(self):
        assert pack_value('bool', False) == b'\x00'

    def test_unpack_true(self):
        assert unpack_value('bool', b'\x01') is True

    def test_unpack_false(self):
        assert unpack_value('bool', b'\x00') is False

    def test_roundtrip(self):
        for v in (True, False):
            assert unpack_value('bool', pack_value('bool', v)) == v


# ---------------------------------------------------------------------------
# IO: send_rpc_call
# ---------------------------------------------------------------------------

class TestIoSendRpcCall:
    def test_sends_correct_header(self):
        io = make_io()
        register_claimed_service(io, service_id=1)

        ok = io.send_rpc_call(service_id=1, function_id=2, call_id=7, params=b'')
        assert ok is True

        sent_data = io._sock.sendto.call_args[0][0]
        hdr_bytes = sent_data[:HEADER_SIZE]
        hdr = struct.unpack('<BBBBHBBHHQI', hdr_bytes)
        assert hdr[1] == int(MessageType.RPC_CALL)
        assert hdr[5] == 2   # arg1 = function_id
        assert hdr[7] == 7   # arg2 = call_id
        assert hdr[10] == 0  # payload_size

    def test_appends_params(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        params = b'\xAB\xCD\xEF'

        io.send_rpc_call(service_id=1, function_id=0, call_id=1, params=params)

        sent = io._sock.sendto.call_args[0][0]
        assert sent[HEADER_SIZE:] == params

    def test_returns_false_if_not_claimed(self):
        io = make_io()
        register_unclaimed_service(io, service_id=1)
        assert io.send_rpc_call(1, 0, 1, b'') is False

    def test_returns_false_if_unknown_service(self):
        io = make_io()
        assert io.send_rpc_call(99, 0, 1, b'') is False

    def test_increments_sequence_no(self):
        io = make_io()
        register_claimed_service(io, service_id=1)
        io.send_rpc_call(1, 0, 1, b'')
        io.send_rpc_call(1, 0, 2, b'')
        assert io._services[1].sequence_no == 2


# ---------------------------------------------------------------------------
# IO: handle RPC_RESPONSE packet
# ---------------------------------------------------------------------------

def make_rpc_response_packet(service_id, call_id, status, payload=b''):
    hdr = pack_header(
        MessageType.RPC_RESPONSE, service_id=service_id,
        arg1=status, arg2=call_id,
        sequence_no=0, timestamp=0, payload_size=len(payload),
    )
    return hdr + payload


class TestIoHandleRpcResponse:
    def test_fires_on_rpc_response_callback(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        cbs['on_rpc_response'] = MagicMock()

        pkt = make_rpc_response_packet(service_id=1, call_id=5, status=0, payload=b'\x01\x02')
        io._handle_packet(pkt)

        cbs['on_rpc_response'].assert_called_once_with(5, 0, b'\x01\x02')

    def test_not_fired_if_unclaimed(self):
        io = make_io()
        cbs = register_unclaimed_service(io, service_id=1)
        cbs['on_rpc_response'] = MagicMock()

        pkt = make_rpc_response_packet(service_id=1, call_id=1, status=0)
        io._handle_packet(pkt)

        cbs['on_rpc_response'].assert_not_called()

    def test_not_fired_for_unknown_service(self):
        io = make_io()
        pkt = make_rpc_response_packet(service_id=99, call_id=1, status=0)
        # Should not raise
        io._handle_packet(pkt)

    def test_status_and_payload_forwarded(self):
        io = make_io()
        cbs = register_claimed_service(io, service_id=1)
        cbs['on_rpc_response'] = MagicMock()

        payload = struct.pack('<i', -42)
        pkt = make_rpc_response_packet(service_id=1, call_id=3, status=0, payload=payload)
        io._handle_packet(pkt)

        _, kwargs = cbs['on_rpc_response'].call_args
        args = cbs['on_rpc_response'].call_args[0]
        assert args[0] == 3        # call_id
        assert args[1] == 0        # status
        assert args[2] == payload  # payload


# ---------------------------------------------------------------------------
# ServiceInterface: RPC dispatch
# ---------------------------------------------------------------------------

def make_rpc_si(connected=True):
    """ServiceInterface with RPC_DESC schema and mock IO."""
    schema = ServiceSchema.from_dict(RPC_DESC)
    si = ServiceInterface(service_id=1, schema=schema)
    if connected:
        object.__setattr__(si, '_active_schema', schema)
        object.__setattr__(si, '_endpoint', ('10.0.0.2', 9000))
        object.__setattr__(si, '_connected', True)
        mock_io = MagicMock()
        mock_io.send_rpc_call.return_value = True
        object.__setattr__(si, '_io', mock_io)
    return si


def _respond_async(si, call_id, status, payload=b'', delay=0.01):
    """Fire _on_rpc_response from a background thread after delay."""
    def _do():
        time.sleep(delay)
        si._on_rpc_response(call_id, status, payload)
    t = threading.Thread(target=_do, daemon=True)
    t.start()
    return t


class TestServiceInterfaceRpc:
    def test_call_method_exists(self):
        si = make_rpc_si()
        assert callable(si.call_no_params_void)

    def test_call_not_connected_raises(self):
        si = make_rpc_si(connected=False)
        with pytest.raises(RuntimeError, match='not connected'):
            si.call_no_params_void()

    def test_call_unknown_function_raises(self):
        si = make_rpc_si()
        with pytest.raises(UnknownChannelError):
            si.call_this_does_not_exist()

    def test_call_void_success_returns_none(self):
        si = make_rpc_si()
        _respond_async(si, call_id=1, status=0)
        result = si.call_no_params_void(timeout_ms=500)
        assert result is None

    def test_call_scalar_return(self):
        si = make_rpc_si()
        payload = struct.pack('<i', 42)
        _respond_async(si, call_id=1, status=0, payload=payload)
        result = si.call_scalar_return(1.0, 2, timeout_ms=500)
        assert result == 42

    def test_call_bool_return(self):
        si = make_rpc_si()
        _respond_async(si, call_id=1, status=0, payload=b'\x01')
        result = si.call_bool_return(True, timeout_ms=500)
        assert result is True

    def test_call_array_param_void(self):
        si = make_rpc_si()
        _respond_async(si, call_id=1, status=0)
        result = si.call_array_param('hello', timeout_ms=500)
        assert result is None

    def test_call_sends_rpc_call_to_io(self):
        si = make_rpc_si()
        _respond_async(si, call_id=1, status=0)
        si.call_no_params_void(timeout_ms=500)
        si._io.send_rpc_call.assert_called_once()
        args = si._io.send_rpc_call.call_args[0]
        assert args[0] == 1   # service_id
        assert args[1] == 0   # function_id (NoParamsVoid = 0)
        assert args[2] == 1   # call_id = first counter value

    def test_call_params_descriptor_framed(self):
        si = make_rpc_si()
        _respond_async(si, call_id=1, status=0, payload=struct.pack('<i', 0))
        si.call_scalar_return(2.5, 3, timeout_ms=500)

        params_bytes = si._io.send_rpc_call.call_args[0][3]
        # Should have 2 DataDescriptor-framed params
        offset = 0
        ids = []
        while offset + DESCRIPTOR_SIZE <= len(params_bytes):
            tid, _, size = struct.unpack_from('<HHI', params_bytes, offset)
            offset += DESCRIPTOR_SIZE
            ids.append(tid)
            offset += size
        assert ids == [0, 1]  # Speed=0, Count=1

    def test_call_timeout_raises(self):
        si = make_rpc_si()
        # No response sent — should time out
        with pytest.raises(RpcTimeoutError):
            si.call_no_params_void(timeout_ms=50)

    def test_call_status_busy_raises_rpc_busy(self):
        si = make_rpc_si()
        _respond_async(si, call_id=1, status=1)
        with pytest.raises(RpcBusyError):
            si.call_no_params_void(timeout_ms=500)

    def test_call_status_error_raises_rpc_error(self):
        si = make_rpc_si()
        _respond_async(si, call_id=1, status=2)
        with pytest.raises(RpcError):
            si.call_no_params_void(timeout_ms=500)

    def test_call_wrong_arg_count_raises(self):
        si = make_rpc_si()
        with pytest.raises(TypeError):
            si.call_scalar_return(1.0, timeout_ms=500)  # missing Count

    def test_call_counter_increments(self):
        si = make_rpc_si()
        _respond_async(si, call_id=1, status=0)
        si.call_no_params_void(timeout_ms=500)
        assert si._rpc_call_counter == 1

        _respond_async(si, call_id=2, status=0)
        si.call_no_params_void(timeout_ms=500)
        assert si._rpc_call_counter == 2

    def test_stale_response_ignored(self):
        si = make_rpc_si()
        # Inject a stale response before any call — should be silently dropped
        si._on_rpc_response(99, 0, b'')
        assert si._rpc_call_active is False

    def test_wrong_call_id_response_ignored(self):
        si = make_rpc_si()
        # Start a call but respond with wrong call_id — should time out
        def _wrong():
            time.sleep(0.01)
            si._on_rpc_response(999, 0, b'')  # wrong call_id
        threading.Thread(target=_wrong, daemon=True).start()
        with pytest.raises(RpcTimeoutError):
            si.call_no_params_void(timeout_ms=100)

    def test_io_send_fail_raises(self):
        si = make_rpc_si()
        si._io.send_rpc_call.return_value = False
        with pytest.raises(RuntimeError, match='Failed to send'):
            si.call_no_params_void(timeout_ms=500)

    def test_not_connected_no_io(self):
        si = make_rpc_si()
        object.__setattr__(si, '_io', None)
        with pytest.raises(RuntimeError):
            si.call_no_params_void(timeout_ms=100)

    def test_oversized_scalar_response_raises(self):
        si = make_rpc_si()
        # int32_t return but service sends 8 bytes — should raise RpcError
        _respond_async(si, call_id=1, status=0, payload=b'\x01\x02\x03\x04\x05\x06\x07\x08')
        with pytest.raises(RpcError):
            si.call_scalar_return(1.0, 2, timeout_ms=500)

    def test_undersized_scalar_response_raises(self):
        si = make_rpc_si()
        # int32_t return but service sends only 2 bytes — should raise RpcError
        _respond_async(si, call_id=1, status=0, payload=b'\x01\x02')
        with pytest.raises(RpcError):
            si.call_scalar_return(1.0, 2, timeout_ms=500)

    def test_undersized_array_response_valid(self):
        si = make_rpc_si()
        # char[32] param, void return — but check char return type works with short payload
        # Use BoolReturn (bool = 1 byte scalar) with exact size to confirm no false positive
        _respond_async(si, call_id=1, status=0, payload=b'\x01')
        result = si.call_bool_return(True, timeout_ms=500)
        assert result is True

    def test_oversized_array_response_raises(self):
        si = make_rpc_si()
        # ArrayParam has void return so use ScalarReturn for this — repurpose:
        # Inject a response bigger than bool (1 byte) for BoolReturn
        _respond_async(si, call_id=1, status=0, payload=b'\x01\x00\x00\x00\x00')
        with pytest.raises(RpcError):
            si.call_bool_return(True, timeout_ms=500)
