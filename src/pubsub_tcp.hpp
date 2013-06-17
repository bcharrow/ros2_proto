#ifndef SUB_TCP_HPP
#define SUB_TCP_HPP

#include "core.hpp"
#include "transport_tcp.h"

#include <list>

namespace ros2 {

class TCPPublish : public PublishProtocol {
public:
  TCPPublish(TransportTCPPtr server) : server_(server) {}

  ~TCPPublish() {
    shutdown();
  }

  void start(int port) {
    if (!server_->listen(port, 5, boost::bind(&TCPPublish::onAccept, this, _1))) {
      ROS_ERROR("Failed to listen!");
      ROS_BREAK();
    }
  }

  virtual void publish(const Message &msg) {
    boost::shared_array<uint8_t> data(new uint8_t[msg.size()]);
    // ROS_INFO("TCPPublish::publish() Publishing a message");
    std::copy(msg.bytes(), msg.bytes() + msg.size(), data.get());

    // Copy list of connections in case writing causes a disconnect
    std::list<TransportTCPPtr> conns;
    {
      boost::mutex::scoped_lock lock(connections_mutex_);
      conns = connections_;
    }

    for (std::list<TransportTCPPtr>::iterator it = conns.begin();
         it != conns.end(); ++it) {
      (*it)->sendMessage(data, msg.size());
    }
  }

  void shutdown() {
    std::list<TransportTCPPtr> local_connections;
    {
      boost::mutex::scoped_lock lock(connections_mutex_);
      local_connections = connections_;
    }
    // This will trigger callback, onDisconnect()
    for (std::list<TransportTCPPtr>::iterator it = local_connections.begin();
         it != local_connections.end(); ++it) {
      (*it)->close();
    }

    server_->close();
  }

  void onAccept(const TransportTCPPtr& tcp) {
    boost::mutex::scoped_lock lock(connections_mutex_);
    tcp->enableMessagePass();
    connections_.push_back(tcp);
    ROS_INFO("Got a connection to %s", tcp->getClientURI().c_str());
    tcp->setDisconnectCallback(boost::bind(&TCPPublish::onDisconnect, this, _1));
  }

  void onDisconnect(const TransportPtr &tcp) {
    boost::mutex::scoped_lock lock(connections_mutex_);

    for (std::list<TransportTCPPtr>::iterator it = connections_.begin();
         it != connections_.end(); ++it) {
      if (*it == tcp) {
        ROS_INFO("Removing %s", tcp->getTransportInfo().c_str());
        connections_.erase(it);
        return;
      }
    }
    ROS_ERROR("Disconnect from Tranposrt something not in list!");
  }

private:
  TransportTCPPtr server_;

  boost::mutex connections_mutex_;
  std::list<TransportTCPPtr> connections_;
};

class TCPSubscribe : public SubscribeProtocol {
public:
  TCPSubscribe(const TransportTCPPtr &tcp) : tcp_(tcp) {}

  ~TCPSubscribe() {
    shutdown();
  }

  virtual void onReceive(const MessageCallback &cb) {
    cb_ = cb;
  }

  void shutdown() {
    tcp_->close();
  }

  void start(const std::string host, int port) {
    if (!tcp_->connect(host, port)) {
      ROS_ERROR("connect()");
      ROS_BREAK();
    }

    tcp_->setDisconnectCallback(boost::bind(&TCPSubscribe::onDisconnect, this, _1));
    tcp_->enableMessagePass();
    tcp_->enableRead();
    tcp_->setMsgCallback(boost::bind(&TCPSubscribe::receive, this, _1, _2));
  }

  void receive(const boost::shared_array<uint8_t> &bytes, uint32_t sz) {
    // ROS_INFO("TCPSubscribe::receive() Got a message!");
    Message msg(sz);
    std::copy(bytes.get(), bytes.get() + sz, msg.bytes());
    cb_(msg);
  }

  void onDisconnect(const TransportPtr &tcp) {
    ROS_INFO("Disconnected");
    tcp_->close();
  }

protected:
  TransportTCPPtr tcp_;
  MessageCallback cb_;
};

}

#endif // SUB_TCP_HPP
