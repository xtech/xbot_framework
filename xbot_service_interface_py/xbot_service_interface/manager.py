"""
Threading model
===============

Four daemon threads are created per XbotServiceIo + ServiceInterface pair.
All four start when XbotServiceIo.start() is called (xbot-cb-* starts at
ServiceInterface construction time).

┌─────────────────────────────────────────────────────────────────────────────┐
│ Thread            │ Created in      │ Role                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│ xbot-discovery    │ ServiceDiscovery│ Listens on multicast for              │
│                   │                 │ SERVICE_ADVERTISEMENT packets.         │
│                   │                 │ On receipt: calls XbotServiceIo.      │
│                   │                 │ _on_service_found() *synchronously*   │
│                   │                 │ on this thread, which validates the   │
│                   │                 │ schema and registers the service with  │
│                   │                 │ ServiceIO. No user callbacks fired.   │
├─────────────────────────────────────────────────────────────────────────────┤
│ xbot-io-recv      │ ServiceIO       │ Receives unicast UDP from services.   │
│                   │                 │ For each packet calls the matching    │
│                   │                 │ interface internal callback directly: │
│                   │                 │                                        │
│                   │                 │  CLAIM ack   → _on_claim_ack          │
│                   │                 │  DATA        → _on_data               │
│                   │                 │  TRANSACTION → _on_transaction_start, │
│                   │                 │                _on_data (per chunk),  │
│                   │                 │                _on_transaction_end    │
│                   │                 │  CONFIG_REQ  → _on_config_request     │
│                   │                 │                (sends config *here*,  │
│                   │                 │                 then enqueues cb)     │
│                   │                 │  RPC_RESPONSE→ _on_rpc_response       │
│                   │                 │                (unblocks rpc cond.)   │
│                   │                 │                                        │
│                   │                 │ Must NEVER block. Internal callbacks  │
│                   │                 │ enqueue work onto xbot-cb-* and       │
│                   │                 │ return immediately.                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ xbot-io-watchdog  │ ServiceIO       │ 1 s tick. Retries CLAIM until ack.   │
│                   │                 │ On heartbeat timeout: marks service   │
│                   │                 │ unclaimed, calls _on_disconnected     │
│                   │                 │ which enqueues user callbacks.        │
├─────────────────────────────────────────────────────────────────────────────┤
│ xbot-cb-{svc_id}  │ ServiceInterface│ Single callback thread per           │
│                   │ __init__        │ ServiceInterface. Drains a            │
│                   │                 │ SimpleQueue of callables.             │
│                   │                 │                                        │
│                   │                 │ ALL user-visible callbacks run here:  │
│                   │                 │   on_connected, on_configured,        │
│                   │                 │   on_disconnected,                    │
│                   │                 │   on_{output}_changed                 │
│                   │                 │                                        │
│                   │                 │ Callbacks are serialized (one at a   │
│                   │                 │ time, in arrival order). Blocking     │
│                   │                 │ calls including RPC (call_*) are safe │
│                   │                 │ here — the recv thread stays free.    │
└─────────────────────────────────────────────────────────────────────────────┘

RPC call flow
─────────────
User code (any thread) calls svc.call_foo(args):
  1. Serializes params, sends RPC_CALL packet (xbot-cb-* or main thread).
  2. Blocks on rpc_condition (Condition.wait_for).
  3. xbot-io-recv receives RPC_RESPONSE → _on_rpc_response() notifies the
     condition immediately (no enqueue needed — it's just a notify).
  4. Caller unblocks, reads response, returns.

If called from xbot-io-recv (e.g. in a raw _on_data override) this would
deadlock. All public user callbacks run on xbot-cb-* so this cannot happen
via the normal API.

Locks
─────
_lock          (ServiceInterface) — guards _transaction_active / _transaction_chunks
_rpc_lock      (ServiceInterface) — serialises concurrent RPC callers
_rpc_condition (ServiceInterface) — recv→caller signalling for RPC responses
ServiceIO._lock                   — guards _services dict and send path
"""
import logging

from .discovery import ServiceDiscovery
from .io import ServiceIO
from .interface import ServiceInterface
from .exceptions import IncompatibleServiceError
from .schema import ServiceSchema

log = logging.getLogger(__name__)


class _DiscoveryListener:
    def __init__(self, manager: 'XbotServiceIo'):
        self._mgr = manager

    def on_service_found(self, service_id: int, ip: str, port: int,
                          schema: ServiceSchema) -> None:
        self._mgr._on_service_found(service_id, ip, port, schema)


class XbotServiceIo:
    """Main entry point.

    Owns ServiceDiscovery and ServiceIO. Routes discovery events to
    registered ServiceInterfaces and wires their IO callbacks.

    Usage:
        xbot = XbotServiceIo(bind_ip='0.0.0.0')
        echo = ServiceInterface(service_id=1, schema='echo_service.json')
        xbot.register(echo)
        xbot.start()
        # ... do stuff ...
        xbot.stop()
    """

    def __init__(self, bind_ip: str = '0.0.0.0'):
        self._bind_ip   = bind_ip
        self._discovery = ServiceDiscovery(bind_ip)
        self._io        = ServiceIO(bind_ip)
        self._interfaces: dict[int, ServiceInterface] = {}  # service_id → interface
        self._listener  = _DiscoveryListener(self)

    def register(self, interface: ServiceInterface) -> None:
        """Register a ServiceInterface before or after calling start()."""
        sid = interface._service_id
        if sid in self._interfaces:
            log.warning(f"Overwriting existing ServiceInterface for service_id={sid}")
        self._interfaces[sid] = interface
        interface._io = self._io

        # If already discovered, trigger connection flow immediately
        info = self._discovery.get_service_info(sid)
        if info:
            self._on_service_found(sid, info['ip'], info['port'], info['schema'])

    def start(self) -> None:
        """Start IO and discovery threads."""
        self._io.start()
        self._discovery.register_listener(self._listener)
        self._discovery.start()
        log.info("XbotServiceIo started")

    def stop(self) -> None:
        """Stop all threads gracefully."""
        self._discovery.stop()
        self._io.stop()
        log.info("XbotServiceIo stopped")

    def ok(self) -> bool:
        """True while running."""
        return self._io.ok()

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    def _on_service_found(self, service_id: int, ip: str, port: int,
                           schema: ServiceSchema) -> None:
        iface = self._interfaces.get(service_id)
        if iface is None:
            return

        try:
            iface._on_service_discovered(ip, port, schema)
        except IncompatibleServiceError as e:
            log.error(str(e))
            return
        except Exception:
            log.exception(f"Error in _on_service_discovered for service {service_id}")
            return

        def on_disconnected():
            iface._on_disconnected()
            # Drop from discovery + IO so the service can be rediscovered cleanly
            self._discovery.drop_service(service_id)
            self._io.unregister_service(service_id)

        callbacks = {
            'on_claim_ack':         iface._on_claim_ack,
            'on_data':              iface._on_data,
            'on_transaction_start': iface._on_transaction_start,
            'on_transaction_end':   iface._on_transaction_end,
            'on_config_request':    iface._on_config_request,
            'on_rpc_response':      iface._on_rpc_response,
            'on_disconnected':      on_disconnected,
        }
        self._io.register_service(service_id, ip, port, callbacks)
        log.info(f"Service {service_id} registered with IO, awaiting claim")
