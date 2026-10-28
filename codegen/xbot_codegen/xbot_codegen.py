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
    seen_input_names = set()
    for json_input in json_service["inputs"]:
        input = common_attrs(json_input, valid_types, "On{}Changed", "Send{}")
        if input["name"] in seen_input_names:
            raise Exception(
                f"Duplicate normalized input name '{input['name']}' "
                f"(from '{json_input['name']}', id={json_input['id']})")
        seen_input_names.add(input["name"])
        service["inputs"].append(input)

    # Transform the output definitions
    seen_output_names = set()
    for json_output in json_service["outputs"]:
        output = common_attrs(json_output, valid_types, "On{}Changed", "Send{}")
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
        if return_type_str == "void":
            return_base_type, return_max_length = None, None
        else:
            return_base_type, return_max_length = parse_type(return_type_str)
            if return_base_type not in valid_types:
                raise Exception(f"Illegal return type for function '{func_name}': {return_type_str}")
            if return_max_length is not None:
                if return_max_length <= 0 or return_max_length > MAX_ARRAY_LENGTH:
                    raise Exception(
                        f"Illegal array return length {return_max_length} for function '{func_name}' "
                        f"return type '{return_type_str}': must be 1..{MAX_ARRAY_LENGTH}")
        return_is_array = return_max_length is not None

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
            if param_type not in valid_types:
                raise Exception(f"Illegal parameter type in function '{func_name}': {param_type}")
            params.append({
                "id": int(json_param["id"]),
                "name": param_name,
                "type": param_type,
                **array_type_attrs(param_max_length),
            })

        service["functions"].append({
            "id": func_id,
            "name": func_name,
            "return_type": return_type_str,
            "return_base_type": return_base_type,
            "return_is_array": return_is_array,
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
