// @formatter:off
// clang-format off

/*[[[cog
import cog
from xbot_codegen import toCamelCase, loadService

service = loadService(service_file)

cog.outl(f'#include "{service["class_name"]}.hpp"')

]]]*/
#include "ServiceTemplateBase.hpp"
//[[[end]]]
#include <cstring>
#include <ulog.h>
#include <xbot-service/Lock.hpp>
#include <xbot-service/portable/system.hpp>
#include <xbot-service/Io.hpp>


/*[[[cog
cog.outl(f"constexpr unsigned char {service['class_name']}::SERVICE_DESCRIPTION_CBOR[];")
]]]*/
constexpr unsigned char ServiceTemplateBase::SERVICE_DESCRIPTION_CBOR[];
//[[[end]]]


/*[[[cog
cog.outl(f"void {service['class_name']}::handleData(uint16_t target_id, const void *payload, size_t length) {{")
]]]*/
void ServiceTemplateBase::handleData(uint16_t target_id, const void *payload, size_t length) {
//[[[end]]]

        /*[[[cog
        if len(service['inputs']) == 0:
            cog.outl("// Avoid unused parameter warnings.")
            cog.outl("(void)payload;")
            cog.outl("(void)length;")
        ]]]*/
        //[[[end]]]

        // Call the callback for this input
        switch (target_id) {
            /*[[[cog
            for i in service['inputs']:
                cog.outl(f"case {i['id']}:");
                if i['is_array']:
                    cog.outl(f"if (length % sizeof({i['type']}) != 0) {{");
                    cog.outl("    ULOG_ARG_ERROR(&service_id_, \"Invalid data size\");");
                    cog.outl("    return;");
                    cog.outl("}");
                    cog.outl(f"{i['callback_name']}(static_cast<const {i['type']}*>(payload), length/sizeof({i['type']}));");
                    cog.outl("return;");
                else:
                    cog.outl(f"if (length != sizeof({i['type']})) {{");
                    cog.outl("    ULOG_ARG_ERROR(&service_id_, \"Invalid data size\");");
                    cog.outl("    return;");
                    cog.outl("}");
                    cog.outl(f"{i['callback_name']}(*static_cast<const {i['type']}*>(payload));");
                    cog.outl("return;");

            ]]]*/
            case 0:
            if (length % sizeof(char) != 0) {
                ULOG_ARG_ERROR(&service_id_, "Invalid data size");
                return;
            }
            OnExampleInput1Changed(static_cast<const char*>(payload), length/sizeof(char));
            return;
            case 1:
            if (length != sizeof(uint32_t)) {
                ULOG_ARG_ERROR(&service_id_, "Invalid data size");
                return;
            }
            OnExampleInput2Changed(*static_cast<const uint32_t*>(payload));
            return;
            //[[[end]]]
        }
}
/*[[[cog
cog.outl(f"bool {service['class_name']}::AdvertiseServiceImpl() {{")
]]]*/
bool ServiceTemplateBase::AdvertiseServiceImpl() {
//[[[end]]]
    static_assert(sizeof(scratch_buffer_)>80+sizeof(SERVICE_DESCRIPTION_CBOR), "scratch_buffer_ too small for service description. increase size");

    size_t index = 0;
    // Build CBOR payload
    // 0xA4 = object with 3 entries
    scratch_buffer_[index++] = 0xA3;
    // Key1
    // 0x62 = text(3)
    scratch_buffer_[index++] = 0x63;
    scratch_buffer_[index++] = 's';
    scratch_buffer_[index++] = 'i';
    scratch_buffer_[index++] = 'd';

    // 0x19 == 16 bit unsigned, positive
    scratch_buffer_[index++] = 0x19;
    scratch_buffer_[index++] = (service_id_>>8) & 0xFF;
    scratch_buffer_[index++] = service_id_ & 0xFF;


    // Key2
    // 0x68 = text(8)
    scratch_buffer_[index++] = 0x68;
    scratch_buffer_[index++] = 'e';
    scratch_buffer_[index++] = 'n';
    scratch_buffer_[index++] = 'd';
    scratch_buffer_[index++] = 'p';
    scratch_buffer_[index++] = 'o';
    scratch_buffer_[index++] = 'i';
    scratch_buffer_[index++] = 'n';
    scratch_buffer_[index++] = 't';

    // Get the IP address
    char address[16]{};
    uint16_t port = 0;

    if(!xbot::service::Io::getEndpoint(address, sizeof(address), &port)) {
        ULOG_ARG_ERROR(&service_id_, "Error fetching socket address");
        return false;
    }

    size_t len = strlen(address);
    if(len >= 16) {
        ULOG_ARG_ERROR(&service_id_, "Got invalid address");
        return false;
    }
    // Object with 2 entries (ip, port)
    scratch_buffer_[index++] = 0xA2;
    // text(2) = "ip"
    scratch_buffer_[index++] = 0x62;
    scratch_buffer_[index++] = 'i';
    scratch_buffer_[index++] = 'p';
    scratch_buffer_[index++] = 0x60 + len;
    strcpy(reinterpret_cast<char*>(scratch_buffer_+index), address);
    index += len;
    scratch_buffer_[index++] = 0x64;
    scratch_buffer_[index++] = 'p';
    scratch_buffer_[index++] = 'o';
    scratch_buffer_[index++] = 'r';
    scratch_buffer_[index++] = 't';
    // 0x19 == 16 bit unsigned, positive
    scratch_buffer_[index++] = 0x19;
    scratch_buffer_[index++] = (port>>8) & 0xFF;
    scratch_buffer_[index++] = port & 0xFF;

    // Key3
    // 0x64 = text(4)
    scratch_buffer_[index++] = 0x64;
    scratch_buffer_[index++] = 'd';
    scratch_buffer_[index++] = 'e';
    scratch_buffer_[index++] = 's';
    scratch_buffer_[index++] = 'c';

    memcpy(scratch_buffer_+index, SERVICE_DESCRIPTION_CBOR, sizeof(SERVICE_DESCRIPTION_CBOR));
    index+=sizeof(SERVICE_DESCRIPTION_CBOR);


    xbot::datatypes::XbotHeader header{};
    if(reboot) {
        header.flags = 1;
    } else {
        header.flags = 0;
    }
    header.message_type = xbot::datatypes::MessageType::SERVICE_ADVERTISEMENT;
    header.payload_size = index;
    header.protocol_version = 1;
    header.arg1 = 0;
    header.arg2 = 0;
    header.sequence_no = sd_sequence_++;
    header.timestamp = xbot::service::system::getTimeMicros();

    // Reset reboot on rollover
    if(sd_sequence_==0) {
        reboot = false;
    }

    xbot::service::packet::PacketPtr ptr = xbot::service::packet::allocatePacket();
    xbot::service::packet::packetAppendData(ptr, &header, sizeof(header));
    xbot::service::packet::packetAppendData(ptr, scratch_buffer_, header.payload_size);
    return xbot::service::Io::transmitPacket(ptr, xbot::config::sd_multicast_address, xbot::config::multicast_port);
}

/*[[[cog
# Generate send function implementations.
for output in service["outputs"]:
    if output['is_array']:
        cog.outl(f"bool {service['class_name']}::{output['send_method_name']}(const {output['type']}* data, uint32_t length) {{")
        cog.outl(f"    return SendData({output['id']}, data, length*sizeof({output['type']}));")
        cog.outl("}")
    else:
        cog.outl(f"bool {service['class_name']}::{output['send_method_name']}(const {output['type']} &data) {{")
        cog.outl(f"    return SendData({output['id']}, &data, sizeof({output['type']}));")
        cog.outl("}")
]]]*/
bool ServiceTemplateBase::SendExampleOutput1(const char* data, uint32_t length) {
    return SendData(0, data, length*sizeof(char));
}
bool ServiceTemplateBase::SendExampleOutput2(const uint32_t &data) {
    return SendData(1, &data, sizeof(uint32_t));
}
//[[[end]]]

/*[[[cog
    # Generate configured check
    cog.outl(f"bool {service['class_name']}::allRegistersValid() {{")
    for register in service["registers"]:
        if register['type'] == "blob":
            continue
        if register.get('optional', False):
            continue
        cog.outl(f"if(!this->{register['name']}.valid) {{return false;}}")
    cog.outl("return true;")
    cog.outl("}")
]]]*/
bool ServiceTemplateBase::allRegistersValid() {
if(!this->Register1.valid) {return false;}
if(!this->Register2.valid) {return false;}
return true;
}
//[[[end]]]

/*[[[cog
    # Generate config reset
    cog.outl(f"void {service['class_name']}::loadConfigurationDefaultsImpl() {{")
    for register in service["registers"]:
        if register['type'] == "blob":
            continue
        elif "default" in register:
            cog.outl("{");
            if register['is_array']:
                cog.outl(f"{register['type']} value[{register['max_length']}] = {register['default']};")
                cog.outl(f"this->{register['name']}.length = {register['default_length']};")
            else:
                cog.outl(f"{register['type']} value = {register['default']};")
            cog.outl(f"memcpy(&this->{register['name']}.value, &value, sizeof(value));")
            cog.outl(f"this->{register['name']}.valid = true;")
            cog.outl("}")
        else:
            cog.outl(f"this->{register['name']}.valid = false;")
    cog.outl("}")
]]]*/
void ServiceTemplateBase::loadConfigurationDefaultsImpl() {
this->Register1.valid = false;
this->Register2.valid = false;
this->Register4Optional.valid = false;
}
//[[[end]]]


/*[[[cog
cog.outl(f"bool {service['class_name']}::setRegister(uint16_t target_id, const void *payload, size_t length) {{")
]]]*/
bool ServiceTemplateBase::setRegister(uint16_t target_id, const void *payload, size_t length) {
  //[[[end]]]

  /*[[[cog
  if len(service['registers']) == 0:
      cog.outl("// Avoid unused parameter warnings.")
      cog.outl("(void)payload;")
      cog.outl("(void)length;")
  ]]]*/
  //[[[end]]]

  // Call the callback for this input
  switch (target_id) {
    /*[[[cog
    for r in service['registers']:
        cog.outl(f"case {r['id']}:");
        if (r['type'] == "blob"):
            cog.outl(f"return {r['callback_name']}(payload, length);")
        elif r['is_array']:
            cog.outl(f"if(length % sizeof({r['type']}) != 0 || length > sizeof({r['name']}.value)) {{");
            cog.outl("    ULOG_ARG_ERROR(&service_id_, \"Invalid data size\");");
            cog.outl("    return false;");
            cog.outl("}");
            cog.outl(f"{r['name']}.length = length/sizeof({r['type']});")
            cog.outl(f"memcpy(&{r['name']}.value, payload, length);")
            cog.outl(f"{r['name']}.valid = true;")
            cog.outl("return true;")
        else:
            cog.outl(f"if(length != sizeof({r['name']}.value)) {{");
            cog.outl("    ULOG_ARG_ERROR(&service_id_, \"Invalid data size\");");
            cog.outl("    return false;");
            cog.outl("}");
            cog.outl(f"memcpy(&{r['name']}.value, payload, length);")
            cog.outl(f"{r['name']}.valid = true;")
            cog.outl("return true;")
    ]]]*/
    case 0:
    if(length % sizeof(char) != 0 || length > sizeof(Register1.value)) {
        ULOG_ARG_ERROR(&service_id_, "Invalid data size");
        return false;
    }
    Register1.length = length/sizeof(char);
    memcpy(&Register1.value, payload, length);
    Register1.valid = true;
    return true;
    case 1:
    if(length != sizeof(Register2.value)) {
        ULOG_ARG_ERROR(&service_id_, "Invalid data size");
        return false;
    }
    memcpy(&Register2.value, payload, length);
    Register2.valid = true;
    return true;
    case 2:
    return OnRegisterRegister3Changed(payload, length);
    case 3:
    if(length != sizeof(Register4Optional.value)) {
        ULOG_ARG_ERROR(&service_id_, "Invalid data size");
        return false;
    }
    memcpy(&Register4Optional.value, payload, length);
    Register4Optional.valid = true;
    return true;
    //[[[end]]]
    default:
      // If the register doesn't exist (interface side is newer),
      // return true to ensure compatibility with newer interface versions
      return true;
  }
  return false;
}

/*[[[cog
cog.outl(f"constexpr unsigned char {service['class_name']}::SERVICE_NAME[];")
cog.outl(f"const char* {service['class_name']}::GetName() {{")
]]]*/
constexpr unsigned char ServiceTemplateBase::SERVICE_NAME[];
const char* ServiceTemplateBase::GetName() {
//[[[end]]]
  return (const char*)SERVICE_NAME;
}

/*[[[cog
if service["functions"]:
    cog.outl(f"void {service['class_name']}::dispatchRpcCall(uint8_t function_id, uint16_t call_id, const void* payload, size_t len) {{")
    has_params = any(func["parameters"] for func in service["functions"])
    if has_params:
        cog.outl("  const auto* buf = static_cast<const uint8_t*>(payload);")
    else:
        cog.outl("  (void)payload;")
        cog.outl("  (void)len;")
    cog.outl("  switch (function_id) {")
    for func in service["functions"]:
        cog.outl(f"    case {func['id']}: {{")
        # Declare parameter variables
        for p in func["parameters"]:
            if p['is_array']:
                cog.outl(f"      {p['type']} param_{p['name']}[{p['max_length']}] = {{}};")
                cog.outl(f"      uint32_t param_{p['name']}Len = 0;")
            else:
                cog.outl(f"      {p['type']} param_{p['name']}{{}};")
        # Deserialize params from DataDescriptor stream
        if func["parameters"]:
            num_params = len(func["parameters"])
            cog.outl("      size_t offset = 0;")
            cog.outl("      bool parse_ok = true;")
            cog.outl(f"      uint8_t params_received = 0;")
            cog.outl("      while (offset + sizeof(xbot::datatypes::DataDescriptor) <= len) {")
            cog.outl("        const auto* desc = reinterpret_cast<const xbot::datatypes::DataDescriptor*>(buf + offset);")
            cog.outl("        offset += sizeof(xbot::datatypes::DataDescriptor);")
            cog.outl("        if (offset + desc->payload_size > len) { parse_ok = false; break; }")
            cog.outl("        switch (desc->target_id) {")
            for p in func["parameters"]:
                cog.outl(f"          case {p['id']}:")
                if p['is_array']:
                    cog.outl(f"            if (desc->payload_size % sizeof({p['type']}) == 0 &&")
                    cog.outl(f"                desc->payload_size <= sizeof(param_{p['name']})) {{")
                    cog.outl(f"              memcpy(param_{p['name']}, buf + offset, desc->payload_size);")
                    cog.outl(f"              param_{p['name']}Len = desc->payload_size / sizeof({p['type']});")
                    cog.outl(f"              params_received++;")
                    cog.outl("            } else { parse_ok = false; }")
                else:
                    cog.outl(f"            if (desc->payload_size == sizeof({p['type']})) {{")
                    cog.outl(f"              memcpy(&param_{p['name']}, buf + offset, sizeof({p['type']}));")
                    cog.outl(f"              params_received++;")
                    cog.outl("            } else { parse_ok = false; }")
                cog.outl("            break;")
            cog.outl("          default: break;")
            cog.outl("        }")
            cog.outl("        offset += desc->payload_size;")
            cog.outl("      }")
            cog.outl(f"      if (!parse_ok || params_received != {num_params}) {{")
            cog.outl("        SendRpcResponse(call_id, xbot::datatypes::RpcStatus::ERROR, nullptr, 0);")
            cog.outl("        return;")
            cog.outl("      }")
        # Build call argument list — call_id first, then params, then return buffer for arrays
        call_args = ["call_id"]
        for p in func["parameters"]:
            if p['is_array']:
                call_args.append(f"param_{p['name']}, param_{p['name']}Len")
            else:
                call_args.append(f"param_{p['name']}")
        if func['return_is_array']:
            cog.outl(f"      {func['return_base_type']} rpc_ret_buf[{func['return_max_length']}] = {{}};")
            cog.outl(f"      uint16_t rpc_ret_len = {func['return_max_length']};")
            call_args.append("rpc_ret_buf, &rpc_ret_len")
        call_str = ", ".join(call_args)
        # Set max response size before dispatching to user code
        if func['return_type'] == 'void':
            cog.outl(f"      rpc_max_response_size_ = 0;")
        elif func['return_is_array']:
            cog.outl(f"      rpc_max_response_size_ = {func['return_max_length']} * sizeof({func['return_base_type']});")
        else:
            cog.outl(f"      rpc_max_response_size_ = sizeof({func['return_type']});")
        # Call is fire-and-forget: the virtual is responsible for calling SendRpcResponse()
        cog.outl(f"      RPC{func['name']}({call_str});")
        cog.outl("      return;")
        cog.outl("    }")
    cog.outl("    default:")
    cog.outl("      SendRpcResponse(call_id, xbot::datatypes::RpcStatus::ERROR, nullptr, 0);")
    cog.outl("  }")
    cog.outl("}")
]]]*/
void ServiceTemplateBase::dispatchRpcCall(uint8_t function_id, uint16_t call_id, const void* payload, size_t len) {
  const auto* buf = static_cast<const uint8_t*>(payload);
  switch (function_id) {
    case 0: {
      rpc_max_response_size_ = 0;
      RPCNoParamsNoReturn(call_id);
      return;
    }
    case 1: {
      float param_Speed{};
      uint32_t param_Count{};
      uint8_t param_Enable{};
      size_t offset = 0;
      bool parse_ok = true;
      uint8_t params_received = 0;
      while (offset + sizeof(xbot::datatypes::DataDescriptor) <= len) {
        const auto* desc = reinterpret_cast<const xbot::datatypes::DataDescriptor*>(buf + offset);
        offset += sizeof(xbot::datatypes::DataDescriptor);
        if (offset + desc->payload_size > len) { parse_ok = false; break; }
        switch (desc->target_id) {
          case 0:
            if (desc->payload_size == sizeof(float)) {
              memcpy(&param_Speed, buf + offset, sizeof(float));
              params_received++;
            } else { parse_ok = false; }
            break;
          case 1:
            if (desc->payload_size == sizeof(uint32_t)) {
              memcpy(&param_Count, buf + offset, sizeof(uint32_t));
              params_received++;
            } else { parse_ok = false; }
            break;
          case 2:
            if (desc->payload_size == sizeof(uint8_t)) {
              memcpy(&param_Enable, buf + offset, sizeof(uint8_t));
              params_received++;
            } else { parse_ok = false; }
            break;
          default: break;
        }
        offset += desc->payload_size;
      }
      if (!parse_ok || params_received != 3) {
        SendRpcResponse(call_id, xbot::datatypes::RpcStatus::ERROR, nullptr, 0);
        return;
      }
      rpc_max_response_size_ = sizeof(int32_t);
      RPCScalarParamsWithReturn(call_id, param_Speed, param_Count, param_Enable);
      return;
    }
    case 2: {
      char param_Label[64] = {};
      uint32_t param_LabelLen = 0;
      size_t offset = 0;
      bool parse_ok = true;
      uint8_t params_received = 0;
      while (offset + sizeof(xbot::datatypes::DataDescriptor) <= len) {
        const auto* desc = reinterpret_cast<const xbot::datatypes::DataDescriptor*>(buf + offset);
        offset += sizeof(xbot::datatypes::DataDescriptor);
        if (offset + desc->payload_size > len) { parse_ok = false; break; }
        switch (desc->target_id) {
          case 0:
            if (desc->payload_size % sizeof(char) == 0 &&
                desc->payload_size <= sizeof(param_Label)) {
              memcpy(param_Label, buf + offset, desc->payload_size);
              param_LabelLen = desc->payload_size / sizeof(char);
              params_received++;
            } else { parse_ok = false; }
            break;
          default: break;
        }
        offset += desc->payload_size;
      }
      if (!parse_ok || params_received != 1) {
        SendRpcResponse(call_id, xbot::datatypes::RpcStatus::ERROR, nullptr, 0);
        return;
      }
      rpc_max_response_size_ = 0;
      RPCArrayParamNoReturn(call_id, param_Label, param_LabelLen);
      return;
    }
    case 3: {
      char param_Name[32] = {};
      uint32_t param_NameLen = 0;
      float param_Value{};
      size_t offset = 0;
      bool parse_ok = true;
      uint8_t params_received = 0;
      while (offset + sizeof(xbot::datatypes::DataDescriptor) <= len) {
        const auto* desc = reinterpret_cast<const xbot::datatypes::DataDescriptor*>(buf + offset);
        offset += sizeof(xbot::datatypes::DataDescriptor);
        if (offset + desc->payload_size > len) { parse_ok = false; break; }
        switch (desc->target_id) {
          case 0:
            if (desc->payload_size % sizeof(char) == 0 &&
                desc->payload_size <= sizeof(param_Name)) {
              memcpy(param_Name, buf + offset, desc->payload_size);
              param_NameLen = desc->payload_size / sizeof(char);
              params_received++;
            } else { parse_ok = false; }
            break;
          case 1:
            if (desc->payload_size == sizeof(float)) {
              memcpy(&param_Value, buf + offset, sizeof(float));
              params_received++;
            } else { parse_ok = false; }
            break;
          default: break;
        }
        offset += desc->payload_size;
      }
      if (!parse_ok || params_received != 2) {
        SendRpcResponse(call_id, xbot::datatypes::RpcStatus::ERROR, nullptr, 0);
        return;
      }
      rpc_max_response_size_ = sizeof(uint8_t);
      RPCMixedParamsWithReturn(call_id, param_Name, param_NameLen, param_Value);
      return;
    }
    default:
      SendRpcResponse(call_id, xbot::datatypes::RpcStatus::ERROR, nullptr, 0);
  }
}
//[[[end]]]
