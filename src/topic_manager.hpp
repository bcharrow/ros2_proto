#ifndef TOPIC_MANAGER_HPP
#define TOPIC_MANAGER_HPP

#include <string>
#include <vector>

#include <boost/thread/recursive_mutex.hpp>
#include <boost/scoped_ptr.hpp>

#include <ros2_comm/TopicRequest.h>
#include <ros2_comm/SetRemappings.h>

#include "subscription.hpp"
#include "service.hpp"

namespace ros2 {

typedef std::map<std::string, std::string> M_string;

class RegistrationProtocol;
class PollSet;
class PublishTransportFactory;
class SubscribeTransportFactory;
class Message;
class MultiServiceManager;

class TopicManager {
public:
  TopicManager(RegistrationProtocol *regp, PollSet *ps)
    : regp_(regp), ps_(ps) { }

  ~TopicManager();
  void shutdown();
  void addPublishTransport(PublishTransportFactory *factory);
  void addSubscribeTransport(SubscribeTransportFactory *factory);
  void unregisterSubscription(const std::string &unmapped);
  void registerPublication(const std::string &unmapped);
  void unregisterPublication(const std::string &unmapped);
  void subscribe(const std::string &unmapped, const MessageCallback &cb);
  void publish(const std::string &unmapped, const Message &msg);
  std::vector<std::string> publicationURIs(const std::string &topic);
  
  //================================= RPCs ==================================//
  void init(int port);
  void requestTopic(const ros2_comm::TopicRequest::Request &request,
                    ros2_comm::TopicRequest::Response *response);
  void setRemappings(const ros2_comm::SetRemappings::Request &request,
                     ros2_comm::SetRemappings::Response *response);
  std::string myURI();
  
  //=============================== Remapping ===============================//
  // Specify a renaming rule
  void setRemap(const std::string &find, const std::string &replace);
  void refreshRemaps();
  // Apply renaming to get new name
  std::string remap(const std::string &name, bool verbose = true);
  
private:
  SubscriptionPtr createSubscription(const std::string &topic, const MessageCallback &cb);
  PublicationPtr createPublication(const std::string &topic);
  
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

}

#endif // TOPIC_MANAGER_HPP
