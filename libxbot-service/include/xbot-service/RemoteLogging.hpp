//
// Created by clemens on 3/25/24.
//

#ifndef REMOTELOGGING_HPP
#define REMOTELOGGING_HPP

#include <ulog.h>

namespace xbot::service {
bool startRemoteLogging(ulog_level_t level = ULOG_INFO_LEVEL);
}

#endif  // REMOTELOGGING_HPP
