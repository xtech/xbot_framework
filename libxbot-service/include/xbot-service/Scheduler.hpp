#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include "Schedule.hpp"
#include "portable/mutex.hpp"

namespace xbot::service {
class Scheduler {
 public:
  explicit Scheduler();
  ~Scheduler();

  // Advances the clock and calls the callbacks of the schedules that are due.
  // Returns the time until the next schedule is due.
  uint32_t Tick(uint32_t count);

  // Special return value for Tick() to indicate that no schedule is enabled.
  enum { NO_ENABLED_SCHEDULE = UINT32_MAX };

 private:
  XBOT_MUTEX_TYPEDEF state_mutex_{};
  uint32_t now_ = 0;
  ScheduleBase* schedules_head_ = nullptr;

  void AddSchedule(ScheduleBase& schedule);

  friend class ScheduleBase;
  friend class Schedule;
};

}  // namespace xbot::service

#endif  // SCHEDULER_HPP
