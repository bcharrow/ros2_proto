#ifndef ROS2_CORE_HPP_
#define ROS2_CORE_HPP_

#include <vector>

#include <boost/scoped_array.hpp>

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
  
private:
  DISALLOW_COPY_AND_ASSIGN(Message);
  uint32_t sz_;
  boost::scoped_array<uint8_t> bytes_;
};

//================================= Publish =================================//

// Abstract interface for publishing using a specific protocol (e.g., TCP, UDP,
// 0MQ pub)
class PublishProtocol {
public:
  virtual void publish(const Message &msg) = 0;
};

class TCPPublish : public PublishProtocol {
public:
  TCPPublish(TransportTCP *server) : server_(server) {}
  virtual void publish(const Message &msg) {
    ROS_ERROR("publish()");
  }
private:
  TransportTCP *server_;
};

// Interface between user facing publisher and various comm protocols
class Publication {
public:
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

class TCPSubscribe : public SubscribeProtocol {
public:
  virtual void onReceive(const MessageCallback &cb);
};

// Manage all subscription protocols
class Subscription {
public:
  Subscription();
  ~Subscription();

  uint64_t addCallback();
  void removeCallback(uint64_t id);
};

// A object for RAII callbacks
class Subscriber {
public:
  Subscriber(const Subscription &s);
  ~Subscriber();
private:
  
};

}
#endif /* ROS2_CORE_HPP_ */
