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

xbot::service::Service::Service(uint16_t service_id, Scheduler &scheduler)
    : ServiceIo(service_id), scratch_buffer_{}, scheduler_(scheduler) {
}

xbot::service::Service::~Service() {
  mutex::deinitialize(&state_mutex_);
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

  // FIXME: Call runProcessing(), but be careful because it never returns.

  heartbeat_schedule_ = scheduler_.AddSchedule(0, [this]() { heartbeat(); });
  request_config_schedule_ = scheduler_.AddSchedule(config::request_configuration_interval_micros, [this]() {
    ULOG_ARG_INFO(&service_id_, "Requesting Configuration");
    SendConfigurationRequest();
  });
  sd_advertisement_schedule_ = scheduler_.AddSchedule(0, [this]() {
    ULOG_ARG_DEBUG(&service_id_, "Sending SD advertisement");
    Lock lk(&state_mutex_);
    advertiseService();
  });

  return true;
}

// FIXME: Call this whenever one of the conditions changes.
void xbot::service::Service::UpdateSchedules() {
  heartbeat_schedule_->SetEnabled(IsClaimed());
  request_config_schedule_->SetEnabled(!is_running_ && IsClaimed() && !isConfigured());

  uint32_t sd_interval =
      IsClaimed() ? config::sd_advertisement_interval_micros : config::sd_advertisement_interval_micros_fast;
  sd_advertisement_schedule_->SetInterval(sd_interval);

  // TODO: Check is_running_ for tick() schedules
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

xbot::service::Schedule *xbot::service::Service::RegisterTick(uint32_t interval_micros, Schedule::Callback callback) {
  return scheduler_.AddSchedule(interval_micros, callback);
}

void xbot::service::Service::runProcessing() {
  OnCreate();
  loadConfigurationDefaults();
  // If after clearing the config, the service is configured, it does not need
  // to be configured.
  if (isConfigured() && OnStart()) {
    ULOG_ARG_INFO(&service_id_, "Service started without requiring configuration");
    is_running_ = true;
  }
  while (true) {
    // Check, if we should stop
    {
      Lock lk(&state_mutex_);
      if (stopped) {
        // TODO: Stop scheduled tasks. Theoretical, as stopped is never set.
        return;
      }
    }

    // Fetch from queue
    packet::PacketPtr packet;
    // TODO: How to block infinitely until either a packet arrives or stopped becomes true?
    int32_t block_time = 1'000'000;
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
    OnStop();
    config_received_ = false;
    is_running_ = false;
    // If after clearing the config, the service is configured, it does not need
    // to be configured.
    if (isConfigured() && OnStart()) {
      ULOG_ARG_INFO(&service_id_, "Service started without requiring configuration");
      is_running_ = true;
    }
  }

  const auto payload_ptr = reinterpret_cast<const datatypes::ClaimPayload *>(payload);
  target_ip_ = payload_ptr->target_ip;
  target_port_ = payload_ptr->target_port;

  uint32_t heartbeat_micros = payload_ptr->heartbeat_micros;

  // Send early in order to allow for jitter
  if (heartbeat_micros > config::heartbeat_jitter) {
    heartbeat_micros -= config::heartbeat_jitter;
  }

  // send heartbeat at twice the requested rate
  heartbeat_micros >>= 1;

  heartbeat_schedule_->SetInterval(heartbeat_micros);

  config_received_ = false;

  ULOG_ARG_INFO(&service_id_, "service claimed successfully.");

  SendDataClaimAck();
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
void xbot::service::Service::HandleConfigurationTransaction(xbot::datatypes::XbotHeader *header, const void *payload,
                                                            size_t payload_len) {
  (void)header;
  ULOG_ARG_INFO(&service_id_, "Received Configuration");
  // Call clean up callback, if service was running
  if (is_running_) {
    OnStop();
  }
  loadConfigurationDefaults();
  is_running_ = false;

  bool register_success = true;
  // Set the registers
  const auto payload_buffer = static_cast<const uint8_t *>(payload);
  // Go through all data packets in the transaction
  size_t processed_len = 0;
  while (processed_len + sizeof(datatypes::DataDescriptor) <= payload_len) {
    // we have at least enough data for the next descriptor, read it
    const auto descriptor = reinterpret_cast<const datatypes::DataDescriptor *>(payload_buffer + processed_len);
    size_t data_size = descriptor->payload_size;
    if (processed_len + sizeof(datatypes::DataDescriptor) + data_size <= payload_len) {
      // we can safely read the data
      register_success &= setRegister(descriptor->target_id,
                                      payload_buffer + processed_len + sizeof(datatypes::DataDescriptor), data_size);
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

  config_received_ = true;

  // regster_success checks if all new config was applied correctly,
  // isConfigured() checks if overall config is correct
  if (register_success && isConfigured()) {
    // successfully set all registers, start the service if it was configured correctly
    if (OnStart()) {
      ULOG_ARG_INFO(&service_id_, "Service started after successful configuration");
      is_running_ = true;
    } else {
      ULOG_ARG_ERROR(&service_id_, "OnStart() returned false");
      // Request new configuration, otherwise we're stuck
      config_received_ = false;
    }
  }
}
bool xbot::service::Service::SendConfigurationRequest() {
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
  return true;
}

bool xbot::service::Service::isConfigured() {
  return !hasRegisters() || (config_received_ && allRegistersValid());
}
