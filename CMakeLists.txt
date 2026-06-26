cmake_minimum_required(VERSION 3.10.0)
project(RedisClone)

add_executable(RedisClone main.cpp Socket.cpp)

set_property(TARGET RedisClone PROPERTY CXX_STANDARD 20)
set_property(TARGET RedisClone PROPERTY CXX_STANDARD_REQUIRED ON)