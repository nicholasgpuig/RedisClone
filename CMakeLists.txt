cmake_minimum_required(VERSION 3.10.0)
project(RedisClone)

add_executable(RedisClone main.cpp Socket.cpp handle.cpp Router.cpp)

set_property(TARGET RedisClone PROPERTY CXX_STANDARD 20)
set_property(TARGET RedisClone PROPERTY CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)
target_link_libraries(RedisClone PRIVATE spdlog)