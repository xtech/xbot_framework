//
// Created by clemens on 3/18/24.
//

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <xbot-service/ServiceIo.h>

#include <xbot-service/Scheduler.hpp>
#include <xbot/config.hpp>

#include "portable/queue.hpp"
#include "portable/thread.hpp"
#include "xbot/datatypes/XbotHeader.hpp"

namespace xbot::service {
class Service : public ServiceIo {
 public:
  explicit Service(uint16_t service_id, uint32_t tick_rate_micros, void *processing_thread_stack,
                   size_t processing_thread_stack_size);

  virtual ~Service();

  /*
   * @brief Start the service.
   *
   * This method starts the service.
   *
   * @return True if the service started successfully, false otherwise.
   */
  bool start();

  /**
   * Since the portable thread implementation does not know what a class is, we
   * use this helper to start the service.
   * @param service Pointer to the service to start
   * @return null
   */
  static void startProcessingHelper(void *service) {
    static_cast<Service *>(service)->runProcessing();
  }

 protected:
  // Buffer to serialize service announcements and also custom serialized data
  // (zcbor) or transactions.
  uint8_t scratch_buffer_[config::max_packet_size - sizeof(datatypes::XbotHeader)];

  // Track how much of the scratch_buffer_ is already full
  size_t scratch_buffer_fill_ = 0;

  // Track, if we have already started a transaction
  bool transaction_started_ = false;

  // Scratch space for the header.
  // Needs to be protected by a mutex, becuase SendData might
  // be called from a different thread
  datatypes::XbotHeader header_{};

  Scheduler scheduler_;

  bool SendData(uint16_t target_id, const void *data, size_t size);

  bool StartTransaction(uint64_t timestamp = 0);

  bool CommitTransaction();

  /**
   * Called only once before OnStart()
   */
  virtual void OnCreate() {};

  /**
   * Called once the configuration is valid and before tick() starts
   * @return true, if startup was successful
   */
  virtual bool OnStart() {
    return true;
  };

  /**
   * Called before reconfiguring the service for cleanup
   */
  virtual void OnStop() {};

  /**
   * Gets the service name
   */
  virtual const char *GetName() = 0;

 private:
  /**
   * The main thread for the service.
   * Here the implementation can do its processing.
   */
  void *processing_thread_stack_;
  size_t processing_thread_stack_size_;
  XBOT_THREAD_TYPEDEF process_thread_{};

  void advertiseServiceHelper() {
    // ULOG_ARG_DEBUG(&service_id_, "Sending SD advertisement");
    mutex::lockMutex(&state_mutex_);
    advertiseService();
    mutex::unlockMutex(&state_mutex_);
  }

  Schedule heartbeat_schedule_{scheduler_, etl::make_delegate<Service, &Service::heartbeat>(*this)};
  Schedule sd_advertisement_schedule{scheduler_, etl::make_delegate<Service, &Service::advertiseServiceHelper>(*this)};
  Schedule config_request_schedule{scheduler_, etl::make_delegate<Service, &Service::SendConfigurationRequest>(*this),
                                   config::request_configuration_interval_micros};
  Schedule tick_schedule{scheduler_, etl::make_delegate<Service, &Service::tick>(*this), tick_rate_micros_};

  uint32_t tick_rate_micros_;
  uint32_t target_ip_ = 0;
  uint32_t target_port_ = 0;
  bool config_required_ = true;

  // True, when the service is running (i.e. configured and tick() is being
  // called)
  bool is_running_ = 0;

  bool Start();
  void Stop();

  // Called whenever the service is_running_, IsClaimed() or config_required_ changes.
  void OnLifecycleStatusChanged();

  void heartbeat();

  void runProcessing();

  void HandleClaimMessage(datatypes::XbotHeader *header, const void *payload, size_t payload_len);
  void HandleDataMessage(datatypes::XbotHeader *header, const void *payload, size_t payload_len);
  void HandleDataTransaction(datatypes::XbotHeader *header, const void *payload, size_t payload_len);
  void HandleConfigurationTransaction(datatypes::XbotHeader *header, const void *payload, size_t payload_len);
  bool SetRegistersFromConfigurationMessage(const void *payload, size_t payload_len);

  void fillHeader();

  bool SendDataClaimAck();
  void SendConfigurationRequest();

  virtual void tick() {};

  virtual bool advertiseService() = 0;

  bool IsClaimed() {
    return target_ip_ != 0 && target_port_ != 0;
  }

  virtual bool hasRegisters() = 0;
  virtual bool allRegistersValid() = 0;
  virtual void loadConfigurationDefaultsImpl() = 0;
  virtual void loadConfigurationDefaults();

  virtual void handleData(uint16_t target_id, const void *payload, size_t length) = 0;

  virtual bool setRegister(uint16_t target_id, const void *payload, size_t length) = 0;
};
}  // namespace xbot::service

#endif  // SERVICE_HPP
