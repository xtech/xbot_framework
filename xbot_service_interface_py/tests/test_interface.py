"""Tests for interface.py — ServiceInterface, RegisterProxy, TransactionContext."""
import struct
import threading
from unittest.mock import MagicMock, patch, call
import pytest

from xbot_service_interface.interface import ServiceInterface, RegisterProxy, _TransactionContext
from xbot_service_interface.schema import ServiceSchema
from xbot_service_interface.exceptions import UnknownChannelError, IncompatibleServiceError
from xbot_service_interface.serialization import pack_value
from tests.conftest import ECHO_DESC, ENUM_DESC


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_si(schema=None, connected=False, service_id=1):
    """Make a ServiceInterface with optional mock IO."""
    si = ServiceInterface(service_id=service_id, schema=schema)
    if connected:
        schema_obj = ServiceSchema.from_dict(ECHO_DESC) if schema is None else (
            schema if isinstance(schema, ServiceSchema) else ServiceSchema.from_dict(schema)
        )
        object.__setattr__(si, '_active_schema', schema_obj)
        object.__setattr__(si, '_endpoint', ('10.0.0.2', 9000))
        object.__setattr__(si, '_connected', True)
        mock_io = MagicMock()
        object.__setattr__(si, '_io', mock_io)
    return si


# ---------------------------------------------------------------------------
# Construction
# ---------------------------------------------------------------------------

class TestServiceInterfaceInit:
    def test_default_not_connected(self):
        si = ServiceInterface(service_id=1)
        assert si._connected == False
        assert si._active_schema is None
        assert si._schema is None

    def test_schema_from_dict(self):
        si = ServiceInterface(service_id=1, schema=ECHO_DESC)
        assert si._schema is not None
        assert si._schema.type == 'EchoService'

    def test_schema_from_service_schema(self):
        s = ServiceSchema.from_dict(ECHO_DESC)
        si = ServiceInterface(service_id=1, schema=s)
        assert si._schema is s

    def test_schema_from_file(self, tmp_path):
        import json
        p = tmp_path / 'svc.json'
        p.write_text(json.dumps(ECHO_DESC))
        si = ServiceInterface(service_id=1, schema=str(p))
        assert si._schema.type == 'EchoService'

    def test_registers_proxy_attached(self):
        si = ServiceInterface(service_id=1)
        assert isinstance(si.registers, RegisterProxy)

    def test_service_id_stored(self):
        si = ServiceInterface(service_id=42)
        assert si._service_id == 42

    def test_different_service_ids(self):
        a = ServiceInterface(service_id=1)
        b = ServiceInterface(service_id=2)
        assert a._service_id != b._service_id


# ---------------------------------------------------------------------------
# Dynamic attribute dispatch — on_*_changed
# ---------------------------------------------------------------------------

class TestOutputCallbacks:
    def test_assignment_style(self):
        si = ServiceInterface(service_id=1)
        cb = MagicMock()
        si.on_echo_changed = cb
        assert si._output_callbacks.get('echo') is cb

    def test_decorator_style(self):
        si = ServiceInterface(service_id=1)
        cb = MagicMock()
        ret = si.on_echo_changed(cb)
        assert ret is cb
        assert si._output_callbacks.get('echo') is cb

    def test_decorator_returns_original_function(self):
        si = ServiceInterface(service_id=1)
        def handler(v, ts): pass
        result = si.on_echo_changed(handler)
        assert result is handler

    def test_multi_word_output(self):
        si = ServiceInterface(service_id=1)
        cb = MagicMock()
        si.on_message_count_changed = cb
        assert si._output_callbacks.get('message_count') is cb

    def test_overwrite_callback(self):
        si = ServiceInterface(service_id=1)
        cb1, cb2 = MagicMock(), MagicMock()
        si.on_echo_changed = cb1
        si.on_echo_changed = cb2
        assert si._output_callbacks['echo'] is cb2

    def test_getattr_returns_callable(self):
        si = ServiceInterface(service_id=1)
        reg = si.on_echo_changed
        assert callable(reg)

    def test_unknown_attr_raises(self):
        si = ServiceInterface(service_id=1)
        with pytest.raises(AttributeError):
            _ = si.totally_unknown_attribute

    def test_on_output_by_id_assignment(self, echo_schema):
        si = make_si(connected=True)
        cb = MagicMock()
        si.on_output[0] = cb
        assert si._output_callbacks.get('echo') is cb

    def test_on_output_by_id_registrar(self, echo_schema):
        si = make_si(connected=True)
        cb = MagicMock()
        si.on_output[0](cb)
        assert si._output_callbacks.get('echo') is cb


# ---------------------------------------------------------------------------
# Dynamic attribute dispatch — send_*
# ---------------------------------------------------------------------------

class TestSendMethods:
    def test_send_returns_callable(self):
        si = ServiceInterface(service_id=1)
        assert callable(si.send_input_text)

    def test_send_before_connect_raises(self):
        si = ServiceInterface(service_id=1)
        with pytest.raises(RuntimeError, match='not connected'):
            si.send_input_text('hello')

    def test_send_calls_io(self):
        si = make_si(connected=True)
        si.send_input_text('hello')
        si._io.send_data.assert_called_once()
        args = si._io.send_data.call_args[0]
        assert args[0] == 1          # service_id
        assert args[1] == 0          # channel id for Input Text
        assert args[2] == b'hello'   # packed char array (no padding)

    def test_send_uint32(self):
        s = ServiceSchema.from_dict({
            'type': 'X', 'version': 1,
            'inputs': [{'id': 0, 'name': 'Count', 'type': 'uint32_t'}],
            'outputs': [], 'registers': [], 'enums': [],
        })
        si = ServiceInterface(service_id=1, schema=s)
        object.__setattr__(si, '_active_schema', s)
        object.__setattr__(si, '_endpoint', ('1.2.3.4', 100))
        object.__setattr__(si, '_connected', True)
        mock_io = MagicMock()
        object.__setattr__(si, '_io', mock_io)
        si.send_count(42)
        _, _, raw = mock_io.send_data.call_args[0]
        assert struct.unpack('<I', raw)[0] == 42

    def test_send_by_id(self):
        si = make_si(connected=True)
        sender = si.send_input[0]
        assert callable(sender)
        sender('test')
        si._io.send_data.assert_called_once()

    def test_send_unknown_channel_raises(self):
        si = make_si(connected=True)
        with pytest.raises(UnknownChannelError):
            si.send_nonexistent_channel('value')


# ---------------------------------------------------------------------------
# Lifecycle callbacks
# ---------------------------------------------------------------------------

class TestLifecycleCallbacks:
    def test_on_connected_decorator(self):
        si = ServiceInterface(service_id=1)
        cb = MagicMock()
        ret = si.on_connected(cb)
        assert ret is cb
        assert cb in si._connected_callbacks

    def test_on_disconnected_decorator(self):
        si = ServiceInterface(service_id=1)
        cb = MagicMock()
        si.on_disconnected(cb)
        assert cb in si._disconnected_callbacks

    def test_multiple_connected_callbacks(self):
        si = ServiceInterface(service_id=1)
        cb1, cb2, cb3 = MagicMock(), MagicMock(), MagicMock()
        si.on_connected(cb1)
        si.on_connected(cb2)
        si.on_connected(cb3)
        si._on_claim_ack()
        si._join_callbacks()
        cb1.assert_called_once()
        cb2.assert_called_once()
        cb3.assert_called_once()

    def test_on_claim_ack_sets_connected(self):
        si = ServiceInterface(service_id=1)
        assert not si._connected
        si._on_claim_ack()
        assert si._connected

    def test_on_disconnected_clears_connected(self):
        si = make_si(connected=True)
        si._on_disconnected()
        assert not si._connected

    def test_on_disconnected_fires_callbacks(self):
        si = ServiceInterface(service_id=1)
        cb = MagicMock()
        si.on_disconnected(cb)
        si._on_disconnected()
        si._join_callbacks()
        cb.assert_called_once()

    def test_connected_callback_exception_does_not_propagate(self):
        si = ServiceInterface(service_id=1)
        def bad_cb(): raise RuntimeError("boom")
        si.on_connected(bad_cb)
        si._on_claim_ack()  # should not raise
        assert si._connected

    def test_on_configured_decorator(self):
        si = ServiceInterface(service_id=1)
        cb = MagicMock()
        ret = si.on_configured(cb)
        assert ret is cb
        assert cb in si._configured_callbacks

    def test_on_configured_fires_on_config_request(self):
        si = make_si(connected=True)
        cb = MagicMock()
        si.on_configured(cb)
        si._on_config_request()
        si._join_callbacks()
        cb.assert_called_once()

    def test_on_configured_fires_even_with_no_registers(self):
        si = make_si(connected=True)
        cb = MagicMock()
        si.on_configured(cb)
        si._on_config_request()   # no registers set → empty send, but callback still fires
        si._join_callbacks()
        cb.assert_called_once()

    def test_on_claim_ack_does_not_auto_send_config(self):
        schema_obj = ServiceSchema.from_dict(ECHO_DESC)
        si = ServiceInterface(service_id=1)
        object.__setattr__(si, '_active_schema', schema_obj)
        mock_io = MagicMock()
        object.__setattr__(si, '_io', mock_io)
        si._register_values['Prefix'] = 'hello'
        si._on_claim_ack()
        mock_io.send_transaction.assert_not_called()


# ---------------------------------------------------------------------------
# _on_service_discovered — Mode 1 and Mode 2
# ---------------------------------------------------------------------------

class TestOnServiceDiscovered:
    def test_mode1_compatible_sets_schema(self):
        si = ServiceInterface(service_id=1, schema=ECHO_DESC)
        adv = ServiceSchema.from_dict(ECHO_DESC)
        si._on_service_discovered('10.0.0.2', 9000, adv)
        assert si._active_schema is si._schema
        assert si._endpoint == ('10.0.0.2', 9000)

    def test_mode1_incompatible_type_raises(self):
        si = ServiceInterface(service_id=1, schema=ECHO_DESC)
        wrong = ServiceSchema.from_dict({**ECHO_DESC, 'type': 'WrongService'})
        with pytest.raises(IncompatibleServiceError) as exc:
            si._on_service_discovered('10.0.0.2', 9000, wrong)
        assert exc.value.expected_type == 'EchoService'
        assert exc.value.found_type == 'WrongService'

    def test_mode1_incompatible_version_raises(self):
        si = ServiceInterface(service_id=1, schema=ECHO_DESC)
        wrong = ServiceSchema.from_dict({**ECHO_DESC, 'version': 99})
        with pytest.raises(IncompatibleServiceError) as exc:
            si._on_service_discovered('10.0.0.2', 9000, wrong)
        assert exc.value.found_version == 99

    def test_mode2_adopts_advertised_schema(self):
        si = ServiceInterface(service_id=1)
        adv = ServiceSchema.from_dict(ECHO_DESC)
        si._on_service_discovered('10.0.0.2', 9000, adv)
        assert si._active_schema is adv

    def test_mode2_sets_endpoint(self):
        si = ServiceInterface(service_id=1)
        adv = ServiceSchema.from_dict(ECHO_DESC)
        si._on_service_discovered('192.168.1.5', 12345, adv)
        assert si._endpoint == ('192.168.1.5', 12345)

    def test_by_id_callbacks_wired_after_discovery(self):
        si = ServiceInterface(service_id=1)
        cb = MagicMock()
        si._output_callbacks_by_id[0] = cb   # register before discovery
        adv = ServiceSchema.from_dict(ECHO_DESC)
        si._on_service_discovered('10.0.0.2', 9000, adv)
        # id 0 = 'echo'
        assert si._output_callbacks.get('echo') is cb
        assert si._output_callbacks_by_id == {}


# ---------------------------------------------------------------------------
# _on_data dispatch
# ---------------------------------------------------------------------------

class TestOnDataDispatch:
    def test_dispatches_string_output(self):
        si = make_si(connected=True)
        cb = MagicMock()
        si.on_echo_changed = cb
        raw = pack_value('char[100]', 'hello world')
        si._on_data(timestamp=999, target_id=0, payload=raw)
        si._join_callbacks()
        cb.assert_called_once_with('hello world', 999)

    def test_dispatches_uint32_output(self):
        si = make_si(connected=True)
        cb = MagicMock()
        si.on_message_count_changed = cb
        raw = pack_value('uint32_t', 42)
        si._on_data(timestamp=0, target_id=1, payload=raw)
        si._join_callbacks()
        cb.assert_called_once_with(42, 0)

    def test_unknown_target_id_ignored(self):
        si = make_si(connected=True)
        cb = MagicMock()
        si.on_echo_changed = cb
        raw = pack_value('uint32_t', 1)
        si._on_data(timestamp=0, target_id=99, payload=raw)
        cb.assert_not_called()

    def test_no_callback_registered_no_error(self):
        si = make_si(connected=True)
        raw = pack_value('char[100]', 'hi')
        si._on_data(timestamp=0, target_id=0, payload=raw)  # no callback registered

    def test_no_active_schema_ignored(self):
        si = ServiceInterface(service_id=1)
        si._on_data(timestamp=0, target_id=0, payload=b'\x00')  # no error

    def test_timestamp_passed_to_callback(self):
        si = make_si(connected=True)
        cb = MagicMock()
        si.on_message_count_changed = cb
        raw = pack_value('uint32_t', 7)
        si._on_data(timestamp=12345678, target_id=1, payload=raw)
        si._join_callbacks()
        assert cb.call_args[0][1] == 12345678


# ---------------------------------------------------------------------------
# _on_config_request
# ---------------------------------------------------------------------------

class TestOnConfigRequest:
    def test_sends_all_registers(self):
        si = make_si(connected=True)
        si._register_values['Prefix']    = 'hello'
        si._register_values['EchoCount'] = 3
        si._on_config_request()
        si._io.send_transaction.assert_called_once()
        _, chunks, kwargs_or_arg = (si._io.send_transaction.call_args[0] +
                                    (si._io.send_transaction.call_args[1].get('is_config', True),))
        assert kwargs_or_arg == True or si._io.send_transaction.call_args[1].get('is_config')

    def test_config_request_chunks_have_correct_ids(self):
        si = make_si(connected=True)
        si._register_values['Prefix']    = 'py: '
        si._register_values['EchoCount'] = 2
        si._on_config_request()
        _, chunks, *_ = si._io.send_transaction.call_args[0]
        ids = [c[0] for c in chunks]
        assert 0 in ids   # Prefix id=0
        assert 1 in ids   # EchoCount id=1

    def test_prefix_value_encoded_correctly(self):
        si = make_si(connected=True)
        si._register_values['Prefix'] = 'py: '
        si._on_config_request()
        _, chunks, *_ = si._io.send_transaction.call_args[0]
        prefix_chunk = next(c for c in chunks if c[0] == 0)
        assert prefix_chunk[1] == b'py: '

    def test_missing_required_register_warns(self, caplog):
        import logging
        si = make_si(connected=True)
        # Don't set Prefix (required)
        si._register_values['EchoCount'] = 1
        with caplog.at_level(logging.DEBUG, logger='xbot_service_interface.interface'):
            si._on_config_request()
        assert 'Prefix' in caplog.text

    def test_no_schema_no_crash(self):
        si = ServiceInterface(service_id=1)
        si._on_config_request()  # no active schema, should not raise

    def test_register_lookup_by_snake_name(self):
        si = make_si(connected=True)
        si._register_values['echo_count'] = 5   # snake key
        si._on_config_request()
        si._io.send_transaction.assert_called_once()


# ---------------------------------------------------------------------------
# RegisterProxy
# ---------------------------------------------------------------------------

class TestRegisterProxy:
    def test_set_and_get(self):
        si = ServiceInterface(service_id=1)
        si.registers['Prefix'] = 'hello'
        assert si.registers['Prefix'] == 'hello'
        assert si._register_values['Prefix'] == 'hello'

    def test_missing_key_raises(self):
        si = ServiceInterface(service_id=1)
        with pytest.raises(KeyError):
            _ = si.registers['NotSet']

    def test_contains(self):
        si = ServiceInterface(service_id=1)
        si.registers['X'] = 1
        assert 'X' in si.registers
        assert 'Y' not in si.registers

    def test_set_while_connected_does_not_auto_send(self):
        # Setting a register while connected does NOT auto-send —
        # caller must invoke send_config() explicitly.
        si = make_si(connected=True)
        si.registers['EchoCount'] = 3
        si._io.send_transaction.assert_not_called()

    def test_send_config_sends_all_registers(self):
        # send_config() must send ALL registers in a single config transaction
        # because the service resets all registers before applying.
        si = make_si(connected=True)
        si._register_values['Prefix']    = 'hello'
        si._register_values['EchoCount'] = 2
        si.send_config()
        si._io.send_transaction.assert_called_once()
        _, chunks, *_ = si._io.send_transaction.call_args[0]
        ids = {c[0] for c in chunks}
        assert ids == {0, 1}   # both Prefix (id=0) and EchoCount (id=1) sent

    def test_set_while_disconnected_does_not_send(self):
        si = ServiceInterface(service_id=1)
        mock_io = MagicMock()
        object.__setattr__(si, '_io', mock_io)
        si.registers['Prefix'] = 'pending'
        mock_io.send_transaction.assert_not_called()

    def test_set_multiple_values(self):
        si = ServiceInterface(service_id=1)
        si.registers['A'] = 1
        si.registers['B'] = 2
        assert si.registers['A'] == 1
        assert si.registers['B'] == 2


# ---------------------------------------------------------------------------
# TransactionContext
# ---------------------------------------------------------------------------

class TestTransactionContext:
    def test_buffers_sends(self):
        si = make_si(connected=True)
        with si.transaction():
            si.send_input_text('hello')
            # IO not called yet during transaction
            si._io.send_data.assert_not_called()

    def test_commits_on_exit(self):
        si = make_si(connected=True)
        with si.transaction():
            si.send_input_text('hello')
        si._io.send_transaction.assert_called_once()
        _, chunks = si._io.send_transaction.call_args[0][:2]
        assert len(chunks) == 1
        assert chunks[0][0] == 0     # input_text id=0
        assert chunks[0][1] == b'hello'

    def test_multiple_sends_in_one_transaction(self):
        s = ServiceSchema.from_dict({
            'type': 'X', 'version': 1,
            'inputs': [{'id': 0, 'name': 'A', 'type': 'uint32_t'},
                       {'id': 1, 'name': 'B', 'type': 'uint32_t'}],
            'outputs': [], 'registers': [], 'enums': [],
        })
        si = ServiceInterface(service_id=1, schema=s)
        object.__setattr__(si, '_active_schema', s)
        object.__setattr__(si, '_endpoint', ('1.2.3.4', 100))
        object.__setattr__(si, '_connected', True)
        mock_io = MagicMock()
        object.__setattr__(si, '_io', mock_io)

        with si.transaction():
            si.send_a(1)
            si.send_b(2)

        _, chunks = mock_io.send_transaction.call_args[0][:2]
        assert len(chunks) == 2

    def test_no_commit_on_exception(self):
        si = make_si(connected=True)
        try:
            with si.transaction():
                si.send_input_text('hello')
                raise ValueError("abort")
        except ValueError:
            pass
        si._io.send_transaction.assert_not_called()

    def test_transaction_flag_cleared_after_exit(self):
        si = make_si(connected=True)
        with si.transaction():
            assert si._transaction_active
        assert not si._transaction_active

    def test_transaction_flag_cleared_on_exception(self):
        si = make_si(connected=True)
        try:
            with si.transaction():
                raise RuntimeError("oops")
        except RuntimeError:
            pass
        assert not si._transaction_active

    def test_nested_transaction_raises(self):
        si = make_si(connected=True)
        with si.transaction():
            with pytest.raises(RuntimeError, match='already active'):
                with si.transaction():
                    pass
