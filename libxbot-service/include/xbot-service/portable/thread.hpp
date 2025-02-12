//
// Created by clemens on 3/20/24.
//

#ifndef THREAD_HPP
#define THREAD_HPP

#include <xbot/thread_impl.hpp>

#ifndef XBOT_THREAD_TYPEDEF
#error XBOT_THREAD_TYPEDEF undefined
#endif

namespace xbot::service::thread {
typedef XBOT_THREAD_TYPEDEF* ThreadPtr;

bool initialize(ThreadPtr thread, void (*threadfunc)(void*), void* arg, void* stackbuf, size_t buflen,
                const char* name);
void deinitialize(ThreadPtr thread);
}  // namespace xbot::service::thread

#endif  // THREAD_HPP
