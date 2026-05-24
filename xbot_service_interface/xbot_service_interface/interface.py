import threading
import logging
from pathlib import Path
from typing import Union, Callable, Optional

from .exceptions import UnknownChannelError, IncompatibleServiceError, RpcError, RpcBusyError, RpcTimeoutError
from .schema import ServiceSchema
from .serialization import pack_value, unpack_value, sizeof_type, parse_type_string
from .datatypes import pack_descriptor

log = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# RegisterProxy
# ---------------------------------------------------------------------------

class RegisterProxy:
    """Dict-like interface for service registers.

    Usage:
        iface.registers['Prefix'] = "hello: "
        iface.registers['EchoCount'] = 2
        val = iface.registers['Prefix']

    Setting a register while the service is connected immediately sends a
    single-register config transaction. Otherwise the value is stored and
    sent on the next CONFIGURATION_REQUEST.
    """

    def __init__(self, si: 'ServiceInterface'):
        object.__setattr__(self, '_si', si)

    def __setitem__(self, name: str, value) -> None:
        si = object.__getattribute__(self, '_si')
        si._register_values[name] = value

        # Must send ALL registers in one config transaction — the service resets
        # all registers to defaults before applying the received transaction, so
        # sending only the changed register leaves required registers invalid.
        if si._connected and si._active_schema is not None and si._io is not None:
            si._on_config_request()

    def __getitem__(self, name: str):
        si = object.__getattribute__(self, '_si')
        if name not in si._register_values:
            raise KeyError(name)
        return si._register_values[name]

    def __contains__(self, name: str) -> bool:
        si = object.__getattribute__(self, '_si')
        return name in si._register_values


# ---------------------------------------------------------------------------
# _TransactionContext
# ---------------------------------------------------------------------------

class _TransactionContext:
    """Context manager for sending multiple inputs atomically."""

    def __init__(self, si: 'ServiceInterface'):
        self._si = si

    def __enter__(self) -> '_TransactionContext':
        si = self._si
        with si._lock:
            if si._transaction_active:
                raise RuntimeError("Transaction already active")
            si._transaction_active = True
            si._transaction_chunks = []
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> bool:
        si = self._si
        with si._lock:
            si._transaction_active = False
            chunks   = list(si._transaction_chunks)
            si._transaction_chunks = []
            connected = si._connected
            io        = si._io

        if exc_type is None and connected and chunks and io is not None:
            io.send_transaction(si._service_id, chunks)
        return False  # never suppress exceptions


# ---------------------------------------------------------------------------
# _ByIdProxy — access by numeric channel id
# ---------------------------------------------------------------------------

class _ByIdProxy:
    def __init__(self, si: 'ServiceInterface', kind: str):
        self._si   = si
        self._kind = kind  # 'input' or 'output'

    def __getitem__(self, channel_id: int):
        si = self._si
        if self._kind == 'input':
            def sender(value):
                schema = si._active_schema
                if schema is None:
                    raise RuntimeError("Service not connected")
                ch  = schema.get_input(channel_id)
                raw = pack_value(ch['type_str'], value, schema.enums_dict)
                si._send_data(ch['id'], raw)
            return sender
        else:
            def registrar(callback: Callable) -> Callable:
                schema = si._active_schema or si._schema
                if schema:
                    ch = schema.get_output(channel_id)
                    si._output_callbacks[ch['snake_name']] = callback
                else:
                    si._output_callbacks_by_id[channel_id] = callback
                return callback
            return registrar

    def __setitem__(self, channel_id: int, callback: Callable) -> None:
        if self._kind == 'output':
            si     = self._si
            schema = si._active_schema or si._schema
            if schema:
                ch = schema.get_output(channel_id)
                si._output_callbacks[ch['snake_name']] = callback
            else:
                si._output_callbacks_by_id[channel_id] = callback


# ---------------------------------------------------------------------------
# ServiceInterface
# ---------------------------------------------------------------------------

class ServiceInterface:
    """Interface for a single xBot service.

    Mode 1 (schema provided):
        iface = ServiceInterface(service_id=1, schema='echo_service.json')
        On discovery the advertised type+version is validated against the schema.

    Mode 2 (no schema):
        iface = ServiceInterface(service_id=1)
        The schema is taken from the service advertisement — all inputs, outputs,
        registers and enums are available.

    Dynamic attributes (resolved against active schema):
        iface.send_{input_snake_name}(value)      — send to a service input
        iface.on_{output_snake_name}_changed = cb  — register output callback
                                                     (also works as decorator)

    Lifecycle callbacks (use as decorator or direct call):
        @iface.on_connected
        def handler(): ...

    Register access:
        iface.registers['Prefix'] = "hello: "

    Atomic send:
        with iface.transaction():
            iface.send_input_text("hello")
            iface.send_other_input(42)
    """

    def __init__(self, service_id: int,
                 schema: Union[str, Path, dict, ServiceSchema, None] = None):
        # All private attrs go through object.__setattr__ to avoid triggering
        # our custom __setattr__ during __init__.
        object.__setattr__(self, '_service_id', service_id)

        if schema is None:
            _schema = None
        elif isinstance(schema, ServiceSchema):
            _schema = schema
        elif isinstance(schema, dict):
            _schema = ServiceSchema.from_dict(schema)
        else:
            _schema = ServiceSchema.from_file(schema)

        object.__setattr__(self, '_schema', _schema)         # user-provided (Mode 1) or None
        object.__setattr__(self, '_active_schema', None)     # set on discovery
        object.__setattr__(self, '_endpoint', None)          # (ip, port) set on discovery
        object.__setattr__(self, '_connected', False)
        object.__setattr__(self, '_io', None)                # injected by XbotServiceIo

        object.__setattr__(self, '_output_callbacks',       {})   # snake_name → callable
        object.__setattr__(self, '_output_callbacks_by_id', {})   # id → callable (pre-discovery)
        object.__setattr__(self, '_connected_callbacks',    [])
        object.__setattr__(self, '_disconnected_callbacks', [])

        object.__setattr__(self, '_register_values',  {})         # name → python value

        object.__setattr__(self, '_transaction_active', False)
        object.__setattr__(self, '_transaction_chunks', [])
        object.__setattr__(self, '_lock', threading.Lock())

        # RPC synchronization state
        object.__setattr__(self, '_rpc_condition',        threading.Condition())
        object.__setattr__(self, '_rpc_call_active',      False)
        object.__setattr__(self, '_rpc_call_counter',     0)
        object.__setattr__(self, '_pending_call_id',      0)
        object.__setattr__(self, '_rpc_response_status',  0)
        object.__setattr__(self, '_rpc_response_payload', b'')

        object.__setattr__(self, 'registers',   RegisterProxy(self))
        object.__setattr__(self, 'send_input',  _ByIdProxy(self, 'input'))
        object.__setattr__(self, 'on_output',   _ByIdProxy(self, 'output'))

    # ------------------------------------------------------------------
    # Lifecycle callbacks
    # ------------------------------------------------------------------

    def on_connected(self, callback: Callable) -> Callable:
        """Register connected callback. Use as decorator or direct call."""
        self._connected_callbacks.append(callback)
        return callback

    def on_disconnected(self, callback: Callable) -> Callable:
        """Register disconnected callback. Use as decorator or direct call."""
        self._disconnected_callbacks.append(callback)
        return callback

    # ------------------------------------------------------------------
    # Transaction context manager
    # ------------------------------------------------------------------

    def transaction(self) -> _TransactionContext:
        """Return a context manager that buffers send_* calls into one TRANSACTION."""
        return _TransactionContext(self)

    # ------------------------------------------------------------------
    # Dynamic attribute dispatch
    # ------------------------------------------------------------------

    def __getattr__(self, name: str):
        # send_{input_snake_name}(value)
        if name.startswith('send_'):
            snake = name[len('send_'):]
            si = self
            def sender(value):
                schema = object.__getattribute__(si, '_active_schema')
                if schema is None:
                    raise RuntimeError(
                        f"Service {si._service_id} not connected — cannot send {name!r}")
                ch  = schema.get_input(snake)
                raw = pack_value(ch['type_str'], value, schema.enums_dict)
                si._send_data(ch['id'], raw)
            return sender

        # call_{function_snake_name}(*params, timeout_ms=1000)
        if name.startswith('call_'):
            snake = name[len('call_'):]
            si = self
            def caller(*args, timeout_ms: int = 1000, _snake=snake):
                schema = object.__getattribute__(si, '_active_schema')
                if schema is None:
                    raise RuntimeError(
                        f"Service {si._service_id} not connected — cannot call {name!r}")
                fn = schema.get_function(_snake)
                return si._call_rpc(fn, args, timeout_ms)
            return caller

        # on_{output_snake_name}_changed  used as decorator:
        #   @iface.on_echo_changed
        #   def handler(value, ts): ...
        if name.startswith('on_') and name.endswith('_changed'):
            snake = name[len('on_'):-len('_changed')]
            si = self
            def registrar(callback: Callable) -> Callable:
                object.__getattribute__(si, '_output_callbacks')[snake] = callback
                return callback
            return registrar

        raise AttributeError(f"'{type(self).__name__}' has no attribute {name!r}")

    def __setattr__(self, name: str, value) -> None:
        # on_{output_snake_name}_changed = callback  (assignment style)
        if name.startswith('on_') and name.endswith('_changed'):
            snake = name[len('on_'):-len('_changed')]
            self._output_callbacks[snake] = value
            return
        object.__setattr__(self, name, value)

    # ------------------------------------------------------------------
    # Internal send helpers
    # ------------------------------------------------------------------

    def _send_data(self, channel_id: int, raw: bytes) -> None:
        with self._lock:
            if self._transaction_active:
                self._transaction_chunks.append((channel_id, raw))
                return
            if not self._connected or self._io is None:
                raise RuntimeError(f"Service {self._service_id} not connected")
        self._io.send_data(self._service_id, channel_id, raw)

    def _call_rpc(self, fn: dict, args: tuple, timeout_ms: int):
        """Serialize params, send RPC_CALL, block until response or timeout."""
        params = fn['parameters']
        if len(args) != len(params):
            raise TypeError(
                f"RPC {fn['name']!r} expects {len(params)} args, got {len(args)}")

        # Serialize parameters as DataDescriptor-framed bytes
        params_bytes = b''
        schema = self._active_schema
        enums  = schema.enums_dict if schema else {}
        for param, arg in zip(params, args, strict=True):
            raw = pack_value(param['type_str'], arg, enums)
            params_bytes += pack_descriptor(param['id'], len(raw)) + raw

        with self._rpc_condition:
            if self._rpc_call_active:
                raise RuntimeError(
                    f"RPC call already in progress for service {self._service_id}")
            counter = (self._rpc_call_counter + 1) & 0xFFFF
            object.__setattr__(self, '_rpc_call_counter', counter)
            object.__setattr__(self, '_pending_call_id',  counter)
            object.__setattr__(self, '_rpc_call_active',  True)

            if not self._io or not self._io.send_rpc_call(
                    self._service_id, fn['id'], counter, params_bytes):
                object.__setattr__(self, '_rpc_call_active', False)
                raise RuntimeError(
                    f"Failed to send RPC call {fn['name']!r} for service {self._service_id}")

            ok = self._rpc_condition.wait_for(
                lambda: not self._rpc_call_active,
                timeout=timeout_ms / 1000.0,
            )
            if not ok:
                object.__setattr__(self, '_rpc_call_active', False)
                raise RpcTimeoutError(
                    f"RPC call {fn['name']!r} timed out after {timeout_ms} ms")

            status  = self._rpc_response_status
            payload = self._rpc_response_payload

        if status == 1:
            raise RpcBusyError()
        if status != 0:
            raise RpcError(status)

        if fn['return_type'] == 'void':
            return None
        _, is_array, _ = parse_type_string(fn['return_type'])
        max_bytes = sizeof_type(fn['return_type'])
        if len(payload) > max_bytes:
            raise RpcError(
                f"RPC {fn['name']!r} response too large: got {len(payload)} bytes, max {max_bytes}")
        if not is_array and len(payload) < max_bytes:
            raise RpcError(
                f"RPC {fn['name']!r} response too small: got {len(payload)} bytes, need {max_bytes}")
        return unpack_value(fn['return_type'], payload, enums)

    # ------------------------------------------------------------------
    # Internal callbacks — called by XbotServiceIo
    # ------------------------------------------------------------------

    def _on_service_discovered(self, ip: str, port: int,
                                advertised_schema: ServiceSchema) -> None:
        provided = self._schema
        if provided is not None:
            # Mode 1: validate
            if not provided.is_compatible(advertised_schema.raw):
                raise IncompatibleServiceError(
                    expected_type=provided.type,
                    expected_version=provided.version,
                    found_type=advertised_schema.raw.get('type', '?'),
                    found_version=int(advertised_schema.raw.get('version', -1)),
                )
            active = provided
        else:
            # Mode 2: adopt advertised schema
            active = advertised_schema

        object.__setattr__(self, '_active_schema', active)
        object.__setattr__(self, '_endpoint', (ip, port))

        # Wire any by-id output callbacks registered before discovery
        for cid, cb in list(self._output_callbacks_by_id.items()):
            try:
                ch = active.get_output(cid)
                self._output_callbacks[ch['snake_name']] = cb
            except UnknownChannelError:
                pass
        self._output_callbacks_by_id.clear()

    def _on_claim_ack(self) -> None:
        object.__setattr__(self, '_connected', True)
        log.info(f"ServiceInterface {self._service_id} connected")
        for cb in self._connected_callbacks:
            try:
                cb()
            except Exception:
                log.exception("Error in on_connected callback")

    def _on_data(self, timestamp: int, target_id: int, payload: bytes) -> None:
        schema = self._active_schema
        if schema is None:
            return
        try:
            ch = schema.get_output(target_id)
        except UnknownChannelError:
            log.debug(f"No output channel id={target_id} in schema, ignoring")
            return
        cb = self._output_callbacks.get(ch['snake_name'])
        if cb is None:
            return
        try:
            value = unpack_value(ch['type_str'], payload, schema.enums_dict)
            cb(value, timestamp)
        except Exception:
            log.exception(f"Error dispatching data for channel {ch['name']!r}")

    def _on_transaction_start(self, timestamp: int) -> None:
        pass  # Reserved for future use

    def _on_transaction_end(self) -> None:
        pass

    def _on_config_request(self) -> None:
        schema = self._active_schema
        if schema is None:
            log.warning(f"Service {self._service_id} requested config but schema unavailable")
            return

        chunks = []
        for reg in schema.registers:
            # Accept original name or snake_name as key in _register_values
            value = self._register_values.get(reg['name'],
                    self._register_values.get(reg['snake_name']))
            if value is None:
                if not reg.get('optional', False):
                    log.warning(
                        f"Required register {reg['name']!r} not set "
                        f"for service {self._service_id}")
                continue
            try:
                raw = pack_value(reg['type_str'], value, schema.enums_dict)
                chunks.append((reg['id'], raw))
            except Exception as e:
                log.error(f"Cannot serialize register {reg['name']!r}: {e}")

        if chunks and self._io is not None:
            self._io.send_transaction(self._service_id, chunks, is_config=True)
            log.info(
                f"Sent configuration for service {self._service_id} "
                f"({len(chunks)} registers)")
        elif not chunks:
            log.debug(f"No registers to send for service {self._service_id}")

    def _on_rpc_response(self, call_id: int, status: int, payload: bytes) -> None:
        with self._rpc_condition:
            if not self._rpc_call_active or call_id != self._pending_call_id:
                log.debug(
                    f"Service {self._service_id}: unexpected RPC response "
                    f"call_id={call_id}, dropping")
                return
            object.__setattr__(self, '_rpc_response_status',  status)
            object.__setattr__(self, '_rpc_response_payload', payload)
            object.__setattr__(self, '_rpc_call_active',      False)
            self._rpc_condition.notify_all()

    def _on_disconnected(self) -> None:
        object.__setattr__(self, '_connected', False)
        log.info(f"ServiceInterface {self._service_id} disconnected")
        for cb in self._disconnected_callbacks:
            try:
                cb()
            except Exception:
                log.exception("Error in on_disconnected callback")
