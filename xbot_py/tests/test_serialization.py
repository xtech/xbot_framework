"""Tests for serialization.py — type parsing, pack/unpack round-trips, edge cases."""
import struct
import pytest

from xbot_service_interface.serialization import (
    to_snake_case, parse_type_string, sizeof_type,
    pack_value, unpack_value,
)


# ---------------------------------------------------------------------------
# to_snake_case
# ---------------------------------------------------------------------------

class TestToSnakeCase:
    @pytest.mark.parametrize('name,expected', [
        ('Input Text',    'input_text'),
        ('EchoCount',     'echo_count'),
        ('Message Count', 'message_count'),
        ('Prefix',        'prefix'),
        ('Echo',          'echo'),
        ('ALLCAPS',       'allcaps'),
        ('already_snake', 'already_snake'),
        ('hyphen-name',   'hyphen_name'),
        ('  Leading  ',   'leading'),
        ('A',             'a'),
        ('ABCDef',        'abcdef'),    # no lowercase before B/C, no split inserted
        ('MyIMUData',     'my_imudata'), # 'IMU' is all-caps run, stays together
    ])
    def test_cases(self, name, expected):
        assert to_snake_case(name) == expected


# ---------------------------------------------------------------------------
# parse_type_string
# ---------------------------------------------------------------------------

class TestParseTypeString:
    @pytest.mark.parametrize('type_str,base,is_array,max_len', [
        ('uint8_t',   'uint8_t',  False, None),
        ('uint16_t',  'uint16_t', False, None),
        ('uint32_t',  'uint32_t', False, None),
        ('uint64_t',  'uint64_t', False, None),
        ('int8_t',    'int8_t',   False, None),
        ('int16_t',   'int16_t',  False, None),
        ('int32_t',   'int32_t',  False, None),
        ('int64_t',   'int64_t',  False, None),
        ('float',     'float',    False, None),
        ('double',    'double',   False, None),
        ('char',      'char',     False, None),
        ('blob',      'blob',     False, None),
        ('char[100]', 'char',     True,  100),
        ('uint32_t[4]', 'uint32_t', True, 4),
        ('float[3]',  'float',    True,  3),
        ('int16_t[8]','int16_t',  True,  8),
    ])
    def test_parses(self, type_str, base, is_array, max_len):
        b, a, m = parse_type_string(type_str)
        assert b == base
        assert a == is_array
        assert m == max_len

    def test_invalid_raises(self):
        with pytest.raises(ValueError):
            parse_type_string('not-a-type!')

    def test_empty_raises(self):
        with pytest.raises(ValueError):
            parse_type_string('')

    def test_strips_whitespace(self):
        b, a, m = parse_type_string('  uint32_t  ')
        assert b == 'uint32_t' and not a


# ---------------------------------------------------------------------------
# sizeof_type
# ---------------------------------------------------------------------------

class TestSizeofType:
    @pytest.mark.parametrize('type_str,expected', [
        ('uint8_t',   1),
        ('uint16_t',  2),
        ('uint32_t',  4),
        ('uint64_t',  8),
        ('int8_t',    1),
        ('int16_t',   2),
        ('int32_t',   4),
        ('int64_t',   8),
        ('float',     4),
        ('double',    8),
        ('char',      1),
        ('char[42]',  42),
        ('uint32_t[4]', 16),
        ('float[3]',  12),
    ])
    def test_sizes(self, type_str, expected):
        assert sizeof_type(type_str) == expected

    def test_blob_raises(self):
        with pytest.raises(ValueError):
            sizeof_type('blob')


# ---------------------------------------------------------------------------
# pack_value / unpack_value — scalar round-trips
# ---------------------------------------------------------------------------

class TestScalarRoundTrips:
    @pytest.mark.parametrize('type_str,value', [
        ('uint8_t',  0),
        ('uint8_t',  127),
        ('uint8_t',  255),
        ('uint16_t', 0),
        ('uint16_t', 1000),
        ('uint16_t', 65535),
        ('uint32_t', 0),
        ('uint32_t', 1),
        ('uint32_t', 2**32 - 1),
        ('uint64_t', 0),
        ('uint64_t', 2**64 - 1),
        ('int8_t',   -128),
        ('int8_t',   0),
        ('int8_t',   127),
        ('int16_t',  -32768),
        ('int16_t',  0),
        ('int16_t',  32767),
        ('int32_t',  -(2**31)),
        ('int32_t',  0),
        ('int32_t',  2**31 - 1),
        ('int64_t',  -(2**63)),
        ('int64_t',  2**63 - 1),
        ('double',   0.0),
        ('double',   1.5),
        ('double',   -1.5),
        ('double',   1e300),
    ])
    def test_round_trip(self, type_str, value):
        raw = pack_value(type_str, value)
        assert unpack_value(type_str, raw) == value

    def test_float_round_trip(self):
        raw = pack_value('float', 3.14)
        assert abs(unpack_value('float', raw) - 3.14) < 1e-5

    def test_float_negative(self):
        raw = pack_value('float', -2.5)
        assert abs(unpack_value('float', raw) - (-2.5)) < 1e-6

    def test_pack_output_is_bytes(self):
        assert isinstance(pack_value('uint32_t', 42), bytes)

    def test_pack_output_length(self):
        assert len(pack_value('uint32_t', 42)) == 4
        assert len(pack_value('uint8_t', 1))   == 1
        assert len(pack_value('double', 1.0))  == 8


# ---------------------------------------------------------------------------
# char arrays
# ---------------------------------------------------------------------------

class TestCharArrays:
    def test_string_round_trip(self):
        raw = pack_value('char[20]', 'hello')
        assert unpack_value('char[20]', raw) == 'hello'

    def test_no_padding(self):
        raw = pack_value('char[42]', 'hi')
        assert raw == b'hi'
        assert len(raw) == 2

    def test_sends_actual_length_not_max(self):
        raw = pack_value('char[100]', 'abc')
        assert len(raw) == 3

    def test_truncation_at_max_len(self):
        raw = pack_value('char[3]', 'hello')
        assert raw == b'hel'

    def test_empty_string(self):
        raw = pack_value('char[10]', '')
        assert raw == b''
        assert unpack_value('char[10]', raw) == ''

    def test_unpack_strips_null_bytes(self):
        # Service may send padded data
        assert unpack_value('char[10]', b'hi\x00\x00\x00\x00\x00\x00\x00\x00') == 'hi'

    def test_unpack_internal_nulls_preserved_before_strip(self):
        # Only TRAILING nulls stripped
        data = b'a\x00b\x00\x00'
        assert unpack_value('char[5]', data) == 'a\x00b'

    def test_bytes_input(self):
        raw = pack_value('char[5]', b'xyz')
        assert raw == b'xyz'

    def test_utf8_encoding(self):
        raw = pack_value('char[10]', 'héllo')
        decoded = unpack_value('char[10]', raw)
        assert 'h' in decoded  # at minimum ASCII part survives


# ---------------------------------------------------------------------------
# blob
# ---------------------------------------------------------------------------

class TestBlob:
    def test_bytes_passthrough(self):
        data = b'\x01\x02\x03\x04'
        assert pack_value('blob', data) == data

    def test_bytearray_passthrough(self):
        data = bytearray(b'\xDE\xAD')
        assert pack_value('blob', data) == b'\xDE\xAD'

    def test_unpack_returns_bytes(self):
        assert unpack_value('blob', b'\xFF\x00') == b'\xFF\x00'

    def test_non_bytes_raises(self):
        with pytest.raises(TypeError):
            pack_value('blob', 'string')

    def test_non_bytes_int_raises(self):
        with pytest.raises(TypeError):
            pack_value('blob', 42)


# ---------------------------------------------------------------------------
# Array types
# ---------------------------------------------------------------------------

class TestArrayTypes:
    def test_uint32_array_round_trip(self):
        values = [1, 2, 3, 4]
        raw = pack_value('uint32_t[4]', values)
        assert unpack_value('uint32_t[4]', raw) == values

    def test_float_array_round_trip(self):
        values = [1.0, 2.0, 3.0]
        raw = pack_value('float[3]', values)
        result = unpack_value('float[3]', raw)
        assert all(abs(a - b) < 1e-5 for a, b in zip(result, values))

    def test_array_truncates_to_max(self):
        raw = pack_value('uint32_t[2]', [10, 20, 30])  # 3 items, max 2
        result = unpack_value('uint32_t[2]', raw)
        assert result == [10, 20]

    def test_array_length_correct(self):
        raw = pack_value('uint16_t[4]', [1, 2, 3, 4])
        assert len(raw) == 8  # 4 × 2 bytes

    def test_int16_array_negative(self):
        values = [-1, -100, 100]
        raw = pack_value('int16_t[3]', values)
        assert unpack_value('int16_t[3]', raw) == values


# ---------------------------------------------------------------------------
# Enum support
# ---------------------------------------------------------------------------

class TestEnumSerialization:
    ENUMS = {
        'MotorMode': {
            'id': 'MotorMode',
            'base_type': 'uint8_t',
            'values': {'IDLE': 0, 'RUN': 1, 'BRAKE': 2},
            'bitmask': False,
        }
    }

    def test_pack_enum_name(self):
        raw = pack_value('MotorMode', 'RUN', self.ENUMS)
        assert raw == struct.pack('<B', 1)

    def test_pack_enum_int_passthrough(self):
        raw = pack_value('MotorMode', 2, self.ENUMS)
        assert raw == struct.pack('<B', 2)

    def test_unpack_enum_name(self):
        raw = struct.pack('<B', 0)
        assert unpack_value('MotorMode', raw, self.ENUMS) == 'IDLE'

    def test_unpack_unknown_int_passthrough(self):
        raw = struct.pack('<B', 99)
        assert unpack_value('MotorMode', raw, self.ENUMS) == 99

    def test_pack_unknown_enum_name_raises(self):
        with pytest.raises(ValueError, match='Unknown enum value'):
            pack_value('MotorMode', 'UNKNOWN_VAL', self.ENUMS)

    def test_no_enums_dict(self):
        # Without enums dict, MotorMode is an unknown type
        with pytest.raises(ValueError, match='Unknown type'):
            pack_value('MotorMode', 1)


# ---------------------------------------------------------------------------
# Error cases
# ---------------------------------------------------------------------------

class TestErrors:
    def test_unknown_base_type_raises(self):
        with pytest.raises(ValueError):
            pack_value('nonexistent_t', 42)

    def test_unpack_too_short_raises(self):
        with pytest.raises(ValueError, match='too short'):
            unpack_value('uint32_t', b'\x01\x02')  # needs 4 bytes, got 2

    def test_char_single_encode(self):
        raw = pack_value('char', 'A')
        assert raw == b'A'
        assert unpack_value('char', b'A') == 'A'
