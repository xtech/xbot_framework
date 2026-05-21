"""Tests for schema.py — ServiceSchema construction, lookup, compatibility."""
import json
import os
import tempfile
import pytest

from xbot_service_interface.schema import ServiceSchema
from xbot_service_interface.exceptions import UnknownChannelError
from tests.conftest import ECHO_DESC, ENUM_DESC, OPTIONAL_REG_DESC


# ---------------------------------------------------------------------------
# Construction
# ---------------------------------------------------------------------------

class TestSchemaConstruction:
    def test_from_dict(self):
        s = ServiceSchema.from_dict(ECHO_DESC)
        assert s.type    == 'EchoService'
        assert s.version == 1

    def test_from_file(self, tmp_path):
        p = tmp_path / 'svc.json'
        p.write_text(json.dumps(ECHO_DESC))
        s = ServiceSchema.from_file(str(p))
        assert s.type == 'EchoService'

    def test_from_file_path_object(self, tmp_path):
        p = tmp_path / 'svc.json'
        p.write_text(json.dumps(ECHO_DESC))
        s = ServiceSchema.from_file(p)
        assert s.version == 1

    def test_real_echo_service_json(self):
        path = os.path.join(os.path.dirname(__file__), '..', 'examples', 'echo_service.json')
        s = ServiceSchema.from_file(path)
        assert s.type == 'EchoService'
        assert s.version == 1

    def test_raw_preserves_original_dict(self):
        s = ServiceSchema.from_dict(ECHO_DESC)
        assert s.raw is ECHO_DESC or s.raw == ECHO_DESC

    def test_empty_sections(self):
        d = {'type': 'Minimal', 'version': 1,
             'inputs': [], 'outputs': [], 'registers': [], 'enums': []}
        s = ServiceSchema.from_dict(d)
        assert s.inputs == []
        assert s.outputs == []
        assert s.registers == []

    def test_missing_sections_default_empty(self):
        s = ServiceSchema.from_dict({'type': 'X', 'version': 1})
        assert s.inputs == []


# ---------------------------------------------------------------------------
# Properties — inputs / outputs / registers
# ---------------------------------------------------------------------------

class TestSchemaProperties:
    def test_inputs_count(self, echo_schema):
        assert len(echo_schema.inputs) == 1

    def test_outputs_count(self, echo_schema):
        assert len(echo_schema.outputs) == 2

    def test_registers_count(self, echo_schema):
        assert len(echo_schema.registers) == 2

    def test_input_entry_fields(self, echo_schema):
        inp = echo_schema.inputs[0]
        assert inp['id']         == 0
        assert inp['name']       == 'Input Text'
        assert inp['snake_name'] == 'input_text'
        assert inp['type_str']   == 'char[100]'
        assert inp['is_array']   == True
        assert inp['max_len']    == 100

    def test_output_entry_fields(self, echo_schema):
        out = echo_schema.get_output('Echo')
        assert out['id']         == 0
        assert out['snake_name'] == 'echo'
        assert out['is_array']   == True

    def test_scalar_output_not_array(self, echo_schema):
        out = echo_schema.get_output('Message Count')
        assert out['is_array'] == False
        assert out['max_len']  is None

    def test_register_optional_flag(self):
        s = ServiceSchema.from_dict(OPTIONAL_REG_DESC)
        req = s.get_register('Required')
        opt = s.get_register('Optional')
        assert req['optional'] == False
        assert opt['optional'] == True

    def test_register_default_preserved(self):
        d = {**ECHO_DESC, 'registers': [
            {'id': 0, 'name': 'Prefix', 'type': 'char[42]',
             'default': 'hello', 'default_length': 5},
        ]}
        s = ServiceSchema.from_dict(d)
        reg = s.get_register('Prefix')
        assert reg['default'] == 'hello'
        assert reg['default_length'] == 5


# ---------------------------------------------------------------------------
# Lookup — by id, name, snake_name
# ---------------------------------------------------------------------------

class TestSchemaLookup:
    def test_get_input_by_id(self, echo_schema):
        assert echo_schema.get_input(0)['name'] == 'Input Text'

    def test_get_input_by_name(self, echo_schema):
        assert echo_schema.get_input('Input Text')['id'] == 0

    def test_get_input_by_snake(self, echo_schema):
        assert echo_schema.get_input('input_text')['id'] == 0

    def test_get_output_by_id(self, echo_schema):
        assert echo_schema.get_output(0)['name'] == 'Echo'
        assert echo_schema.get_output(1)['name'] == 'Message Count'

    def test_get_output_by_name(self, echo_schema):
        assert echo_schema.get_output('Echo')['id'] == 0

    def test_get_output_by_snake(self, echo_schema):
        assert echo_schema.get_output('message_count')['id'] == 1

    def test_get_register_by_id(self, echo_schema):
        assert echo_schema.get_register(0)['name'] == 'Prefix'

    def test_get_register_by_name(self, echo_schema):
        assert echo_schema.get_register('Prefix')['id'] == 0

    def test_get_register_by_snake(self, echo_schema):
        assert echo_schema.get_register('echo_count')['id'] == 1

    def test_unknown_input_raises(self, echo_schema):
        with pytest.raises(UnknownChannelError):
            echo_schema.get_input('no_such_input')

    def test_unknown_output_raises(self, echo_schema):
        with pytest.raises(UnknownChannelError):
            echo_schema.get_output(99)

    def test_unknown_register_raises(self, echo_schema):
        with pytest.raises(UnknownChannelError):
            echo_schema.get_register('ghost')

    def test_input_not_found_in_output(self, echo_schema):
        with pytest.raises(UnknownChannelError):
            echo_schema.get_output('Input Text')  # input, not output


# ---------------------------------------------------------------------------
# Enum support
# ---------------------------------------------------------------------------

class TestSchemaEnums:
    def test_enums_dict_keys(self, enum_schema):
        assert 'MotorMode' in enum_schema.enums_dict

    def test_enum_values(self, enum_schema):
        e = enum_schema.enums_dict['MotorMode']
        assert e['values'] == {'IDLE': 0, 'RUN': 1, 'BRAKE': 2}
        assert e['base_type'] == 'uint8_t'
        assert e['bitmask'] == False

    def test_no_enums_returns_empty(self, echo_schema):
        assert echo_schema.enums_dict == {}

    def test_bitmask_flag(self):
        d = {**ECHO_DESC, 'enums': [{'id': 'Flags', 'base_type': 'uint8_t',
                                      'values': {'A': 0}, 'bitmask': True}]}
        s = ServiceSchema.from_dict(d)
        assert s.enums_dict['Flags']['bitmask'] == True


# ---------------------------------------------------------------------------
# Compatibility check
# ---------------------------------------------------------------------------

class TestSchemaCompatibility:
    def test_exact_match(self, echo_schema):
        assert echo_schema.is_compatible({'type': 'EchoService', 'version': 1})

    def test_wrong_type(self, echo_schema):
        assert not echo_schema.is_compatible({'type': 'IMUService', 'version': 1})

    def test_wrong_version(self, echo_schema):
        assert not echo_schema.is_compatible({'type': 'EchoService', 'version': 2})

    def test_both_wrong(self, echo_schema):
        assert not echo_schema.is_compatible({'type': 'X', 'version': 99})

    def test_missing_type(self, echo_schema):
        assert not echo_schema.is_compatible({'version': 1})

    def test_missing_version(self, echo_schema):
        assert not echo_schema.is_compatible({'type': 'EchoService'})

    def test_version_as_string_coerced(self, echo_schema):
        # is_compatible coerces version to int, so '1' == 1 is compatible
        assert echo_schema.is_compatible({'type': 'EchoService', 'version': '1'}) is True
