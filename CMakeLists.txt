cmake_minimum_required(VERSION 2.8.3)
project(ros2_comm)

find_package(catkin REQUIRED COMPONENTS cpp_common rosconsole roscpp_serialization)

## System dependencies are found with CMake's conventions
find_package(Boost REQUIRED COMPONENTS system signals)

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
include_directories(${catkin_INCLUDE_DIRS} ${Boost_INCLUDE_DIR})

## Declare a cpp library
# add_library(ros2_comm
#   src/${PROJECT_NAME}/ros2_comm.cpp
# )

add_library(ros2_comm src/core.cpp src/io.cpp src/poll_set.cpp 
  src/poll_manager.cpp src/common.cpp src/transport_tcp.cpp src/header.cpp)
target_link_libraries(ros2_comm ${catkin_LIBRARIES} ${Boost_LIBRARIES})

# Declare a cpp executable
add_executable(simple_sub src/simple_sub.cpp)
add_executable(simple_pub src/simple_pub.cpp)
target_link_libraries(simple_sub ros2_comm)
target_link_libraries(simple_pub ros2_comm)

## Add cmake target dependencies of the executable/library
## as an example, message headers may need to be generated before nodes
# add_dependencies(ros2_comm_node ros2_comm_generate_messages_cpp)

## Specify libraries to link a library or executable target against
# target_link_libraries(ros2_comm_node
#   ${catkin_LIBRARIES}
# )

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
