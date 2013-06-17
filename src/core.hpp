#ifndef ROS2_CORE_HPP_
#define ROS2_CORE_HPP_

#include <vector>
#include <deque>

#include <boost/scoped_array.hpp>
#include <boost/bind.hpp>
#include <boost/thread/condition_variable.hpp>

#include "transport_tcp.h"

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

//================================= Publish =================================//

// Abstract interface for publishing using a specific protocol (e.g., TCP, UDP,
// 0MQ pub)
class PublishProtocol {
public:
  virtual void publish(const Message &msg) = 0;
};

// Interface between user facing publisher and various comm protocols
class Publication {
public:
  Publication() {}
  void publish(const Message &msg) {
    for (int i = 0; i < protos_.size(); ++i) {
      protos_.at(i)->publish(msg);
    }
  }
  void registerProtocol(PublishProtocol *proto) {
    protos_.push_back(proto);
  }

protected:
  std::vector<PublishProtocol*> protos_;
};

//================================ Subscribe ================================//

typedef boost::function<void(const Message&)> MessageCallback;

// Abstract interface for subscribing using a specific protocol (e.g., TCP,
// UDP, 0MQ pub)
class SubscribeProtocol {
public:
  virtual void onReceive(const MessageCallback &cb) = 0;
};

// Manage all subscription protocols
class Subscription {
public:
  Subscription() {}
  ~Subscription() {}

  void addSubscription(SubscribeProtocol *cb) {
    subs_.push_back(cb);
    cb->onReceive(boost::bind(&Subscription::msgCallback, this, _1));
  }

  void msgCallback(const Message &msg) {
    boost::mutex::scoped_lock lock(callback_mutex_);
    for (int i = 0; i < cbs_.size(); ++i) {
      cbs_.at(i)(msg);
    }
  }

  void registerCallback(const MessageCallback &cb) {
    boost::mutex::scoped_lock lock(callback_mutex_);
    cbs_.push_back(cb);
  }

protected:
  boost::mutex callback_mutex_;
  std::vector<SubscribeProtocol*> subs_;
  std::vector<MessageCallback> cbs_;
};

}
#endif /* ROS2_CORE_HPP_ */
