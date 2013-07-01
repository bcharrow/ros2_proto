#include <list>

#include "core.hpp"
#include "transport_tcp.h"
#include "poll_manager.h"

#include "pubsub_tcp.hpp"
#include "pubsub_zmq.hpp"

using namespace std;
using namespace ros2;

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "simple_pub msg_size\n");
    return 1;
  }
  uint64_t sz = atoi(argv[1]);

  PollManager pm;

  // Message to send
  Message msg(sz);
  uint8_t *bytes = msg.bytes();
  for (int i = 0; i < msg.size(); ++i) {
    bytes[i] = 0;
  }

  // Publisher
  Publication pub("/data");

  // Tcp
  boost::shared_ptr<TransportTCP> trans_tcp(new TransportTCP(&pm.getPollSet()));
  TCPPublish pub_tcp(trans_tcp);
  pub.registerProtocol(&pub_tcp);

  // zmq
  zmq::context_t ctx(1);
  PublishZMQ pub_zmq(&ctx, 100);
  pub.registerProtocol(&pub_zmq);

  // Setup services
  ServiceManager<ros2_comm::TopicRequest> sm(boost::shared_ptr<TransportTCP>(new TransportTCP(&pm.getPollSet())));
  sm.init(5555);

  TopicManager tm;
  tm.init(&sm);
  tm.addPublication(&pub);

  // Startup tcp + zmq
  int port = 0;
  pub_tcp.start(port);
  ROS_INFO("TCP listening on port %i", trans_tcp->getServerPort());

  pub_zmq.start("tcp://127.0.0.1:0");

  pm.start();
  while (1) {
    pub.publish(msg);
    // usleep(1000 * 100);
  }

  ROS_INFO("Shutting down");
  pub_zmq.stop();
  pub_tcp.shutdown();
  pm.shutdown();
}
