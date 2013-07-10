#include "core.hpp"
#include "subscription.hpp"
#include "poll_manager.h"

#include <string>
#include <set>
#include <map>

using namespace std;

namespace ros2 {
class Master {
public:  
  Master(PollSet *ps) : ps_(ps) {
    
  }

  ~Master() {
    for (map<string, NodeInfo*>::iterator it = nodes_.begin();
         it != nodes_.end(); ++it) {
      delete it->second;
    }
  }
  
  void registerSubscription(const ros2_comm::RegisterSubscription::Request &req,
                            ros2_comm::RegisterSubscription::Response *resp) {
    NodeInfo *node = getOrCreateNode(req.node_uri);
    ROS_INFO("Registering subscription on %s for node %s",
             req.topic.c_str(), node->uri_.c_str());
    node->subscribe(req.topic);
    lookupPublishers(req.topic, &resp->publisher_uris);
    ROS_INFO_STREAM("Responding to node: " << *resp);
    printState();
  }

  void unregisterSubscription(const ros2_comm::UnregisterSubscription::Request &req,
                              ros2_comm::UnregisterSubscription::Response *resp) {
    NodeInfo *node = getOrCreateNode(req.node_uri);
    node->unsubscribe(req.topic);
    ROS_INFO("Unregister subscription on %s for node %s",
             req.topic.c_str(), node->uri_.c_str());
    printState();    
  }

  void registerPublication(const ros2_comm::RegisterPublication::Request &req,
                           ros2_comm::RegisterPublication::Response *resp) {
    NodeInfo *node = getOrCreateNode(req.node_uri);
    node->publish(req.topic);
    ROS_INFO("Registering publication on %s for node %s",
             req.topic.c_str(), node->uri_.c_str());
    printState();
  }

  void unregisterPublication(const ros2_comm::UnregisterPublication::Request &req,
                             ros2_comm::UnregisterPublication::Response *resp) {
    NodeInfo *node = getOrCreateNode(req.node_uri);
    node->unpublish(req.topic);
    ROS_INFO("Unregister publication on %s for node %s",
             req.topic.c_str(), node->uri_.c_str());
    printState();        
  }
  
  void lookupPublishers(const string &topic, vector<string> *node_uris) {
    set<NodeInfo*> &pubs = topics_[topic].publishers;
    for (set<NodeInfo*>::iterator it = pubs.begin(); it != pubs.end(); ++it) {
      node_uris->push_back((*it)->uri_);
    }
  }
  
  void printState() {
    for (map<string, TopicInfo>::iterator it = topics_.begin();
         it != topics_.end(); ++it) {
      ROS_INFO("Topic = %s", it->first.c_str());
      TopicInfo &ti = it->second;
      
      std::stringstream ss("");
      for (set<NodeInfo*>::iterator node_it = ti.subscribers.begin();
           node_it != ti.subscribers.end(); ++node_it) {
        ss << (*node_it)->uri_ << " ";
      }
      ROS_INFO_STREAM("  Subscribers: " << ss.str());

      ss.str("");
      for (set<NodeInfo*>::iterator node_it = ti.publishers.begin();
           node_it != ti.publishers.end(); ++node_it) {
        ss << (*node_it)->uri_ << " ";
      }
      ROS_INFO_STREAM("  Publishers: " << ss.str());
    }
  }
  
  void start(int port) {
    services_.reset(new MultiServiceManager(TransportTCPPtr(new TransportTCP(ps_))));
    services_->init(port);
    ServiceCallbackPtr regSubCb(new ServiceCallbackT<ros2_comm::RegisterSubscription>(boost::bind(&Master::registerSubscription, this, _1, _2)));    
    services_->bind("registerSubscription", regSubCb);

    ServiceCallbackPtr unregSubCb(new ServiceCallbackT<ros2_comm::UnregisterSubscription>(boost::bind(&Master::unregisterSubscription, this, _1, _2)));    
    services_->bind("unregisterSubscription", unregSubCb);

    ServiceCallbackPtr regPubCb(new ServiceCallbackT<ros2_comm::RegisterPublication>(boost::bind(&Master::registerPublication, this, _1, _2)));    
    services_->bind("registerPublication", regPubCb);

    ServiceCallbackPtr unregPubCb(new ServiceCallbackT<ros2_comm::UnregisterPublication>(boost::bind(&Master::unregisterPublication, this, _1, _2)));    
    services_->bind("unregisterPublication", unregPubCb);        
  }
  
private:
  class NodeInfo {
  public:
    NodeInfo(const std::string &uri, Master *parent) : uri_(uri), parent_(parent) {}

    void subscribe(const string &topic) {
      subscriptions_.insert(topic);
      parent_->topics_[topic].subscribers.insert(this);
    }

    void unsubscribe(const string &topic) {
      subscriptions_.erase(topic);
      parent_->topics_[topic].subscribers.erase(this);
    }
    
    void publish(const string &topic) {
      publications_.insert(topic);
      parent_->topics_[topic].publishers.insert(this);
    }

    void unpublish(const string &topic) {
      publications_.erase(topic);
      parent_->topics_[topic].publishers.erase(this);
    }
    
    ~NodeInfo() {
      for (set<string>::iterator it = subscriptions_.begin();
           it != subscriptions_.end(); ++it) {
        unsubscribe(*it);
      }
      for (set<string>::iterator it = publications_.begin();
           it != publications_.end(); ++it) {
        unpublish(*it);
      }      
    }

    Master *parent_;
    string uri_;
    set<string> subscriptions_;
    set<string> publications_;
  };

  NodeInfo* getOrCreateNode(const std::string &node_uri) {
    NodeInfo* &node = nodes_[node_uri];
    if (node == NULL) {
      ROS_INFO("Creating new node at %s", node_uri.c_str());
      node = new NodeInfo(node_uri, this);
    }
    return node;
  }
  
  PollSet *ps_;
  boost::scoped_ptr<MultiServiceManager> services_;  
  map<string, NodeInfo*> nodes_;

  struct TopicInfo {
    set<NodeInfo*> subscribers;
    set<NodeInfo*> publishers;      
  };
  map<string, TopicInfo> topics_;  
};

}

int main(int argc, char **argv) {
  ros2::PollManager pm;
  pm.start();
  ros2::Master m(&pm.getPollSet());

  m.start(11311);
  while (1) {
    sleep(1);
  }
  pm.shutdown();
}
