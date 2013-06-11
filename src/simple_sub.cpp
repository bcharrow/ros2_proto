// #include <ros/ros.h>

#include "core.hpp"

using namespace ros2;

void callback(const Message &msg) {
  ROS_INFO("Got msg with %i bytes", msg.size());
}

int main(int argc, char **argv) {
  ROS_INFO("Hi");
}
