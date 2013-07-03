#ifndef SUBSCRIPTION_HPP
#define SUBSCRIPTION_HPP

#include "core.hpp"

namespace ros2 {

typedef boost::function<void(const Message&)> MessageCallback;

// Abstract interface for subscribing using a specific protocol (e.g., TCP,
// UDP, 0MQ pub)
class SubscribeProtocol {
public:
  virtual void onReceive(const MessageCallback &cb) = 0;
  virtual void start(const std::string &endpoint) = 0;
  virtual void shutdown() = 0;
  virtual const char* protocol() const = 0;
};

// Manage all subscription protocols
class Subscription {
public:
  Subscription() {}
  ~Subscription() {}

  void addProtocol(SubscribeProtocol *cb) {
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

class DiscoveryProtocol {
public:
  // Synchronous announcement
  virtual void announceSubscription(ros2_comm::TopicRequest *req_rep) = 0;
};

class ServiceDiscovery : public DiscoveryProtocol {
public:
  ServiceDiscovery(PollSet *ps) : ps_(ps) {

  }

  virtual void announceSubscription(ros2_comm::TopicRequest *req_rep) {
    boost::shared_ptr<TransportTCP> remote_service(new TransportTCP(ps_));
    remote_service->connect("127.0.0.1", 5555);
    srv_client_.call(remote_service, req_rep);
    ROS_INFO_STREAM("Request / response:\n" <<
                    req_rep->request << std::endl << req_rep->response);
    remote_service->close();
  }

private:
  PollSet *ps_;
  ServiceClient<ros2_comm::TopicRequest> srv_client_;
};

class ComposeSubscription {
public:
  ComposeSubscription(const std::string &topic, DiscoveryProtocol *dp,
                      SubscribeProtocol *sub, const MessageCallback &cb)
    : topic_(topic), discovery_(dp), sub_(sub), cb_(cb) {

  }

  void start() {
    ros2_comm::TopicRequest tr;
    tr.request.topic = topic_;
    discovery_->announceSubscription(&tr);
    newDiscovery(tr);
  }

  void newDiscovery(const ros2_comm::TopicRequest &req_rep) {
    // Check if any endpoints match current
    for (int i = 0; i < req_rep.response.protocols.size(); ++i) {
      if (req_rep.response.protocols[i] == sub_->protocol()) {
        ROS_INFO_STREAM("Got a match: " << sub_->protocol());
        sub_->onReceive(cb_);
        sub_->start(req_rep.response.endpoints[i]);
        return;
      }
    }
    ROS_WARN("No match found for discovery");
  }

protected:
  boost::mutex callback_mutex_;
  std::string topic_;
  DiscoveryProtocol *discovery_;
  SubscribeProtocol *sub_;
  MessageCallback cb_;
};

}

#endif
