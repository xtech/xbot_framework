//
// Created by clemens on 3/21/24.
//
#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <queue>
#include <xbot-service/portable/queue.hpp>

using namespace xbot::service::queue;

bool BlockingQueue::init(size_t size, void* buffer, size_t buffer_size) {
  (void)buffer_size;
  // We need at least one space in the queue
  assert(size >= 1);
  buffer_ = static_cast<void**>(buffer);
  assert(buffer_size >= size * sizeof(void*));
  assert(buffer_ != nullptr);
  // Queue is size long, we don't have any items in there and start
  // by putting the first item into index 0.
  queue_size_ = size;
  item_count_ = 0;
  front_idx_ = 0;
  return true;
}

BlockingQueue::~BlockingQueue() {
  free(buffer_);
}

bool BlockingQueue::push(void* ptr, uint32_t timeout_micros) {
  std::unique_lock lock(mutex_);
  // If empty, wait for data until timeout
  if (isFull()) {
    if (timeout_micros > 0) {
      // Wait for pop event
      cv_pop_.wait_for(lock, std::chrono::microseconds(timeout_micros));
    } else {
      // No timeout, just return nullptr
      return false;
    }
  }

  if (isFull()) {
    // still full, signal error
    return false;
  }

  // Calcuate the back index
  size_t back_idx = (front_idx_ + item_count_) % queue_size_;
  buffer_[back_idx] = ptr;
  item_count_++;
  cv_push_.notify_one();
  return true;
}

void* BlockingQueue::pop(uint32_t timeout_micros) {
  std::unique_lock lock(mutex_);
  // If empty, wait for data until timeout
  if (isEmpty()) {
    if (timeout_micros > 0) {
      cv_push_.wait_for(lock, std::chrono::microseconds(timeout_micros));
    } else {
      // Not timeout, just return nullptr
      return nullptr;
    }
  }

  if (isEmpty()) {
    // Still empty even after waiting -> timeout
    return nullptr;
  }

  // Get the front, we know that there is valid data
  void* item = buffer_[front_idx_];
  // Move index to next
  front_idx_ = (front_idx_ + 1) % queue_size_;
  // Decrease item_count_, so that the back points to the same item still.
  item_count_--;
  cv_pop_.notify_one();
  return item;
}

// Helpers
bool BlockingQueue::isEmpty() const {
  return item_count_ == 0;
}

bool BlockingQueue::isFull() const {
  return item_count_ == queue_size_;
}

bool xbot::service::queue::initialize(QueuePtr queue, size_t queue_length, void* buffer, size_t buffer_size) {
  // create the queue on the heap, we don't need the buffer.
  return queue->init(queue_length, buffer, buffer_size);
}

bool xbot::service::queue::queuePopItem(QueuePtr queue, void** result, uint32_t timeout_micros) {
  void* item = queue->pop(timeout_micros);
  if (item) {
    *result = item;
    return true;
  }
  return false;
}

bool xbot::service::queue::queuePushItem(QueuePtr queue, void* item) {
  if (queue == nullptr) return false;
  return queue->push(item, 0);
}

void xbot::service::queue::deinitialize(QueuePtr queue) {
  (void)queue;
}
