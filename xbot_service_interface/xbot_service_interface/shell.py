"""
Interactive IPython shell for xBot services.

Usage:
    xbot-shell [--bind <ip>]

In the shell:
    services()          — list discovered services
    connect(id_or_name) — connect to a service, returns ServiceProxy
    svc.info()          — show service schema
    svc.wait_connected()— block until claimed
    svc.watch_all()     — print all output updates to console
    svc.send_<TAB>      — tab-complete inputs
    svc.call_<TAB>      — tab-complete RPC functions
    svc.registers.<TAB> — tab-complete registers
"""
import ast
import sys
import threading
import time
from typing import Any, Optional

try:
    from rich.console import Console
    from rich.table import Table
    HAS_RICH = True
except ImportError:
    HAS_RICH = False

try:
    import IPython
    from IPython.terminal.embed import InteractiveShellEmbed
    from IPython.core.magic import register_line_magic
    HAS_IPYTHON = True
except ImportError:
    HAS_IPYTHON = False

from .interface import ServiceInterface
from .manager import XbotServiceIo
from .schema import ServiceSchema


_console: Optional['Console'] = Console() if HAS_RICH else None  # type: ignore[assignment]


# ---------------------------------------------------------------------------
# CompletableRegisters
# ---------------------------------------------------------------------------

class CompletableRegisters:
    """Dict/attribute access to service registers with tab completion support."""

    def __init__(self, iface: ServiceInterface):
        object.__setattr__(self, '_iface', iface)

    def __dir__(self):
        iface = object.__getattribute__(self, '_iface')
        schema = iface._active_schema
        if schema is None:
            return []
        return [r['snake_name'] for r in schema.registers]

    def __getattr__(self, name: str):
        iface = object.__getattribute__(self, '_iface')
        schema = iface._active_schema
        if schema is not None:
            for r in schema.registers:
                if r['snake_name'] == name or r['name'] == name:
                    val = iface._register_values.get(r['name'],
                          iface._register_values.get(r['snake_name']))
                    if val is None:
                        raise AttributeError(f"Register {name!r} not set yet")
                    return val
        raise AttributeError(name)

    def __setattr__(self, name: str, value: Any):
        iface = object.__getattribute__(self, '_iface')
        iface.registers[name] = value

    def __getitem__(self, name: str):
        iface = object.__getattribute__(self, '_iface')
        return iface.registers[name]

    def __setitem__(self, name: str, value: Any):
        iface = object.__getattribute__(self, '_iface')
        iface.registers[name] = value

    def __repr__(self) -> str:
        iface = object.__getattribute__(self, '_iface')
        schema = iface._active_schema
        if schema is None:
            return '<Registers — not connected>'
        lines = []
        for r in schema.registers:
            val = iface._register_values.get(r['name'],
                  iface._register_values.get(r['snake_name'], '<not set>'))
            flag = ' (optional)' if r.get('optional') else ''
            lines.append(f"  {r['snake_name']}: {r['type_str']} = {val!r}{flag}")
        return ('Registers:\n' + '\n'.join(lines)) if lines else 'Registers: <none>'


# ---------------------------------------------------------------------------
# ServiceProxy
# ---------------------------------------------------------------------------

class ServiceProxy:
    """Tab-completable proxy for a connected xBot service.

    Dynamic attributes (populated once the service schema is known):
        send_{input_snake_name}(value)        — send to a service input
        call_{function_snake_name}(*args)     — synchronous RPC call
        on_{output_snake_name}_changed = cb   — subscribe to output callback

    Register access:
        svc.registers.prefix = "hello: "     (attribute style)
        svc.registers['Prefix'] = "hello: "  (dict style)

    Helpers:
        svc.info()            — print schema details
        svc.wait_connected()  — block until the service is claimed
        svc.watch('name')     — print specific output to console
        svc.watch_all()       — print all outputs to console
    """

    def __init__(self, iface: ServiceInterface):
        object.__setattr__(self, '_iface', iface)
        object.__setattr__(self, 'registers', CompletableRegisters(iface))

    # ------------------------------------------------------------------
    # Tab completion — IPython calls dir() on the proxy object
    # ------------------------------------------------------------------

    def __dir__(self):
        base = ['registers', 'connected', 'transaction', 'on_connected', 'on_disconnected',
                'info', 'watch', 'watch_all', 'wait_connected', 'configure_registers']
        iface = object.__getattribute__(self, '_iface')
        schema = iface._active_schema
        if schema is not None:
            base += [f'send_{ch["snake_name"]}' for ch in schema.inputs]
            base += [f'call_{fn["snake_name"]}' for fn in schema.functions]
            base += [f'on_{ch["snake_name"]}_changed' for ch in schema.outputs]
        return sorted(set(base))

    # ------------------------------------------------------------------
    # Attribute dispatch
    # ------------------------------------------------------------------

    def __getattr__(self, name: str):
        iface = object.__getattribute__(self, '_iface')
        fn = getattr(iface, name)

        # Attach docstrings to dynamic methods so IPython can show signatures
        schema = iface._active_schema
        if schema is not None:
            try:
                if name.startswith('call_'):
                    rpc = schema.get_function(name[len('call_'):])
                    params = ', '.join(
                        f'{p["snake_name"]}: {p["type_str"]}'
                        for p in rpc['parameters']
                    )
                    fn.__doc__ = (
                        f"{rpc['name']}({params}) -> {rpc['return_type']}\n\n"
                        f"RPC call. Optional keyword: timeout_ms (default 1000)."
                    )
                elif name.startswith('send_'):
                    ch = schema.get_input(name[len('send_'):])
                    fn.__doc__ = f"Send {ch['name']} ({ch['type_str']}) to service."
            except Exception:
                pass

        return fn

    def __setattr__(self, name: str, value: Any):
        if name.startswith('on_') and name.endswith('_changed'):
            iface = object.__getattribute__(self, '_iface')
            setattr(iface, name, value)
        else:
            object.__setattr__(self, name, value)

    # ------------------------------------------------------------------
    # Helper methods
    # ------------------------------------------------------------------

    def wait_connected(self, timeout: float = 10.0) -> bool:
        """Block until the service is claimed, or timeout seconds pass."""
        iface = object.__getattribute__(self, '_iface')
        if iface._connected:
            _print(f"[green]✓[/green] Already connected", plain="✓ Already connected")
            self._warn_unconfigured(iface)
            return True
        ev = threading.Event()
        iface.on_connected(lambda: ev.set())
        ok = ev.wait(timeout=timeout)
        if ok:
            schema = iface._active_schema
            name = schema.type if schema else f'service {iface._service_id}'
            _print(f"[green]✓[/green] Connected to [bold]{name}[/bold]",
                   plain=f"✓ Connected to {name}")
            self._warn_unconfigured(iface)
        else:
            _print(f"[yellow]⚠[/yellow] Timeout after {timeout}s — service not connected",
                   plain=f"⚠ Timeout after {timeout}s")
        return ok

    @staticmethod
    def _warn_unconfigured(iface):
        missing = _missing_required(iface)
        if missing:
            names = ', '.join(missing)
            _print(
                f"  [yellow]⚠[/yellow] Required registers not set: "
                f"[bold]{names}[/bold]  — call [bold]configure_registers()[/bold]",
                plain=f"  ⚠ Required registers not set: {names}"
                      " — call configure_registers()",
            )

    def configure_registers(self):
        """Interactive wizard: prompt for each register value, then send config.

        Press Enter to keep the current value. Type '-' to clear an optional register.
        """
        iface = object.__getattribute__(self, '_iface')
        schema = iface._active_schema
        if schema is None:
            _print("[yellow]No schema — call wait_connected() first[/yellow]",
                   plain="No schema — call wait_connected() first")
            return
        if not schema.registers:
            _print("[dim]Service has no registers.[/dim]", plain="Service has no registers.")
            return

        _print(f"\n[bold]Configuring registers for {schema.type}[/bold]  "
               f"[dim](Enter = keep current, '-' = clear optional)[/dim]\n",
               plain=f"\nConfiguring registers for {schema.type}"
                     " (Enter = keep current, '-' = clear optional)\n")

        changed = False
        for reg in schema.registers:
            current = iface._register_values.get(
                reg['name'], iface._register_values.get(reg['snake_name']))
            optional = reg.get('optional', False)

            flag = ' [dim](optional)[/dim]' if optional else ' [red]*[/red]'
            hint = _type_hint(reg['type_str'], schema.enums_dict)

            from .serialization import parse_type_string as _pts
            is_blob = _pts(reg['type_str'])[0] == 'blob'
            value_label = 'file path' if is_blob else 'new value'

            if HAS_RICH and _console is not None:
                _console.print(
                    f"  [cyan]{reg['snake_name']}[/cyan]{flag}  "
                    f"[dim]{reg['type_str']}[/dim]"
                    + (f"  [dim]hint: {hint}[/dim]" if hint else "")
                )
                if is_blob and current is not None:
                    current_s = f'[dim]{len(current)} bytes[/dim]'
                elif current is not None:
                    current_s = repr(current)
                else:
                    current_s = '[dim]<not set>[/dim]'
                prompt_str = f"    current={current_s}  {value_label}: "
                raw = _console.input(prompt_str)
                if is_blob and raw.strip():
                    compress_s = _console.input(
                        "    heatshrink compress? [y/N]: ")
                    if compress_s.strip().lower() == 'y':
                        raw = raw.strip() + ' --compress'
            else:
                if is_blob and current is not None:
                    current_s = f'{len(current)} bytes'
                elif current is not None:
                    current_s = repr(current)
                else:
                    current_s = '<not set>'
                type_s = f"  [{reg['type_str']}]" + (f"  ({hint})" if hint else "")
                raw = input(f"  {reg['snake_name']}{type_s}  current={current_s}  {value_label}: ")
                if is_blob and raw.strip():
                    compress_s = input("    heatshrink compress? [y/N]: ")
                    if compress_s.strip().lower() == 'y':
                        raw = raw.strip() + ' --compress'

            raw = raw.strip()

            if raw == '':
                continue  # keep current

            if raw == '-':
                if optional:
                    iface._register_values.pop(reg['name'], None)
                    iface._register_values.pop(reg['snake_name'], None)
                    _print(f"    [dim]cleared[/dim]", plain="    cleared")
                    changed = True
                else:
                    _print(f"    [yellow]Cannot clear required register — skipped[/yellow]",
                           plain="    Cannot clear required register — skipped")
                continue

            try:
                value = _parse_register_input(reg['type_str'], raw, schema.enums_dict)
                iface._register_values[reg['name']] = value
                _print(f"    [green]✓[/green] set to {value!r}", plain=f"    ✓ set to {value!r}")
                changed = True
            except (ValueError, TypeError) as e:
                _print(f"    [red]✗ parse error: {e} — skipped[/red]",
                       plain=f"    ✗ parse error: {e} — skipped")

        if changed and iface._connected and schema is not None and iface._io is not None:
            iface._on_config_request()
            _print("\n[green]✓[/green] Configuration sent.\n", plain="\n✓ Configuration sent.\n")
        elif changed:
            _print("\n[dim]Values stored — will be sent on next connect.[/dim]\n",
                   plain="\nValues stored — will be sent on next connect.\n")
        else:
            _print("\n[dim]No changes.[/dim]\n", plain="\nNo changes.\n")

    def watch(self, *output_names: str):
        """Print received output values to the console.

        Args:
            *output_names: snake_case output names. If none given, watches all.
        """
        iface = object.__getattribute__(self, '_iface')
        schema = iface._active_schema
        if schema is None:
            print("Not connected — call wait_connected() first")
            return
        targets = schema.outputs if not output_names else [
            schema.get_output(n) for n in output_names
        ]
        for ch in targets:
            _install_watcher(iface, ch)
        names = ', '.join(ch['snake_name'] for ch in targets)
        _print(f"[dim]Watching:[/dim] {names}", plain=f"Watching: {names}")

    def watch_all(self):
        """Print all output values to the console as they arrive."""
        self.watch()

    def info(self):
        """Print a rich summary of this service's schema."""
        iface = object.__getattribute__(self, '_iface')
        _print_schema(iface)

    def transaction(self):
        """Context manager: buffer multiple sends into one UDP transaction."""
        iface = object.__getattribute__(self, '_iface')
        return iface.transaction()

    # ------------------------------------------------------------------
    # Display
    # ------------------------------------------------------------------

    def __repr__(self) -> str:
        iface = object.__getattribute__(self, '_iface')
        schema = iface._active_schema
        sid = iface._service_id
        if schema is None:
            return f'<ServiceProxy id={sid} — not connected>'
        status = 'connected' if iface._connected else 'discovered'
        inputs_s  = ', '.join(ch['snake_name'] for ch in schema.inputs)  or '—'
        outputs_s = ', '.join(ch['snake_name'] for ch in schema.outputs) or '—'
        fns_s     = ', '.join(fn['snake_name'] for fn in schema.functions) or '—'
        return (
            f"<ServiceProxy '{schema.type}' id={sid} [{status}]>\n"
            f"  inputs:    {inputs_s}\n"
            f"  outputs:   {outputs_s}\n"
            f"  functions: {fns_s}"
        )


# ---------------------------------------------------------------------------
# Rich helpers
# ---------------------------------------------------------------------------

def _missing_required(iface) -> list:
    """Return list of required register names that have no value set."""
    schema = iface._active_schema
    if schema is None:
        return []
    missing = []
    for r in schema.registers:
        if not r.get('optional', False):
            if (r['name'] not in iface._register_values and
                    r['snake_name'] not in iface._register_values):
                missing.append(r['snake_name'])
    return missing


def _config_status(iface) -> tuple[str, str]:
    """Return (rich_string, plain_string) config status for a service."""
    schema = iface._active_schema
    if schema is None or not schema.registers:
        return '', ''
    missing = _missing_required(iface)
    if missing:
        return '[yellow]⚠ needs config[/yellow]', '⚠ needs config'
    return '[green]configured[/green]', 'configured'


def _heatshrink_compress(data: bytes) -> bytes:
    """Compress bytes with heatshrink (window=9, lookahead=5 — matches C++ defaults)."""
    try:
        import heatshrink2
        return heatshrink2.compress(data, window_sz2=9, lookahead_sz2=5)
    except ImportError:
        raise RuntimeError(
            "heatshrink2 not installed. Run: pip install heatshrink2")


def _parse_blob_input(raw: str) -> tuple[str, bool]:
    """Parse blob input string: 'path/to/file [--compress]' → (path, compress)."""
    compress = '--compress' in raw
    path = raw.replace('--compress', '').strip().strip('"\'')
    return path, compress


def _type_hint(type_str: str, enums: dict) -> str:
    """Return a short human hint for a type, e.g. for arrays or enums."""
    from .serialization import parse_type_string
    base, is_array, max_len = parse_type_string(type_str)
    if base == 'blob':
        return 'path to file  (will ask about heatshrink compression)'
    if base in enums:
        vals = ', '.join(enums[base]['values'].keys())
        return f"one of: {vals}"
    if is_array and base != 'char':
        return f"comma-separated list of {max_len} {base} values"
    return ''


def _parse_register_input(type_str: str, raw: str, enums: dict) -> Any:
    """Convert user-typed string to a Python value matching type_str."""
    from .serialization import parse_type_string
    base, is_array, max_len = parse_type_string(type_str)

    # Enum
    if base in enums:
        enum_def = enums[base]
        if raw in enum_def['values']:
            return raw  # pack_value accepts name string
        try:
            return int(raw)
        except ValueError:
            raise ValueError(
                f"Expected one of {list(enum_def['values'].keys())} or an integer, got {raw!r}")

    # String / char array
    if base == 'char':
        # strip surrounding quotes if user typed them
        if len(raw) >= 2 and raw[0] in ('"', "'") and raw[-1] == raw[0]:
            raw = raw[1:-1]
        return raw

    # Numeric array
    if is_array:
        # Accept both "1, -2, 3" and "[1, -2, 3]"
        try:
            parsed = ast.literal_eval(raw if raw.startswith('[') else f'[{raw}]')
        except (ValueError, SyntaxError) as e:
            raise ValueError(f"Cannot parse list: {e}")
        if not isinstance(parsed, list):
            raise ValueError("Expected a list of values")
        conv = float if base in ('float', 'double') else int
        return [conv(v) for v in parsed]

    # Blob — treat input as file path (with optional heatshrink compression)
    if base == 'blob':
        path, compress = _parse_blob_input(raw)
        try:
            with open(path, 'rb') as f:
                data = f.read()
        except OSError as e:
            raise ValueError(f"Cannot read file {path!r}: {e}")
        if compress:
            data = _heatshrink_compress(data)
        return data

    # Scalar numeric
    if base in ('float', 'double'):
        return float(raw)
    return int(raw, 0)  # 0 base supports 0x hex, 0b binary, etc.


def _print(rich_msg: str, plain: Optional[str] = None):
    if HAS_RICH and _console is not None:
        _console.print(rich_msg)
    else:
        print(plain or rich_msg)


def _install_watcher(iface: ServiceInterface, ch: dict):
    snake = ch['snake_name']

    def _cb(value, timestamp):
        ts_ms = timestamp // 1000
        if HAS_RICH and _console is not None:
            _console.print(
                f"[dim]{ts_ms:>12}ms[/dim] "
                f"[cyan]{snake}[/cyan] = {value!r}"
            )
        else:
            print(f"{ts_ms:>12}ms {snake} = {value!r}")

    iface._output_callbacks[snake] = _cb


def _print_schema(iface):
    sid = iface._service_id
    schema = iface._active_schema
    connected = iface._connected

    if schema is None:
        _print(f"[yellow]Service {sid}: schema not available[/yellow]",
               plain=f"Service {sid}: schema not available")
        return

    status_s = '[green]connected[/green]' if connected else '[yellow]discovered[/yellow]'
    plain_status = 'connected' if connected else 'discovered'

    cfg_rich, cfg_plain = _config_status(iface)

    if HAS_RICH and _console is not None:
        header = (
            f"[bold]{schema.type}[/bold]  v{schema.version}  "
            f"(id={sid})  {status_s}"
        )
        if cfg_rich:
            header += f"  {cfg_rich}"
        _console.print()
        _console.print(header)

        if schema.inputs:
            t = Table(title='Inputs', show_header=True, header_style='bold blue',
                      show_lines=False)
            t.add_column('method')
            t.add_column('type', style='dim')
            for ch in schema.inputs:
                t.add_row(f'send_{ch["snake_name"]}(value)', ch['type_str'])
            _console.print(t)

        if schema.outputs:
            t = Table(title='Outputs', show_header=True, header_style='bold green',
                      show_lines=False)
            t.add_column('callback attribute')
            t.add_column('type', style='dim')
            for ch in schema.outputs:
                t.add_row(f'on_{ch["snake_name"]}_changed', ch['type_str'])
            _console.print(t)

        if schema.registers:
            t = Table(title='Registers', show_header=True, header_style='bold magenta',
                      show_lines=False)
            t.add_column('registers.name')
            t.add_column('type', style='dim')
            t.add_column('value')
            t.add_column('req', justify='center', style='dim')
            for r in schema.registers:
                val = iface._register_values.get(
                    r['name'], iface._register_values.get(r['snake_name']))
                if val is None:
                    if r.get('optional'):
                        val_s = '[dim]—[/dim]'
                    else:
                        val_s = '[yellow]<not set>[/yellow]'
                elif r['type_str'] == 'blob' or (
                        isinstance(val, (bytes, bytearray)) ):
                    val_s = f'[dim]{len(val)} bytes[/dim]'
                else:
                    val_s = repr(val)
                req_s = '' if r.get('optional') else '[red]*[/red]'
                t.add_row(r['snake_name'], r['type_str'], val_s, req_s)
            _console.print(t)

        if schema.functions:
            t = Table(title='RPC Functions', show_header=True,
                      header_style='bold yellow', show_lines=False)
            t.add_column('method')
            t.add_column('parameters', style='dim')
            t.add_column('returns', style='dim')
            for fn in schema.functions:
                params_s = ', '.join(
                    f'{p["snake_name"]}: {p["type_str"]}' for p in fn['parameters']
                ) or '—'
                t.add_row(f'call_{fn["snake_name"]}(...)', params_s, fn['return_type'])
            _console.print(t)

        _console.print()
    else:
        cfg_part = f'  [{cfg_plain}]' if cfg_plain else ''
        print(f"\n{schema.type} v{schema.version} (id={sid}) [{plain_status}]{cfg_part}")
        if schema.inputs:
            print("  Inputs:")
            for ch in schema.inputs:
                print(f"    send_{ch['snake_name']}(value)  [{ch['type_str']}]")
        if schema.outputs:
            print("  Outputs:")
            for ch in schema.outputs:
                print(f"    on_{ch['snake_name']}_changed  [{ch['type_str']}]")
        if schema.registers:
            print("  Registers:")
            for r in schema.registers:
                val = iface._register_values.get(
                    r['name'], iface._register_values.get(r['snake_name']))
                val_s = f'{len(val)} bytes' if isinstance(val, (bytes, bytearray)) \
                    else (repr(val) if val is not None else '<not set>')
                flag = ' (optional)' if r.get('optional') else ' *'
                print(f"    registers.{r['snake_name']}  [{r['type_str']}]{flag}  = {val_s}")
        if schema.functions:
            print("  RPC Functions:")
            for fn in schema.functions:
                params_s = ', '.join(
                    f'{p["snake_name"]}: {p["type_str"]}' for p in fn['parameters']
                )
                print(f"    call_{fn['snake_name']}({params_s}) -> {fn['return_type']}")
        print()


# ---------------------------------------------------------------------------
# Discovery tracker
# ---------------------------------------------------------------------------

class _DiscoveryTracker:
    """Collects all service advertisements independent of registered interfaces."""

    def __init__(self):
        self._lock = threading.Lock()
        self._services: dict[int, dict] = {}

    def on_service_found(self, sid: int, ip: str, port: int,
                          schema: ServiceSchema):
        with self._lock:
            is_new = sid not in self._services
            self._services[sid] = {'ip': ip, 'port': port, 'schema': schema}

        if is_new:
            _print(
                f"\n[bold green]→[/bold green] Discovered: "
                f"[bold]{schema.type}[/bold] (id={sid}) at {ip}:{port}  "
                f"— connect({sid})",
                plain=f"\n→ Discovered: {schema.type} (id={sid}) at {ip}:{port}"
                      f" — connect({sid})",
            )

    def snapshot(self) -> dict:
        with self._lock:
            return dict(self._services)


# ---------------------------------------------------------------------------
# XbotShell
# ---------------------------------------------------------------------------

class XbotShell:
    """Discovery + connection manager for the interactive shell."""

    def __init__(self, bind_ip: str = '0.0.0.0'):
        self._io = XbotServiceIo(bind_ip=bind_ip)
        self._tracker = _DiscoveryTracker()
        self._proxies: dict[int, ServiceProxy] = {}

    def start(self):
        self._io._discovery.register_listener(self._tracker)
        self._io.start()

    def stop(self):
        self._io.stop()

    # ------------------------------------------------------------------
    # Shell commands
    # ------------------------------------------------------------------

    def services(self):
        """List all discovered services."""
        known = self._tracker.snapshot()
        if not known:
            _print('[yellow]No services discovered yet. Are services running?[/yellow]',
                   plain='No services discovered yet.')
            return

        if HAS_RICH and _console is not None:
            t = Table(title='Discovered Services', show_header=True, show_lines=False)
            t.add_column('ID', justify='right', style='bold')
            t.add_column('Type')
            t.add_column('Endpoint', style='dim')
            t.add_column('In', justify='right', style='blue')
            t.add_column('Out', justify='right', style='green')
            t.add_column('Fn', justify='right', style='yellow')
            t.add_column('Status')
            t.add_column('Config')
            for sid, info in sorted(known.items()):
                s: ServiceSchema = info['schema']
                proxy = self._proxies.get(sid)
                connected = proxy is not None and proxy._iface._connected
                status_s = '[green]connected[/green]' if connected else ''
                if proxy is not None:
                    cfg_rich, _ = _config_status(proxy._iface)
                else:
                    cfg_rich = ''
                t.add_row(
                    str(sid), s.type,
                    f"{info['ip']}:{info['port']}",
                    str(len(s.inputs)),
                    str(len(s.outputs)),
                    str(len(s.functions)),
                    status_s,
                    cfg_rich,
                )
            _console.print(t)
        else:
            for sid, info in sorted(known.items()):
                s: ServiceSchema = info['schema']
                proxy = self._proxies.get(sid)
                connected = proxy is not None and proxy._iface._connected
                _, cfg_plain = _config_status(proxy._iface) if proxy else ('', '')
                flags = (' [connected]' if connected else '') + (f' [{cfg_plain}]' if cfg_plain else '')
                print(f"  [{sid}] {s.type} @ {info['ip']}:{info['port']}{flags}")

    def connect(self, id_or_name) -> ServiceProxy:
        """Connect to a service by ID (int) or type name (str).

        Returns a ServiceProxy. Call svc.wait_connected() to block until ready.
        """
        known = self._tracker.snapshot()

        target_sid: Optional[int] = None
        if isinstance(id_or_name, int):
            if id_or_name in known:
                target_sid = id_or_name
        else:
            needle = str(id_or_name).lower()
            for sid, info in known.items():
                if info['schema'].type.lower() == needle:
                    target_sid = sid
                    break

        if target_sid is None:
            avail = ', '.join(
                f"{s['schema'].type}({sid})" for sid, s in sorted(known.items())
            ) or 'none'
            raise ValueError(
                f"Service {id_or_name!r} not found. "
                f"Available: {avail}. "
                f"Call services() to list."
            )

        if target_sid in self._proxies:
            existing = self._proxies[target_sid]
            _print(f"[yellow]Reusing existing proxy for service {target_sid}[/yellow]",
                   plain=f"Reusing existing proxy for service {target_sid}")
            return existing

        info = known[target_sid]
        schema: ServiceSchema = info['schema']

        _print(
            f"Connecting to [bold]{schema.type}[/bold] (id={target_sid})…"
            f"  call [bold]wait_connected()[/bold] on result to block until ready.",
            plain=f"Connecting to {schema.type} (id={target_sid})...",
        )

        iface = ServiceInterface(service_id=target_sid)
        self._io.register(iface)
        proxy = ServiceProxy(iface)
        self._proxies[target_sid] = proxy
        return proxy


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main(argv=None):
    if not HAS_IPYTHON:
        print("IPython not installed. Install the shell extras:")
        print("  pip install xbot-service-interface[shell]")
        sys.exit(1)

    import argparse
    parser = argparse.ArgumentParser(
        description='Interactive xBot service shell')
    parser.add_argument('--bind', default='0.0.0.0',
                        metavar='IP',
                        help='bind IP for UDP socket (default: 0.0.0.0)')
    args = parser.parse_args(argv)

    shell_mgr = XbotShell(bind_ip=args.bind)
    shell_mgr.start()

    _print_banner(args.bind)

    ns: dict[str, Any] = {
        'services': shell_mgr.services,
        'connect':  shell_mgr.connect,
        'xbot':     shell_mgr,
    }

    ipshell = InteractiveShellEmbed(
        banner1='',
        banner2='',
        exit_msg='\nStopping xbot IO...',
        user_ns=ns,
    )

    _register_magics(shell_mgr, ipshell, ns)

    try:
        ipshell()
    finally:
        shell_mgr.stop()


def _register_magics(shell_mgr: XbotShell, ipshell, ns: dict):
    """Register %services and %connect IPython magic commands."""

    @ipshell.register_magic_function
    def services(line):
        """%services — list all discovered xBot services."""
        shell_mgr.services()

    @ipshell.register_magic_function
    def connect(line):
        """%connect <id|name> [as <varname>]
        Connect to an xBot service and inject the proxy into the namespace.
        """
        parts = line.strip().split()
        if not parts:
            print("Usage: %connect <id|name> [as <varname>]")
            return

        raw = parts[0]
        arg: Any = int(raw) if raw.isdigit() else raw

        varname = None
        if len(parts) >= 3 and parts[1].lower() == 'as':
            varname = parts[2]

        try:
            proxy = shell_mgr.connect(arg)
        except ValueError as e:
            _print(f'[red]{e}[/red]', plain=str(e))
            return

        if varname is None:
            schema = proxy._iface._active_schema
            varname = (schema.type.lower() if schema else f'svc_{arg}')

        ns[varname] = proxy
        ipshell.user_ns[varname] = proxy
        _print(
            f"[green]→[/green] Proxy available as [bold]{varname}[/bold]  "
            f"(call [bold]{varname}.wait_connected()[/bold] to block until ready)",
            plain=f"→ Proxy available as {varname}",
        )
        return proxy


def _print_banner(bind_ip: str):
    if HAS_RICH and _console is not None:
        _console.print()
        _console.rule('[bold cyan]xBot Service Shell[/bold cyan]')
        _console.print()
        _console.print('  [bold]services()[/bold]              list discovered services')
        _console.print('  [bold]connect(id_or_name)[/bold]     connect, returns ServiceProxy')
        _console.print('  [bold]svc.wait_connected()[/bold]    block until service is ready')
        _console.print('  [bold]svc.info()[/bold]              print schema (inputs/outputs/registers/RPC)')
        _console.print('  [bold]svc.watch_all()[/bold]         stream output values to console')
        _console.print()
        _console.print('  Magic commands:  [bold]%connect EchoService[/bold]  /  [bold]%services[/bold]')
        _console.print(f'  Listening on [dim]{bind_ip}[/dim]  (multicast 233.255.255.0:4242)')
        _console.print()
        _console.rule()
        _console.print()
    else:
        print(f"""
xBot Service Shell
==================
  services()           list discovered services
  connect(id_or_name)  connect, returns ServiceProxy
  svc.wait_connected() block until service is ready
  svc.info()           print schema
  svc.watch_all()      stream outputs to console

  Listening on {bind_ip} (multicast 233.255.255.0:4242)
""")


if __name__ == '__main__':
    main()
