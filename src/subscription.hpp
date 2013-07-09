#ifndef SUBSCRIPTION_HPP
#define SUBSCRIPTION_HPP

#include "core.hpp"
#include <boost/scoped_ptr.hpp>

namespace ros2 {

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
  virtual void setTopicManager(TopicManager *tm) {};
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
    protos_[transport->protocol()] = boost::shared_ptr<SubscribeTransport>(transport);
  }

  void foundPublisher(const std::vector<std::string> &uris) {
    for (int i = 0; i < uris.size(); ++i) {
      // split each into protocol://endpoint
      const std::string &uri = uris.at(i);
      std::string protocol = uri.substr(0, uri.find(':'));
      std::string endpoint = uri.substr(uri.find("://") + 3);
      ROS_INFO("Protocol = %s Endpoint = %s",
               protocol.c_str(), endpoint.c_str());
      if (protos_.count(protocol) != 0) {
        protos_[protocol]->onReceive(cb_);
        protos_[protocol]->start(endpoint);
        return;
      }
    }
    ROS_WARN("No match found for discovery");
  }

  void shutdown() {
    for (std::map<std::string, boost::shared_ptr<SubscribeTransport> >::iterator it = protos_.begin(); it != protos_.end(); ++it) {
      it->second->shutdown();
    }
  }

protected:
  boost::mutex callback_mutex_;
  std::string topic_;
  std::map<std::string, boost::shared_ptr<SubscribeTransport> > protos_;
  MessageCallback cb_;
};

typedef boost::shared_ptr<Subscription> SubscriptionPtr;

//============================== TopicManager ===============================//

class TopicManager {
public:
  TopicManager(RegistrationProtocol *regp) : regp_(regp) {
    regp->setTopicManager(this);
  }

  ~TopicManager() {
    shutdown();
  }

  void shutdown() {
    std::map<std::string, PublicationPtr> pubs = publications_;
    for (std::map<std::string, PublicationPtr>::iterator it = pubs.begin();
         it != pubs.end(); ++it) {
      unregisterPublication(it->first);
    }

    std::map<std::string, SubscriptionPtr> subs = subscriptions_;
    for (std::map<std::string, SubscriptionPtr>::iterator it = subs.begin();
         it != subs.end(); ++it) {
      unregisterSubscription(it->first);
    }
  }

  void addPublishTransport(PublishTransportFactory *factory) {
    pub_factories_.push_back(factory);
  }

  void addSubscribeTransport(SubscribeTransportFactory *factory) {
    sub_factories_.push_back(factory);
  }

  void unregisterSubscription(const std::string &topic) {
    assert(subscriptions_.count(topic) != 0);
    subscriptions_[topic]->shutdown();
    subscriptions_.erase(topic);
    regp_->unregisterSubscription(topic);
  }

  void unregisterPublication(const std::string &topic) {
    assert(publications_.count(topic) != 0);
    publications_[topic]->shutdown();
    publications_.erase(topic);
    regp_->unregisterPublication(topic);
  }

  void subscribe(const std::string &topic, const MessageCallback &cb) {
    assert(subscriptions_.count(topic) == 0);
    boost::shared_ptr<Subscription> sub = createSubscription(topic, cb);
    subscriptions_[topic] = sub;
    regp_->registerSubscription(topic, boost::bind(&Subscription::foundPublisher,
                                                   sub.get(), _1));
  }

  void publish(const std::string &topic, const Message &msg) {
    if (publications_.count(topic) == 0) {
      publications_[topic] = createPublication(topic);
      regp_->registerPublication(topic);
    }
    publications_[topic]->publish(msg);
  }

  std::vector<std::string> publicationURIs(const std::string &topic) const {
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

  RegistrationProtocol *regp_;

  std::vector<SubscribeTransportFactory*> sub_factories_;
  std::vector<PublishTransportFactory*> pub_factories_;
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
  NodeAddress(const std::string &hostname, int port)
    : hostname_(hostname), port_(port) {}

  const std::string& hostname() const { return hostname_; }
  int port() const { return port_; }

private:
  std::string hostname_;
  int port_;
};

// Find publishers / subscribers using service calls to master and nodes
class MasterRegistration : public RegistrationProtocol {
public:
  MasterRegistration(const NodeAddress &master, PollSet *ps)
    : master_(master), ps_(ps), tm_(NULL), service_(NULL) {
  }

  virtual void registerSubscription(const std::string &topic,
                                    const SubscriptionCallback &callback) {
    std::vector<std::string> publisher_uris;
    findPublishers(topic, &publisher_uris);
    contactPublishers(topic, publisher_uris, callback);
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
    return true;
  }

  void init(int port) {
    service_.reset(new ServiceManager<ros2_comm::TopicRequest>(TransportTCPPtr(new TransportTCP(ps_))));
    service_->init(port);
    service_->bind(boost::bind(&MasterRegistration::handleRequestTopic, this, _1, _2));
  }

  void handleRequestTopic(const ros2_comm::TopicRequest::Request &req,
                          ros2_comm::TopicRequest::Response *rep) {
    ROS_INFO_STREAM("Got a request:\n" << req);
    rep->uris = tm_->publicationURIs(req.topic);
  }

  virtual void setTopicManager(TopicManager *tm) {
    tm_ = tm;
  }

private:
  void findPublishers(const std::string &topic, std::vector<std::string> *uris) {
    // hardcoded node address; TODO: master resolution
    uris->push_back("127.0.0.1:5555");
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

      ServiceClient<ros2_comm::TopicRequest> sc;
      ros2_comm::TopicRequest tr;
      tr.request.topic = topic;
      sc.call(tcp, &tr);

      callback(tr.response.uris);
      tcp->close();
    }
  }


  NodeAddress master_;
  PollSet *ps_;
  TopicManager *tm_;
  boost::scoped_ptr<ServiceManager<ros2_comm::TopicRequest> > service_;
  std::map<std::string, SubscriptionCallback> sub_cbs_;
};

}
#endif
