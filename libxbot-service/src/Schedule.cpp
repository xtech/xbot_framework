#include <xbot-service/Lock.hpp>
#include <xbot-service/Schedule.hpp>
#include <xbot-service/Scheduler.hpp>

void xbot::service::Schedule::SetInterval(uint32_t interval_micros) {
  Lock lk(&scheduler_.state_mutex_);
  interval_ = interval_micros;
}

void xbot::service::Schedule::SetEnabled(bool enabled) {
  Lock lk(&scheduler_.state_mutex_);
  // FIXME: Implement
  (void)enabled;
}
