# RedisClone

A key value store written in C++20 with *current* support for strings, lists, and TTL expiry in addition to leader-follower replication. I am building this project to learn more about and practice writing modern C++ while also thinking about performance and how my code interacts with hardware.


## Architecture

Currently in the main branch, this server is driven by a multithreaded edge-triggered epoll loop, configurable at startup via command line args. These listener threads both accept new clients and handle commands from clients; most of which access the shared key value store, which is sharded into a configurable (16 by default) number of independent hash maps that each own their own mutex, which is acquired by any thread reading or writing to the shard's hash map.

Client Commands -> epoll_wait -> Command Parser -> Router -> Storage methods -> send(client_fd)

*Note:* If replicas are attached, RESP write commands are written to each replica's command buffer, and a separate thread periodically flushes every buffer to its replica

## Build
This compiles the project as RelWithDebugInfo. I'll add other presets later

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

## Run

```sh
./build/RedisClone
```

Optional:
-t \<threads>                           How many threads listen to epoll event loop (default 8)
-p \<port>                              Which port this server listens on
--replica <leader_ip> <leader_port>     Run this server as a replica and replicate the leader server at given ipv4 address and port

## Benchmarks

See ./bench/bench.md for benchmark reports on various changes/attempted optimizations

## Project Layout

```
src/      # implementation
bench/    # benchmarking scripts and results
```
