#ifndef CORE_HPP
#define CORE_HPP

#include <vector>
#include <deque>

#include <boost/scoped_array.hpp>
#include <boost/bind.hpp>
#include <boost/thread/condition_variable.hpp>

#define DISALLOW_COPY_AND_ASSIGN(Type) \
  Type(const Type &other);             \
  void operator=(const Type &other);

namespace ros2 {

uint64_t usectime() {
  timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  uint32_t sec = now.tv_sec;
  uint32_t nsec = now.tv_nsec;
  uint64_t time = sec * 1000000ULL + nsec / 1000U;
  return time;
}

class Message {
public:
  Message(uint32_t sz) : sz_(sz) {
    bytes_.reset(new uint8_t[sz]);
  }

  uint32_t size() const { return sz_; }
  const uint8_t *bytes() const { return bytes_.get(); }
  uint8_t *bytes() { return bytes_.get(); }

  Message* Copy() const {
    Message *msg = new Message(sz_);
    std::copy(bytes_.get(), bytes_.get() + sz_, msg->bytes_.get());
    return msg;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(Message);
  uint32_t sz_;
  boost::scoped_array<uint8_t> bytes_;
};

template<typename T>
class MessageQueue {
public:
  MessageQueue(size_t max_size) : max_size_(max_size) {}

  void push(T* msg) {
    boost::unique_lock<boost::mutex> lock(mutex_);
    queue_.push_back(msg);
    if (max_size_ != 0 && queue_.size() > max_size_) {
      delete queue_.front();
      queue_.pop_front();
    }
    cond_.notify_one();
  }

  T* pop(double msec_wait) {
    boost::unique_lock<boost::mutex> lock(mutex_);
    boost::system_time stop = boost::get_system_time() + boost::posix_time::milliseconds(msec_wait);
    while (queue_.empty()) {
      if (!cond_.timed_wait(lock, stop)) {
        return NULL;
      }
    }

    T* front = queue_.front();
    queue_.pop_front();
    return front;
  }

private:
  boost::mutex mutex_;
  boost::condition_variable cond_;
  std::deque<T*> queue_;
  size_t max_size_;
};

} // ros2

#endif /* CORE_HPP */
