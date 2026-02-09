#This is the main CMAKE for build bin
cmake_minimum_required(VERSION 3.16.5)
project(libdtoe_api)

#定义一个变量保存源码目录
set(PROJECT_SRC_DIR ${PROJECT_SOURCE_DIR})
#include_directories()

set(WANING_FLAGS "-Wall -Wfloat-equal -Wformat-2")
file (GLOB SRC_FILES ${PROJECT_SRC_DIR}/*.c)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fstack-protector-strong -Werror -Wall -fPIC -ftrapv -O2  -finline-functions")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fstack-protector-strong -Werror -Wall -fPIC -ftrapv -O2  -finline-functions")

#设置头文件和库安装到/usr
set (CMAKE_INSTALL_PREFIX "/usr")

add_library(kbdtoe SHARED ${SRC_FILES})
target_link_directories(kbdtoe PRIVATE ${PROJECT_SRC_DIR}/lib)
target_link_libraries(kbdtoe PUBLIC -lknet_frame)

install(FILES  ${PROJECT_SRC_DIR}/kbdtoe.h DESTINATION include)
install(TARGETS kbdtoe
      LIBRARY DESTINATION lib64
)