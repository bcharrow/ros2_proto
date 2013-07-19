#include "topic_manager.hpp"

#include "poll_set.h"
#include "registration.hpp"
#include "pubsub.hpp"
#include "core.hpp"
#include "service.hpp"

namespace ros2 {

TopicManager::~TopicManager() {
  shutdown();
}

void TopicManager::shutdown() {
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

void TopicManager::addPublishTransport(PublishTransportFactory *factory) {
  boost::recursive_mutex::scoped_lock lock(mutex_);

  pub_factories_.push_back(factory);
}

void TopicManager::addSubscribeTransport(SubscribeTransportFactory *factory) {
  boost::recursive_mutex::scoped_lock lock(mutex_);

  sub_factories_.push_back(factory);
}

void TopicManager::unregisterSubscription(const std::string &unmapped) {
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

void TopicManager::registerPublication(const std::string &unmapped) {
  boost::recursive_mutex::scoped_lock lock(mutex_);

  std::string &topic = pub_map_[unmapped];
  topic = remap(unmapped);
  assert(publications_.count(topic) == 0);
  publications_[topic] = createPublication(topic);
  regp_->registerPublication(topic);
}

void TopicManager::unregisterPublication(const std::string &unmapped) {
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

void TopicManager::subscribe(const std::string &unmapped, const MessageCallback &cb) {
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

void TopicManager::publish(const std::string &unmapped, const Message &msg) {
  boost::recursive_mutex::scoped_lock lock(mutex_);

  if (pub_map_.count(unmapped) == 0) {
    registerPublication(unmapped);
  } else {
    assert(pub_map_[unmapped] == remap(unmapped, false));
  }
  std::string topic = pub_map_[unmapped];
  publications_[topic]->publish(msg);
}

std::vector<std::string> TopicManager::publicationURIs(const std::string &topic) {
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
void TopicManager::init(int port) {
  services_.reset(new MultiServiceManager(TransportTCPPtr(new TransportTCP(ps_))));
  services_->init(port);
  ServiceCallbackPtr scht(new ServiceCallbackT<ros2_proto::TopicRequest>(boost::bind(&TopicManager::requestTopic, this, _1, _2)));
  services_->bind("requestTopic", scht);

  ServiceCallbackPtr remaps(new ServiceCallbackT<ros2_proto::SetRemappings>(boost::bind(&TopicManager::setRemappings, this, _1, _2)));
  services_->bind("setRemappings", remaps);

  ROS_INFO("TopicManager @ %s", myURI().c_str());
}

void TopicManager::requestTopic(const ros2_proto::TopicRequest::Request &request,
                                ros2_proto::TopicRequest::Response *response) {
  boost::recursive_mutex::scoped_lock lock(mutex_);

  ROS_INFO_STREAM("Got a request:\n" << request);
  response->uris = publicationURIs(request.topic);
  ROS_INFO_STREAM("Responding with:\n" << *response);
}

void TopicManager::setRemappings(const ros2_proto::SetRemappings::Request &request,
                                 ros2_proto::SetRemappings::Response *response) {
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

std::string TopicManager::myURI() {
  boost::recursive_mutex::scoped_lock lock(mutex_);

  char s[80];
  sprintf(s, "127.0.0.1:%i", services_->socket().getServerPort());
  return std::string(s);
}

//=============================== Remapping ===============================//
// Specify a renaming rule
void TopicManager::setRemap(const std::string &find, const std::string &replace) {
  boost::recursive_mutex::scoped_lock lock(mutex_);

  remaps_[find] = replace;
  refreshRemaps();
}

void TopicManager::refreshRemaps() {
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
std::string TopicManager::remap(const std::string &name, bool verbose) {
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

SubscriptionPtr TopicManager::createSubscription(const std::string &topic, const MessageCallback &cb) {
  SubscriptionPtr sub(new Subscription(topic, cb));

  for (int i = 0; i < sub_factories_.size(); ++i) {
    sub->addTransport(sub_factories_[i]->CreateSubTransport());
  }
  return sub;
}

PublicationPtr TopicManager::createPublication(const std::string &topic) {
  PublicationPtr pub(new Publication(topic));
  for (int i = 0; i < pub_factories_.size(); ++i) {
    pub->addTransport(pub_factories_[i]->CreatePubTransport());
  }
  return pub;
}
}
