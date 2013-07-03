#include <string>

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

#include "core.hpp"
#include "pubsub_tcp.hpp"
#include "pubsub_zmq.hpp"

#include "poll_manager.h"

using namespace std;
using namespace ros2;

size_t n_received = 0;
size_t n_msg = 0;

boost::mutex mutex;
boost::condition_variable cv;
void callback(const Message &msg) {
  n_received++;
  // ROS_INFO("msg.size() = %i", msg.size());
  if (n_msg <= n_received) {
    boost::mutex::scoped_lock lock(mutex);
    cv.notify_all();
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: simple_sub n_msgs <protocols>\n");
    return 1;
  }
  n_msg = atoi(argv[1]);

  std::string topic("/data");

  PollManager pm;
  pm.start();

  ServiceDiscovery sd(&pm.getPollSet());

  zmq::context_t ctx(1);

  boost::scoped_ptr<SubscribeProtocol> sub_proto;
  if (argv[2] == string("TCPROS")) {
    ros2_comm::TCPOptions opts;
    opts.tcp_nodelay = true;
    opts.compression = ros2_comm::TCPOptions::SNAPPY;
    opts.filter = 1;
    boost::shared_ptr<TransportTCP> trans;
    trans.reset(new TransportTCP(&pm.getPollSet()));
    sub_proto.reset(new TCPSubscribe(trans, opts));
  } else if (argv[2] == string("ZMQROS")) {
    sub_proto.reset(new SubscribeZMQ(&ctx));
  } else {
    ROS_ERROR("Unknown protocol: %s", argv[2]);
    ROS_BREAK();
  }

  ComposeSubscription sub(topic, &sd, sub_proto.get(), callback);
  sub.start();

  uint64_t start = usectime();
  {
    boost::mutex::scoped_lock lock(mutex);
    while (n_received < n_msg) {
      cv.wait(lock);
    }
  }
  uint64_t stop = usectime();
  uint64_t elapsed = stop - start;
  sub_proto->shutdown();
  pm.shutdown();
  printf("Messages received: %zu\n", n_received);
  printf("Elapsed: %zu [us]\n", elapsed);
  printf("Average latency: %.3f [us]\n", (double) elapsed / n_received);
}
