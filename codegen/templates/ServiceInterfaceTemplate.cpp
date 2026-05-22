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
#include <chrono>
#include <cstring>
#include <mutex>
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
# Generate blocking Call* implementations.
for func in service["functions"]:
    # Build method signature
    params = []
    for p in func["parameters"]:
        if p['is_array']:
            params.append(f"const {p['type']}* {p['name']}, uint32_t {p['name']}Len")
        else:
            params.append(f"const {p['type']}& {p['name']}")
    if func["return_type"] != "void":
        params.append(f"{func['return_type']}& result")
    params.append("uint32_t timeout_ms")
    params_str = ", ".join(params)
    cog.outl(f"bool {service['interface_class_name']}::Call{func['name']}({params_str}) {{")

    # Serialize parameters into a DataDescriptor-framed byte vector, then
    # build the packet (acquires state_mutex_ only) before locking rpc_mutex_.
    # This ordering prevents state_mutex_ from ever being acquired while
    # rpc_mutex_ is held, eliminating the lock-inversion deadlock.
    if func["parameters"]:
        cog.outl("  std::vector<uint8_t> params;")
        for p in func["parameters"]:
            if p['is_array']:
                cog.outl(f"  {{")
                cog.outl(f"    const size_t byte_len = {p['name']}Len * sizeof({p['type']});")
                cog.outl(f"    const size_t off = params.size();")
                cog.outl(f"    params.resize(off + sizeof(xbot::datatypes::DataDescriptor) + byte_len);")
                cog.outl(f"    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params.data() + off);")
                cog.outl(f"    desc->target_id = {p['id']}; desc->reserved = 0; desc->payload_size = static_cast<uint32_t>(byte_len);")
                cog.outl(f"    memcpy(params.data() + off + sizeof(xbot::datatypes::DataDescriptor), {p['name']}, byte_len);")
                cog.outl(f"  }}")
            else:
                cog.outl(f"  {{")
                cog.outl(f"    const size_t off = params.size();")
                cog.outl(f"    params.resize(off + sizeof(xbot::datatypes::DataDescriptor) + sizeof({p['type']}));")
                cog.outl(f"    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params.data() + off);")
                cog.outl(f"    desc->target_id = {p['id']}; desc->reserved = 0; desc->payload_size = sizeof({p['type']});")
                cog.outl(f"    memcpy(params.data() + off + sizeof(xbot::datatypes::DataDescriptor), &{p['name']}, sizeof({p['type']}));")
                cog.outl(f"  }}")
        cog.outl(f"  auto pkt = BuildRpcPacket({func['id']}, params.data(), params.size());")
    else:
        cog.outl(f"  auto pkt = BuildRpcPacket({func['id']}, nullptr, 0);")
    cog.outl("  std::unique_lock<std::mutex> lk(rpc_mutex_);")
    cog.outl(f"  if (!SendRpcPacket(lk, std::move(pkt))) return false;")

    cog.outl("  const bool ok = rpc_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),")
    cog.outl("                                    [this] { return !rpc_call_active_; });")
    cog.outl("  if (!ok) { rpc_call_active_ = false; return false; }")
    cog.outl("  if (rpc_response_status_ != 0) return false;")

    if func["return_type"] != "void":
        cog.outl(f"  if (rpc_response_payload_.size() < sizeof({func['return_type']})) return false;")
        cog.outl(f"  memcpy(&result, rpc_response_payload_.data(), sizeof({func['return_type']}));")

    cog.outl("  return true;")
    cog.outl("}")
]]]*/
bool ServiceTemplateInterfaceBase::CallNoParamsNoReturn(uint32_t timeout_ms) {
  auto pkt = BuildRpcPacket(0, nullptr, 0);
  std::unique_lock<std::mutex> lk(rpc_mutex_);
  if (!SendRpcPacket(lk, std::move(pkt))) return false;
  const bool ok = rpc_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                    [this] { return !rpc_call_active_; });
  if (!ok) { rpc_call_active_ = false; return false; }
  if (rpc_response_status_ != 0) return false;
  return true;
}
bool ServiceTemplateInterfaceBase::CallScalarParamsWithReturn(const float& Speed, const uint32_t& Count, const bool& Enable, int32_t& result, uint32_t timeout_ms) {
  std::vector<uint8_t> params;
  {
    const size_t off = params.size();
    params.resize(off + sizeof(xbot::datatypes::DataDescriptor) + sizeof(float));
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params.data() + off);
    desc->target_id = 0; desc->reserved = 0; desc->payload_size = sizeof(float);
    memcpy(params.data() + off + sizeof(xbot::datatypes::DataDescriptor), &Speed, sizeof(float));
  }
  {
    const size_t off = params.size();
    params.resize(off + sizeof(xbot::datatypes::DataDescriptor) + sizeof(uint32_t));
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params.data() + off);
    desc->target_id = 1; desc->reserved = 0; desc->payload_size = sizeof(uint32_t);
    memcpy(params.data() + off + sizeof(xbot::datatypes::DataDescriptor), &Count, sizeof(uint32_t));
  }
  {
    const size_t off = params.size();
    params.resize(off + sizeof(xbot::datatypes::DataDescriptor) + sizeof(bool));
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params.data() + off);
    desc->target_id = 2; desc->reserved = 0; desc->payload_size = sizeof(bool);
    memcpy(params.data() + off + sizeof(xbot::datatypes::DataDescriptor), &Enable, sizeof(bool));
  }
  auto pkt = BuildRpcPacket(1, params.data(), params.size());
  std::unique_lock<std::mutex> lk(rpc_mutex_);
  if (!SendRpcPacket(lk, std::move(pkt))) return false;
  const bool ok = rpc_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                    [this] { return !rpc_call_active_; });
  if (!ok) { rpc_call_active_ = false; return false; }
  if (rpc_response_status_ != 0) return false;
  if (rpc_response_payload_.size() < sizeof(int32_t)) return false;
  memcpy(&result, rpc_response_payload_.data(), sizeof(int32_t));
  return true;
}
bool ServiceTemplateInterfaceBase::CallArrayParamNoReturn(const char* Label, uint32_t LabelLen, uint32_t timeout_ms) {
  std::vector<uint8_t> params;
  {
    const size_t byte_len = LabelLen * sizeof(char);
    const size_t off = params.size();
    params.resize(off + sizeof(xbot::datatypes::DataDescriptor) + byte_len);
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params.data() + off);
    desc->target_id = 0; desc->reserved = 0; desc->payload_size = static_cast<uint32_t>(byte_len);
    memcpy(params.data() + off + sizeof(xbot::datatypes::DataDescriptor), Label, byte_len);
  }
  auto pkt = BuildRpcPacket(2, params.data(), params.size());
  std::unique_lock<std::mutex> lk(rpc_mutex_);
  if (!SendRpcPacket(lk, std::move(pkt))) return false;
  const bool ok = rpc_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                    [this] { return !rpc_call_active_; });
  if (!ok) { rpc_call_active_ = false; return false; }
  if (rpc_response_status_ != 0) return false;
  return true;
}
bool ServiceTemplateInterfaceBase::CallMixedParamsWithReturn(const char* Name, uint32_t NameLen, const float& Value, bool& result, uint32_t timeout_ms) {
  std::vector<uint8_t> params;
  {
    const size_t byte_len = NameLen * sizeof(char);
    const size_t off = params.size();
    params.resize(off + sizeof(xbot::datatypes::DataDescriptor) + byte_len);
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params.data() + off);
    desc->target_id = 0; desc->reserved = 0; desc->payload_size = static_cast<uint32_t>(byte_len);
    memcpy(params.data() + off + sizeof(xbot::datatypes::DataDescriptor), Name, byte_len);
  }
  {
    const size_t off = params.size();
    params.resize(off + sizeof(xbot::datatypes::DataDescriptor) + sizeof(float));
    auto* desc = reinterpret_cast<xbot::datatypes::DataDescriptor*>(params.data() + off);
    desc->target_id = 1; desc->reserved = 0; desc->payload_size = sizeof(float);
    memcpy(params.data() + off + sizeof(xbot::datatypes::DataDescriptor), &Value, sizeof(float));
  }
  auto pkt = BuildRpcPacket(3, params.data(), params.size());
  std::unique_lock<std::mutex> lk(rpc_mutex_);
  if (!SendRpcPacket(lk, std::move(pkt))) return false;
  const bool ok = rpc_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                    [this] { return !rpc_call_active_; });
  if (!ok) { rpc_call_active_ = false; return false; }
  if (rpc_response_status_ != 0) return false;
  if (rpc_response_payload_.size() < sizeof(bool)) return false;
  memcpy(&result, rpc_response_payload_.data(), sizeof(bool));
  return true;
}
//[[[end]]]
