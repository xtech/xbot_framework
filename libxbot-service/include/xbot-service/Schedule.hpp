#ifndef SCHEDULE_HPP
#define SCHEDULE_HPP

#include "portable/function.hpp"

namespace xbot::service {
class Scheduler;

class Schedule {
 public:
  typedef XBOT_FUNCTION_TYPEDEF<void()> Callback;

  explicit Schedule(Scheduler& scheduler, Callback callback, uint32_t interval = 0, bool enabled = true);

  void SetInterval(uint32_t interval, bool resetLastTick = true);
  void SetEnabled(bool enabled, bool resetLastTick = true);

 private:
  Scheduler& scheduler_;
  Schedule* next_ = nullptr;

  uint32_t last_tick_ = 0;
  Callback callback_;
  uint32_t interval_;
  bool enabled_;

  friend class Scheduler;
};

}  // namespace xbot::service

#endif  // SCHEDULE_HPP
