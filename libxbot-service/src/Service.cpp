//
// Created by clemens on 3/20/24.
//
#include <ulog.h>

#include <cstring>
#include <xbot-service/Io.hpp>
#include <xbot-service/Lock.hpp>
#include <xbot-service/Service.hpp>
#include <xbot-service/portable/system.hpp>
#include <xbot/datatypes/ClaimPayload.hpp>

xbot::service::Service::Service(uint16_t service_id, void *processing_thread_stack, size_t processing_thread_stack_size)
    : ServiceIo(service_id),
      scratch_buffer_{},
      processing_thread_stack_(processing_thread_stack),
      processing_thread_stack_size_(processing_thread_stack_size) {
}

xbot::service::Service::~Service() {
  mutex::deinitialize(&state_mutex_);
  thread::deinitialize(&process_thread_);
}

bool xbot::service::Service::start() {
  stopped = false;

  // Set reboot flag
  header_.flags = 1;

  if (!mutex::initialize(&state_mutex_)) {
    return false;
  }
  if (!queue::initialize(&packet_queue_, packet_queue_length, packet_queue_buffer, sizeof(packet_queue_buffer))) {
    return false;
  }

  Io::registerServiceIo(this);

  if (!thread::initialize(&process_thread_, Service::startProcessingHelper, this, processing_thread_stack_,
                          processing_thread_stack_size_, GetName())) {
    return false;
  }

  return true;
}

bool xbot::service::Service::Start() {
  if (!OnStart()) {
    ULOG_ARG_ERROR(&service_id_, "OnStart() returned false");
    return false;
  }
  is_running_ = true;
  OnLifecycleStatusChanged();
  return true;
}

void xbot::service::Service::Stop() {
  OnStop();
  is_running_ = false;
  OnLifecycleStatusChanged();
}

void xbot::service::Service::OnLifecycleStatusChanged() {
  heartbeat_schedule_.SetEnabled(IsClaimed());
  sd_advertisement_schedule.SetInterval(IsClaimed() ? config::sd_advertisement_interval_micros
                                                    : config::sd_advertisement_interval_micros_fast);
  config_request_schedule.SetEnabled(config_required_ && IsClaimed());
}

bool xbot::service::Service::SendData(uint16_t target_id, const void *data, size_t size) {
  if (transaction_started_) {
    // We are using a transaction, append the data
    if (scratch_buffer_fill_ + size + sizeof(datatypes::DataDescriptor) <= sizeof(scratch_buffer_)) {
      // add the size to the header
      // we can fit the data, write descriptor and data
      auto descriptor_ptr = reinterpret_cast<datatypes::DataDescriptor *>(scratch_buffer_ + scratch_buffer_fill_);
      auto data_target_ptr = (scratch_buffer_ + scratch_buffer_fill_ + sizeof(datatypes::DataDescriptor));
      descriptor_ptr->payload_size = size;
      descriptor_ptr->reserved = 0;
      descriptor_ptr->target_id = target_id;
      memcpy(data_target_ptr, data, size);
      scratch_buffer_fill_ += size + sizeof(datatypes::DataDescriptor);
      return true;
    } else {
      // TODO: send the current packet and create a new one.
      return false;
    }
  }
  if (!IsClaimed()) {
    ULOG_ARG_DEBUG(&service_id_, "Service has no target, dropping packet");
    return false;
  }
  // Send header and data
  packet::PacketPtr ptr = packet::allocatePacket();
  {
    Lock lk(&state_mutex_);
    fillHeader();
    header_.message_type = datatypes::MessageType::DATA;
    header_.payload_size = size;
    header_.arg2 = target_id;

    packet::packetAppendData(ptr, &header_, sizeof(header_));
  }
  packet::packetAppendData(ptr, data, size);
  return Io::transmitPacket(ptr, target_ip_, target_port_);
}

bool xbot::service::Service::SendDataClaimAck() {
  if (!IsClaimed()) {
    ULOG_ARG_WARNING(&service_id_, "Service has no target, dropping packet");
    return false;
  }
  // Send header and data
  packet::PacketPtr ptr = packet::allocatePacket();

  {
    Lock lk(&state_mutex_);
    fillHeader();
    header_.message_type = datatypes::MessageType::CLAIM;
    header_.payload_size = 0;
    header_.arg1 = 1;
    packet::packetAppendData(ptr, &header_, sizeof(header_));
  }

  return Io::transmitPacket(ptr, target_ip_, target_port_);
}
bool xbot::service::Service::StartTransaction(uint64_t timestamp) {
  if (transaction_started_) {
    return false;
  }
  // Lock like this, because we need to keep it locked until Commit()
  mutex::lockMutex(&state_mutex_);
  fillHeader();
  // If the user has provided a timestamp for the data, set it here.
  if (timestamp) {
    header_.timestamp = timestamp;
  }
  scratch_buffer_fill_ = 0;
  transaction_started_ = true;
  return true;
}
bool xbot::service::Service::CommitTransaction() {
  mutex::lockMutex(&state_mutex_);
  if (transaction_started_) {
    // unlock the StartTransaction() lock
    mutex::unlockMutex(&state_mutex_);
  }
  transaction_started_ = false;
  if (!IsClaimed()) {
    ULOG_ARG_DEBUG(&service_id_, "Service has no target, dropping packet");
    mutex::unlockMutex(&state_mutex_);
    return false;
  }
  header_.message_type = datatypes::MessageType::TRANSACTION;
  header_.payload_size = scratch_buffer_fill_;

  // Send header and data
  packet::PacketPtr ptr = packet::allocatePacket();
  packet::packetAppendData(ptr, &header_, sizeof(header_));
  packet::packetAppendData(ptr, scratch_buffer_, scratch_buffer_fill_);
  // done with the scratch buffer, release it
  mutex::unlockMutex(&state_mutex_);
  return Io::transmitPacket(ptr, target_ip_, target_port_);
}

void xbot::service::Service::fillHeader() {
  header_.service_id = service_id_;
  header_.message_type = datatypes::MessageType::UNKNOWN;
  header_.payload_size = 0;
  header_.protocol_version = 1;
  header_.arg1 = 0;
  header_.arg2 = 0;
  header_.sequence_no++;
  if (header_.sequence_no == 0) {
    // Clear reboot flag on rollover
    header_.flags &= 0xFE;
  }
  header_.timestamp = system::getTimeMicros();
}

void xbot::service::Service::heartbeat() {
  ULOG_ARG_DEBUG(&service_id_, "Sending heartbeat");
  // Send header and data
  packet::PacketPtr ptr = packet::allocatePacket();
  {
    Lock lk(&state_mutex_);
    fillHeader();
    header_.message_type = datatypes::MessageType::HEARTBEAT;
    header_.payload_size = 0;
    header_.arg1 = 0;
    packet::packetAppendData(ptr, &header_, sizeof(header_));
  }
  Io::transmitPacket(ptr, target_ip_, target_port_);
}

void xbot::service::Service::runProcessing() {
  OnCreate();

  // If we have no registers, we can start the service immediately.
  // Otherwise, we will send a configuration request.
  loadConfigurationDefaults();
  // TODO: Load previous configuration from flash.
  if (!config_required_ && Start()) {
    ULOG_ARG_INFO(&service_id_, "Service started without requiring configuration");
  }

  uint32_t last_tick_micros = system::getTimeMicros();
  while (true) {
    // Check, if we should stop
    {
      Lock lk(&state_mutex_);
      if (stopped) {
        return;
      }
    }

    // Run schedules.
    uint32_t now_micros = system::getTimeMicros();
    uint32_t block_time = scheduler_.Tick(now_micros - last_tick_micros);
    if (block_time > 1'000'000) {
      block_time = 1'000'000;
    }
    last_tick_micros = now_micros;

    // Fetch packet from queue.
    packet::PacketPtr packet;
    if (queue::queuePopItem(&packet_queue_, reinterpret_cast<void **>(&packet), block_time)) {
      void *buffer = nullptr;
      size_t used_data = 0;
      if (packet::packetGetData(packet, &buffer, &used_data)) {
        const auto header = reinterpret_cast<datatypes::XbotHeader *>(buffer);
        const uint8_t *const payload_buffer = reinterpret_cast<uint8_t *>(buffer) + sizeof(datatypes::XbotHeader);

        switch (header->message_type) {
          case datatypes::MessageType::CLAIM: HandleClaimMessage(header, payload_buffer, header->payload_size); break;
          case datatypes::MessageType::DATA:
            if (is_running_) {
              HandleDataMessage(header, payload_buffer, header->payload_size);
            }
            break;
          case datatypes::MessageType::TRANSACTION:
            if (header->arg1 == 0 && is_running_) {
              HandleDataTransaction(header, payload_buffer, header->payload_size);
            } else if (header->arg1 == 1) {
              HandleConfigurationTransaction(header, payload_buffer, header->payload_size);
            }
            break;
          default: ULOG_ARG_WARNING(&service_id_, "Got unsupported message"); break;
        }
      }

      packet::freePacket(packet);
    }
  }
}

void xbot::service::Service::AdvertiseService() {
  ULOG_ARG_DEBUG(&service_id_, "Sending SD advertisement");
  mutex::lockMutex(&state_mutex_);
  AdvertiseServiceImpl();
  mutex::unlockMutex(&state_mutex_);
}

static uint32_t CalculateHeartbeatInterval(uint32_t heartbeat_micros) {
  // Send early in order to allow for jitter.
  if (heartbeat_micros > xbot::config::heartbeat_jitter) {
    heartbeat_micros -= xbot::config::heartbeat_jitter;
  }
  // Send at twice the requested rate.
  return heartbeat_micros / 2;
}

void xbot::service::Service::HandleClaimMessage(xbot::datatypes::XbotHeader *header, const void *payload,
                                                size_t payload_len) {
  (void)header;
  ULOG_ARG_INFO(&service_id_, "Received claim message");
  if (payload_len != sizeof(datatypes::ClaimPayload)) {
    ULOG_ARG_ERROR(&service_id_, "claim message with invalid payload size");
    return;
  }

  // Stop the service, if running. This way it can be reconfigured
  if (is_running_) {
    Stop();
  }

  const auto payload_ptr = reinterpret_cast<const datatypes::ClaimPayload *>(payload);
  target_ip_ = payload_ptr->target_ip;
  target_port_ = payload_ptr->target_port;
  heartbeat_schedule_.SetInterval(CalculateHeartbeatInterval(payload_ptr->heartbeat_micros));

  ULOG_ARG_INFO(&service_id_, "service claimed successfully.");
  SendDataClaimAck();
  OnLifecycleStatusChanged();

  // If we have no registers, we can start the service immediately.
  // Otherwise, we will send a configuration request.
  loadConfigurationDefaults();
  if (!config_required_ && Start()) {
    ULOG_ARG_INFO(&service_id_, "Service started without requiring configuration");
  }
}
void xbot::service::Service::HandleDataMessage(xbot::datatypes::XbotHeader *header, const void *payload,
                                               size_t payload_len) {
  (void)payload_len;
  // Packet seems OK, hand to service implementation
  handleData(header->arg2, payload, header->payload_size);
}
void xbot::service::Service::HandleDataTransaction(xbot::datatypes::XbotHeader *header, const void *payload,
                                                   size_t payload_len) {
  (void)header;
  const auto payload_buffer = static_cast<const uint8_t *>(payload);
  // Go through all data packets in the transaction
  size_t processed_len = 0;
  while (processed_len + sizeof(datatypes::DataDescriptor) <= payload_len) {
    // we have at least enough data for the next descriptor, read it
    const auto descriptor = reinterpret_cast<const datatypes::DataDescriptor *>(payload_buffer + processed_len);
    size_t data_size = descriptor->payload_size;
    if (processed_len + sizeof(datatypes::DataDescriptor) + data_size <= payload_len) {
      // we can safely read the data
      handleData(descriptor->target_id, payload_buffer + processed_len + sizeof(datatypes::DataDescriptor), data_size);
    } else {
      // error parsing transaction, payload size does not match
      // transaction size!
      break;
    }
    processed_len += data_size + sizeof(datatypes::DataDescriptor);
  }

  if (processed_len != payload_len) {
    ULOG_ARG_ERROR(&service_id_, "Transaction size mismatch");
  }
}

void xbot::service::Service::loadConfigurationDefaults() {
  loadConfigurationDefaultsImpl();
  config_required_ = hasRegisters();
  OnLifecycleStatusChanged();
}

void xbot::service::Service::HandleConfigurationTransaction(xbot::datatypes::XbotHeader *header, const void *payload,
                                                            size_t payload_len) {
  (void)header;
  ULOG_ARG_INFO(&service_id_, "Received Configuration");

  // We need to stop the service before trying to reconfigure it.
  if (is_running_) {
    Stop();
  }

  // Load default configuration and override with the received one.
  loadConfigurationDefaults();
  if (!SetRegistersFromConfigurationMessage(payload, payload_len)) {
    // The error was already logged.
    return;
  }

  // Check if any registers weren't included in the message.
  if (!allRegistersValid()) {
    ULOG_ARG_ERROR(&service_id_, "Configuration message did not contain all required registers");
    return;
  }

  // Try to start the service now, and only then mark the configuration as successful.
  if (Start()) {
    ULOG_ARG_INFO(&service_id_, "Service started after successful configuration");
    config_required_ = false;
    OnLifecycleStatusChanged();
  }
}

bool xbot::service::Service::SetRegistersFromConfigurationMessage(const void *payload, size_t payload_len) {
  const auto payload_buffer = static_cast<const uint8_t *>(payload);

  // Go through all data packets in the transaction
  size_t processed_len = 0;
  while (processed_len + sizeof(datatypes::DataDescriptor) <= payload_len) {
    // we have at least enough data for the next descriptor, read it
    const auto descriptor = reinterpret_cast<const datatypes::DataDescriptor *>(payload_buffer + processed_len);
    size_t data_size = descriptor->payload_size;
    if (processed_len + sizeof(datatypes::DataDescriptor) + data_size <= payload_len) {
      // we can safely read the data
      const void *data = payload_buffer + processed_len + sizeof(datatypes::DataDescriptor);
      if (!setRegister(descriptor->target_id, data, data_size)) {
        return false;
      }
    } else {
      // Error parsing transaction, payload size does not match transaction size!
      break;
    }
    processed_len += data_size + sizeof(datatypes::DataDescriptor);
  }

  if (processed_len != payload_len) {
    ULOG_ARG_ERROR(&service_id_, "Configuration transaction size mismatch");
    return false;
  }

  return true;
}

void xbot::service::Service::SendConfigurationRequest() {
  ULOG_ARG_INFO(&service_id_, "Requesting Configuration");
  // Send header and data
  packet::PacketPtr ptr = packet::allocatePacket();
  {
    Lock lk(&state_mutex_);
    fillHeader();
    header_.message_type = datatypes::MessageType::CONFIGURATION_REQUEST;
    header_.payload_size = 0;
    header_.arg1 = 0;
    packet::packetAppendData(ptr, &header_, sizeof(header_));
  }
  Io::transmitPacket(ptr, target_ip_, target_port_);
}
