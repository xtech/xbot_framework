import json
import cbor2
import cog

# Supported types for raw encoding
# we can also encode arrays of these basic types.
raw_encoding_valid_types = [
    "char",
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "int8_t",
    "int16_t",
    "int32_t",
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
                result += ", "
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
    json_service.setdefault("enums", [])
    check_unique_ids(json_service["enums"])
    check_unique_ids(json_service["inputs"])
    check_unique_ids(json_service["outputs"])
    check_unique_ids(json_service["registers"])

    # Transform enums
    valid_types = raw_encoding_valid_types
    service["enums"] = []
    for enum in json_service["enums"]:
        valid_types.append(enum["id"])
        service["enums"].append({
            "id": enum["id"],
            "base_type": enum["base_type"],
            "values": enum["values"]
        })

    # Transform the input definitions
    inputs = []
    for json_input in json_service["inputs"]:
        # Convert to valid C++ function name
        input_name = toCamelCase(json_input['name'])
        input_id = int(json_input['id'])
        callback_name = f"On{input_name}Changed"
        method_name = f"Send{input_name}"
        custom_decoder_code = None
        # Handle array types (type[length])
        if "[" in json_input["type"] and "]" in json_input["type"]:
            # Split the type definition at the [, validate and get max length
            type, _, rest = json_input["type"].rpartition("[")
            # Rest needs to end with "]", it needs to be something like 123]
            if not rest.endswith("]") or type not in valid_types:
                raise Exception(f"Illegal data type: {type}!")
            max_length = int(rest.replace("]", ""))
            input = {
                "id": input_id,
                "name": input_name,
                "type": type,
                "is_array": True,
                "max_length": max_length,
                "callback_name": callback_name,
                "method_name": method_name
            }
        else:
            # Not an array type
            type = json_input["type"]
            if type not in valid_types:
                raise Exception(f"Illegal data type: {type}!")
            input = {
                "id": input_id,
                "name": input_name,
                "type": type,
                "is_array": False,
                "callback_name": callback_name,
                "custom_decoder_code": custom_decoder_code,
                "method_name": method_name
            }

        inputs.append(input)
    service["inputs"] = inputs

    # Transform the output definitions
    outputs = []
    for json_output in json_service["outputs"]:
        # Convert to valid C++ function name
        output_name = toCamelCase(json_output['name'])
        output_id = int(json_output['id'])
        method_name = f"Send{output_name}"
        callback_name = f"On{output_name}Changed"
        custom_encoder_code = None
        # Handle array types (type[length])
        if "[" in json_output["type"] and "]" in json_output["type"]:
            # Split the type definition at the [, validate and get max length
            type, _, rest = json_output["type"].rpartition("[")
            # Rest needs to end with "]", it needs to be something like 123]
            if not rest.endswith("]") or type not in valid_types:
                raise Exception(f"Illegal data type: {type}!")
            max_length = int(rest.replace("]", ""))
            output = {
                "id": output_id,
                "name": output_name,
                "type": type,
                "is_array": True,
                "max_length": max_length,
                "method_name": method_name,
                "callback_name": callback_name
            }
        else:
            # Not an array type
            type = json_output["type"]
            if type not in valid_types:
                raise Exception(f"Illegal data type: {type}!")
            output = {
                "id": output_id,
                "name": output_name,
                "type": type,
                "is_array": False,
                "method_name": method_name,
                "custom_encoder_code": custom_encoder_code,
                "callback_name": callback_name
            }

        outputs.append(output)
    service["outputs"] = outputs

    # Transform register definitions
    registers = []
    if "registers" in json_service:
        for json_register in json_service["registers"]:
            # Convert to valid C++ function name
            register_name = toCamelCase(json_register['name'])
            register_id = int(json_register['id'])
            callback_name = f"OnRegister{register_name}Changed"
            method_name = f"SetRegister{register_name}"
            custom_decoder_code = None
            # Handle array types (type[length])
            if "[" in json_register["type"] and "]" in json_register["type"]:
                # Split the type definition at the [, validate and get max length
                type, _, rest = json_register["type"].rpartition("[")
                # Rest needs to end with "]", it needs to be something like 123]
                if not rest.endswith("]") or type not in valid_types:
                    raise Exception(f"Illegal data type: {type}!")
                if "default" in json_register and not "default_length" in json_register:
                    raise Exception(f"Default value provided for array register but no default_length provided")
                max_length = int(rest.replace("]", ""))
                register = {
                    "id": register_id,
                    "name": register_name,
                    "type": type,
                    "is_array": True,
                    "max_length": max_length,
                    "callback_name": callback_name,
                    "method_name": method_name,
                    "default": json_register.get("default", None),
                    "default_length": json_register.get("default_length", None)
                }
            else:
                # Not an array type
                type = json_register["type"]
                if type not in valid_types:
                    raise Exception(f"Illegal data type: {type}!")
                register = {
                    "id": register_id,
                    "name": register_name,
                    "type": type,
                    "is_array": False,
                    "callback_name": callback_name,
                    "custom_decoder_code": custom_decoder_code,
                    "method_name": method_name,
                    "default": json_register.get("default", None),
                }

            registers.append(register)
        service["registers"] = registers
    else:
        service["registers"] = []

    additional_includes = []
    service["additional_includes"] = additional_includes

    return service

def generateEnums(service):
    for enum in service["enums"]:
        cog.outl(f"enum class {enum['id']} : {enum['base_type']} {{")
        for id, value in enum["values"].items():
            cog.outl(f"  {id} = {value},")
        cog.outl("};\n")
