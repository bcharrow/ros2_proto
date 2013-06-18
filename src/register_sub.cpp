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
  if (argc < 2) {
    fprintf(stderr, "usage: simple_sub n_msgs <protocols>\n");
    return 1;
  }
  n_msg = atoi(argv[1]);

  vector<string> preferred_protocols;
  for (int i = 2; i < argc; ++i) {
    preferred_protocols.push_back(argv[i]);
  }
  
  PollManager pm;
  pm.start();
  Subscription sub;
  sub.registerCallback(callback);

  TopicManager tm;
  ros2_comm::TopicRequest req_rep;
  req_rep.request.topic = "/data";
  req_rep.request.method = "requestTopic";  
  boost::shared_ptr<TransportTCP> remote_service(new TransportTCP(&pm.getPollSet()));
  remote_service->connect("127.0.0.1", 5555);
  tm.requestTopic(remote_service, &req_rep);
  ROS_INFO_STREAM("Request / response:\n" << req_rep.request << endl << req_rep.response);
  remote_service->close();

  zmq::context_t ctx(1);
  TCPSubscribe sub_tcp;  
  SubscribeZMQ sub_zmq(&ctx);
      
  const char *foo[] = {"foo", "bar", "baz"};
  map<string, string> protos;
  for (int i = 0; i < req_rep.response.protocols.size(); ++i) {
    protos[req_rep.response.protocols[i]] = req_rep.response.endpoints[i];
  }
      
  bool something = false;
  for (int j = 0; j < preferred_protocols.size(); ++j) {
    const string &protocol = preferred_protocols[j];
    map<string, string>::const_iterator it = protos.find(protocol);
    if (it == protos.end()) {
      continue;
    }
    const string &endpoint = it->second;

    if (protocol == sub_tcp.protocol()) {
      ROS_INFO("Using %s", protocol.c_str());
      sub_tcp.start(TransportTCPPtr(new TransportTCP(&pm.getPollSet())), "localhost",
                    atoi(endpoint.c_str()));
      sub.addProtocol(&sub_tcp); 
      something = true;
      break;
    } else if (protocol == sub_zmq.protocol()) {
      ROS_INFO("Using %s", protocol.c_str());      
      sub.addProtocol(&sub_zmq);
      sub_zmq.start(endpoint.c_str());
      something = true;
      break;
    } else {
      ROS_WARN("Unrecognized protocol: %s", protocol.c_str());
    }
  }
  if (!something) {
    ROS_WARN("No match between publisher's supported protocols and subscribers desired protocols");
    return 1;
  }

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
