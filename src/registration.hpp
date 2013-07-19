#ifndef REGISTRATION_HPP
#define REGISTRATION_HPP

#include <boost/function.hpp>

#include <ros/console.h>

#include <ros2_proto/TopicRequest.h>
#include <ros2_proto/RegisterSubscription.h>
#include <ros2_proto/UnregisterSubscription.h>
#include <ros2_proto/RegisterPublication.h>
#include <ros2_proto/UnregisterPublication.h>

#include "service.hpp"

namespace ros2 {

class RegistrationProtocol {
public:
  typedef boost::function<void(const std::vector<std::string>&)>
  SubscriptionCallback;

  virtual ~RegistrationProtocol() {}
  virtual void registerSubscription(const std::string &topic,
                                    const SubscriptionCallback &cb) = 0;
  virtual bool unregisterSubscription(const std::string &topic) = 0;
  virtual void registerPublication(const std::string &topic) = 0;
  virtual bool unregisterPublication(const std::string &topic) = 0;
};

class StaticRegistration : public RegistrationProtocol {
public:
  StaticRegistration() {

  }

  void addPub(const std::string &topic, const std::string &uri) {
    ROS_INFO("Adding pub %s %s", topic.c_str(), uri.c_str());
    pub_uris_[topic] = uri;
  }

  void addSub(const std::string &topic, const std::string &uri) {
    sub_uris_[topic] = uri;
  }

  virtual void registerSubscription(const std::string &topic,
                                    const SubscriptionCallback &callback) {
    if (pub_uris_.count(topic) != 0) {
      std::vector<std::string> uris;
      uris.push_back(pub_uris_[topic]);
      callback(uris);
    } else {
      ROS_WARN("No publishers found for topic '%s'", topic.c_str());
    }
  }

  virtual bool unregisterSubscription(const std::string &topic) {
    ROS_INFO("Unregistering subscripton on %s", topic.c_str());
    return true;
  }

  virtual void registerPublication(const std::string &topic) {
    ROS_INFO("Registering publication on %s", topic.c_str());
  }

  virtual bool unregisterPublication(const std::string &topic) {
    ROS_INFO("Unregistering publication on %s", topic.c_str());
  }

private:
  std::map<std::string, std::string> sub_uris_;
  std::map<std::string, std::string> pub_uris_;
};

class NodeAddress {
public:
  NodeAddress(const std::string &addr, int port)
    : addr_(addr), port_(port) {}

  const std::string& addr() const { return addr_; }
  int port() const { return port_; }

private:
  std::string addr_;
  int port_;
};

// Find publishers / subscribers using service calls to master and nodes
class MasterRegistration : public RegistrationProtocol {
public:
  MasterRegistration(const NodeAddress &master, PollSet *ps)
    : master_(master), ps_(ps), my_uri_("") {
  }

  virtual void registerSubscription(const std::string &topic,
                                    const SubscriptionCallback &callback) {
    // Contact master to get publishers
    ros2_proto::RegisterSubscription reg_sub;
    reg_sub.request.topic = topic;
    reg_sub.request.node_uri = myURI();
    callMaster("registerSubscription", reg_sub,
               boost::bind(&MasterRegistration::registerSubscriptionCb, this,
                           _1, callback));
  }

  void registerSubscriptionCb(const ros2_proto::RegisterSubscription &reg_sub,
                              const SubscriptionCallback &callback) {
    // Contact publishers to get endpoints
    contactPublishers(reg_sub.request.topic,
                      reg_sub.response.publisher_uris,
                      callback);
  }

  virtual bool unregisterSubscription(const std::string &topic) {
    ROS_INFO("Unregistering subscripton on %s", topic.c_str());
    ros2_proto::UnregisterSubscription unreg_sub;
    unreg_sub.request.topic = topic;
    unreg_sub.request.node_uri = myURI();
    callMaster("unregisterSubscription", unreg_sub);
    return true;
  }

  virtual void registerPublication(const std::string &topic) {
    ROS_INFO("Registering publication on %s", topic.c_str());
    // Inform master we're publishing
    ros2_proto::RegisterPublication reg_pub;
    reg_pub.request.topic = topic;
    reg_pub.request.node_uri = myURI();
    callMaster("registerPublication", reg_pub);
  }

  virtual bool unregisterPublication(const std::string &topic) {
    ROS_INFO("Unregistering publication on %s", topic.c_str());
    ros2_proto::UnregisterPublication unreg_pub;
    unreg_pub.request.topic = topic;
    unreg_pub.request.node_uri = my_uri_;
    callMaster("unregisterPublication", unreg_pub);
    return true;
  }

  void myURI(const std::string &uri) {
    my_uri_ = uri;
  }

  std::string myURI() {
    if (my_uri_.empty()) {
      ROS_BREAK();
    }
    return my_uri_;
  }

private:
  template <typename M>
  void callMaster(const std::string &method, const M &req_rep,
                  const typename ServiceResponseT<M>::Callback &cb) {
    TransportTCPPtr tcp(new TransportTCP(ps_));
    tcp->connect(master_.addr(), master_.port());
    msc_.call(tcp, method, req_rep, cb);
  }

  template <typename M>
  void emptyCallback(const M &req_rep) {

  }

  template <typename M>
  void callMaster(const std::string &method, const M &req_rep) {
    TransportTCPPtr tcp(new TransportTCP(ps_));
    tcp->connect(master_.addr(), master_.port());
    msc_.call(tcp, method, req_rep,
              boost::bind(&MasterRegistration::emptyCallback<M>, this, _1));
  }


  void handleCallback(const ros2_proto::TopicRequest &req_resp,
                      const SubscriptionCallback &callback) {
    callback(req_resp.response.uris);
  }

  void contactPublishers(const std::string &topic,
                         const std::vector<std::string> &node_uris,
                         const SubscriptionCallback &callback) {
    for (int i = 0; i < node_uris.size(); ++i) {
      const std::string &endpoint = node_uris.at(i);
      std::string addr = endpoint.substr(0, endpoint.find(":"));
      int port = atoi(endpoint.substr(endpoint.find(":") + 1).c_str());

      TransportTCPPtr tcp(new TransportTCP(ps_));
      tcp->connect(addr, port);

      ros2_proto::TopicRequest req_rep;
      req_rep.request.topic = topic;
      msc_.call(tcp, "requestTopic", req_rep,
                boost::bind(&MasterRegistration::handleCallback, this, _1, callback));
    }
  }


  MultiServiceClient msc_;
  NodeAddress master_;
  PollSet *ps_;
  std::string my_uri_;
};

}

#endif // REGISTRATION_HPP
