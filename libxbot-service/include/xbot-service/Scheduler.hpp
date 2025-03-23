#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP

#include "Schedule.hpp"
#include "portable/mutex.hpp"
#include "portable/thread.hpp"

namespace xbot::service {
class Scheduler {
 public:
  explicit Scheduler() {};
  ~Scheduler();

  bool Start();
  void ThreadLoop();

  Schedule *AddSchedule(uint32_t interval_micros, Schedule::Callback callback);

  /**
   * Since the portable thread implementation does not know what a class is, we
   * use this helper to start the scheduler.
   * @param scheduler Pointer to the scheduler to start
   * @return null
   */
  static void ThreadLoopHelper(void *scheduler) {
    static_cast<Scheduler *>(scheduler)->ThreadLoop();
  }

 private:
#ifdef XBOT_ENABLE_STATIC_STACK
  // TODO: Make stack size configurable?
  THD_WORKING_AREA(thread_stack_, 1500);
#endif
  xbot::service::thread::ThreadPtr thread_ = nullptr;

  XBOT_MUTEX_TYPEDEF state_mutex_{};
  bool stopped_ = true;

  Schedule *schedules_head_ = nullptr;

  friend class Schedule;
};

}  // namespace xbot::service

#endif  // SCHEDULER_HPP
