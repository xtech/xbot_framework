// @formatter:off
// clang-format off

/*[[[cog
import cog
import xbot_codegen

service = xbot_codegen.loadService(service_file)

#Generate include guard
cog.outl(f"#ifndef {service['interface_class_name'].upper()}_HPP")
cog.outl(f"#define {service['interface_class_name'].upper()}_HPP")

]]]*/
#ifndef SERVICETEMPLATEINTERFACEBASE_HPP
#define SERVICETEMPLATEINTERFACEBASE_HPP
//[[[end]]]

#include <xbot-service-interface/ServiceInterfaceBase.hpp>
#include <xbot-service-interface/XbotServiceInterface.hpp>
#include <xbot/config.hpp>

#include <string>

/*[[[cog
xbot_codegen.generateEnums(service)
]]]*/
enum class ExampleEnumClass : uint8_t {
  VALUE1 = 0,
  VALUE2 = 1,
  VALUE3 = 2,
};

namespace ExampleBitmaskEnum {
  enum Value : uint8_t {
    VALUE1 = 1 << 0,
    VALUE2 = 1 << 1,
    VALUE3 = 1 << 2,
  };
};

//[[[end]]]
/*[[[cog
cog.outl(f"class {service['interface_class_name']} : public xbot::serviceif::ServiceInterfaceBase {{")
]]]*/
class ServiceTemplateInterfaceBase : public xbot::serviceif::ServiceInterfaceBase {
//[[[end]]]
public:
    /*[[[cog
    cog.outl(f"explicit {service['interface_class_name']}(uint16_t service_id, xbot::serviceif::Context ctx) : ServiceInterfaceBase(service_id, \"{service['type']}\", {service['version']}, ctx) {{}}")
    ]]]*/
    explicit ServiceTemplateInterfaceBase(uint16_t service_id, xbot::serviceif::Context ctx) : ServiceInterfaceBase(service_id, "ServiceTemplate", 1, ctx) {}
    //[[[end]]]

    /*[[[cog
    # Generate send functions for each input.
    for input in service["inputs"]:
        if input['is_blob']:
            cog.outl(f"bool {input['send_method_name']}(const uint8_t* data, uint32_t length);")
        elif input['is_array']:
            cog.outl(f"bool {input['send_method_name']}(const {input['type']}* data, uint32_t length);")
        else:
            cog.outl(f"bool {input['send_method_name']}(const {input['type']} &data);")
    ]]]*/
    bool SendExampleInput1(const char* data, uint32_t length);
    bool SendExampleInput2(const uint32_t &data);
    bool SendExampleInputBlob(const uint8_t* data, uint32_t length);
    //[[[end]]]

    /*[[[cog
    # Generate blocking Call* methods for each function.
    for func in service["functions"]:
        params = []
        for p in func["parameters"]:
            if p['is_blob']:
                params.append(f"const uint8_t* {p['name']}, uint32_t {p['name']}Len")
            elif p['is_string']:
                params.append(f"const char* {p['name']}")
            elif p['is_array']:
                params.append(f"const {p['type']}* {p['name']}, uint32_t {p['name']}Len")
            else:
                params.append(f"const {p['type']}& {p['name']}")
        if func['return_is_blob']:
            params.append("uint8_t* data, uint16_t& result_length")
        elif func['return_is_string']:
            params.append("std::string& result")
        elif func['return_is_array']:
            params.append(f"{func['return_base_type']}* data, uint16_t& result_length")
        elif func["return_type"] != "void":
            params.append(f"{func['return_type']}& result")
        params.append("uint32_t timeout_ms = 1000")
        params_str = ", ".join(params)
        cog.outl(f"bool Call{func['name']}({params_str});")
    ]]]*/
    bool CallNoParamsNoReturn(uint32_t timeout_ms = 1000);
    bool CallScalarParamsWithReturn(const float& Speed, const uint32_t& Count, const uint8_t& Enable, int32_t& result, uint32_t timeout_ms = 1000);
    bool CallArrayParamNoReturn(const char* Label, uint32_t LabelLen, uint32_t timeout_ms = 1000);
    bool CallMixedParamsWithReturn(const char* Name, uint32_t NameLen, const float& Value, uint8_t& result, uint32_t timeout_ms = 1000);
    bool CallBlobParamsWithBlobReturn(const uint8_t* Data, uint32_t DataLen, uint8_t* data, uint16_t& result_length, uint32_t timeout_ms = 1000);
    bool CallLargeArrayReturn(uint8_t* data, uint16_t& result_length, uint32_t timeout_ms = 1000);
    bool CallStringParamsWithStringReturn(const char* Path, std::string& result, uint32_t timeout_ms = 1000);
    bool CallLargeStringReturn(std::string& result, uint32_t timeout_ms = 1000);
    //[[[end]]]

    /*[[[cog
    # A blob parameter is chunked by the caller, so the frame budget belongs next to the
    # call that has the limit: one packet of body minus the framing of the call. Every
    # subtraction saturates, so a call whose parameters use up the whole packet reports 0
    # instead of a wrapped (huge) budget.
    # A string parameter is text: it is sent as its bytes (no terminator) and a variable
    # length. Like a blob it therefore uses up budget, but the caller only passes the C
    # string, the generated code measures it.
    blob_funcs = [f for f in service["functions"] if any(p['is_blob'] or p['is_string'] for p in f["parameters"])]
    if blob_funcs:
        cog.outl("    // Saturating subtraction for the frame budgets below: a call whose parameters")
        cog.outl("    // use up the whole packet reports 0 instead of a wrapped (huge) budget.")
        cog.outl("    static constexpr size_t RemainingBudget(size_t budget) {")
        cog.outl("      return budget;")
        cog.outl("    }")
        cog.outl("    static constexpr size_t RemainingBudget(size_t budget, size_t used) {")
        cog.outl("      return (used < budget) ? (budget - used) : 0;")
        cog.outl("    }")
        cog.outl("    template <typename... Rest>")
        cog.outl("    static constexpr size_t RemainingBudget(size_t budget, size_t used, Rest... rest) {")
        cog.outl("      return RemainingBudget((used < budget) ? (budget - used) : 0, rest...);")
        cog.outl("    }")
    for func in blob_funcs:
        blobs = [p for p in func["parameters"] if p['is_blob'] or p['is_string']]
        fixed = sum(
            (p['max_length'] * xbot_codegen.typeSize(service, p['type'])) if p['is_array']
            else xbot_codegen.typeSize(service, p['type'])
            for p in func["parameters"] if not (p['is_blob'] or p['is_string']))
        for target in blobs:
            others = [p for p in blobs if p['name'] != target['name']]
            args = ", ".join(f"size_t {p['name']}Len" for p in others)
            # Everything but the packet itself goes through RemainingBudget, the descriptor
            # framing included: a call with 182 parameters needs more bytes for its
            # descriptors than a packet has, and that subtraction would wrap as well.
            used = [f"sizeof(xbot::datatypes::XbotHeader) + "
                    f"{len(func['parameters'])} * sizeof(xbot::datatypes::DataDescriptor)"]
            if fixed:
                used.append(f"static_cast<size_t>({fixed})")
            used += [f"{p['name']}Len" for p in others]
            budget = "RemainingBudget(xbot::config::max_packet_size, " + ", ".join(used) + ")"
            name = f"Max{func['name']}{target['name']}"
            cog.outl(f"    // {name} is the largest {target['name']} payload one {func['name']} call can carry.")
            cog.outl(f"    static constexpr size_t {name}({args}) {{")
            cog.outl(f"      return {budget};")
            cog.outl("    }")
    ]]]*/
    // Saturating subtraction for the frame budgets below: a call whose parameters
    // use up the whole packet reports 0 instead of a wrapped (huge) budget.
    static constexpr size_t RemainingBudget(size_t budget) {
      return budget;
    }
    static constexpr size_t RemainingBudget(size_t budget, size_t used) {
      return (used < budget) ? (budget - used) : 0;
    }
    template <typename... Rest>
    static constexpr size_t RemainingBudget(size_t budget, size_t used, Rest... rest) {
      return RemainingBudget((used < budget) ? (budget - used) : 0, rest...);
    }
    // MaxBlobParamsWithBlobReturnData is the largest Data payload one BlobParamsWithBlobReturn call can carry.
    static constexpr size_t MaxBlobParamsWithBlobReturnData() {
      return RemainingBudget(xbot::config::max_packet_size, sizeof(xbot::datatypes::XbotHeader) + 1 * sizeof(xbot::datatypes::DataDescriptor));
    }
    // MaxStringParamsWithStringReturnPath is the largest Path payload one StringParamsWithStringReturn call can carry.
    static constexpr size_t MaxStringParamsWithStringReturnPath() {
      return RemainingBudget(xbot::config::max_packet_size, sizeof(xbot::datatypes::XbotHeader) + 1 * sizeof(xbot::datatypes::DataDescriptor));
    }
    //[[[end]]]

    /*[[[cog
    # Generate send functions for each register.
    for register in service["registers"]:
        if register['type'] == "blob":
          cog.outl(f"bool {register['send_method_name']}(const void* data, size_t length);")
        elif register['is_array']:
            cog.outl(f"bool {register['send_method_name']}(const {register['type']}* data, uint32_t length);")
        else:
            cog.outl(f"bool {register['send_method_name']}(const {register['type']} &data);")
    ]]]*/
    bool SetRegisterRegister1(const char* data, uint32_t length);
    bool SetRegisterRegister2(const uint32_t &data);
    bool SetRegisterRegister3(const void* data, size_t length);
    bool SetRegisterRegister4Optional(const uint32_t &data);
    //[[[end]]]

protected:
    /*[[[cog
    # Generate callback functions for each service output.
    for output in service["outputs"]:
        if output['is_blob']:
            cog.outl(f"virtual void {output['callback_name']}(const uint8_t* new_value, uint32_t length) {{")
            cog.outl(f"(void)new_value;")
            cog.outl(f"(void)length;")
            cog.outl(f"}};")
        elif output['is_array']:
            cog.outl(f"virtual void {output['callback_name']}(const {output['type']}* new_value, uint32_t length) {{")
            cog.outl(f"(void)new_value;")
            cog.outl(f"(void)length;")
            cog.outl(f"}};")
        else:
            cog.outl(f"virtual void {output['callback_name']}(const {output['type']} &new_value) {{(void) new_value;}};")
    ]]]*/
    virtual void OnExampleOutput1Changed(const char* new_value, uint32_t length) {
    (void)new_value;
    (void)length;
    };
    virtual void OnExampleOutput2Changed(const uint32_t &new_value) {(void) new_value;};
    virtual void OnExampleOutputBlobChanged(const uint8_t* new_value, uint32_t length) {
    (void)new_value;
    (void)length;
    };
    //[[[end]]]

private:
  void OnData(uint16_t service_id, uint64_t timestamp, uint16_t target_id, const void *payload, size_t buflen) final;
};

/*[[[cog
cog.outl(f"\u002f*\n{service['service_json']}\n*\u002f")
]]]*/
/*
{
  "type": "ServiceTemplate",
  "version": 1,
  "inputs": [
    {
      "id": 0,
      "name": "ExampleInput1",
      "type": "char[100]"
    },
    {
      "id": 1,
      "name": "ExampleInput2",
      "type": "uint32_t"
    },
    {
      "id": 2,
      "name": "ExampleInputBlob",
      "type": "blob"
    }
  ],
  "outputs": [
    {
      "id": 0,
      "name": "ExampleOutput1",
      "type": "char[100]"
    },
    {
      "id": 1,
      "name": "ExampleOutput2",
      "type": "uint32_t"
    },
    {
      "id": 2,
      "name": "ExampleOutputBlob",
      "type": "blob"
    }
  ],
  "registers": [
    {
      "id": 0,
      "name": "Register1",
      "type": "char[42]"
    },
    {
      "id": 1,
      "name": "Register2",
      "type": "uint32_t"
    },
    {
      "id": 2,
      "name": "Register3",
      "type": "blob"
    },
    {
      "id": 3,
      "name": "Register4 Optional",
      "type": "uint32_t",
      "optional": true
    }
  ],
  "functions": [
    {
      "id": 0,
      "name": "NoParamsNoReturn",
      "parameters": [],
      "return_type": "void"
    },
    {
      "id": 1,
      "name": "ScalarParamsWithReturn",
      "parameters": [
        {
          "id": 0,
          "name": "Speed",
          "type": "float"
        },
        {
          "id": 1,
          "name": "Count",
          "type": "uint32_t"
        },
        {
          "id": 2,
          "name": "Enable",
          "type": "uint8_t"
        }
      ],
      "return_type": "int32_t"
    },
    {
      "id": 2,
      "name": "ArrayParamNoReturn",
      "parameters": [
        {
          "id": 0,
          "name": "Label",
          "type": "char[64]"
        }
      ],
      "return_type": "void"
    },
    {
      "id": 3,
      "name": "MixedParamsWithReturn",
      "parameters": [
        {
          "id": 0,
          "name": "Name",
          "type": "char[32]"
        },
        {
          "id": 1,
          "name": "Value",
          "type": "float"
        }
      ],
      "return_type": "uint8_t"
    },
    {
      "id": 4,
      "name": "BlobParamsWithBlobReturn",
      "parameters": [
        {
          "id": 0,
          "name": "Data",
          "type": "blob"
        }
      ],
      "return_type": "blob"
    },
    {
      "id": 5,
      "name": "LargeArrayReturn",
      "parameters": [],
      "return_type": "uint8_t[256]"
    },
    {
      "id": 6,
      "name": "StringParamsWithStringReturn",
      "parameters": [
        {
          "id": 0,
          "name": "Path",
          "type": "string",
          "max_length": 64
        }
      ],
      "return_type": "string",
      "max_length": 96
    },
    {
      "id": 7,
      "name": "LargeStringReturn",
      "parameters": [],
      "return_type": "string",
      "max_length": 200
    }
  ],
  "enums": [
    {
      "id": "ExampleEnumClass",
      "base_type": "uint8_t",
      "values": {
        "VALUE1": 0,
        "VALUE2": 1,
        "VALUE3": 2
      }
    },
    {
      "id": "ExampleBitmaskEnum",
      "base_type": "uint8_t",
      "bitmask": true,
      "values": {
        "VALUE1": 0,
        "VALUE2": 1,
        "VALUE3": 2
      }
    }
  ]
}
*/
//[[[end]]]

#endif
