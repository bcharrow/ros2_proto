cmake_minimum_required(VERSION 2.8.3)
project(ros2_comm)

set(CMAKE_BUILD_TYPE RelWithDebInfo)

find_package(catkin REQUIRED COMPONENTS cpp_common rosconsole roscpp_serialization message_generation)

## System dependencies are found with CMake's conventions
find_package(Boost REQUIRED COMPONENTS system signals)

find_package(PkgConfig REQUIRED)
pkg_search_module(zmq REQUIRED libzmq)

add_service_files(FILES TopicRequest.srv)
add_message_files(FILES TCPOptions.msg)
generate_messages()

catkin_package(
#  INCLUDE_DIRS include
#  LIBRARIES ros2_comm
 CATKIN_DEPENDS cpp_common rosconsole roscpp_serialization
#  DEPENDS system_lib
)

###########
## Build ##
###########

## Specify additional locations of header files
## Your package locations should be listed before other locations
# include_directories(include ${catkin_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
include_directories(${catkin_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})

add_library(ros2_comm src/core.cpp src/io.cpp src/poll_set.cpp
  src/poll_manager.cpp src/common.cpp src/transport_tcp.cpp src/header.cpp)
target_link_libraries(ros2_comm ${catkin_LIBRARIES} ${Boost_LIBRARIES} ${zmq_LIBRARIES} "snappy")

# Basic pub/sub
add_executable(simple_sub src/simple_sub.cpp)
add_executable(simple_pub src/simple_pub.cpp)
target_link_libraries(simple_sub ros2_comm)
target_link_libraries(simple_pub ros2_comm)

# Pub/sub with registration
add_executable(register_sub src/register_sub.cpp)
add_executable(register_pub src/register_pub.cpp)
target_link_libraries(register_sub ros2_comm)
target_link_libraries(register_pub ros2_comm)

#############
## Install ##
#############

# all install targets should use catkin DESTINATION variables
# See http://ros.org/doc/groovy/api/catkin/html/adv_user_guide/variables.html

## Mark executable scripts (Python etc.) for installation
## in contrast to setup.py, you can choose the destination
# install(PROGRAMS
#   scripts/my_python_script
#   DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
# )

## Mark executables and/or libraries for installation
# install(TARGETS ros2_comm ros2_comm_node
#   ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
#   LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
#   RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
# )

## Mark cpp header files for installation
# install(DIRECTORY include/${PROJECT_NAME}/
#   DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
#   FILES_MATCHING PATTERN "*.h"
#   PATTERN ".svn" EXCLUDE
# )

## Mark other files for installation (e.g. launch and bag files, etc.)
# install(FILES
#   # myfile1
#   # myfile2
#   DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
# )

#############
## Testing ##
#############

## Add gtest based cpp test target and link libraries
# catkin_add_gtest(${PROJECT_NAME}-test test/test_ros2_comm.cpp)
# if(TARGET ${PROJECT_NAME}-test)
#   target_link_libraries(${PROJECT_NAME}-test ${PROJECT_NAME})
# endif()

## Add folders to be run by python nosetests
# catkin_add_nosetests(test)
