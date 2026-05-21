import fcntl
import socket
import struct
import threading
import logging
import time
from typing import Optional

from .datatypes import (
    HEADER_SIZE, DESCRIPTOR_SIZE, MessageType,
    DEFAULT_HEARTBEAT_MICROS, HEARTBEAT_JITTER, MAX_PACKET_SIZE, PROTOCOL_VERSION,
    unpack_header, pack_header, unpack_descriptor,
    pack_descriptor, pack_claim_payload,
)

log = logging.getLogger(__name__)


_SKIP_IFACE_PREFIXES = ('lo', 'docker', 'veth', 'virbr', 'br-', 'wg', 'tun', 'tap')
_SIOCGIFADDR = 0x8915  # Linux ioctl: get interface address


def _get_primary_ip() -> str:
    """Return primary non-loopback IP by iterating interfaces.

    Mirrors C++ get_ip(): uses SIOCGIFADDR ioctl, skips loopback and
    virtual interfaces (docker, veth, virbr, br-, wg, tun, tap).
    """
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            for _idx, name in socket.if_nameindex():
                if any(name.startswith(p) for p in _SKIP_IFACE_PREFIXES):
                    continue
                try:
                    # ifreq: 16-byte name + sockaddr (sin_addr at offset 20)
                    ifreq = struct.pack('16sH14s', name.encode(), socket.AF_INET, b'\x00' * 14)
                    result = fcntl.ioctl(s, _SIOCGIFADDR, ifreq)
                    return socket.inet_ntoa(result[20:24])
                except OSError:
                    continue
        finally:
            s.close()
    except Exception:
        pass
    return '127.0.0.1'


class _ServiceState:
    __slots__ = ('ip', 'port', 'callbacks', 'claimed',
                 'last_claim_sent', 'last_heartbeat', 'sequence_no')

    def __init__(self, ip: str, port: int, callbacks: dict):
        self.ip              = ip
        self.port            = port
        self.callbacks       = callbacks
        self.claimed         = False
        self.last_claim_sent = 0.0   # monotonic seconds
        self.last_heartbeat  = time.monotonic()
        self.sequence_no     = 0


class ServiceIO:
    """Low-level unicast UDP IO layer.

    Handles: socket, claim, heartbeat watchdog, recv dispatch.
    Thread-safe: all public methods may be called from any thread.
    """

    def __init__(self, bind_ip: str = '0.0.0.0'):
        self._bind_ip = bind_ip
        self._sock: Optional[socket.socket] = None
        self._my_ip: Optional[str] = None
        self._my_port: Optional[int] = None
        self._running = False
        self._lock = threading.Lock()
        self._services: dict[int, _ServiceState] = {}
        self._recv_thread: Optional[threading.Thread] = None
        self._watchdog_thread: Optional[threading.Thread] = None

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def start(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        self._sock.bind((self._bind_ip, 0))
        self._sock.settimeout(1.0)

        addr = self._sock.getsockname()
        self._my_port = addr[1]
        self._my_ip   = addr[0] if addr[0] != '0.0.0.0' else _get_primary_ip()
        log.info(f"ServiceIO started, endpoint: {self._my_ip}:{self._my_port}")

        self._running = True
        self._recv_thread = threading.Thread(
            target=self._recv_loop, daemon=True, name='xbot-io-recv')
        self._watchdog_thread = threading.Thread(
            target=self._watchdog_loop, daemon=True, name='xbot-io-watchdog')
        self._recv_thread.start()
        self._watchdog_thread.start()

    def stop(self) -> None:
        self._running = False
        for t in (self._recv_thread, self._watchdog_thread):
            if t:
                t.join(timeout=3.0)
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
        log.info("ServiceIO stopped")

    def ok(self) -> bool:
        return self._running

    # ------------------------------------------------------------------
    # Service registration
    # ------------------------------------------------------------------

    def register_service(self, service_id: int, ip: str, port: int,
                          callbacks: dict) -> None:
        with self._lock:
            self._services[service_id] = _ServiceState(ip, port, callbacks)

    def unregister_service(self, service_id: int) -> None:
        with self._lock:
            self._services.pop(service_id, None)

    def get_endpoint(self) -> tuple[str, int]:
        return self._my_ip, self._my_port

    # ------------------------------------------------------------------
    # Sending
    # ------------------------------------------------------------------

    def send_data(self, service_id: int, target_id: int, payload: bytes) -> bool:
        with self._lock:
            state = self._services.get(service_id)
            if state is None or not state.claimed:
                return False
            ip, port = state.ip, state.port
            seq = state.sequence_no
            state.sequence_no = (seq + 1) & 0xFFFF

        hdr = pack_header(
            message_type=MessageType.DATA,
            service_id=service_id,
            arg1=0, arg2=target_id,
            sequence_no=seq,
            timestamp=self._now_us(),
            payload_size=len(payload),
        )
        return self._transmit(ip, port, hdr + payload)

    def send_transaction(self, service_id: int,
                          chunks: list[tuple[int, bytes]],
                          is_config: bool = False) -> bool:
        with self._lock:
            state = self._services.get(service_id)
            if state is None or not state.claimed:
                return False
            ip, port = state.ip, state.port
            seq = state.sequence_no
            state.sequence_no = (seq + 1) & 0xFFFF

        body = b''.join(pack_descriptor(tid, len(data)) + data for tid, data in chunks)
        hdr = pack_header(
            message_type=MessageType.TRANSACTION,
            service_id=service_id,
            arg1=1 if is_config else 0,
            arg2=0,
            sequence_no=seq,
            timestamp=self._now_us(),
            payload_size=len(body),
        )
        return self._transmit(ip, port, hdr + body)

    # ------------------------------------------------------------------
    # Internal: receive loop
    # ------------------------------------------------------------------

    def _recv_loop(self) -> None:
        while self._running:
            try:
                data, _ = self._sock.recvfrom(MAX_PACKET_SIZE)
            except socket.timeout:
                continue
            except OSError:
                if self._running:
                    log.exception("IO recv socket error")
                break
            try:
                self._handle_packet(data)
            except Exception:
                log.exception("Error handling IO packet")

    def _handle_packet(self, data: bytes) -> None:
        if len(data) < HEADER_SIZE:
            log.warning("IO packet too short, ignoring")
            return
        hdr = unpack_header(data)
        if len(data) != HEADER_SIZE + hdr['payload_size']:
            log.debug("IO packet size mismatch, ignoring")
            return

        service_id = hdr['service_id']
        payload    = data[HEADER_SIZE:]
        msg_type   = hdr['message_type']

        with self._lock:
            state = self._services.get(service_id)
        if state is None:
            return

        if msg_type == MessageType.CLAIM:
            if hdr['arg1'] == 1:
                self._handle_claim_ack(service_id, state)

        elif msg_type == MessageType.DATA:
            if state.claimed:
                self._fire(state, 'on_data', hdr['timestamp'], hdr['arg2'], payload)

        elif msg_type == MessageType.TRANSACTION:
            if state.claimed and hdr['arg1'] == 0:
                self._handle_transaction(state, hdr, payload)

        elif msg_type == MessageType.HEARTBEAT:
            with self._lock:
                if service_id in self._services:
                    self._services[service_id].last_heartbeat = time.monotonic()

        elif msg_type == MessageType.CONFIGURATION_REQUEST:
            if state.claimed:
                self._fire(state, 'on_config_request')

        else:
            log.debug(f"Unknown message type 0x{msg_type:02X} from service {service_id}")

    def _handle_claim_ack(self, service_id: int, state: _ServiceState) -> None:
        with self._lock:
            if service_id not in self._services:
                return
            if self._services[service_id].claimed:
                return
            self._services[service_id].claimed         = True
            self._services[service_id].last_heartbeat  = time.monotonic()
        log.info(f"Service {service_id} claimed successfully")
        self._fire(state, 'on_claim_ack')

    def _handle_transaction(self, state: _ServiceState, hdr: dict,
                             payload: bytes) -> None:
        self._fire(state, 'on_transaction_start', hdr['timestamp'])
        offset = 0
        while offset + DESCRIPTOR_SIZE <= len(payload):
            desc   = unpack_descriptor(payload, offset)
            offset += DESCRIPTOR_SIZE
            size   = desc['payload_size']
            if offset + size > len(payload):
                log.error("Transaction chunk overflows payload, aborting")
                break
            self._fire(state, 'on_data', hdr['timestamp'], desc['target_id'],
                       payload[offset:offset + size])
            offset += size
        self._fire(state, 'on_transaction_end')

    # ------------------------------------------------------------------
    # Internal: watchdog (claim retries + heartbeat timeouts)
    # ------------------------------------------------------------------

    def _watchdog_loop(self) -> None:
        timeout_s = (DEFAULT_HEARTBEAT_MICROS + HEARTBEAT_JITTER) / 1_000_000

        while self._running:
            time.sleep(1.0)
            if not self._running:
                break

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
                        log.warning(f"Service {service_id} heartbeat timeout")
                        with self._lock:
                            if service_id in self._services:
                                self._services[service_id].claimed = False
                        self._fire(state, 'on_disconnected')

    def _send_claim(self, service_id: int, state: _ServiceState) -> None:
        if not self._my_ip or not self._my_port:
            return
        ip_int = struct.unpack('!I', socket.inet_aton(self._my_ip))[0]
        claim  = pack_claim_payload(ip_int, self._my_port, DEFAULT_HEARTBEAT_MICROS)

        seq = state.sequence_no
        state.sequence_no = (seq + 1) & 0xFFFF
        state.last_claim_sent = time.monotonic()

        hdr = pack_header(
            message_type=MessageType.CLAIM,
            service_id=service_id,
            arg1=0, arg2=0,
            sequence_no=seq,
            timestamp=self._now_us(),
            payload_size=len(claim),
        )
        log.info(f"Claiming service {service_id} at {state.ip}:{state.port}")
        self._transmit(state.ip, state.port, hdr + claim)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _fire(state: _ServiceState, event: str, *args) -> None:
        cb = state.callbacks.get(event)
        if cb:
            try:
                cb(*args)
            except Exception:
                log.exception(f"Error in {event!r} callback for service")

    def _transmit(self, ip: str, port: int, data: bytes) -> bool:
        try:
            self._sock.sendto(data, (ip, port))
            return True
        except Exception as e:
            log.error(f"Send to {ip}:{port} failed: {e}")
            return False

    @staticmethod
    def _now_us() -> int:
        return int(time.monotonic() * 1_000_000)
