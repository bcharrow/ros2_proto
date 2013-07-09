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

  StaticRegistration static_res;
  MasterRegistration master_res(NodeAddress("127.0.0.1", 11311), &pm.getPollSet());
  master_res.init(5555);

  RegistrationProtocol *reg = &master_res;

  TopicManager tm(reg);

  // tcp
  TCPPubFactory tcp_pub_factory(&pm.getPollSet());
  tm.addPublishTransport(&tcp_pub_factory);
  // ZMQ
  ZMQPubFactory zmq_pub_factory;
  tm.addPublishTransport(&zmq_pub_factory);

  // Message to send
  Message msg(sz);
  uint8_t *bytes = msg.bytes();
  for (int i = 0; i < msg.size(); ++i) {
    bytes[i] = 0;
  }

  pm.start();
  while (1) {
    tm.publish("/data", msg);
  }

  ROS_INFO("Shutting down");
  pm.shutdown();
}
