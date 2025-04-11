#ifndef SCHEDULE_HPP
#define SCHEDULE_HPP

#include "portable/function.hpp"

namespace xbot::service {
class Scheduler;

class ScheduleBase {
 public:
  typedef XBOT_FUNCTION_TYPEDEF<void()> Callback;

  explicit ScheduleBase(Scheduler& scheduler, uint32_t interval, Callback callback);

  void SetInterval(uint32_t interval, bool resetLastTick = true);

  virtual bool IsEnabled() = 0;

 protected:
  Scheduler& scheduler_;
  ScheduleBase* next_ = nullptr;

  uint32_t last_tick_ = 0;
  uint32_t interval_;
  Callback callback_;

  friend class Scheduler;
};

class Schedule : public ScheduleBase {
 public:
  explicit Schedule(Scheduler& scheduler, bool enabled, uint32_t interval, Callback callback)
      : ScheduleBase(scheduler, interval, callback), enabled_(enabled) {
  }

  void SetEnabled(bool enabled, bool resetLastTick = true);

  bool IsEnabled() override {
    return enabled_ && interval_ != 0;
  }

 private:
  bool enabled_;
};

class ManagedSchedule : public ScheduleBase {
 public:
  explicit ManagedSchedule(Scheduler& scheduler, const bool& enabled, uint32_t interval, Callback callback)
      : ScheduleBase(scheduler, interval, callback), enabled_(enabled) {
  }

  bool IsEnabled() override {
    return enabled_ && interval_ != 0;
  }

 private:
  const bool& enabled_;
};

}  // namespace xbot::service

#endif  // SCHEDULE_HPP
