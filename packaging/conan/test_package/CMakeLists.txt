cmake_minimum_required(VERSION 3.20)
project(test_signet_forge CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(signet_forge REQUIRED)

add_executable(test_signet_forge test_package.cpp)
target_link_libraries(test_signet_forge PRIVATE signet::forge)
