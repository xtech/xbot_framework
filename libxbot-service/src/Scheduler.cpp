#include <xbot-service/Lock.hpp>
#include <xbot-service/Scheduler.hpp>
#include <xbot-service/portable/system.hpp>

xbot::service::Scheduler::~Scheduler() {
  thread::deinitialize(thread_);
  mutex::deinitialize(&state_mutex_);
}

bool xbot::service::Scheduler::Start() {
  stopped_ = false;

  if (!mutex::initialize(&state_mutex_)) {
    return false;
  }

#ifdef XBOT_ENABLE_STATIC_STACK
  if (!thread::initialize(thread_, ThreadLoopHelper, this, thread_stack_, sizeof(thread_stack_), "Scheduler"))
#else
  if (!thread::initialize(thread_, ThreadLoopHelper, this, nullptr, 0, "Scheduler"))
#endif  // XBOT_ENABLE_STATIC_STACK
  {
    return false;
  }

  return true;
}

void xbot::service::Scheduler::ThreadLoop() {
  while (true) {
    {
      Lock lk(&state_mutex_);
      if (stopped_) return;
    }

    uint32_t now_micros = system::getTimeMicros();

    // TODO: Handle overflow

    if (schedules_head_->next_execution_ > now_micros) {
      uint32_t sleep_time = schedules_head_->next_execution_ - now_micros;
      // TODO: Sleep in an interruptible way (new schedule, stopped_ set).
      //       chThdSleepUntil() might be a candidate, but it's not interruptible.
      now_micros = system::getTimeMicros();
      (void)sleep_time;
    }

    // TODO: Locking.
    for (Schedule* schedule = schedules_head_; schedule != nullptr; schedule = schedule->next_) {
      if (now_micros >= schedule->next_execution_) {
        schedule->callback_();
        schedule->next_execution_ = now_micros + schedule->interval_;
        // Move schedule to the correct position in the list, so that the list is sorted by next_execution
        // auto it = schedules_.begin();
        // while (it != schedules_.end() && it->next_execution < schedule.next_execution) {
        //   ++it;
        // }
        // schedules_.splice_after(it, schedules_, schedules_.before_begin());
      }
    }
  }
}

xbot::service::Schedule* xbot::service::Scheduler::AddSchedule(uint32_t interval_micros, Schedule::Callback callback) {
  // FIXME: Allocate from pool, core memory or whatever
  Schedule* schedule = new Schedule{*this, interval_micros, callback};
  Lock lk(&state_mutex_);
  if (schedules_head_ == nullptr) {
    schedules_head_ = schedule;
  } else {
    // FIXME: Insert in sorted order.
    schedule->next_ = schedules_head_;
    schedules_head_ = schedule;
  }
  schedules_head_ = schedule;
  return schedule;
}
