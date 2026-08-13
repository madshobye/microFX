cmake_minimum_required(VERSION 3.16)
project(microfx-peer-bridge LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CJSON REQUIRED libcjson)
pkg_check_modules(LWS REQUIRED libwebsockets)

add_library(microfx-project-protocol STATIC src/project_protocol.cpp)
target_compile_features(microfx-project-protocol PUBLIC cxx_std_17)
target_compile_options(microfx-project-protocol PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(microfx-project-protocol PUBLIC src ${CJSON_INCLUDE_DIRS})
target_link_directories(microfx-project-protocol PRIVATE ${CJSON_LIBRARY_DIRS})
target_link_libraries(microfx-project-protocol PUBLIC ${CJSON_LIBRARIES})

add_executable(microfx-peer-bridge src/main.cpp)
target_compile_features(microfx-peer-bridge PRIVATE cxx_std_17)
target_compile_options(microfx-peer-bridge PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(microfx-peer-bridge PRIVATE
  ${CJSON_INCLUDE_DIRS}
  ${LWS_INCLUDE_DIRS}
  ${LIBPEER_INCLUDE_DIR}
)
target_link_directories(microfx-peer-bridge PRIVATE ${CJSON_LIBRARY_DIRS} ${LWS_LIBRARY_DIRS})
target_link_libraries(microfx-peer-bridge PRIVATE microfx-project-protocol peer ${LWS_LIBRARIES} pthread)

option(MICROFX_BUILD_PROTOCOL_TESTS "Build project protocol host tests" OFF)
if(MICROFX_BUILD_PROTOCOL_TESTS)
  enable_testing()
  add_executable(microfx-project-protocol-test tests/project_protocol_test.cpp)
  target_link_libraries(microfx-project-protocol-test PRIVATE microfx-project-protocol)
  add_test(NAME project-protocol COMMAND microfx-project-protocol-test)
endif()

install(TARGETS microfx-peer-bridge RUNTIME DESTINATION bin)
