#include <string>

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

#include "poll_manager.h"
#include "pubsub_tcp.hpp"
#include "pubsub_zmq.hpp"
#include "registration.hpp"
#include "topic_manager.hpp"

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
  if (argc != 2) {
    fprintf(stderr, "usage: simple_sub n_msgs\n");
    return 1;
  }
  n_msg = atoi(argv[1]);

  PollManager pm;
  pm.start();

  // StaticRegistration static_res;
  // static_res.addPub("/data", "tcpros://127.0.0.1:" + string(argv[2]));
  MasterRegistration master_res(NodeAddress("127.0.0.1", 11311), &pm.getPollSet());
  RegistrationProtocol *reg = &master_res;
  TopicManager tm(reg, &pm.getPollSet());
  tm.init(0);
  master_res.myURI(tm.myURI());

  ZMQSubFactory sub_factory_zmq;
  tm.addSubscribeTransport(&sub_factory_zmq);

  TCPSubFactory sub_factory_tcp(&pm.getPollSet());
  tm.addSubscribeTransport(&sub_factory_tcp);

  tm.subscribe("/foo", callback);
  // tm.setRemap("/foo", "/data");

  uint64_t start = usectime();
  {
    boost::mutex::scoped_lock lock(mutex);
    while (n_received < n_msg) {
      cv.wait(lock);
    }
  }
  uint64_t stop = usectime();
  uint64_t elapsed = stop - start;
  tm.shutdown();
  pm.shutdown();

  printf("Messages received: %zu\n", n_received);
  printf("Elapsed: %zu [us]\n", elapsed);
  printf("Average latency: %.3f [us]\n", (double) elapsed / n_received);
}
