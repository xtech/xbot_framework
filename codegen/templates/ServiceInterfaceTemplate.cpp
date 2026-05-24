// @formatter:off
// clang-format off

/*[[[cog
import cog
from xbot_codegen import toCamelCase, loadService

service = loadService(service_file)

cog.outl(f'#include <{service["interface_class_name"]}.hpp>')

]]]*/
#include <ServiceTemplateInterfaceBase.hpp>
//[[[end]]]
#include <cstring>
#include <vector>
#include <spdlog/spdlog.h>
#include <xbot/datatypes/XbotHeader.hpp>
/*[[[cog
cog.outl(f"void {service['interface_class_name']}::OnData(uint16_t service_id, uint64_t timestamp, uint16_t target_id, const void *payload, size_t length) {{")
]]]*/
void ServiceTemplateInterfaceBase::OnData(uint16_t service_id, uint64_t timestamp, uint16_t target_id, const void *payload, size_t length) {
//[[[end]]]
    (void) service_id;
    (void) timestamp;
    // Call the callback for this input
        switch (target_id) {
            /*[[[cog
            for o in service['outputs']:
                cog.outl(f"case {o['id']}:");
                if o['is_array']:
                    cog.outl(f"if(length % sizeof({o['type']}) != 0) {{");
                    cog.outl("    spdlog::error(\"Invalid data size\");");
                    cog.outl("    return;");
                    cog.outl("}");
                    cog.outl(f"{o['callback_name']}(static_cast<const {o['type']}*>(payload), length/sizeof({o['type']}));");
                else:
                    cog.outl(f"if(length != sizeof({o['type']})) {{");
                    cog.outl("    spdlog::error(\"Invalid data size\");");
                    cog.outl("    return;");
                    cog.outl("}");
                    cog.outl(f"{o['callback_name']}(*static_cast<const {o['type']}*>(payload));");
                cog.outl(f"break;");
            ]]]*/
            case 0:
            if(length % sizeof(char) != 0) {
                spdlog::error("Invalid data size");
                return;
            }
            OnExampleOutput1Changed(static_cast<const char*>(payload), length/sizeof(char));
            break;
            case 1:
            if(length != sizeof(uint32_t)) {
                spdlog::error("Invalid data size");
                return;
            }
            OnExampleOutput2Changed(*static_cast<const uint32_t*>(payload));
            break;
            //[[[end]]]
            default:
                return;
        }
        return;
}

/*[[[cog
# Generate send function implementations.
for register in service["registers"]:
    if register['type'] == "blob":
        cog.outl(f"bool {service['interface_class_name']}::{register['send_method_name']}(const void* data, size_t length) {{")
        cog.outl(f"    return SendData({register['id']}, data, length, true);")
        cog.outl("}")
    elif register['is_array']:
        cog.outl(f"bool {service['interface_class_name']}::{register['send_method_name']}(const {register['type']}* data, uint32_t length) {{")
        cog.outl(f"    return SendData({register['id']}, data, length*sizeof({register['type']}), true);")
        cog.outl("}")
    else:
        cog.outl(f"bool {service['interface_class_name']}::{register['send_method_name']}(const {register['type']} &data) {{")
        cog.outl(f"    return SendData({register['id']}, &data, sizeof({register['type']}), true);")
        cog.outl("}")
]]]*/
bool ServiceTemplateInterfaceBase::SetRegisterRegister1(const char* data, uint32_t length) {
    return SendData(0, data, length*sizeof(char), true);
}
bool ServiceTemplateInterfaceBase::SetRegisterRegister2(const uint32_t &data) {
    return SendData(1, &data, sizeof(uint32_t), true);
}
bool ServiceTemplateInterfaceBase::SetRegisterRegister3(const void* data, size_t length) {
    return SendData(2, data, length, true);
}
bool ServiceTemplateInterfaceBase::SetRegisterRegister4Optional(const uint32_t &data) {
    return SendData(3, &data, sizeof(uint32_t), true);
}
//[[[end]]]

/*[[[cog
# Generate send function implementations.
for input in service["inputs"]:
    if input['is_array']:
        cog.outl(f"bool {service['interface_class_name']}::{input['send_method_name']}(const {input['type']}* data, uint32_t length) {{")
        cog.outl(f"    return SendData({input['id']}, data, length*sizeof({input['type']}), false);")
        cog.outl("}")
    else:
        cog.outl(f"bool {service['interface_class_name']}::{input['send_method_name']}(const {input['type']} &data) {{")
        cog.outl(f"    return SendData({input['id']}, &data, sizeof({input['type']}), false);")
        cog.outl("}")
]]]*/
bool ServiceTemplateInterfaceBase::SendExampleInput1(const char* data, uint32_t length) {
    return SendData(0, data, length*sizeof(char), false);
}
bool ServiceTemplateInterfaceBase::SendExampleInput2(const uint32_t &data) {
    return SendData(1, &data, sizeof(uint32_t), false);
}
//[[[end]]]

/*[[[cog
# Generate blocking Call* implementations using SendRpc().
for func in service["functions"]:
    # Build method signature
    sig_params = []
    for p in func["parameters"]:
        if p['is_array']:
            sig_params.append(f"const {p['type']}* {p['name']}, uint32_t {p['name']}Len")
        else:
            sig_params.append(f"const {p['type']}& {p['name']}")
    if func['return_is_array']:
        sig_params.append(f"{func['return_base_type']}* data, uint16_t& result_length")
    elif func["return_type"] != "void":
        sig_params.append(f"{func['return_type']}& result")
    sig_params.append("uint32_t timeout_ms")
    params_str = ", ".join(sig_params)
    cog.outl(f"bool {service['interface_class_name']}::Call{func['name']}({params_str}) {{")

    # Serialize parameters into a DataDescriptor-framed byte vector
    if func["parameters"]:
        cog.outl("  std::vector<uint8_t> params_buf;")
        for p in func["parameters"]:
            if p['is_array']:
                cog.outl(f"  {{")
                cog.outl(f"    const size_t byte_len = {p['name']}Len * sizeof({p['type']});")
                cog.outl(f"    const size_t off = params_buf.size();")
                cog.outl(f"    params_buf.resize(off + sizeof(xbot::datatypes::DataDescriptor) + byte_len);")
                cog.outl(f"    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params_buf.data() + off);")
                cog.outl(f"    desc->target_id = {p['id']}; desc->reserved = 0; desc->payload_size = static_cast<uint32_t>(byte_len);")
                cog.outl(f"    if (byte_len > 0 && {p['name']} == nullptr) return false;")
                cog.outl(f"    memcpy(params_buf.data() + off + sizeof(xbot::datatypes::DataDescriptor), {p['name']}, byte_len);")
                cog.outl(f"  }}")
            else:
                cog.outl(f"  {{")
                cog.outl(f"    const size_t off = params_buf.size();")
                cog.outl(f"    params_buf.resize(off + sizeof(xbot::datatypes::DataDescriptor) + sizeof({p['type']}));")
                cog.outl(f"    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params_buf.data() + off);")
                cog.outl(f"    desc->target_id = {p['id']}; desc->reserved = 0; desc->payload_size = sizeof({p['type']});")
                cog.outl(f"    memcpy(params_buf.data() + off + sizeof(xbot::datatypes::DataDescriptor), &{p['name']}, sizeof({p['type']}));")
                cog.outl(f"  }}")
        params_call = "params_buf.data(), params_buf.size()"
    else:
        params_call = "nullptr, 0"

    # Call SendRpc with appropriate response buffer
    if func['return_is_array']:
        cog.outl(f"  size_t resp_size = result_length * sizeof({func['return_base_type']});")
        cog.outl(f"  const auto r = SendRpc({func['id']}, {params_call}, reinterpret_cast<uint8_t*>(data), &resp_size, timeout_ms);")
        cog.outl(f"  if (r != RPC_OK) return false;")
        cog.outl(f"  result_length = static_cast<uint16_t>(resp_size / sizeof({func['return_base_type']}));")
    elif func["return_type"] != "void":
        cog.outl(f"  {func['return_type']} resp_buf{{}};")
        cog.outl(f"  size_t resp_size = sizeof({func['return_type']});")
        cog.outl(f"  const auto r = SendRpc({func['id']}, {params_call}, reinterpret_cast<uint8_t*>(&resp_buf), &resp_size, timeout_ms);")
        cog.outl(f"  if (r != RPC_OK) return false;")
        cog.outl(f"  if (resp_size < sizeof({func['return_type']})) return false;")
        cog.outl(f"  result = resp_buf;")
    else:
        cog.outl(f"  size_t resp_size = 0;")
        cog.outl(f"  const auto r = SendRpc({func['id']}, {params_call}, nullptr, &resp_size, timeout_ms);")
        cog.outl(f"  if (r != RPC_OK) return false;")

    cog.outl("  return true;")
    cog.outl("}")
]]]*/
bool ServiceTemplateInterfaceBase::CallNoParamsNoReturn(uint32_t timeout_ms) {
  size_t resp_size = 0;
  const auto r = SendRpc(0, nullptr, 0, nullptr, &resp_size, timeout_ms);
  if (r != RPC_OK) return false;
  return true;
}
bool ServiceTemplateInterfaceBase::CallScalarParamsWithReturn(const float& Speed, const uint32_t& Count, const bool& Enable, int32_t& result, uint32_t timeout_ms) {
  std::vector<uint8_t> params_buf;
  {
    const size_t off = params_buf.size();
    params_buf.resize(off + sizeof(xbot::datatypes::DataDescriptor) + sizeof(float));
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params_buf.data() + off);
    desc->target_id = 0; desc->reserved = 0; desc->payload_size = sizeof(float);
    memcpy(params_buf.data() + off + sizeof(xbot::datatypes::DataDescriptor), &Speed, sizeof(float));
  }
  {
    const size_t off = params_buf.size();
    params_buf.resize(off + sizeof(xbot::datatypes::DataDescriptor) + sizeof(uint32_t));
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params_buf.data() + off);
    desc->target_id = 1; desc->reserved = 0; desc->payload_size = sizeof(uint32_t);
    memcpy(params_buf.data() + off + sizeof(xbot::datatypes::DataDescriptor), &Count, sizeof(uint32_t));
  }
  {
    const size_t off = params_buf.size();
    params_buf.resize(off + sizeof(xbot::datatypes::DataDescriptor) + sizeof(bool));
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params_buf.data() + off);
    desc->target_id = 2; desc->reserved = 0; desc->payload_size = sizeof(bool);
    memcpy(params_buf.data() + off + sizeof(xbot::datatypes::DataDescriptor), &Enable, sizeof(bool));
  }
  int32_t resp_buf{};
  size_t resp_size = sizeof(int32_t);
  const auto r = SendRpc(1, params_buf.data(), params_buf.size(), reinterpret_cast<uint8_t*>(&resp_buf), &resp_size, timeout_ms);
  if (r != RPC_OK) return false;
  if (resp_size < sizeof(int32_t)) return false;
  result = resp_buf;
  return true;
}
bool ServiceTemplateInterfaceBase::CallArrayParamNoReturn(const char* Label, uint32_t LabelLen, uint32_t timeout_ms) {
  std::vector<uint8_t> params_buf;
  {
    const size_t byte_len = LabelLen * sizeof(char);
    const size_t off = params_buf.size();
    params_buf.resize(off + sizeof(xbot::datatypes::DataDescriptor) + byte_len);
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params_buf.data() + off);
    desc->target_id = 0; desc->reserved = 0; desc->payload_size = static_cast<uint32_t>(byte_len);
    if (byte_len > 0 && Label == nullptr) return false;
    memcpy(params_buf.data() + off + sizeof(xbot::datatypes::DataDescriptor), Label, byte_len);
  }
  size_t resp_size = 0;
  const auto r = SendRpc(2, params_buf.data(), params_buf.size(), nullptr, &resp_size, timeout_ms);
  if (r != RPC_OK) return false;
  return true;
}
bool ServiceTemplateInterfaceBase::CallMixedParamsWithReturn(const char* Name, uint32_t NameLen, const float& Value, bool& result, uint32_t timeout_ms) {
  std::vector<uint8_t> params_buf;
  {
    const size_t byte_len = NameLen * sizeof(char);
    const size_t off = params_buf.size();
    params_buf.resize(off + sizeof(xbot::datatypes::DataDescriptor) + byte_len);
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params_buf.data() + off);
    desc->target_id = 0; desc->reserved = 0; desc->payload_size = static_cast<uint32_t>(byte_len);
    if (byte_len > 0 && Name == nullptr) return false;
    memcpy(params_buf.data() + off + sizeof(xbot::datatypes::DataDescriptor), Name, byte_len);
  }
  {
    const size_t off = params_buf.size();
    params_buf.resize(off + sizeof(xbot::datatypes::DataDescriptor) + sizeof(float));
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params_buf.data() + off);
    desc->target_id = 1; desc->reserved = 0; desc->payload_size = sizeof(float);
    memcpy(params_buf.data() + off + sizeof(xbot::datatypes::DataDescriptor), &Value, sizeof(float));
  }
  bool resp_buf{};
  size_t resp_size = sizeof(bool);
  const auto r = SendRpc(3, params_buf.data(), params_buf.size(), reinterpret_cast<uint8_t*>(&resp_buf), &resp_size, timeout_ms);
  if (r != RPC_OK) return false;
  if (resp_size < sizeof(bool)) return false;
  result = resp_buf;
  return true;
}
//[[[end]]]
