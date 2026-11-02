"""The advertisement is one packet, so a service advertises only as much of its schema as
fits and lists the rest under "omitted". A client that needs those blocks (one without
generated classes) fetches them with DESCRIPTION_REQUEST / DESCRIPTION_PART.
"""

import socket
import struct
import threading

import cbor2

from xbot_service_interface.datatypes import (
    HEADER_SIZE, MAX_PACKET_SIZE, MessageType, pack_header, unpack_header,
)
from xbot_service_interface.discovery import ServiceDiscovery

SERVICE_ID = 13
ENUMS = {'enums': [{'id': 'SoundId', 'base_type': 'uint8_t',
                    'values': {'BOOT_PING': 0, 'MOWING_START': 14, 'RECOVERED': 31}}]}


class FakeService:
    """Answers DESCRIPTION_REQUEST with `window` bytes per packet."""

    def __init__(self, payload: dict, window: int = 40):
        self.blob = cbor2.dumps(payload)
        self.window = window
        self.requests: list[tuple[int, int]] = []
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1', 0))
        self.sock.settimeout(0.2)
        self.port = self.sock.getsockname()[1]
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _serve(self) -> None:
        while True:
            try:
                data, _ = self.sock.recvfrom(MAX_PACKET_SIZE)
            except OSError:
                return
            hdr = unpack_header(data)
            if hdr['message_type'] != MessageType.DESCRIPTION_REQUEST:
                continue
            sid, ip, port = struct.unpack('<HIH', data[HEADER_SIZE:])
            if sid != SERVICE_ID:
                continue
            self.requests.append((hdr['arg1'], hdr['arg2']))
            chunk = self.blob[hdr['arg1']:hdr['arg1'] + self.window]
            answer = pack_header(MessageType.DESCRIPTION_PART, SERVICE_ID, hdr['arg1'],
                                 len(self.blob), 0, 0, len(chunk)) + chunk
            # target_ip is the host-order integer of the address (ClaimPayload convention).
            self.sock.sendto(answer, (socket.inet_ntoa(struct.pack('!I', ip)), port))

    def advertisement(self, omitted: list[str] | None = None) -> bytes:
        desc = {'type': 'SoundService', 'version': 1,
                'inputs': [{'id': 0, 'name': 'Volume', 'type': 'uint8_t'}],
                'functions': []}
        desc['omitted'] = ['enums'] if omitted is None else omitted
        adv = {'sid': SERVICE_ID, 'endpoint': {'ip': '127.0.0.1', 'port': self.port}, 'desc': desc}
        body = cbor2.dumps(adv)
        return pack_header(MessageType.SERVICE_ADVERTISEMENT, SERVICE_ID, 0, 0, 0, 0, len(body)) + body

    def close(self) -> None:
        self.sock.close()


class Collector:
    def __init__(self):
        self.found = []

    def on_service_found(self, service_id, ip, port, schema):
        self.found.append(schema)


def _discovery(service: FakeService) -> tuple[ServiceDiscovery, Collector]:
    discovery = ServiceDiscovery(bind_ip='127.0.0.1')
    collector = Collector()
    discovery.register_listener(collector)
    return discovery, collector


def test_omitted_blocks_are_fetched_in_windows():
    service = FakeService(ENUMS, window=40)
    discovery, collector = _discovery(service)
    try:
        discovery._handle_packet(service.advertisement())
    finally:
        service.close()

    assert len(collector.found) == 1
    schema = collector.found[0]
    assert schema.type == 'SoundService'
    assert 'SoundId' in schema.enums_dict, "the fetched enums must end up in the schema"
    assert [offset for offset, _ in service.requests] == [0, 40, 80]
    assert all(want == MAX_PACKET_SIZE - HEADER_SIZE for _, want in service.requests)


def test_fetch_is_cached_per_version():
    service = FakeService(ENUMS)
    discovery, collector = _discovery(service)
    try:
        discovery._handle_packet(service.advertisement())
        requests_after_first = len(service.requests)
        discovery._handle_packet(service.advertisement())
    finally:
        service.close()

    assert len(collector.found) == 1, "the second advertisement changes nothing"
    assert len(service.requests) == requests_after_first, "a repeating advertisement must not fetch again"


def test_failed_fetch_keeps_the_advertisement_only():
    service = FakeService(ENUMS)
    request_count = 0
    discovery, collector = _discovery(service)
    discovery.FETCH_TIMEOUT_S = 0.05
    service.window = 0  # every window is empty -> the client never makes progress
    try:
        discovery._handle_packet(service.advertisement())
    finally:
        service.close()
        request_count = len(service.requests)

    assert len(collector.found) == 1
    assert collector.found[0].enums_dict == {}, "a failed fetch must not abort discovery"
    assert request_count >= 1


def test_nothing_is_fetched_when_the_service_advertises_everything():
    service = FakeService(ENUMS)
    discovery, collector = _discovery(service)
    try:
        discovery._handle_packet(service.advertisement(omitted=[]))
    finally:
        service.close()

    assert len(collector.found) == 1
    assert service.requests == []


# ---------------------------------------------------------------------------------
# Both usage modes must work with a description that had to be completed by a fetch:
# the interface receives the *merged* schema from the discovery, never a partial one.
# ---------------------------------------------------------------------------------

def _advertised_desc(version: int = 1) -> dict:
    return {'type': 'SoundService', 'version': version,
            'inputs': [{'id': 0, 'name': 'Volume', 'type': 'uint8_t'}],
            'functions': []}


def _merged_schema():
    from xbot_service_interface.schema import ServiceSchema
    desc = _advertised_desc()
    desc.pop('omitted', None)
    desc.update(ENUMS)
    return ServiceSchema.from_dict(desc)


def test_mode_2_uses_the_completed_schema():
    from xbot_service_interface.interface import ServiceInterface
    from xbot_service_interface.serialization import pack_value

    interface = ServiceInterface(service_id=SERVICE_ID)  # no schema= argument
    interface._on_service_discovered('127.0.0.1', 4711, _merged_schema())

    enums = interface._active_schema.enums_dict
    assert 'SoundId' in enums
    # An enum-typed argument can be given by name, which is the whole point of the enums.
    assert pack_value('SoundId', 'MOWING_START', enums) == b'\x0e'


def test_mode_1_validates_against_the_completed_schema():
    from xbot_service_interface.interface import ServiceInterface

    provided = _advertised_desc()
    provided['functions'] = []
    interface = ServiceInterface(service_id=SERVICE_ID, schema=provided)
    # Must not raise: the advertised type/version still match, the enums were fetched.
    interface._on_service_discovered('127.0.0.1', 4711, _merged_schema())
    assert interface._active_schema is interface._schema


def test_mode_1_still_rejects_an_incompatible_service():
    import pytest

    from xbot_service_interface.exceptions import IncompatibleServiceError
    from xbot_service_interface.interface import ServiceInterface
    from xbot_service_interface.schema import ServiceSchema

    provided = _advertised_desc(version=2)
    interface = ServiceInterface(service_id=SERVICE_ID, schema=provided)
    with pytest.raises(IncompatibleServiceError):
        interface._on_service_discovered('127.0.0.1', 4711, _merged_schema())


def test_description_request_uses_the_claim_ip_convention():
    """The wire carries the host-order integer of the address, not the network bytes.

    The service turns target_ip into a destination with htonl(), exactly like the claim
    response does. Packing it the other way round sends every answer to a byte-swapped
    address - which is silent: the request is served, the answer just goes nowhere.
    """
    from xbot_service_interface.datatypes import pack_claim_payload, pack_description_request

    ip_host = struct.unpack('!I', socket.inet_aton('172.16.78.1'))[0]
    request = pack_description_request(13, ip_host, 4711)
    claim = pack_claim_payload(ip_host, 4711, 1_000_000)

    assert request[2:6] == claim[0:4], "target_ip must be packed like the claim payload"
    assert request[6:8] == claim[4:6], "target_port must be packed like the claim payload"
