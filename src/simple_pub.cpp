// #include <ros/ros.h>

#include "core.hpp"

#include "transport_tcp.h"
#include "poll_manager.h"

using namespace ros2;

int main(int argc, char **argv) {
  PollManager pm;
  
  TransportTCP trans_tcp(&pm.getPollSet());
  TCPPublish pub_tcp(NULL);

  Publication publication;
  publication.registerProtocol(&pub_tcp);

  Message msg(100);
  uint8_t *bytes = msg.bytes();
  for (int i = 0; i < msg.size(); ++i) {
    bytes[i] = 0;
  }
  
  for (int i = 0; i < 100; ++i) {
    ROS_INFO("Publishing");
    publication.publish(msg);
    usleep(1000 * 500);
  }
  
}
