#ifndef SCHEDULE_HPP
#define SCHEDULE_HPP

#include "portable/function.hpp"

namespace xbot::service {
class Scheduler;

class ScheduleBase {
 public:
  typedef XBOT_FUNCTION_TYPEDEF<void()> Callback;

  explicit ScheduleBase(Scheduler& scheduler, Callback callback, uint32_t interval, bool enabled);

  void SetInterval(uint32_t interval, bool resetLastTick = true);
  void SetEnabled(bool enabled, bool resetLastTick = true);

 private:
  Scheduler& scheduler_;
  ScheduleBase* next_ = nullptr;

  uint32_t last_tick_ = 0;
  Callback callback_;
  uint32_t interval_;
  bool enabled_;

  friend class Scheduler;
};

class Schedule : public ScheduleBase {
 public:
  explicit Schedule(Scheduler& scheduler, Callback callback, uint32_t interval = 0, bool enabled = true)
      : ScheduleBase(scheduler, callback, interval, enabled) {};
};

}  // namespace xbot::service

#endif  // SCHEDULE_HPP
