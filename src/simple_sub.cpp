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
  Subscription sub;
  sub.registerCallback(callback);

  // tcp
  TCPSubscribe sub_tcp;
  sub.addProtocol(&sub_tcp);

  // zmq
  zmq::context_t ctx(1);
  SubscribeZMQ sub_zmq(&ctx);
  sub.addProtocol(&sub_zmq);

  // Startup tcp + zmq
  string host = "localhost";
  sub_tcp.start(boost::shared_ptr<TransportTCP>(new TransportTCP(&pm.getPollSet())), host, 50000);
  sub_zmq.start("tcp://localhost:60000");

  pm.start();
  uint64_t start = usectime();
  {
    boost::mutex::scoped_lock lock(mutex);
    while (n_received < n_msg) {
      cv.wait(lock);
    }
  }
  uint64_t stop = usectime();
  uint64_t elapsed = stop - start;
  sub_tcp.shutdown();
  sub_zmq.stop();
  pm.shutdown();

  printf("Messages received: %zu\n", n_received);
  printf("Elapsed: %zu [us]\n", elapsed);
  printf("Average latency: %.3f [us]\n", (double) elapsed / n_received);
}
