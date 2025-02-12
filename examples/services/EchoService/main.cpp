
#include <ulog.h>
#include <unistd.h>

#include <cstdio>
#include <mutex>
#include <xbot-service/Io.hpp>

#include "EchoService.hpp"
#include "xbot-service/RemoteLogging.hpp"
#include "xbot-service/portable/system.hpp"

#ifdef ULOG_ENABLED
void console_logger(ulog_level_t severity, char* msg, const void* arg) {
  printf("[%s]: %s\n", ulog_level_name(severity), msg);
}
#endif

int main() {
  xbot::service::system::initSystem();
  xbot::service::startRemoteLogging();
#ifdef ULOG_ENABLED
  ULOG_SUBSCRIBE(console_logger, ULOG_DEBUG_LEVEL);
#endif

  EchoService service{1};

  service.start();

  xbot::service::Io::start();

  while (1) {
    sleep(1);
  }
}
