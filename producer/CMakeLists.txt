cmake_minimum_required(VERSION 3.16)
project(v4l2_demo_producer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build the producer/tests GoogleTest suite" ON)

find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZMQ REQUIRED libzmq)

include(FetchContent)

# cppzmq (design.md §2: "header-only C++ binding ... fetched via CMake
# FetchContent"). Only the header is used — cppzmq's own CMakeLists.txt is
# bypassed so it doesn't need to (re)discover ZeroMQ itself; libzmq is
# already located above via pkg-config.
FetchContent_Declare(
  cppzmq
  GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
  GIT_TAG v4.10.0
)
FetchContent_GetProperties(cppzmq)
if(NOT cppzmq_POPULATED)
  FetchContent_Populate(cppzmq)
endif()
add_library(cppzmq INTERFACE)
target_include_directories(cppzmq INTERFACE ${cppzmq_SOURCE_DIR})
target_link_libraries(cppzmq INTERFACE ${LIBZMQ_LIBRARIES})
target_include_directories(cppzmq INTERFACE ${LIBZMQ_INCLUDE_DIRS})

# proto/telemetry.proto is the single source of truth for the wire format
# (requirements.md §4) and lives one level above producer/. Generated
# explicitly (rather than via protobuf_generate_cpp's path-inference, which
# is unreliable for a .proto outside the current source tree) into a known
# directory under the build tree; neither generated file is checked in
# (design.md §8).
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../proto)
set(PROTO_FILE ${PROTO_DIR}/telemetry.proto)
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})
set(PROTO_SRCS ${PROTO_GEN_DIR}/telemetry.pb.cc)
set(PROTO_HDRS ${PROTO_GEN_DIR}/telemetry.pb.h)

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
  COMMAND protobuf::protoc
  ARGS --cpp_out=${PROTO_GEN_DIR} -I ${PROTO_DIR} ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
  COMMENT "Generating C++ protobuf bindings from telemetry.proto"
  VERBATIM
)

add_library(producer_core STATIC
  src/synthetic_v4l2_camera.cpp
  src/telemetry.cpp
  ${PROTO_SRCS}
  ${PROTO_HDRS}
)
target_include_directories(producer_core PUBLIC
  include
  ${PROTO_GEN_DIR}
  ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(producer_core PUBLIC
  ${Protobuf_LIBRARIES}
  cppzmq
  Threads::Threads
)

add_executable(v4l2_producer src/main.cpp)
target_link_libraries(v4l2_producer PRIVATE producer_core)

if(BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()

  add_executable(producer_tests
    tests/synthetic_v4l2_camera_test.cpp
    tests/bounded_drop_oldest_queue_test.cpp
    tests/running_stats_test.cpp
    tests/telemetry_test.cpp
  )
  target_link_libraries(producer_tests PRIVATE
    producer_core
    GTest::gtest_main
  )

  include(GoogleTest)
  gtest_discover_tests(producer_tests)
endif()
