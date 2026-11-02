import fcntl
import socket
import struct
import threading
import logging

import cbor2

from .datatypes import (
    HEADER_SIZE, MULTICAST_PORT, SD_MULTICAST_ADDR,
    MAX_PACKET_SIZE, MessageType, unpack_header, pack_header,
    pack_description_request,
)
from .schema import ServiceSchema

log = logging.getLogger(__name__)


_SKIP_IFACE_PREFIXES = ('lo', 'docker', 'veth', 'virbr', 'br-', 'wg', 'tun', 'tap')


def _route_source_ip(destination: str) -> str:
    """Local address the kernel would send a packet to @p destination from.

    A UDP connect() only performs the route lookup (nothing is transmitted), so this
    picks the interface that can actually reach the service.

    Falls back to _primary_ip() when the host has no route at all.
    """
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            probe.connect((destination, 9))  # 'discard' port, only the route matters
            address = probe.getsockname()[0]
            if address and address != '0.0.0.0':
                return address
        finally:
            probe.close()
    except OSError:
        pass
    return _primary_ip()


def _primary_ip() -> str:
    """Fallback for _route_source_ip(): first plausible non-loopback interface address.

    Skips loopback and virtual interfaces (docker, veth, bridges, VPNs), mirroring
    io._get_primary_ip() to avoid a circular import.

    The discovery socket is bound to the multicast group, so its own address is useless
    as a reply target.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            for _idx, name in socket.if_nameindex():
                if any(name.startswith(prefix) for prefix in _SKIP_IFACE_PREFIXES):
                    continue
                try:
                    ifreq = struct.pack('16sH14s', name.encode(), socket.AF_INET, b'\x00' * 14)
                    result = fcntl.ioctl(sock, 0x8915, ifreq)  # SIOCGIFADDR
                    return socket.inet_ntoa(result[20:24])
                except OSError:
                    continue
        finally:
            sock.close()
    except Exception:
        pass
    return '127.0.0.1'


class ServiceDiscovery:
    """Listens for SERVICE_ADVERTISEMENT packets on the multicast group
    233.255.255.0:4242 and notifies registered listeners.

    Listener interface (duck-typed):
        def on_service_found(service_id, ip, port, schema: ServiceSchema): ...
    """

    #: How long to wait for the DESCRIPTION_PART answers of one window.
    FETCH_TIMEOUT_S = 1

    #: Upper bound on the windows one fetch may take (guards a misbehaving service).
    FETCH_MAX_WINDOWS = 16

    def __init__(self, bind_ip: str = '0.0.0.0', fetch_omitted: bool = True):
        self._bind_ip = bind_ip
        #: A service advertises only as much of its schema as fits into one packet and
        #: lists the rest under "omitted". Set False to keep the schema incomplete -
        #: clients that have generated classes do not need the blocks at all.
        self._fetch_omitted = fetch_omitted
        #: service_id -> {"version": int, "blocks": dict} so a repeating advertisement
        #: does not fetch again.
        self._omitted_cache: dict[int, dict] = {}
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
    # Omitted schema blocks (see DESCRIPTION_REQUEST in libxbot-service)
    # ------------------------------------------------------------------

    def _omitted_blocks(self, service_id: int, ip: str, port: int, version: int) -> dict | None:
        """Fetch the schema blocks the advertisement left out; None when it did not work."""
        cached = self._omitted_cache.get(service_id)
        if cached is not None and cached['version'] == version:
            return cached['blocks']

        target_ip = self._bind_ip
        if target_ip == '0.0.0.0':
            # The service has to send the answer somewhere: hand it the address this host
            # reaches the service from (the request payload carries ip:port, see
            # DescriptionRequestPayload).
            target_ip = _route_source_ip(ip)

        data = bytearray()
        total = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            sock.bind(('', 0))
            sock.settimeout(self.FETCH_TIMEOUT_S)
            my_port = sock.getsockname()[1]
            # Same convention as the claim payload: the wire carries the host-order
            # integer of the address (io._send_claim uses struct.unpack('!I', ...)).
            target_ip_host = struct.unpack('!I', socket.inet_aton(target_ip))[0]
            request = pack_description_request(service_id, target_ip_host, my_port)

            for _ in range(self.FETCH_MAX_WINDOWS):
                if total is not None and len(data) >= total:
                    break
                offset = len(data)
                header = pack_header(MessageType.DESCRIPTION_REQUEST, service_id, offset,
                                     MAX_PACKET_SIZE - HEADER_SIZE, 0, 0, len(request))
                sock.sendto(header + request, (ip, port))

                # One window per request: wait for the answer that starts at len(data).
                # Everything else (a stale window, a foreign packet) is skipped.
                while True:
                    try:
                        answer, _ = sock.recvfrom(MAX_PACKET_SIZE)
                    except socket.timeout:
                        return None
                    if len(answer) < HEADER_SIZE:
                        continue
                    hdr = unpack_header(answer)
                    if hdr['message_type'] != MessageType.DESCRIPTION_PART:
                        continue
                    if len(answer) != HEADER_SIZE + hdr['payload_size']:
                        continue
                    if hdr['arg1'] != len(data):
                        continue
                    total = hdr['arg2']
                    data += answer[HEADER_SIZE:]
                    break
                if total == 0:
                    return {}
        except OSError as e:
            log.warning(f"Service {service_id}: description fetch failed: {e}")
            return None
        finally:
            try:
                sock.close()
            except OSError:
                pass

        if total is None or len(data) != total:
            return None
        try:
            blocks = cbor2.loads(bytes(data))
        except Exception as e:
            log.warning(f"Service {service_id}: omitted schema is not valid CBOR: {e}")
            return None
        if not isinstance(blocks, dict):
            return None

        self._omitted_cache[service_id] = {'version': version, 'blocks': blocks}
        log.debug(f"Service {service_id}: fetched omitted schema blocks {list(blocks)}")
        return blocks

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
            if not isinstance(msg, dict):
                raise ValueError("advertisement payload is not a CBOR map")
        except Exception as e:
            log.warning(f"CBOR decode failed: {e}")
            return

        service_id = msg.get('sid')
        endpoint   = msg.get('endpoint', {})
        desc       = dict(msg.get('desc', {}))

        if service_id is None:
            return

        ip   = endpoint.get('ip', '')
        port = endpoint.get('port', 0)
        if not ip or not port:
            log.warning(f"Service {service_id} advertised with invalid endpoint, ignoring")
            return

        # The advertisement is one packet: whatever did not fit is fetched on demand.
        omitted = desc.get('omitted') or []
        if omitted and self._fetch_omitted:
            details = self._omitted_blocks(service_id, ip, port, desc.get('version', 0))
            if details is None:
                log.warning(
                    f"Service {service_id}: could not fetch the omitted schema blocks {omitted};"
                    " using the advertisement only")
            else:
                del desc['omitted']
                desc.update(details)
        elif 'omitted' in desc:
            del desc['omitted']
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
