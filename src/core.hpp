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
  MessageQueue() {}

  void push(T* msg) {
    boost::unique_lock<boost::mutex> lock(mutex_);
    queue_.push_back(msg);
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
    ROS_INFO("Subscription::msgCallback Got msg with %i bytes", msg.size());
  }

protected:
  boost::mutex callback_mutex_;
  std::vector<SubscribeProtocol*> subs_;
};

}
#endif /* ROS2_CORE_HPP_ */
