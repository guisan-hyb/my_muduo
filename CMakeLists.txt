cmake_minimum_required(VERSION 3.16)

project(my_muduo VERSION 1.0.0 LANGUAGES CXX)

if(NOT UNIX)
    message(FATAL_ERROR "my_muduo currently supports Linux/Unix only")
endif()

find_package(Threads REQUIRED)

set(MYMUDUO_BASE_SOURCES
    src/base/CurrentThread.cc
    src/base/Logger.cc
    src/base/Thread.cc
    src/base/Timestamp.cc
)

set(MYMUDUO_NET_SOURCES
    src/net/Acceptor.cc
    src/net/Buffer.cc
    src/net/Channel.cc
    src/net/DefaultPoller.cc
    src/net/EPollPoller.cc
    src/net/EventLoop.cc
    src/net/EventLoopThread.cc
    src/net/EventLoopThreadPool.cc
    src/net/InetAddress.cc
    src/net/Poller.cc
    src/net/Socket.cc
    src/net/TcpConnection.cc
    src/net/TcpServer.cc
)

add_library(my_muduo SHARED
    ${MYMUDUO_BASE_SOURCES}
    ${MYMUDUO_NET_SOURCES}
)
add_library(my_muduo::my_muduo ALIAS my_muduo)

set_target_properties(my_muduo PROPERTIES
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_compile_features(my_muduo PUBLIC cxx_std_17)
target_link_libraries(my_muduo PUBLIC Threads::Threads)

target_include_directories(my_muduo
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(my_muduo PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(MYMUDUO_BUILD_EXAMPLES "Build my_muduo examples" ON)
if(MYMUDUO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

include(GNUInstallDirs)
install(TARGETS my_muduo
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

