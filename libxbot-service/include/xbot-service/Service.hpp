//
// Created by clemens on 3/18/24.
//

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <xbot-service/ServiceIo.h>

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
  static void startProcessingHelper(void *service) { static_cast<Service *>(service)->runProcessing(); }

 protected:
  // Buffer to serialize service announcements and also custom serialized data
  // (zcbor) or transactions.
  uint8_t scratch_buffer[config::max_packet_size - sizeof(datatypes::XbotHeader)];

  // Track how much of the scratch_buffer is already full
  size_t scratch_buffer_fill_ = 0;

  // Track, if we have already started a transaction
  bool transaction_started_ = false;

  // Scratch space for the header.
  // Needs to be protected by a mutex, becuase SendData might
  // be called from a different thread
  datatypes::XbotHeader header_{};

  bool SendData(uint16_t target_id, const void *data, size_t size);

  bool StartTransaction(uint64_t timestamp = 0);

  bool CommitTransaction();

  /**
   * Called before Configure()
   */
  virtual void OnCreate() {};

  /**
   * Called before OnStart
   * @return true, if configuration was success
   */
  virtual bool Configure() { return true; };

  /**
   * Called after successfully Configure() and before tick() starts
   */
  virtual void OnStart() {};

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

  uint32_t tick_rate_micros_;
  uint32_t last_tick_micros_ = 0;
  uint32_t last_service_discovery_micros_ = 0;
  uint32_t last_heartbeat_micros_ = 0;
  uint32_t heartbeat_micros_ = 0;
  uint32_t target_ip = 0;
  uint32_t target_port = 0;
  uint32_t last_configuration_request_micros_ = 0;

  // True, when the service is running (i.e. configured and tick() is being
  // called)
  bool is_running_ = 0;

  void heartbeat();

  void runProcessing();

  void HandleClaimMessage(datatypes::XbotHeader *header, const void *payload, size_t payload_len);
  void HandleDataMessage(datatypes::XbotHeader *header, const void *payload, size_t payload_len);
  void HandleDataTransaction(datatypes::XbotHeader *header, const void *payload, size_t payload_len);
  void HandleConfigurationTransaction(datatypes::XbotHeader *header, const void *payload, size_t payload_len);

  void fillHeader();

  bool SendDataClaimAck();
  bool SendConfigurationRequest();

  virtual void tick() = 0;

  virtual bool advertiseService() = 0;

  // Return true, if the service is configured properly
  virtual bool isConfigured() = 0;
  virtual void clearConfiguration() = 0;

  virtual bool handleData(uint16_t target_id, const void *payload, size_t length) = 0;

  virtual bool setRegister(uint16_t target_id, const void *payload, size_t length) = 0;
};
}  // namespace xbot::service

#endif  // SERVICE_HPP
