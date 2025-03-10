//
// Created by clemens on 7/22/24.
//
#include <crow.h>

#include <csignal>
#include <mutex>
#include <xbot-service-interface/XbotServiceInterface.hpp>

#include "CrowToSpeedlogHandler.hpp"
#include "PlotJugglerBridge.hpp"
#include "RemoteLoggingReceiverImpl.hpp"
#include "ServiceDiscoveryImpl.hpp"
#include "ServiceIOImpl.hpp"

using namespace xbot::serviceif;

std::mutex mtx;
bool started{false};

Context ctx{};
std::future<void> crow_future{};

hub::CrowToSpeedlogHandler logger{};

std::unique_ptr<PlotJugglerBridge> pjb = nullptr;
std::unique_ptr<crow::SimpleApp> crow_app = nullptr;

void SignalHandler(int signal) {
  Stop();
}
struct sigaction act;

xbot::serviceif::Context xbot::serviceif::Start(bool register_handlers, std::string bind_ip, bool start_rest_api) {
  std::unique_lock lk{mtx};

  if (started) {
    return ctx;
  }
  started = true;

  // Service Discovery needs to join the multicast on the specified interface
  ServiceDiscoveryImpl::SetMulticastIfAddress(bind_ip);
  // Service IO needs to bind to the specified interface (unicast)
  ServiceIOImpl::SetBindAddress(bind_ip);
  // Remote Logging needs to join the multicast on the specified interface
  RemoteLoggingReceiverImpl::SetMulticastIfAddress(bind_ip);

  //
  //  // Register signal handler for graceful shutdown
  //  signal(SIGINT, SignalHandler);
  //  signal(SIGTERM, SignalHandler);
  //  signal(SIGSTOP, SignalHandler);


  const auto ioImpl = ServiceIOImpl::GetInstance();
  const auto sdImpl = ServiceDiscoveryImpl::GetInstance();
  const auto rlImpl = RemoteLoggingReceiverImpl::GetInstance();

  ctx = {.io = ioImpl, .serviceDiscovery = sdImpl, .ctx = rlImpl};

  // Register the ServiceIO before starting service discovery
  // this way, whenever a service is found, ServiceIO claims it automatically
  ctx.serviceDiscovery->RegisterCallbacks(ioImpl);

  pjb = std::make_unique<PlotJugglerBridge>(ctx);
  pjb->Start();
  ioImpl->Start();
  sdImpl->Start();
  rlImpl->Start();

  if(start_rest_api) {
    crow::logger::setHandler(&logger);
  crow_app = std::make_unique<crow::SimpleApp>();

  crow::SimpleApp &app = *crow_app;

  CROW_ROUTE(app, "/")
  ([]() { return "OK"; });
  CROW_ROUTE(app, "/services")
  ([sdImpl]() {
    nlohmann::json result = nlohmann::detail::value_t::object;
    const auto services = sdImpl->GetAllServices();

    for (const auto &s : *services) {
      result[s.first] = s.second;
    }
    return result.dump(2);
  });

  CROW_WEBSOCKET_ROUTE(app, "/socket")
      .onopen([&](crow::websocket::connection &conn) { CROW_LOG_INFO << "New Websocket Connection"; })
      .onclose([&](crow::websocket::connection &conn, const std::string &reason) {
        CROW_LOG_INFO << "Closed Connection. Reason: " << reason;
      })
      .onmessage([&](crow::websocket::connection & /*conn*/, const std::string &data, bool is_binary) {
        CROW_LOG_INFO << "New Websocket Message: " << data;
      });

  // Clear signals, otherwise crow will register signal handlers so we can't
  app.signal_clear();
  crow_future = app.port(18080).run_async();
  }

  if (register_handlers) {
    // Register own signal handlers
    act.sa_handler = SignalHandler;
    sigaction(SIGINT, &act, nullptr);
    sigaction(SIGTERM, &act, nullptr);
  }
  return ctx;
}

void xbot::serviceif::Stop() {
  spdlog::info("Shutting Down");
  std::unique_lock lk{mtx};
  if (started) {
    dynamic_cast<ServiceDiscoveryImpl *>(ctx.serviceDiscovery)->Stop();
    dynamic_cast<ServiceIOImpl *>(ctx.io)->Stop();
    if (ctx.ctx != nullptr) {
      static_cast<RemoteLoggingReceiverImpl *>(ctx.ctx)->Stop();
    }
    if (crow_app) {
      crow_app->stop();
    }
  }
  started = false;
}
