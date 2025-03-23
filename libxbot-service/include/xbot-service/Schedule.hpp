#ifndef SCHEDULE_HPP
#define SCHEDULE_HPP

#include "portable/function.hpp"

namespace xbot::service {
class Scheduler;

class Schedule {
 public:
  typedef XBOT_FUNCTION_TYPEDEF<void()> Callback;

  explicit Schedule(Scheduler& scheduler, uint32_t interval_micros, Callback callback)
      : scheduler_(scheduler), interval_(interval_micros), callback_(callback) {
  }

  void SetInterval(uint32_t interval_micros);
  void SetEnabled(bool enabled);

 private:
  Scheduler& scheduler_;
  Schedule* next_ = nullptr;

  uint32_t next_execution_ = 0;
  uint32_t interval_;
  Callback callback_;

  friend class Scheduler;
};

}  // namespace xbot::service

#endif  // SCHEDULE_HPP
