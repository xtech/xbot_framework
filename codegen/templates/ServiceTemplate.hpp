// @formatter:off
// clang-format off

/*[[[cog
import cog
import xbot_codegen

service = xbot_codegen.loadService(service_file)

#Generate include guard
cog.outl(f"#ifndef {service['class_name'].upper()}_HPP")
cog.outl(f"#define {service['class_name'].upper()}_HPP")

]]]*/
#ifndef SERVICETEMPLATEBASE_HPP
#define SERVICETEMPLATEBASE_HPP
//[[[end]]]

/*[[[cog
cog.outl(f"#include <{vars().get('service_ext', 'xbot-service/Service.hpp')}>")
]]]*/
#include <xbot-service/Service.hpp>
//[[[end]]]

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
service_class = 'ServiceExt' if 'service_ext' in vars() else 'Service'
cog.outl(f"class {service['class_name']} : public xbot::service::{service_class} {{")
]]]*/
class ServiceTemplateBase : public xbot::service::Service {
//[[[end]]]
public:
    /*[[[cog
    cog.outl("#ifdef XBOT_ENABLE_STATIC_STACK")
    cog.outl(f"explicit {service['class_name']}(uint16_t service_id, void* stack, size_t stack_size)")
    cog.outl(f"    : {service_class}(service_id, stack, stack_size) {{")
    cog.outl("#else")
    cog.outl(f"explicit {service['class_name']}(uint16_t service_id)")
    cog.outl(f"    : {service_class}(service_id, nullptr, 0) {{")
    cog.outl("#endif")
    ]]]*/
    #ifdef XBOT_ENABLE_STATIC_STACK
    explicit ServiceTemplateBase(uint16_t service_id, void* stack, size_t stack_size)
        : Service(service_id, stack, stack_size) {
    #else
    explicit ServiceTemplateBase(uint16_t service_id)
        : Service(service_id, nullptr, 0) {
    #endif
    //[[[end]]]
    }

    /*[[[cog
    cog.outl(f"static constexpr unsigned char SERVICE_NAME[] = \"{service['type']}\";")
    ]]]*/
    static constexpr unsigned char SERVICE_NAME[] = "ServiceTemplate";
    //[[[end]]]
    const char* GetName() override;

    /*[[[cog
    cog.out("static constexpr unsigned char SERVICE_DESCRIPTION_CBOR[] = ");
    cog.outl(xbot_codegen.binary2c_array(service["service_cbor"]));
    ]]]*/
    static constexpr unsigned char SERVICE_DESCRIPTION_CBOR[] = {
      0xA6, 0x64, 0x74, 0x79, 0x70, 0x65, 0x6F, 0x53,
      0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x54, 0x65,
      0x6D, 0x70, 0x6C, 0x61, 0x74, 0x65, 0x67, 0x76,
      0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x01, 0x66,
      0x69, 0x6E, 0x70, 0x75, 0x74, 0x73, 0x82, 0xA3,
      0x62, 0x69, 0x64, 0x00, 0x64, 0x6E, 0x61, 0x6D,
      0x65, 0x6D, 0x45, 0x78, 0x61, 0x6D, 0x70, 0x6C,
      0x65, 0x49, 0x6E, 0x70, 0x75, 0x74, 0x31, 0x64,
      0x74, 0x79, 0x70, 0x65, 0x69, 0x63, 0x68, 0x61,
      0x72, 0x5B, 0x31, 0x30, 0x30, 0x5D, 0xA3, 0x62,
      0x69, 0x64, 0x01, 0x64, 0x6E, 0x61, 0x6D, 0x65,
      0x6D, 0x45, 0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65,
      0x49, 0x6E, 0x70, 0x75, 0x74, 0x32, 0x64, 0x74,
      0x79, 0x70, 0x65, 0x68, 0x75, 0x69, 0x6E, 0x74,
      0x33, 0x32, 0x5F, 0x74, 0x67, 0x6F, 0x75, 0x74,
      0x70, 0x75, 0x74, 0x73, 0x82, 0xA3, 0x62, 0x69,
      0x64, 0x00, 0x64, 0x6E, 0x61, 0x6D, 0x65, 0x6E,
      0x45, 0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x4F,
      0x75, 0x74, 0x70, 0x75, 0x74, 0x31, 0x64, 0x74,
      0x79, 0x70, 0x65, 0x69, 0x63, 0x68, 0x61, 0x72,
      0x5B, 0x31, 0x30, 0x30, 0x5D, 0xA3, 0x62, 0x69,
      0x64, 0x01, 0x64, 0x6E, 0x61, 0x6D, 0x65, 0x6E,
      0x45, 0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x4F,
      0x75, 0x74, 0x70, 0x75, 0x74, 0x32, 0x64, 0x74,
      0x79, 0x70, 0x65, 0x68, 0x75, 0x69, 0x6E, 0x74,
      0x33, 0x32, 0x5F, 0x74, 0x69, 0x72, 0x65, 0x67,
      0x69, 0x73, 0x74, 0x65, 0x72, 0x73, 0x83, 0xA3,
      0x62, 0x69, 0x64, 0x00, 0x64, 0x6E, 0x61, 0x6D,
      0x65, 0x69, 0x52, 0x65, 0x67, 0x69, 0x73, 0x74,
      0x65, 0x72, 0x31, 0x64, 0x74, 0x79, 0x70, 0x65,
      0x68, 0x63, 0x68, 0x61, 0x72, 0x5B, 0x34, 0x32,
      0x5D, 0xA3, 0x62, 0x69, 0x64, 0x01, 0x64, 0x6E,
      0x61, 0x6D, 0x65, 0x69, 0x52, 0x65, 0x67, 0x69,
      0x73, 0x74, 0x65, 0x72, 0x32, 0x64, 0x74, 0x79,
      0x70, 0x65, 0x68, 0x75, 0x69, 0x6E, 0x74, 0x33,
      0x32, 0x5F, 0x74, 0xA3, 0x62, 0x69, 0x64, 0x02,
      0x64, 0x6E, 0x61, 0x6D, 0x65, 0x69, 0x52, 0x65,
      0x67, 0x69, 0x73, 0x74, 0x65, 0x72, 0x33, 0x64,
      0x74, 0x79, 0x70, 0x65, 0x64, 0x62, 0x6C, 0x6F,
      0x62, 0x65, 0x65, 0x6E, 0x75, 0x6D, 0x73, 0x82,
      0xA3, 0x62, 0x69, 0x64, 0x70, 0x45, 0x78, 0x61,
      0x6D, 0x70, 0x6C, 0x65, 0x45, 0x6E, 0x75, 0x6D,
      0x43, 0x6C, 0x61, 0x73, 0x73, 0x69, 0x62, 0x61,
      0x73, 0x65, 0x5F, 0x74, 0x79, 0x70, 0x65, 0x67,
      0x75, 0x69, 0x6E, 0x74, 0x38, 0x5F, 0x74, 0x66,
      0x76, 0x61, 0x6C, 0x75, 0x65, 0x73, 0xA3, 0x66,
      0x56, 0x41, 0x4C, 0x55, 0x45, 0x31, 0x00, 0x66,
      0x56, 0x41, 0x4C, 0x55, 0x45, 0x32, 0x01, 0x66,
      0x56, 0x41, 0x4C, 0x55, 0x45, 0x33, 0x02, 0xA4,
      0x62, 0x69, 0x64, 0x72, 0x45, 0x78, 0x61, 0x6D,
      0x70, 0x6C, 0x65, 0x42, 0x69, 0x74, 0x6D, 0x61,
      0x73, 0x6B, 0x45, 0x6E, 0x75, 0x6D, 0x69, 0x62,
      0x61, 0x73, 0x65, 0x5F, 0x74, 0x79, 0x70, 0x65,
      0x67, 0x75, 0x69, 0x6E, 0x74, 0x38, 0x5F, 0x74,
      0x67, 0x62, 0x69, 0x74, 0x6D, 0x61, 0x73, 0x6B,
      0xF5, 0x66, 0x76, 0x61, 0x6C, 0x75, 0x65, 0x73,
      0xA3, 0x66, 0x56, 0x41, 0x4C, 0x55, 0x45, 0x31,
      0x00, 0x66, 0x56, 0x41, 0x4C, 0x55, 0x45, 0x32,
      0x01, 0x66, 0x56, 0x41, 0x4C, 0x55, 0x45, 0x33,
      0x02
    };
    //[[[end]]]

private:
    uint32_t sd_sequence_ = 0;
    bool reboot = true;
    void handleData(uint16_t target_id, const void *payload, size_t length) override final;
    bool AdvertiseServiceImpl() override final;
    /*[[[cog
      cog.outl(f"bool hasRegisters() override final {{ return {'true' if service['registers'] else 'false'}; }}")
    ]]]*/
    bool hasRegisters() override final { return true; }
    //[[[end]]]
    bool allRegistersValid() override final;
    void loadConfigurationDefaultsImpl() override final;
    bool setRegister(uint16_t target_id, const void *payload,
                          size_t length)override final;

protected:
    /*[[[cog
    # Generate callback functions for each input.
    for input in service["inputs"]:
        if input['is_array']:
            cog.outl(f"virtual void {input['callback_name']}(const {input['type']}* new_value, uint32_t length) {{")
            cog.outl("  (void)new_value;")
            cog.outl("  (void)length;")
        else:
            cog.outl(f"virtual void {input['callback_name']}(const {input['type']} &new_value) {{")
            cog.outl(f"  (void)new_value;")
        cog.outl("}\n")
    ]]]*/
    virtual void OnExampleInput1Changed(const char* new_value, uint32_t length) {
      (void)new_value;
      (void)length;
    }

    virtual void OnExampleInput2Changed(const uint32_t &new_value) {
      (void)new_value;
    }

    //[[[end]]]
    /*[[[cog
    # Generate send functions for each output.
    for output in service["outputs"]:
        if output['is_array']:
            cog.outl(f"bool {output['send_method_name']}(const {output['type']}* data, uint32_t length);")
        else:
            cog.outl(f"bool {output['send_method_name']}(const {output['type']} &data);")
    ]]]*/
    bool SendExampleOutput1(const char* data, uint32_t length);
    bool SendExampleOutput2(const uint32_t &data);
    //[[[end]]]
    /*[[[cog
    # Generate register struct

    for register in service["registers"]:
      if register['type'] == "blob":
          cog.outl(f"virtual bool {register['callback_name']}(const void* data, size_t length) {{")
          cog.outl("  (void)data;")
          cog.outl("  (void)length;")
          cog.outl("  return true;")
          cog.outl("}")
          continue

      cog.outl("\nstruct {");
      if register['is_array']:
          cog.outl(f"  {register['type']} value[{register ['max_length']}];")
          cog.outl("  size_t length = 0;")
      else:
          cog.outl(f"  {register['type']} value;")
      cog.outl("  bool valid = false;")
      cog.outl(f"}} {register['name']};");
    ]]]*/

    struct {
      char value[42];
      size_t length = 0;
      bool valid = false;
    } Register1;

    struct {
      uint32_t value;
      bool valid = false;
    } Register2;
    virtual bool OnRegisterRegister3Changed(const void* data, size_t length) {
      (void)data;
      (void)length;
      return true;
    }
    //[[[end]]]
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
