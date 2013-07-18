#ifndef SUBSCRIPTION_HPP
#define SUBSCRIPTION_HPP

#include "core.hpp"

#include <set>

#include <boost/scoped_ptr.hpp>
#include <boost/algorithm/string.hpp>

#include <ros2_comm/SetRemappings.h>
#include <ros2_comm/RegisterSubscription.h>
#include <ros2_comm/UnregisterSubscription.h>
#include <ros2_comm/RegisterPublication.h>
#include <ros2_comm/UnregisterPublication.h>

namespace ros2 {

typedef std::map<std::string, std::string> M_string;

//============================== Registration ===============================//
class TopicManager;

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

//============================== TopicManager ===============================//

class TopicManager {
public:
  TopicManager(RegistrationProtocol *regp, PollSet *ps)
    : regp_(regp), ps_(ps) { }

  ~TopicManager() {
    shutdown();
  }

  void shutdown() {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    M_string pmap = pub_map_;
    for (M_string::iterator it = pmap.begin(); it != pmap.end(); ++it) {
      unregisterPublication(it->first);
    }

    M_string smap = sub_map_;
    for (M_string::iterator it = smap.begin(); it != smap.end(); ++it) {
      unregisterSubscription(it->first);
    }
  }

  void addPublishTransport(PublishTransportFactory *factory) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    pub_factories_.push_back(factory);
  }

  void addSubscribeTransport(SubscribeTransportFactory *factory) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    sub_factories_.push_back(factory);
  }

  void unregisterSubscription(const std::string &unmapped) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    M_string::iterator it = sub_map_.find(unmapped);
    assert(it != sub_map_.end());
    std::string &topic = it->second;
    assert(subscriptions_.count(topic) != 0);
    subscriptions_[topic]->shutdown();
    subscriptions_.erase(topic);
    regp_->unregisterSubscription(topic);
    sub_map_.erase(it);
  }

  void registerPublication(const std::string &unmapped) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    std::string &topic = pub_map_[unmapped];
    topic = remap(unmapped);
    assert(publications_.count(topic) == 0);
    publications_[topic] = createPublication(topic);
    regp_->registerPublication(topic);
  }

  void unregisterPublication(const std::string &unmapped) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    M_string::iterator it = pub_map_.find(unmapped);
    assert(it != pub_map_.end());
    std::string &topic = it->second;
    assert(publications_.count(topic) != 0);
    publications_[topic]->shutdown();
    publications_.erase(topic);
    regp_->unregisterPublication(topic);
    pub_map_.erase(it);
  }

  void subscribe(const std::string &unmapped, const MessageCallback &cb) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    std::string topic = remap(unmapped);
    assert(sub_map_.count(topic) == 0);
    sub_map_[unmapped] = topic;
    assert(subscriptions_.count(topic) == 0);
    boost::shared_ptr<Subscription> sub = createSubscription(topic, cb);
    subscriptions_[topic] = sub;
    regp_->registerSubscription(topic, boost::bind(&Subscription::foundPublisher,
                                                   sub.get(), _1));
  }

  void publish(const std::string &unmapped, const Message &msg) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    if (pub_map_.count(unmapped) == 0) {
      registerPublication(unmapped);
    } else {
      assert(pub_map_[unmapped] == remap(unmapped, false));
    }
    std::string topic = pub_map_[unmapped];
    publications_[topic]->publish(msg);
  }

  std::vector<std::string> publicationURIs(const std::string &topic) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    std::vector<std::string> uris;
    std::map<std::string, PublicationPtr>::const_iterator it = publications_.find(topic);
    if (it != publications_.end()) {
      PublicationPtr pub = it->second;
      for (Publication::const_iterator it = pub->begin(); it != pub->end(); ++it) {
        std::string uri = std::string((*it)->protocol()) + "://" +
          std::string((*it)->endpoint());
        uris.push_back(uri);
      }
    }
    return uris;
  }

  //================================= RPCs ==================================//
  void init(int port) {
    services_.reset(new MultiServiceManager(TransportTCPPtr(new TransportTCP(ps_))));
    services_->init(port);
    ServiceCallbackPtr scht(new ServiceCallbackT<ros2_comm::TopicRequest>(boost::bind(&TopicManager::requestTopic, this, _1, _2)));
    services_->bind("requestTopic", scht);

    ServiceCallbackPtr remaps(new ServiceCallbackT<ros2_comm::SetRemappings>(boost::bind(&TopicManager::setRemappings, this, _1, _2)));
    services_->bind("setRemappings", remaps);

    ROS_INFO("TopicManager @ %s", myURI().c_str());
  }

  void requestTopic(const ros2_comm::TopicRequest::Request &request,
                    ros2_comm::TopicRequest::Response *response) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    ROS_INFO_STREAM("Got a request:\n" << request);
    response->uris = publicationURIs(request.topic);
    ROS_INFO_STREAM("Responding with:\n" << *response);
  }

  void setRemappings(const ros2_comm::SetRemappings::Request &request,
                     ros2_comm::SetRemappings::Response *response) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    const std::vector<std::string> &find = request.find;
    const std::vector<std::string> &replace = request.replace;
    assert(find.size() == replace.size());

    remaps_.clear();
    for (int i = 0; i < find.size(); ++i) {
      remaps_[find.at(i)] = replace.at(i);
    }
    refreshRemaps();
  }

  std::string myURI() {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    char s[80];
    sprintf(s, "127.0.0.1:%i", services_->socket().getServerPort());
    return std::string(s);
  }

  //=============================== Remapping ===============================//
  // Specify a renaming rule
  void setRemap(const std::string &find, const std::string &replace) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    remaps_[find] = replace;
    refreshRemaps();
  }

  void refreshRemaps() {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    // Copy map to avoid invalid references when subscribing / unsubscribing
    M_string pmap = pub_map_;
    for (M_string::iterator it = pmap.begin(); it != pmap.end(); ++it) {
      std::string &current_topic = it->second;
      std::string remapped = remap(it->first);
      if (remapped != current_topic) {
        unregisterPublication(it->first);
        registerPublication(it->first);
        assert(it->second == remapped);
      }
    }

    // Copy map to avoid invalid references when subscribing / unsubscribing
    M_string smap = sub_map_;
    for (M_string::iterator it = smap.begin(); it != smap.end(); ++it) {
      const std::string &orig = it->first;
      const std::string &current_topic = it->second;
      std::string remapped = remap(orig);
      if (remapped != current_topic) {
        MessageCallback cb = subscriptions_[current_topic]->callback();
        unregisterSubscription(orig);
        subscribe(orig, cb);
        assert(sub_map_[orig] == remapped);
      }
    }
  }

  // Apply renaming to get new name
  std::string remap(const std::string &name, bool verbose = true) {
    boost::recursive_mutex::scoped_lock lock(mutex_);

    std::string remapped = name;
    for (M_string::iterator it = remaps_.begin(); it != remaps_.end(); ++it) {
      const std::string &start = it->first;
      const std::string &replace = it->second;
      if (boost::starts_with(name, start)) {
        remapped.replace(remapped.begin(), remapped.begin() + start.size(),
                         replace);
        break;
      }
    }
    if (verbose) {
      ROS_INFO("Changed %s to %s", name.c_str(), remapped.c_str());
    }
    return remapped;
  }

private:
  SubscriptionPtr createSubscription(const std::string &topic, const MessageCallback &cb) {
    SubscriptionPtr sub(new Subscription(topic, cb));

    for (int i = 0; i < sub_factories_.size(); ++i) {
      sub->addTransport(sub_factories_[i]->CreateSubTransport());
    }
    return sub;
  }

  PublicationPtr createPublication(const std::string &topic) {
    PublicationPtr pub(new Publication(topic));
    for (int i = 0; i < pub_factories_.size(); ++i) {
      pub->addTransport(pub_factories_[i]->CreatePubTransport());
    }
    return pub;
  }

  boost::recursive_mutex mutex_;

  PollSet *ps_;
  boost::scoped_ptr<MultiServiceManager> services_;
  RegistrationProtocol *regp_;

  std::vector<SubscribeTransportFactory*> sub_factories_;
  std::vector<PublishTransportFactory*> pub_factories_;

  M_string pub_map_;
  M_string sub_map_;

  M_string remaps_;

  std::map<std::string, PublicationPtr> publications_;
  std::map<std::string, SubscriptionPtr> subscriptions_;
};

//============================== Registration ===============================//

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
    ros2_comm::RegisterSubscription reg_sub;
    reg_sub.request.topic = topic;
    reg_sub.request.node_uri = myURI();
    callMaster("registerSubscription", reg_sub,
               boost::bind(&MasterRegistration::registerSubscriptionCb, this,
                           _1, callback));
  }

  void registerSubscriptionCb(const ros2_comm::RegisterSubscription &reg_sub,
                              const SubscriptionCallback &callback) {
    // Contact publishers to get endpoints
    contactPublishers(reg_sub.request.topic,
                      reg_sub.response.publisher_uris,
                      callback);
  }

  virtual bool unregisterSubscription(const std::string &topic) {
    ROS_INFO("Unregistering subscripton on %s", topic.c_str());
    ros2_comm::UnregisterSubscription unreg_sub;
    unreg_sub.request.topic = topic;
    unreg_sub.request.node_uri = myURI();
    callMaster("unregisterSubscription", unreg_sub);
    return true;
  }

  virtual void registerPublication(const std::string &topic) {
    ROS_INFO("Registering publication on %s", topic.c_str());
    // Inform master we're publishing
    ros2_comm::RegisterPublication reg_pub;
    reg_pub.request.topic = topic;
    reg_pub.request.node_uri = myURI();
    callMaster("registerPublication", reg_pub);
  }

  virtual bool unregisterPublication(const std::string &topic) {
    ROS_INFO("Unregistering publication on %s", topic.c_str());
    ros2_comm::UnregisterPublication unreg_pub;
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


  void handleCallback(const ros2_comm::TopicRequest &req_resp,
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

      ros2_comm::TopicRequest req_rep;
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
#endif
