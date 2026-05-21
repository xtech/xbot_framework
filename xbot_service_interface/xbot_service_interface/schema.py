import json
from pathlib import Path
from typing import Union

from .exceptions import UnknownChannelError
from .serialization import to_snake_case, parse_type_string


class ServiceSchema:
    """Parsed representation of a service.json definition."""

    def __init__(self, d: dict):
        self._raw = d
        self.type: str = d['type']
        self.version: int = int(d['version'])

        # Parse enums first — they extend the valid type set
        self._enums: dict = {}
        for e in d.get('enums', []):
            self._enums[e['id']] = {
                'id':        e['id'],
                'base_type': e['base_type'],
                'values':    dict(e['values']),   # name → int
                'bitmask':   e.get('bitmask', False),
            }

        self._inputs    = self._index(d.get('inputs',    []))
        self._outputs   = self._index(d.get('outputs',   []))
        self._registers = self._index(d.get('registers', []), is_register=True)

    # ------------------------------------------------------------------
    # Construction helpers
    # ------------------------------------------------------------------

    @classmethod
    def from_dict(cls, d: dict) -> 'ServiceSchema':
        return cls(d)

    @classmethod
    def from_file(cls, path: Union[str, Path]) -> 'ServiceSchema':
        with open(path) as f:
            return cls(json.load(f))

    # ------------------------------------------------------------------
    # Compatibility check (mirrors C++ ServiceInterfaceBase::OnServiceDiscovered)
    # ------------------------------------------------------------------

    def is_compatible(self, advertised_desc: dict) -> bool:
        """Return True if advertised type and version match this schema."""
        return (advertised_desc.get('type') == self.type and
                int(advertised_desc.get('version', -1)) == self.version)

    # ------------------------------------------------------------------
    # Channel lookup (by id, original name, or snake_case name)
    # ------------------------------------------------------------------

    def get_input(self, name_or_id) -> dict:
        return self._lookup(self._inputs, name_or_id, 'input')

    def get_output(self, name_or_id) -> dict:
        return self._lookup(self._outputs, name_or_id, 'output')

    def get_register(self, name_or_id) -> dict:
        return self._lookup(self._registers, name_or_id, 'register')

    # ------------------------------------------------------------------
    # Properties
    # ------------------------------------------------------------------

    @property
    def inputs(self) -> list:
        return list(self._inputs['by_id'].values())

    @property
    def outputs(self) -> list:
        return list(self._outputs['by_id'].values())

    @property
    def registers(self) -> list:
        return list(self._registers['by_id'].values())

    @property
    def enums_dict(self) -> dict:
        return self._enums

    @property
    def raw(self) -> dict:
        return self._raw

    # ------------------------------------------------------------------
    # Internal
    # ------------------------------------------------------------------

    def _index(self, items: list, is_register: bool = False) -> dict:
        by_id    = {}
        by_name  = {}
        by_snake = {}

        for item in items:
            cid       = int(item['id'])
            name      = item['name']
            type_str  = item['type']
            snake     = to_snake_case(name)
            base, is_array, max_len = parse_type_string(type_str)

            entry = {
                'id':         cid,
                'name':       name,
                'snake_name': snake,
                'type_str':   type_str,
                'base_type':  base,
                'is_array':   is_array,
                'max_len':    max_len,
            }
            if is_register:
                entry['optional'] = item.get('optional', False)
                if 'default' in item:
                    entry['default'] = item['default']
                if 'default_length' in item:
                    entry['default_length'] = item['default_length']

            by_id[cid]      = entry
            by_name[name]   = entry
            by_snake[snake] = entry

        return {'by_id': by_id, 'by_name': by_name, 'by_snake': by_snake}

    def _lookup(self, index: dict, name_or_id, kind: str) -> dict:
        if isinstance(name_or_id, int):
            if name_or_id in index['by_id']:
                return index['by_id'][name_or_id]
        else:
            if name_or_id in index['by_name']:
                return index['by_name'][name_or_id]
            snake = to_snake_case(str(name_or_id))
            if snake in index['by_snake']:
                return index['by_snake'][snake]
        raise UnknownChannelError(name_or_id, kind)
