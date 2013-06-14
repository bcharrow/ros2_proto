#include <list>

#include "core.hpp"
#include "transport_tcp.h"
#include "poll_manager.h"

#include "pubsub_tcp.hpp"
#include "pubsub_zmq.hpp"

using namespace std;
using namespace ros2;

int main(int argc, char **argv) {
  PollManager pm;

  // Message to send
  Message msg(1000);
  uint8_t *bytes = msg.bytes();
  for (int i = 0; i < msg.size(); ++i) {
    bytes[i] = 0;
  }

  // Publisher
  Publication pub;

  // Tcp
  boost::shared_ptr<TransportTCP> trans_tcp(new TransportTCP(&pm.getPollSet()));
  TCPPublish pub_tcp(trans_tcp);
  pub.registerProtocol(&pub_tcp);

  // zmq
  zmq::context_t ctx(1);
  PublishZMQ pub_zmq(&ctx);
  pub.registerProtocol(&pub_zmq);

  // Startup tcp + zmq
  int port = 50000;
  pub_tcp.start(port);
  ROS_INFO("TCP listening on port %i", trans_tcp->getServerPort());

  pub_zmq.start("tcp://*:60000");

  pm.start();
  for (int i = 0; i < 1000; ++i) {
    ROS_INFO("simple_pub: Publishing");
    pub.publish(msg);
    usleep(1000 * 100);
  }

  ROS_INFO("Shutting down");
  pub_zmq.stop();
  pub_tcp.shutdown();
  pm.shutdown();
}
