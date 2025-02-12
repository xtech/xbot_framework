//
// Created by clemens on 3/22/24.
//
#include <xbot-service/Lock.hpp>

xbot::service::Lock::Lock(mutex::MutexPtr mutex) : mutex_(mutex) {
  mutex::lockMutex(mutex_);
}

xbot::service::Lock::~Lock() {
  mutex::unlockMutex(mutex_);
}
