class IncompatibleServiceError(Exception):
    def __init__(self, expected_type: str, expected_version: int,
                 found_type: str, found_version: int):
        self.expected_type = expected_type
        self.expected_version = expected_version
        self.found_type = found_type
        self.found_version = found_version
        super().__init__(
            f"Service incompatible: expected {expected_type!r} v{expected_version}, "
            f"got {found_type!r} v{found_version}"
        )


class UnknownChannelError(KeyError):
    def __init__(self, name_or_id, kind: str = 'channel'):
        self.name_or_id = name_or_id
        self.kind = kind
        super().__init__(f"Unknown {kind}: {name_or_id!r}")
