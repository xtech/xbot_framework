//
// Created by clemens on 7/17/24.
//
#include <utility>
#include <xbot-service-interface/ServiceInterfaceBase.hpp>

#include "spdlog/spdlog.h"
using namespace xbot::serviceif;

ServiceInterfaceBase::ServiceInterfaceBase(uint16_t service_id, std::string type, uint32_t version, Context ctx)
    : service_id_(service_id), type_(std::move(type)), version_(version), ctx(ctx) {
}

void ServiceInterfaceBase::Start() {
  ctx.serviceDiscovery->RegisterCallbacks(this);
}

bool ServiceInterfaceBase::StartTransaction(bool is_configuration) {
  // Lock like this, we need to keep locked until CommitTransaction()
  state_mutex_.lock();
  if (transaction_started_) {
    state_mutex_.unlock();
    return false;
  }
  transaction_started_ = true;
  is_configuration_transaction_ = is_configuration;
  buffer_.resize(sizeof(xbot::datatypes::XbotHeader));
  FillHeader();

  auto header_ptr = reinterpret_cast<xbot::datatypes::XbotHeader *>(buffer_.data());
  *header_ptr = header_;

  return true;
}

bool ServiceInterfaceBase::CommitTransaction() {
  std::unique_lock lk{state_mutex_};
  if (!transaction_started_) {
    return false;
  }
  // Clear it here in case we encounter an error
  transaction_started_ = false;
  // Unlock for the Lock() in StartTransaction();
  // we still have the lk - so it's OK to do this here
  state_mutex_.unlock();

  if (!service_discovered_) {
    spdlog::debug("Service has no target, dropping packet");
    return false;
  }
  if (buffer_.size() < sizeof(xbot::datatypes::XbotHeader)) {
    spdlog::error("illegal state: buffer smaller than header size");
    return false;
  }
  auto header_ptr = reinterpret_cast<xbot::datatypes::XbotHeader *>(buffer_.data());
  header_ptr->message_type = xbot::datatypes::MessageType::TRANSACTION;
  if (is_configuration_transaction_) {
    header_ptr->arg1 = 1;
  } else {
    header_ptr->arg1 = 0;
  }
  header_ptr->payload_size = buffer_.size() - sizeof(xbot::datatypes::XbotHeader);

  return ctx.io->SendData(service_id_, buffer_);
}

bool ServiceInterfaceBase::SendData(uint16_t target_id, const void *data, size_t size, bool is_configuration) {
  if (!service_discovered_) {
    spdlog::debug("Service has no target, dropping packet");
    return false;
  }

  std::unique_lock lk{state_mutex_};

  if (transaction_started_) {
    if (is_configuration != is_configuration_transaction_) {
      spdlog::warn("Cannot mix configration and data in a single transaction");
      return false;
    }
    // Store the offset before resizing the buffer (that's where we want to
    // append)
    size_t buffer_size = buffer_.size();
    // Reserve enough space for the new data
    buffer_.resize(buffer_.size() + size + sizeof(xbot::datatypes::DataDescriptor));
    auto descriptor_ptr = reinterpret_cast<xbot::datatypes::DataDescriptor *>(buffer_.data() + buffer_size);
    auto data_target_ptr = (buffer_.data() + buffer_size + sizeof(xbot::datatypes::DataDescriptor));
    descriptor_ptr->payload_size = size;
    descriptor_ptr->reserved = 0;
    descriptor_ptr->target_id = target_id;
    memcpy(data_target_ptr, data, size);
    return true;
  }

  if (is_configuration) {
    spdlog::error("Sending configuration is only supported in a transaction");
    return false;
  }

  buffer_.resize(sizeof(xbot::datatypes::XbotHeader) + size);
  FillHeader();

  auto header_ptr = reinterpret_cast<xbot::datatypes::XbotHeader *>(buffer_.data());
  *header_ptr = header_;
  header_ptr->payload_size = size;
  header_ptr->arg2 = target_id;
  header_ptr->message_type = xbot::datatypes::MessageType::DATA;

  memcpy(buffer_.data() + sizeof(xbot::datatypes::XbotHeader), data, size);

  return ctx.io->SendData(service_id_, buffer_);
}

bool ServiceInterfaceBase::OnServiceDiscovered(uint16_t service_id) {
  std::unique_lock lk(state_mutex_);
  // Check, if the service we're interested was discovered
  if (service_id_ != service_id) {
    return false;
  }
  if (!service_discovered_) {
    // Not bound yet, check, if requirements match. If so, bind do this service.
    const auto info = ctx.serviceDiscovery->GetServiceInfo(service_id_);
    if (info->service_id_ == service_id_ && info->description.type == type_ && info->description.version == version_) {
      spdlog::info("Found matching service, registering callbacks");
      // Unregister service discovery callbacks, we're not interested anymore
      ctx.serviceDiscovery->UnregisterCallbacks(this);
      service_discovered_ = true;
      // Register for data
      ctx.io->RegisterCallbacks(service_id_, this);
      return true;
    } else {
      spdlog::warn("Got ServiceDiscovered call twice. There might be an issue with duplicated service_ids.");
    }
  }
  return false;
}

bool ServiceInterfaceBase::OnEndpointChanged(uint16_t service_id_, uint32_t old_ip, uint16_t old_port, uint32_t new_ip,
                                             uint16_t new_port) {
  /** we don't care, since we IO will lookup the endpoint for us **/
  return true;
}

void ServiceInterfaceBase::FillHeader() {
  using namespace std::chrono;
  header_.service_id = service_id_;
  header_.message_type = xbot::datatypes::MessageType::UNKNOWN;
  header_.payload_size = 0;
  header_.protocol_version = 1;
  header_.arg1 = 0;
  header_.arg2 = 0;
  header_.sequence_no++;
  if (header_.sequence_no == 0) {
    // Clear reboot flag on rolloger
    header_.flags &= 0xFE;
  }
  header_.timestamp = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
