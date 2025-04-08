#include <xbot-service/Lock.hpp>
#include <xbot-service/Schedule.hpp>
#include <xbot-service/Scheduler.hpp>

namespace xbot::service {

Schedule::Schedule(Scheduler& scheduler, Callback callback, uint32_t interval, bool enabled)
    : scheduler_(scheduler), callback_(callback), interval_(interval), enabled_(enabled) {
  scheduler_.AddSchedule(*this);
}

Schedule& Schedule::SetInterval(uint32_t interval, bool resetLastTick) {
  Lock lk(&scheduler_.state_mutex_);
  if (interval_ == interval) return *this;
  interval_ = interval;
  if (resetLastTick) {
    last_tick_ = scheduler_.now_;
  }
  return *this;
}

Schedule& Schedule::SetEnabled(bool enabled, bool resetLastTick) {
  Lock lk(&scheduler_.state_mutex_);
  if (enabled_ == enabled) return *this;
  enabled_ = enabled;
  if (resetLastTick) {
    last_tick_ = scheduler_.now_;
  }
  return *this;
}

}  // namespace xbot::service
