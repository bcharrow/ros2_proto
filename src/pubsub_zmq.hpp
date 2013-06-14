#ifndef PUBSUB_ZMQ_HPP
#define PUBSUB_ZMQ_HPP

#include "zmq.hpp"

#include <deque>

#include <boost/thread/condition_variable.hpp>
#include <boost/thread.hpp>

#include "core.hpp"

namespace ros2 {

class PublishZMQ : public PublishProtocol {
public:
  PublishZMQ(zmq::context_t *ctx) : shutdown_(false), sock_(NULL), ctx_(ctx) {

  }

  // Thread safe
  virtual void publish(const Message &msg) {
    queue_.push(msg.Copy());
  }

  void start(const std::string &endpoint) {
    thread_ = boost::thread(&PublishZMQ::run, this);
    endpoint_ = endpoint;
  }

  void stop() {
    shutdown_ = true;
    thread_.join();
  }

  void run() {
    // Now we can create the socket
    sock_.reset(new zmq::socket_t(*ctx_, ZMQ_PUB));
    sock_->bind(endpoint_.c_str());

    char topic[] = "pubsub_zmq";
    zmq::message_t zmqmsg_topic(sizeof(topic) - 1);
    memcpy(zmqmsg_topic.data(), topic, sizeof(topic) - 1);

    boost::scoped_ptr<Message> msg;
    while (!shutdown_) {
      msg.reset(queue_.pop(100));
      if (msg.get() == NULL) {
        continue;
      }

      zmq::message_t zmqmsg_copy;
      zmqmsg_copy.copy(&zmqmsg_topic);

      ROS_INFO("PublishZMQ::run() Publishing");
      zmq::message_t zmqmsg_data(msg->size());
      memcpy(zmqmsg_data.data(), msg->bytes(), msg->size());

      bool result = sock_->send(zmqmsg_copy, ZMQ_SNDMORE);
      sock_->send(zmqmsg_data);
    }
  }

private:
  boost::thread thread_;
  std::string endpoint_;
  volatile bool shutdown_;

  MessageQueue<Message> queue_;

  boost::scoped_ptr<zmq::socket_t> sock_;
  zmq::context_t *ctx_;
};

class SubscribeZMQ : public SubscribeProtocol {
public:
  SubscribeZMQ(zmq::context_t *ctx) : ctx_(ctx), shutdown_(false) {}

  virtual void onReceive(const MessageCallback &cb) {
    cb_ = cb;
  }

  void start(const std::string &endpoint) {
    endpoint_ = endpoint;
    thread_ = boost::thread(&SubscribeZMQ::run, this);
  }

  void stop() {
    shutdown_ = true;
    thread_.join();
  }

  void run() {
    zmq::socket_t socket(*ctx_, ZMQ_SUB);
    socket.setsockopt(ZMQ_SUBSCRIBE, "pubsub_zmq", 0);
    socket.connect(endpoint_.c_str());

    zmq::pollitem_t pollitem;
    pollitem.socket = socket;
    pollitem.events = ZMQ_POLLIN;

    int wait_msec = 100;
    int err;
    while (!shutdown_) {
      if ((err = zmq::poll(&pollitem, 1, wait_msec)) == 0) {
        continue;
      } else if (err < 0) {
        ROS_WARN("zmq_poll() error = %i", err);
        continue;
      }

      int msg_count = 0;
      zmq::message_t zmsg_topic, zmsg_data;

      if (!socket.recv(&zmsg_topic)) {
        ROS_WARN("Couldn't receive topic");
        continue;
      }

      char topic[100];
      memcpy(topic, zmsg_topic.data(), zmsg_topic.size());
      topic[zmsg_topic.size()] = '\0';

      ROS_INFO("SubscribeZMQ::run() Got data on topic '%s'",
               topic, zmsg_topic.size());

      int rcvmore;
      size_t rcvmore_sz = sizeof(rcvmore);
      socket.getsockopt(ZMQ_RCVMORE, &rcvmore, &rcvmore_sz);

      if (rcvmore != 1) {
        ROS_WARN("Topic, but no payload");
        continue;
      }

      if (!socket.recv(&zmsg_data, ZMQ_DONTWAIT)) {
        ROS_WARN("Couldn't receive data");
        continue;
      }

      socket.getsockopt(ZMQ_RCVMORE, &rcvmore, &rcvmore_sz);
      if (rcvmore != 0) {
        ROS_WARN("Got more than 2 message parts, failing");
        break;
      }

      Message msg(zmsg_data.size());
      memcpy(msg.bytes(), zmsg_data.data(), zmsg_data.size());
      cb_(msg);
    }
  }

private:
  zmq::context_t *ctx_;
  MessageCallback cb_;

  boost::mutex mutex_;
  std::string endpoint_;
  boost::thread thread_;
  volatile bool shutdown_;
};

}
#endif // PUBSUB_ZMQ_HPP
