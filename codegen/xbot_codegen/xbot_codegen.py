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
        max_length = int(match.group(2))
        return base_type, max_length
    else:
        return type, None


def array_type_attrs(max_length):
    if max_length is not None:
        return {"is_array": True, "max_length": max_length}
    else:
        return {"is_array": False}


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
        "callback_name": callback_name.format(name),
        "send_method_name": send_method_name.format(name),
        **array_type_attrs(max_length)
    }


def loadService(path: str) -> dict:
    # Fetch the service definition
    with open(path) as f:
        json_service = json.load(f)

    # Build the dict for code generation.
    service = {
        "type": json_service["type"],
        "version": int(json_service["version"]),
        "class_name": toCamelCase(json_service["type"]) + "Base",
        "interface_class_name": toCamelCase(json_service["type"]) + "InterfaceBase",
        "service_json": json.dumps(json_service, indent=2),
        "service_cbor": cbor2.dumps(json_service)
    }

    # Consistency checks
    for key in ["enums", "inputs", "outputs", "registers"]:
        json_service.setdefault(key, [])
        service.setdefault(key, [])
        check_unique_ids(json_service[key])

    # Transform enums
    valid_types = raw_encoding_valid_types
    for enum in json_service["enums"]:
        valid_types.append(enum["id"])
        service["enums"].append({
            "id": enum["id"],
            "base_type": enum["base_type"],
            "values": enum["values"],
            "bitmask": enum.get("bitmask", False),
        })

    # Transform the input definitions
    for json_input in json_service["inputs"]:
        input = common_attrs(json_input, valid_types, "On{}Changed", "Send{}")
        service["inputs"].append(input)

    # Transform the output definitions
    for json_output in json_service["outputs"]:
        output = common_attrs(json_output, valid_types, "On{}Changed", "Send{}")
        service["outputs"].append(output)

    # Transform register definitions
    for json_register in json_service["registers"]:
        register = common_attrs(json_register, valid_types + ["blob"], "OnRegister{}Changed", "SetRegister{}")

        if "default" in json_register:
            register["default"] = json_register["default"]
            if register['is_array']:
                if "default_length" not in json_register:
                    raise Exception(f"Default value provided for array register but no default_length provided")
                register["default_length"] = json_register["default_length"]

        service["registers"].append(register)

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
