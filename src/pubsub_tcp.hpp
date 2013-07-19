#ifndef PUBSUB_TCP_HPP
#define PUBSUB_TCP_HPP

#include <list>

#include <snappy.h>

#include <boost/scoped_ptr.hpp>

#include <ros2_proto/TCPOptions.h>

#include "transport_tcp.h"
#include "pubsub.hpp"

namespace ros2 {

//=================================== Pub ===================================//

class TCPPublish : public PublishTransport {
public:
  TCPPublish(TransportTCPPtr server) : server_(server) {}

  virtual ~TCPPublish() {
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
    size_t snappy_sz;
    boost::shared_array<uint8_t> snappy_data(NULL);

    // ROS_INFO("TCPPublish::publish() Publishing a message");
    std::copy(msg.bytes(), msg.bytes() + msg.size(), data.get());

    // Copy list of connections in case writing causes a disconnect
    std::list<Connection> conns;
    {
      boost::mutex::scoped_lock lock(connections_mutex_);
      conns = connections_;
    }

    for (std::list<Connection>::iterator it = conns.begin();
         it != conns.end(); ++it) {
      if (it->setup) {
        switch (it->opts.compression) {
        case ros2_proto::TCPOptions::NONE:
          it->tcp->sendMessage(data, msg.size());
          break;
        case ros2_proto::TCPOptions::SNAPPY:
          if (snappy_data == NULL) {
            uint32_t max_snappy_sz = snappy::MaxCompressedLength(msg.size());
            snappy_data.reset(new uint8_t[max_snappy_sz]);
            snappy::RawCompress((char*)data.get(), msg.size(),
                                (char*)snappy_data.get(), &snappy_sz);
          }
          it->tcp->sendMessage(snappy_data, snappy_sz);
          break;
        default:
          ROS_BREAK();
          break;
        }
      }
    }
  }

  virtual const char* protocol() const { return "tcpros"; }

  virtual std::string endpoint() const {
    std::stringstream ss;
    ss << "localhost:" << server_->getServerPort();
    return ss.str();
  }

  void shutdown() {
    std::list<Connection> local_connections;
    {
      boost::mutex::scoped_lock lock(connections_mutex_);
      local_connections = connections_;
    }
    // This will trigger callback, onDisconnect()
    for (std::list<Connection>::iterator it = local_connections.begin();
         it != local_connections.end(); ++it) {
      it->tcp->close();
    }

    server_->close();
  }

  void onAccept(const TransportTCPPtr& tcp) {
    boost::mutex::scoped_lock lock(connections_mutex_);
    ROS_INFO("Got a connection to %s", tcp->getClientURI().c_str());

    Connection conn;
    conn.setup = false;
    conn.tcp = tcp;
    connections_.push_back(conn);

    tcp->enableMessagePass();
    tcp->setDisconnectCallback(boost::bind(&TCPPublish::onDisconnect, this, _1));
    tcp->enableRead();
    tcp->setMsgCallback(boost::bind(&TCPPublish::onOptions, this,
                                    boost::weak_ptr<TransportTCP>(tcp), _1));
  }

  void onOptions(const boost::weak_ptr<TransportTCP> weak_tcp,
                 const std::vector<Frame> &frames) {
    boost::mutex::scoped_lock lock(connections_mutex_);
    if (frames.size() != 1) {
      ROS_ERROR("Got %zu frames; expected 1", frames.size());
      ROS_BREAK();
    }
    const boost::shared_array<uint8_t> &bytes = frames[0].data;
    uint32_t sz = frames[0].size;

    TransportTCPPtr tcp = weak_tcp.lock();
    if (!tcp) {
      ROS_WARN("Disconnect before negotiation");
      return;
    }

    // Lookup connection object
    tcp->disableRead();
    for (std::list<Connection>::iterator it = connections_.begin();
         it != connections_.end(); ++it) {
      Connection &conn = *it;
      if (conn.tcp == tcp) {
        if (conn.setup) {
          ROS_ERROR("Told to setup messages twice");
          ROS_BREAK();
        }
        ros::serialization::IStream istream(bytes.get(), sz);
        ros::serialization::Serializer<ros2_proto::TCPOptions>::read(istream, conn.opts);
        tcp->setNoDelay(conn.opts.tcp_nodelay);
        tcp->setFilter(conn.opts.filter);
        conn.setup = true;
        ROS_INFO_STREAM("Setup connection: " << tcp->getTransportInfo() << std::endl << conn.opts);
        return;
      }
    }

    ROS_WARN("Got strong pointer, but connection not found in list!");
  }

  void onDisconnect(const TransportPtr &trans) {
    boost::mutex::scoped_lock lock(connections_mutex_);

    for (std::list<Connection>::iterator it = connections_.begin();
         it != connections_.end(); ++it) {
      if (it->tcp == trans) {
        ROS_INFO("Removing %s", trans->getTransportInfo().c_str());
        connections_.erase(it);
        return;
      }
    }
    ROS_ERROR("Disconnect from TransportPtr, but not in list!");
  }

private:
  struct Connection {
    TransportTCPPtr tcp;
    bool setup;
    ros2_proto::TCPOptions opts;
  };

  TransportTCPPtr server_;

  boost::mutex connections_mutex_;
  std::list<Connection> connections_;
};


class TCPPubFactory : public PublishTransportFactory {
public:
  TCPPubFactory(PollSet *ps) : ps_(ps) {

  }

  virtual TCPPublish* CreatePubTransport() {
    boost::shared_ptr<TransportTCP> trans_tcp(new TransportTCP(ps_));
    TCPPublish *tcp = new TCPPublish(trans_tcp);
    int port = 0;
    tcp->start(port);
    ROS_INFO("TCP listening on port %i", trans_tcp->getServerPort());
    return tcp;
  }

private:
  PollSet *ps_;
};

//=================================== Sub ===================================//

class TCPSubscribe : public SubscribeTransport {
public:
  TCPSubscribe(PollSet *ps) : ps_(ps) {
    opts_.tcp_nodelay = false;
    opts_.compression = ros2_proto::TCPOptions::NONE;
  }

  TCPSubscribe(PollSet *ps, const ros2_proto::TCPOptions &opts)
    : ps_(ps), opts_(opts) {}

  ~TCPSubscribe() {
    shutdown();
  }

  virtual void onReceive(const MessageCallback &cb) {
    cb_ = cb;
  }

  virtual const char* protocol() const { return "tcpros"; }

  void shutdown() {
    if (tcp_) {
      tcp_->close();
    }
  }

  void start(const std::string &endpoint) {
    tcp_ = boost::shared_ptr<TransportTCP>(new TransportTCP(ps_));
    ROS_INFO("%s", endpoint.c_str());
    size_t n = endpoint.find(':');
    std::string host = endpoint.substr(0, n);
    int port = atoi(endpoint.substr(n+1).c_str());
    if (host == "localhost") {
      host = "127.0.0.1";
    }
    if (!tcp_->connect(host, port)) {
      ROS_ERROR("connect()");
      ROS_BREAK();
    }

    tcp_->setDisconnectCallback(boost::bind(&TCPSubscribe::onDisconnect, this, _1));
    tcp_->enableMessagePass();
    tcp_->enableRead();
    tcp_->setMsgCallback(boost::bind(&TCPSubscribe::receive, this, _1));

    uint32_t opts_sz = ros::serialization::serializationLength(opts_);
    boost::shared_array<uint8_t> opts_bytes(new uint8_t[opts_sz]);
    ros::serialization::OStream ostream(opts_bytes.get(), opts_sz);
    ros::serialization::serialize(ostream, opts_);
    ROS_INFO_STREAM("Sending options: \n" << opts_);
    tcp_->sendMessage(opts_bytes, opts_sz);
  }

  void receive(const std::vector<Frame> &frames) {
    if (frames.size() != 1) {
      ROS_ERROR("Got %zu frames; expected 1", frames.size());
      ROS_BREAK();
    }
    const boost::shared_array<uint8_t> &bytes = frames[0].data;
    uint32_t sz = frames[0].size;

    // ROS_INFO("TCPSubscribe::receive() Got a message!");
    if (opts_.compression == ros2_proto::TCPOptions::NONE) {
      Message msg(sz);
      std::copy(bytes.get(), bytes.get() + sz, msg.bytes());
      cb_(msg);
    } else if (opts_.compression == ros2_proto::TCPOptions::SNAPPY) {
      bool success;
      size_t uncompressed_sz;
      if (!snappy::GetUncompressedLength((char*)bytes.get(), sz, &uncompressed_sz)) {
        ROS_WARN("Couldn't get uncompressed size");
        return;
      }

      Message msg(uncompressed_sz);
      if (!snappy::RawUncompress((char*)bytes.get(), sz, (char*)msg.bytes())) {
        ROS_WARN("Couldn't uncompress message");
        return;
      }
      cb_(msg);
    } else {
      ROS_BREAK();
    }
  }

  void onDisconnect(const TransportPtr &tcp) {
    ROS_INFO("Disconnected");
    tcp_->close();
  }

protected:
  PollSet *ps_;
  TransportTCPPtr tcp_;
  MessageCallback cb_;
  ros2_proto::TCPOptions opts_;
};

class TCPSubFactory : public SubscribeTransportFactory {
public:
  TCPSubFactory(PollSet *ps) : ps_(ps) {

  }

  virtual TCPSubscribe* CreateSubTransport() {
    ros2_proto::TCPOptions opts;
    opts.tcp_nodelay = true;
    opts.compression = ros2_proto::TCPOptions::NONE;
    opts.filter = 1;
    return new TCPSubscribe(ps_, opts);
  }

private:
  PollSet *ps_;
};

}

#endif // PUBSUB_TCP_HPP
