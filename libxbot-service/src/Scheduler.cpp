#include <xbot-service/Lock.hpp>
#include <xbot-service/Scheduler.hpp>
#include <xbot-service/portable/system.hpp>

namespace xbot::service {

Scheduler::Scheduler() {
  // TODO: Theoretically, is could fail, which we should handle.
  //       But both implementations always return true.
  mutex::initialize(&state_mutex_);
}

Scheduler::~Scheduler() {
  mutex::deinitialize(&state_mutex_);
}

void Scheduler::AddSchedule(ScheduleBase& schedule) {
  Lock lk(&state_mutex_);
  if (schedules_head_ == nullptr) {
    schedules_head_ = &schedule;
  } else {
    // Insert schedule at the end.
    ScheduleBase* current = schedules_head_;
    while (current->next_ != nullptr) {
      current = current->next_;
    }
    current->next_ = &schedule;
  }
  schedule.last_tick_ = now_;
}

uint32_t Scheduler::Tick(uint32_t count) {
  mutex::lockMutex(&state_mutex_);
  now_ += count;

  uint32_t min_sleep_time = NO_ENABLED_SCHEDULE;
  for (ScheduleBase* schedule = schedules_head_; schedule != nullptr; schedule = schedule->next_) {
    if (!schedule->IsEnabled()) continue;

    uint32_t sleep_time;
    const uint32_t since_last_tick = now_ - schedule->last_tick_;
    if (since_last_tick >= schedule->interval_) {
      // Temporarily unlock the mutex so the callback can adjust the schedule.
      mutex::unlockMutex(&state_mutex_);
      schedule->callback_();
      mutex::lockMutex(&state_mutex_);
      schedule->last_tick_ = now_;
      sleep_time = schedule->interval_;
    } else {
      sleep_time = schedule->interval_ - since_last_tick;
    }

    if (sleep_time < min_sleep_time) {
      min_sleep_time = sleep_time;
    }
  }

  mutex::unlockMutex(&state_mutex_);
  return min_sleep_time;
}

}  // namespace xbot::service
