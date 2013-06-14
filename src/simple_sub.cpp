#include <string>

#include "core.hpp"
#include "pubsub_tcp.hpp"
#include "pubsub_zmq.hpp"

#include "poll_manager.h"

using namespace std;
using namespace ros2;

int main(int argc, char **argv) {
  int port;
  if (argc > 2) {
    ROS_ERROR("usage: simple_sub <pub_port>");
    ROS_BREAK();
  } else if (argc == 2) {
    port = atoi(argv[1]);
  } else {
    port = 50000;
  }
  string host = "localhost";

  PollManager pm;

  Subscription sub;

  // tcp
  boost::shared_ptr<TransportTCP> trans_tcp(new TransportTCP(&pm.getPollSet()));
  TCPSubscribe sub_tcp(trans_tcp);
  sub.addSubscription(&sub_tcp);

  // zmq
  zmq::context_t ctx(1);
  SubscribeZMQ sub_zmq(&ctx);
  sub.addSubscription(&sub_zmq);

  // Startup tcp + zmq
  sub_tcp.start(host, port);
  sub_zmq.start("tcp://localhost:60000");

  pm.start();
  usleep(1000 * 1000 * 10);

  sub_tcp.shutdown();
  sub_zmq.stop();
  pm.shutdown();
}
