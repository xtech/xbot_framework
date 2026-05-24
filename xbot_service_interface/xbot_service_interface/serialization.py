import re
import struct
from typing import Optional, Tuple

# Maps C type names to (struct format char, byte size)
_FORMATS: dict[str, tuple[str, int]] = {
    'uint8_t':  ('B', 1),
    'uint16_t': ('H', 2),
    'uint32_t': ('I', 4),
    'uint64_t': ('Q', 8),
    'int8_t':   ('b', 1),
    'int16_t':  ('h', 2),
    'int32_t':  ('i', 4),
    'int64_t':  ('q', 8),
    'float':    ('f', 4),
    'double':   ('d', 8),
    'char':     ('s', 1),
}

_SNAKE_RE = re.compile(r'([a-z0-9])([A-Z])')
_TYPE_RE  = re.compile(r'^([a-zA-Z_][a-zA-Z0-9_]*)(?:\[(\d+)\])?$')


def to_snake_case(name: str) -> str:
    """Convert channel name to snake_case method suffix.

    "Input Text"   → "input_text"
    "EchoCount"    → "echo_count"
    "Message Count"→ "message_count"
    """
    s = re.sub(r'[\s\-]+', '_', name.strip())
    s = _SNAKE_RE.sub(r'\1_\2', s)
    return s.lower()


def parse_type_string(type_str: str) -> Tuple[str, bool, Optional[int]]:
    """Parse 'uint32_t' → ('uint32_t', False, None)
       Parse 'char[100]' → ('char', True, 100)
    """
    m = _TYPE_RE.match(type_str.strip())
    if not m:
        raise ValueError(f"Cannot parse type: {type_str!r}")
    base = m.group(1)
    if m.group(2) is not None:
        return base, True, int(m.group(2))
    return base, False, None


def sizeof_type(type_str: str) -> int:
    base, is_array, max_len = parse_type_string(type_str)
    if base == 'blob':
        raise ValueError("blob has no fixed size")
    if base not in _FORMATS:
        raise ValueError(f"Unknown type: {base!r}")
    _, elem_size = _FORMATS[base]
    return elem_size * max_len if is_array else elem_size


def pack_value(type_str: str, value, enums: dict = None) -> bytes:
    """Serialize a Python value to bytes using the given xbot type string."""
    base, is_array, max_len = parse_type_string(type_str)

    # Enum resolution: name → int, then pack as enum's base_type
    if enums and base in enums:
        enum_def = enums[base]
        if isinstance(value, str):
            if value not in enum_def['values']:
                raise ValueError(f"Unknown enum value {value!r} for type {base!r}")
            value = enum_def['values'][value]
        base = enum_def['base_type']

    if base == 'blob':
        if not isinstance(value, (bytes, bytearray, memoryview)):
            raise TypeError("blob type requires bytes-like object")
        return bytes(value)

    if base == 'char':
        if is_array:
            raw = value.encode('utf-8', errors='replace') if isinstance(value, str) else bytes(value)
            return raw[:max_len]  # truncate to max_len but never pad — C++ uses actual length
        raw = value.encode('utf-8')[:1] if isinstance(value, str) else bytes([int(value) & 0xFF])
        return raw

    if base not in _FORMATS:
        raise ValueError(f"Unknown type: {base!r}")
    fmt, _ = _FORMATS[base]

    if is_array:
        items = list(value)[:max_len]
        return struct.pack(f'<{len(items)}{fmt}', *items)
    return struct.pack(f'<{fmt}', value)


def unpack_value(type_str: str, data: bytes, enums: dict = None):
    """Deserialize bytes to a Python value using the given xbot type string."""
    base, is_array, max_len = parse_type_string(type_str)

    # Track original base for enum reverse-mapping
    enum_reverse = None
    if enums and base in enums:
        enum_def = enums[base]
        enum_reverse = {v: k for k, v in enum_def['values'].items()}
        base = enum_def['base_type']

    if base == 'blob':
        return bytes(data)

    if base == 'char':
        if is_array:
            return data.rstrip(b'\x00').decode('utf-8', errors='replace')
        return data[:1].decode('utf-8', errors='replace')

    if base not in _FORMATS:
        raise ValueError(f"Unknown type: {base!r}")
    fmt, elem_size = _FORMATS[base]

    if is_array:
        n = len(data) // elem_size
        values = list(struct.unpack(f'<{n}{fmt}', data[:n * elem_size]))
        if enum_reverse:
            values = [enum_reverse.get(v, v) for v in values]
        return values

    if len(data) < elem_size:
        raise ValueError(f"Data too short for {type_str!r}: need {elem_size}, got {len(data)}")
    value = struct.unpack(f'<{fmt}', data[:elem_size])[0]
    if enum_reverse:
        value = enum_reverse.get(value, value)
    return value
