#This is the main CMAKE for build bin
cmake_minimum_required(VERSION 3.16.5)
project(libdtoe_api)
include(CTest)

#定义一个变量保存源码目录
set(PROJECT_SRC_DIR ${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SRC_DIR}/include)
set(CMAKE_SKIP_RPATH TRUE)
set(WANING_FLAGS "-Wall -Wfloat-equal -Wformat-2")
file (GLOB SRC_FILES ${PROJECT_SRC_DIR}/src/*.c)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fstack-protector-strong -Werror -Wall -fPIC -ftrapv -O2  -finline-functions")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fstack-protector-strong -Werror -Wall -fPIC -ftrapv -O2  -finline-functions")

#设置头文件和库安装到/usr
set (CMAKE_INSTALL_PREFIX "/usr")

add_library(kbdtoe SHARED ${SRC_FILES})
target_link_directories(kbdtoe PRIVATE ${PROJECT_SRC_DIR}/lib)
target_link_libraries(kbdtoe PUBLIC -ldtoe)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_options(kbdtoe PUBLIC -Wl,-z,relro,-z,now,-s)
endif()

install(FILES  ${PROJECT_SRC_DIR}/include/kbdtoe.h DESTINATION include)
install(TARGETS kbdtoe
      LIBRARY DESTINATION lib64
)

install(FILES conf/kbdtoe_rsyslog.conf DESTINATION /etc/rsyslog.d RENAME kbdtoe.conf)
install(FILES conf/logrotate/kbdtoe DESTINATION /etc/logrotate.d)

if(BUILD_TESTING)
    add_executable(kbdtoe_log_test tests/kbdtoe_log_test.c)
    target_include_directories(kbdtoe_log_test PRIVATE
        ${PROJECT_SRC_DIR}/src
        ${PROJECT_SRC_DIR}/include
    )
    target_link_libraries(kbdtoe_log_test PRIVATE kbdtoe)
    add_test(NAME kbdtoe_log_test COMMAND kbdtoe_log_test)
endif()
