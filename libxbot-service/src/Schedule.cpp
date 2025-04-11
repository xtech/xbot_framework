#include <xbot-service/Lock.hpp>
#include <xbot-service/Schedule.hpp>
#include <xbot-service/Scheduler.hpp>

namespace xbot::service {

ScheduleBase::ScheduleBase(Scheduler& scheduler, Callback callback, uint32_t interval, bool enabled)
    : scheduler_(scheduler), callback_(callback), interval_(interval), enabled_(enabled) {
  scheduler_.AddSchedule(*this);
}

void ScheduleBase::SetInterval(uint32_t interval, bool resetLastTick) {
  Lock lk(&scheduler_.state_mutex_);
  if (interval_ == interval) return;
  interval_ = interval;
  if (resetLastTick) {
    last_tick_ = scheduler_.now_;
  }
}

void ScheduleBase::SetEnabled(bool enabled, bool resetLastTick) {
  Lock lk(&scheduler_.state_mutex_);
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  if (resetLastTick) {
    last_tick_ = scheduler_.now_;
  }
}

}  // namespace xbot::service
