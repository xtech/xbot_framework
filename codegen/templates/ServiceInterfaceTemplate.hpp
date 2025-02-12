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

/*[[[cog
for include in service['additional_includes']:
    cog.outl(f"#include {include}")
]]]*/
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
    xbot_codegen.generateEnums(service)
    ]]]*/
    //[[[end]]]
    /*[[[cog
    # Generate send functions for each input.
    for input in service["inputs"]:
        if input['is_array']:
            cog.outl(f"bool {input['method_name']}(const {input['type']}* data, uint32_t length);")
        else:
            cog.outl(f"bool {input['method_name']}(const {input['type']} &data);")
    ]]]*/
    bool SendExampleInput1(const char* data, uint32_t length);
    bool SendExampleInput2(const uint32_t &data);
    //[[[end]]]

    /*[[[cog
    # Generate send functions for each register.
    for register in service["registers"]:
        if register['is_array']:
            cog.outl(f"bool {register['method_name']}(const {register['type']}* data, uint32_t length);")
        else:
            cog.outl(f"bool {register['method_name']}(const {register['type']} &data);")
    ]]]*/
    bool SetRegisterRegister1(const char* data, uint32_t length);
    bool SetRegisterRegister2(const uint32_t &data);
    //[[[end]]]

protected:
    /*[[[cog
    # Generate callback functions for each service output.
    for output in service["outputs"]:
        if output['is_array']:
            cog.outl(f"virtual void {output['callback_name']}(const {output['type']}* new_value, uint32_t length) {{}};")
        else:
            cog.outl(f"virtual void {output['callback_name']}(const {output['type']} &new_value) {{}};")
    ]]]*/
    virtual void OnExampleOutput1Changed(const char* new_value, uint32_t length) {};
    virtual void OnExampleOutput2Changed(const uint32_t &new_value) {};
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
    }
  ]
}
*/
//[[[end]]]

#endif
