import json
import cbor2
import cog
import re

# Supported types for raw encoding
# we can also encode arrays of these basic types.
raw_encoding_valid_types = [
    "char",
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "uint64_t",
    "int8_t",
    "int16_t",
    "int32_t",
    "int64_t",
    "float",
    "double",
]


# Convert a binary string to a value we can use in our header file.
def binary2c_array(data):
    result = "{\n"
    # Spread across lines, so that we don't have one huge line
    for line in range(0, len(data), 8):
        result += "  "
        for idx, b in enumerate(data[line:line + 8], start=line):
            result += f"0x{b:02X}"
            if idx < (len(data) - 1):
                result += ","
                if idx % 8 < 7:
                    result += " "
        result += "\n"
    result += "};"
    return result


# Convert names to CamelCase for use in function names.
def toCamelCase(name):
    return ''.join(x for x in name if not x.isspace())


def check_unique_ids(l):
    id_set = set()
    for dict in l:
        id = dict['id']
        if id in id_set:
            raise Exception("Duplicate ID found: {}".format(id))
        else:
            id_set.add(id)


def parse_type(type):
    match = re.match(r"(.+)\[(\d+)\]$", type)
    if match:
        base_type = match.group(1)
        if base_type == "blob":
            raise Exception("Blob type cannot be an array!")
        if base_type == "string":
            raise Exception("String type declares its limit as max_length, not as an array length!")
        max_length = int(match.group(2))
        return base_type, max_length
    else:
        return type, None


def array_type_attrs(max_length):
    if max_length is not None:
        return {"is_array": True, "max_length": max_length}
    else:
        return {"is_array": False}


# sizeof of the scalar types in the generated C++ code (and on the wire).
TYPE_SIZES = {
    "char": 1,
    "uint8_t": 1,
    "int8_t": 1,
    "uint16_t": 2,
    "int16_t": 2,
    "uint32_t": 4,
    "int32_t": 4,
    "float": 4,
    "uint64_t": 8,
    "int64_t": 8,
    "double": 8,
}

# Largest RPC body that fits into one packet: max_packet_size(1472) - sizeof(XbotHeader)(24).
# A return value can never be larger, which is what the shared buffer pool is sized for.
MAX_RPC_RETURN_BYTES = 1472 - 24

# Array returns up to this size stay on the service thread stack. Larger ones and blob
# returns are taken from the shared buffer pool (see xbot-service/Buffer.hpp), so a
# service never has to reserve that stack space permanently.
RPC_STACK_RETURN_MAX = 128

# One parameter occupies a descriptor plus its payload in the RPC body, so it can never be
# longer than that body minus the descriptor: 1448 - 8. A larger limit could never be
# fulfilled, and the dispatcher would have to reserve max_length + 1 bytes of stack for it.
MAX_PARAMETER_BYTES = MAX_RPC_RETURN_BYTES - 8

# `string` is text on the wire: like blob it carries no terminator and its length is the
# payload size. The difference is the declared maximum, so the generated server can decode
# it into a checked, always terminated char[max_length + 1] and hand the handler a plain
# C string - no copy in the handler, no silent truncation.
MAX_STRING_LENGTH = MAX_PARAMETER_BYTES


def string_max_length(json_field, where):
    """The mandatory max_length of a string field, validated."""
    length = int(json_field.get("max_length", 0) or 0)
    if length <= 0 or length > MAX_STRING_LENGTH:
        raise Exception(f"{where}: a string needs a max_length of 1..{MAX_STRING_LENGTH}, got {length}")
    return length


def typeSize(service, type_name):
    """sizeof of a scalar or enum type in the generated C++ code."""
    for enum in service["enums"]:
        if enum["id"] == type_name:
            return TYPE_SIZES[enum["base_type"]]
    if type_name in TYPE_SIZES:
        return TYPE_SIZES[type_name]
    raise Exception(f"Cannot determine the size of type '{type_name}'")


def pooledReturnBytes(service, func):
    """Bytes a return value needs from the buffer pool, or None if it fits on the stack."""
    if func["return_type"] == "void":
        return None
    if func["return_is_blob"]:
        return MAX_RPC_RETURN_BYTES
    if func["return_is_string"]:
        # One byte more than max_length: the terminator the dispatcher appends.
        bytes_needed = func["return_max_length"] + 1
        return bytes_needed if bytes_needed > RPC_STACK_RETURN_MAX else None
    if func["return_is_array"]:
        bytes_needed = func["return_max_length"] * typeSize(service, func["return_base_type"])
        return bytes_needed if bytes_needed > RPC_STACK_RETURN_MAX else None
    return None


def needsBufferPool(service):
    """True if any generated return value comes from the buffer pool."""
    return any(pooledReturnBytes(service, func) is not None for func in service["functions"])


def common_attrs(json, valid_types, callback_name, send_method_name):
    id = int(json['id'])
    name = toCamelCase(json['name'])
    type, max_length = parse_type(json["type"])
    if type not in valid_types:
        raise Exception(f"Illegal data type: {type}!")
    return {
        "id": id,
        "name": name,
        "type": type,
        "is_blob": type == "blob",
        "callback_name": callback_name.format(name),
        "send_method_name": send_method_name.format(name),
        **array_type_attrs(max_length)
    }


# The advertisement is one UDP packet. What does not fit is not advertised but served
# on request (Service::HandleDescriptionRequest), so a host without generated classes can
# still fetch the complete schema; hosts with generated classes never need it.
#
# Budget: max_packet_size(1472) - sizeof(XbotHeader)(24) - the bytes the base class adds
# around the description (80, see the static_assert in templates/ServiceTemplate.cpp).
ADVERTISED_DESCRIPTION_BUDGET = 1472 - 24 - 80

# Blocks the advertisement may drop to fit, widest/most optional first. `type` and
# `version` always stay: they identify the service.
DROPPABLE_BLOCKS = ("enums", "functions", "registers", "outputs", "inputs")

# The omitted description is handed out in DESCRIPTION_PART windows whose total size field
# is 16 bits, so more than this cannot be addressed and the split has to fail loudly here
# instead of silently truncating on the wire.
MAX_OMITTED_CBOR_SIZE = 0xFFFF


def splitDescription(json_service: dict):
    """Split the schema into what the advertisement carries and what it leaves out.

    Returns (advertised_cbor, omitted_cbor, omitted_names). Blocks are added in their
    schema order while they fit, so the packet is filled as far as possible; the names of
    everything that did not fit are listed under "omitted".
    """
    droppable = [key for key in DROPPABLE_BLOCKS if key in json_service]
    for dropped in range(len(droppable) + 1):
        omitted_names = droppable[:dropped]
        advertised = {"type": json_service["type"], "version": json_service["version"]}
        for key, value in json_service.items():
            if key in ("type", "version") or key in omitted_names:
                continue
            advertised[key] = value
        if omitted_names:
            advertised["omitted"] = omitted_names
        advertised_cbor = cbor2.dumps(advertised)
        if len(advertised_cbor) <= ADVERTISED_DESCRIPTION_BUDGET:
            omitted = {key: json_service[key] for key in omitted_names}
            omitted_cbor = cbor2.dumps(omitted)
            if len(omitted_cbor) > MAX_OMITTED_CBOR_SIZE:
                raise Exception(
                    f"{json_service['type']}: the omitted description is {len(omitted_cbor)} bytes, "
                    f"more than the 16-bit DESCRIPTION_PART total limit of {MAX_OMITTED_CBOR_SIZE}")
            return advertised_cbor, omitted_cbor, omitted_names
    kept = [key for key in json_service if key not in DROPPABLE_BLOCKS]
    raise Exception(
        f"{json_service['type']}: the blocks that cannot be omitted ({', '.join(kept)}) do not "
        f"fit into the advertisement budget of {ADVERTISED_DESCRIPTION_BUDGET} bytes")


def loadService(path: str) -> dict:
    # Fetch the service definition
    with open(path) as f:
        json_service = json.load(f)

    advertised_cbor, omitted_cbor, omitted_names = splitDescription(json_service)

    # Build the dict for code generation.
    service = {
        "type": json_service["type"],
        "version": int(json_service["version"]),
        "class_name": toCamelCase(json_service["type"]) + "Base",
        "interface_class_name": toCamelCase(json_service["type"]) + "InterfaceBase",
        "service_json": json.dumps(json_service, indent=2),
        # Advertised as far as it fits; the rest is served on request.
        "service_cbor": advertised_cbor,
        "omitted_cbor": omitted_cbor,
        "omitted_names": omitted_names
    }

    # Consistency checks
    for key in ["enums", "inputs", "outputs", "registers"]:
        json_service.setdefault(key, [])
        service.setdefault(key, [])
        check_unique_ids(json_service[key])

    # Transform enums
    valid_types = raw_encoding_valid_types
    for enum in json_service["enums"]:
        # An enum without values has no members to generate, and the to_string
        # path below derives its count from max(values) - so reject it here
        # instead of failing with a ValueError while emitting code.
        if not enum.get("values"):
            raise Exception(f"Enum '{enum.get('id', '?')}' has no values")
        valid_types.append(enum["id"])
        service["enums"].append({
            "id": enum["id"],
            "base_type": enum["base_type"],
            "values": enum["values"],
            "bitmask": enum.get("bitmask", False),
            "to_string": enum.get("to_string", False),
        })

    # Transform the input definitions
    seen_input_names = set()
    for json_input in json_service["inputs"]:
        input = common_attrs(json_input, valid_types + ["blob"], "On{}Changed", "Send{}")
        if input["name"] in seen_input_names:
            raise Exception(
                f"Duplicate normalized input name '{input['name']}' "
                f"(from '{json_input['name']}', id={json_input['id']})")
        seen_input_names.add(input["name"])
        service["inputs"].append(input)

    # Transform the output definitions
    seen_output_names = set()
    for json_output in json_service["outputs"]:
        output = common_attrs(json_output, valid_types + ["blob"], "On{}Changed", "Send{}")
        if output["name"] in seen_output_names:
            raise Exception(
                f"Duplicate normalized output name '{output['name']}' "
                f"(from '{json_output['name']}', id={json_output['id']})")
        seen_output_names.add(output["name"])
        service["outputs"].append(output)

    # Transform function definitions
    json_service.setdefault("functions", [])
    service["functions"] = []
    check_unique_ids(json_service["functions"])

    MAX_ARRAY_LENGTH = 65535
    seen_func_names = set()
    for json_func in json_service["functions"]:
        func_id = int(json_func["id"])
        if func_id < 0 or func_id > 255:
            raise Exception(f"Function id {func_id} out of range: must be 0..255 (wire type is uint8_t)")
        func_name = toCamelCase(json_func["name"])
        if func_name in seen_func_names:
            raise Exception(f"Duplicate normalized function name '{func_name}' after name normalization")
        seen_func_names.add(func_name)
        return_type_str = json_func.get("return_type", "void")
        return_is_blob = False
        return_is_string = False
        if return_type_str == "void":
            return_base_type, return_max_length = None, None
        else:
            return_base_type, return_max_length = parse_type(return_type_str)
            if return_base_type not in valid_types + ["blob", "string"]:
                raise Exception(f"Illegal return type for function '{func_name}': {return_type_str}")
            if return_base_type == "blob":
                # Variable length and therefore bounded by one packet only
                # (MAX_RPC_RETURN_BYTES); the generated dispatcher takes the buffer
                # from the shared pool.
                return_is_blob = True
            if return_base_type == "string":
                return_is_string = True
                # string_max_length() already caps this at one packet body minus a
                # descriptor, which a return value always fits.
                return_max_length = string_max_length(json_func, f"function '{func_name}' return")
            if return_max_length is not None and not return_is_string:
                if return_max_length <= 0 or return_max_length > MAX_ARRAY_LENGTH:
                    raise Exception(
                        f"Illegal array return length {return_max_length} for function '{func_name}' "
                        f"return type '{return_type_str}': must be 1..{MAX_ARRAY_LENGTH}")
                # A return value travels in one packet, so a schema that declares more can
                # never be answered. Fail here instead of at runtime.
                return_bytes = return_max_length * typeSize(service, return_base_type)
                if return_bytes > MAX_RPC_RETURN_BYTES:
                    raise Exception(
                        f"Function '{func_name}' return type '{return_type_str}' is {return_bytes} bytes, "
                        f"more than one packet can carry ({MAX_RPC_RETURN_BYTES})")
        return_is_array = (return_max_length is not None) and not return_is_string

        check_unique_ids(json_func.get("parameters", []))
        params = []
        seen_param_names = set()
        for json_param in json_func.get("parameters", []):
            param_name = toCamelCase(json_param["name"])
            if param_name in seen_param_names:
                raise Exception(
                    f"Duplicate normalized parameter name '{param_name}' "
                    f"(from '{json_param['name']}', id={json_param['id']}) "
                    f"in function '{func_name}'")
            seen_param_names.add(param_name)
            param_type, param_max_length = parse_type(json_param["type"])
            if param_type not in valid_types + ["blob", "string"]:
                raise Exception(f"Illegal parameter type in function '{func_name}': {param_type}")
            param_is_string = param_type == "string"
            if param_is_string:
                param_max_length = string_max_length(
                    json_param, f"parameter '{param_name}' of function '{func_name}'")
            elif param_max_length is not None:
                # A parameter travels in one packet, and the dispatcher reserves that much
                # stack for it - a declaration above one packet is never usable.
                param_bytes = param_max_length * typeSize(service, param_type)
                if param_bytes > MAX_PARAMETER_BYTES:
                    raise Exception(
                        f"Function '{func_name}' parameter '{param_name}' is {param_bytes} bytes, "
                        f"more than one packet can carry ({MAX_PARAMETER_BYTES})")
            params.append({
                "id": int(json_param["id"]),
                "name": param_name,
                "type": param_type,
                "is_blob": param_type == "blob",
                "is_string": param_is_string,
                "max_length": param_max_length if param_is_string else None,
                **( {"is_array": False} if param_is_string else array_type_attrs(param_max_length) ),
            })

        service["functions"].append({
            "id": func_id,
            "name": func_name,
            "return_type": return_type_str,
            "return_base_type": return_base_type,
            "return_is_array": return_is_array,
            "return_is_blob": return_is_blob,
            "return_is_string": return_is_string,
            "return_max_length": return_max_length,
            "parameters": params,
        })

    # Transform register definitions
    seen_register_names = set()
    for json_register in json_service["registers"]:
        register = common_attrs(json_register, valid_types + ["blob"], "OnRegister{}Changed", "SetRegister{}")
        if register["name"] in seen_register_names:
            raise Exception(
                f"Duplicate normalized register name '{register['name']}' "
                f"(from '{json_register['name']}', id={json_register['id']})")
        seen_register_names.add(register["name"])

        if "default" in json_register:
            register["default"] = json_register["default"]
            if register['is_array']:
                if "default_length" not in json_register:
                    raise Exception(f"Default value provided for array register but no default_length provided")
                register["default_length"] = json_register["default_length"]

        if "optional" in json_register:
            register["optional"] = json_register["optional"]

        service["registers"].append(register)

    # The frame budget helpers are members of one class, and their names are built from the
    # function and parameter names: Write(PathData) and WritePath(Data) both generate
    # MaxWritePathData, which cannot compile. The Go validator reserves the same names.
    seen_helpers = {}
    for func in service["functions"]:
        for p in func["parameters"]:
            if not (p["is_blob"] or p["is_string"]):
                continue
            helper = f"Max{func['name']}{p['name']}"
            what = f"the frame budget helper of parameter '{p['name']}' of function '{func['name']}'"
            if helper in seen_helpers:
                raise Exception(f"{seen_helpers[helper]} and {what} both generate {helper}")
            seen_helpers[helper] = what

    return service


def generateEnums(service):
    for enum in service["enums"]:
        if enum['bitmask']:
            cog.outl(f"namespace {enum['id']} {{")
            cog.outl(f"  enum Value : {enum['base_type']} {{")
            for id, value in enum["values"].items():
                cog.outl(f"    {id} = 1 << {value},")
            cog.outl("  };")
            cog.outl("};\n")
        else:
            cog.outl(f"enum class {enum['id']} : {enum['base_type']} {{")
            for id, value in enum["values"].items():
                cog.outl(f"  {id} = {value},")
            cog.outl("};\n")

            if enum["to_string"]:
                cog.outl(f"inline constexpr size_t {enum['id']}_count = {max(enum['values'].values()) + 1};")
                cog.outl(f"inline const char* {enum['id']}_to_string({enum['id']} v) {{")
                cog.outl("  switch (v) {")
                seen = set()
                for name, value in enum["values"].items():
                    if value in seen:
                        continue
                    seen.add(value)
                    cog.outl(f"    case {enum['id']}::{name}: return \"{name.lower()}\";")
                cog.outl("    default: return \"\";")
                cog.outl("  }")
                cog.outl("}")
