#include "service.hpp"
#include "pubsub.hpp"
#include "poll_manager.h"

#include <ros2_comm/SetRemappings.h>

using namespace ros2;
using namespace std;

volatile bool done = false;

void callback(const ros2_comm::SetRemappings &req_resp) {
  ROS_INFO("Done!");
  done = true;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    ROS_ERROR("usage: call_remap node_uri find replace");
    ROS_BREAK();
  }
  string node_uri = argv[1];
  string find = argv[2];
  string replace = argv[3];

  PollManager pm;
  pm.start();
 
  string addr = node_uri.substr(0, node_uri.find(":"));
  int port = atoi(node_uri.substr(node_uri.find(":") + 1).c_str());
  TransportTCPPtr tcp(new TransportTCP(&pm.getPollSet()));
  tcp->connect(addr, port);

  ros2_comm::SetRemappings remaps;
  remaps.request.find.push_back(find);
  remaps.request.replace.push_back(replace);
  
  MultiServiceClient msc;
  msc.call(tcp, "setRemappings", remaps, boost::bind(callback, _1));

  while (!done) {
    sleep(1);
  }
}
