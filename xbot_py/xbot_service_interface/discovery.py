import socket
import struct
import threading
import logging

import cbor2

from .datatypes import (
    HEADER_SIZE, MULTICAST_PORT, SD_MULTICAST_ADDR,
    MAX_PACKET_SIZE, MessageType, unpack_header,
)
from .schema import ServiceSchema

log = logging.getLogger(__name__)


class ServiceDiscovery:
    """Listens for SERVICE_ADVERTISEMENT packets on the multicast group
    233.255.255.0:4242 and notifies registered listeners.

    Listener interface (duck-typed):
        def on_service_found(service_id, ip, port, schema: ServiceSchema): ...
    """

    def __init__(self, bind_ip: str = '0.0.0.0'):
        self._bind_ip = bind_ip
        self._sock: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._running = False
        self._lock = threading.Lock()
        self._services: dict[int, dict] = {}   # service_id → {ip, port, schema}
        self._listeners: list = []

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def register_listener(self, listener) -> None:
        with self._lock:
            if listener not in self._listeners:
                self._listeners.append(listener)
                # Replay already-discovered services to the new listener
                for sid, info in self._services.items():
                    try:
                        listener.on_service_found(sid, info['ip'], info['port'], info['schema'])
                    except Exception:
                        log.exception("Error replaying service to new listener")

    def unregister_listener(self, listener) -> None:
        with self._lock:
            self._listeners = [l for l in self._listeners if l is not listener]

    def get_service_info(self, service_id: int) -> dict | None:
        with self._lock:
            info = self._services.get(service_id)
            return dict(info) if info else None

    def drop_service(self, service_id: int) -> None:
        with self._lock:
            self._services.pop(service_id, None)

    def start(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except AttributeError:
            pass  # Not available on all platforms

        # Bind to the multicast address on the discovery port (same as C++)
        self._sock.bind((SD_MULTICAST_ADDR, MULTICAST_PORT))
        self._sock.settimeout(1.0)

        # Join multicast group on the specified interface
        iface_addr = self._bind_ip if self._bind_ip != '0.0.0.0' else '0.0.0.0'
        mreq = struct.pack('4s4s',
                           socket.inet_aton(SD_MULTICAST_ADDR),
                           socket.inet_aton(iface_addr))
        self._sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

        self._running = True
        self._thread = threading.Thread(
            target=self._recv_loop, daemon=True, name='xbot-discovery')
        self._thread.start()
        log.info(f"ServiceDiscovery started on {SD_MULTICAST_ADDR}:{MULTICAST_PORT}")

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=3.0)
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
        log.info("ServiceDiscovery stopped")

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    def _recv_loop(self) -> None:
        while self._running:
            try:
                data, _ = self._sock.recvfrom(MAX_PACKET_SIZE)
            except socket.timeout:
                continue
            except OSError:
                if self._running:
                    log.exception("Discovery socket error")
                break
            try:
                self._handle_packet(data)
            except Exception:
                log.exception("Error handling discovery packet")

    def _handle_packet(self, data: bytes) -> None:
        if len(data) < HEADER_SIZE:
            return

        hdr = unpack_header(data)
        if hdr['message_type'] != MessageType.SERVICE_ADVERTISEMENT:
            return
        if len(data) != HEADER_SIZE + hdr['payload_size']:
            log.warning("Advertisement packet size mismatch")
            return

        payload = data[HEADER_SIZE:]
        try:
            msg = cbor2.loads(payload)
        except Exception as e:
            log.warning(f"CBOR decode failed: {e}")
            return

        service_id = msg.get('sid')
        endpoint   = msg.get('endpoint', {})
        desc       = msg.get('desc', {})

        if service_id is None:
            return

        ip   = endpoint.get('ip', '')
        port = endpoint.get('port', 0)
        if not ip or not port:
            log.warning(f"Service {service_id} advertised with invalid endpoint, ignoring")
            return

        try:
            schema = ServiceSchema.from_dict(desc)
        except Exception as e:
            log.warning(f"Failed to parse service description for {service_id}: {e}")
            return

        listeners_to_notify = []
        with self._lock:
            existing = self._services.get(service_id)
            if existing:
                if existing['ip'] == ip and existing['port'] == port:
                    return  # Nothing changed
                log.info(f"Service {service_id} endpoint changed to {ip}:{port}")
            else:
                log.info(f"New service: {schema.type!r} id={service_id} at {ip}:{port}")

            self._services[service_id] = {'ip': ip, 'port': port, 'schema': schema}
            listeners_to_notify = list(self._listeners)

        for listener in listeners_to_notify:
            try:
                listener.on_service_found(service_id, ip, port, schema)
            except Exception:
                log.exception("Error notifying discovery listener")
