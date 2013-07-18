#ifndef SUBSCRIPTION_HPP
#define SUBSCRIPTION_HPP

#include "core.hpp"

#include <set>
#include <boost/scoped_ptr.hpp>
#include <boost/algorithm/string.hpp>

#include <ros/console.h>

namespace ros2 {

//=============================== Publication ===============================//

// Interface for publishing using a specific protocol (e.g., TCP, UDP, 0MQ pub)
class PublishTransport {
public:
  virtual ~PublishTransport() {};
  virtual void publish(const Message &msg) = 0;
  virtual void shutdown() = 0;
  virtual const char* protocol() const = 0;
  virtual std::string endpoint() const = 0;
};

class PublishTransportFactory {
public:
  virtual ~PublishTransportFactory() {}
  virtual PublishTransport* CreatePubTransport() = 0;
};

class Publication {
public:
  Publication(const std::string &topic)
    : topic_(topic) {
  }

  void addTransport(PublishTransport *proto) {
    protos_.push_back(boost::shared_ptr<PublishTransport>(proto));
  }

  void publish(const Message &msg) {
    for (int i = 0; i < protos_.size(); ++i) {
      protos_.at(i)->publish(msg);
    }
  }

  void shutdown() {
    for (std::vector<boost::shared_ptr<PublishTransport> >::iterator it = protos_.begin(); it != protos_.end(); ++it) {
      (*it)->shutdown();
    }
  }

  const std::string& topic() const { return topic_; }
  typedef std::vector<boost::shared_ptr<PublishTransport> >::const_iterator const_iterator;
  const_iterator begin() const { return protos_.begin(); }
  const_iterator end() const { return protos_.end(); }

protected:
  std::vector<boost::shared_ptr<PublishTransport> > protos_;
  std::string topic_;
};

typedef boost::shared_ptr<Publication> PublicationPtr;

//============================== Subscription ===============================//

typedef boost::function<void(const Message&)> MessageCallback;

// Interface for subscribing using a specific protocol (e.g., TCP, UDP, 0MQ
// pub)
class SubscribeTransport {
public:
  virtual ~SubscribeTransport() {};
  virtual void onReceive(const MessageCallback &cb) = 0;
  virtual void start(const std::string &endpoint) = 0;
  virtual void shutdown() = 0;
  virtual const char* protocol() const = 0;
};

class SubscribeTransportFactory {
public:
  virtual ~SubscribeTransportFactory() {}
  virtual SubscribeTransport* CreateSubTransport() = 0;
};

class Subscription {
public:
  Subscription(const std::string &topic, const MessageCallback &cb)
    : topic_(topic), cb_(cb) {
  }

  void addTransport(SubscribeTransport *transport) {
    ROS_INFO("Adding transport %s for topic %s", transport->protocol(), topic_.c_str());
    protos_.push_back(boost::shared_ptr<SubscribeTransport>(transport));
  }

  void foundPublisher(const std::vector<std::string> &uris) {
    std::map<std::string, std::string> remote_proto_endpoint;

    for (int i = 0; i < uris.size(); ++i) {
      // split each into protocol://endpoint
      const std::string &uri = uris.at(i);
      std::string protocol = uri.substr(0, uri.find(':'));
      std::string endpoint = uri.substr(uri.find("://") + 3);
      remote_proto_endpoint[protocol] = endpoint;
      ROS_INFO("Remote Protocol = %s Endpoint = %s",
               protocol.c_str(), endpoint.c_str());
    }

    for (int i = 0; i < protos_.size(); ++i) {
      boost::shared_ptr<SubscribeTransport> &proto = protos_.at(i);
      std::map<std::string, std::string>::iterator it;
      it = remote_proto_endpoint.find(std::string(proto->protocol()));
      if (it != remote_proto_endpoint.end()) {
        ROS_INFO("Using protocol %s w/ endpoint %s",
                 it->first.c_str(), it->second.c_str());
        proto->onReceive(cb_);
        proto->start(it->second);
        return;
      }
    }
    ROS_WARN("No match found for discovery");
  }

  void shutdown() {
    for (std::vector<boost::shared_ptr<SubscribeTransport> >::iterator it = protos_.begin(); it != protos_.end(); ++it) {
      (*it)->shutdown();
    }
  }

  MessageCallback callback() {
    return cb_;
  }
protected:
  boost::mutex callback_mutex_;
  std::string topic_;
  std::vector<boost::shared_ptr<SubscribeTransport> > protos_;
  MessageCallback cb_;
};

typedef boost::shared_ptr<Subscription> SubscriptionPtr;

}
#endif
