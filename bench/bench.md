### Method
**Ensure**
`sudo cpupower frequency-set -g performance`

Initially tried `sudo cpupower idle-set -D 0` but this hurt latency and throughput:
Disabling C-states prevents the CPU from idling between requests, which exhausts the power budget that Turbo Boost relies on for frequency spikes. The result is lower sustained clock frequency and potential thermal throttling — the opposite of the intended effect for a heavily loaded benchmark.


**rate-sweep** — fixes 80 connections, sweeps from 25k->300k RPS. This is your latency-vs-load curve. When P99 starts climbing sharply while actual RPS flatlines below target, that's saturation. Uses --rate-limiting to avoid coordinated-omission, leading to true latency numbers: wanted-to-send time to actual response time.
**saturation** — unlimited rate, sweeps connection count 4→200. Throughput grows until the bottleneck (lock, single core, etc.) caps it. The plateau is your ceiling for that model. Ignore latency due to coordinated omission and varying client counts.

*note:* my ubuntu machine has 8 performance cores, each w/ 2 HyperThreading cores for 16 logical CPU units. Each pair shares physical resources (L1 + L2 caches, TLB, branch predictor) so I need to pin each command to a single physical core to prevent physical resource contention.
*pin server to core 0/1 first*
`taskset -c 0, 1 ./build/RedisClone &`

*find saturation point*
`taskset -c 2,4,6,8 bench/benchmark.sh saturation single-thread`

*rate sweep*
`taskset -c 2,4,6,8 bench/benchmark.sh rate-sweep single-thread`

### Numbers

**Single-threaded; using connMap lookups no lock_guards**
commit: 09b67dcb64a56591ece4320c8ae68d48ec374054

Target RPS      Actual RPS      P50 (ms)        P99 (ms)       
----------      ----------      --------        --------       
25000           24641.12        0.22300         0.53500        
50000           49923.69        0.29500         0.55100        
100000          99999.39        0.29500         0.53500        
200000          199992.54       0.30300         0.57500        
250000          246734.10       0.31100         0.43900        
300000          260359.26       0.31100         0.33500


**Global lock - 4 threads core pinned**
Config               Target RPS      Actual RPS      P50 (ms)        P99 (ms)       
-------------------- ----------      ----------      --------        --------             
4threads             100000          99998.74        0.08700         0.15900        
4threads             200000          199987.11       0.08700         0.17500        
4threads             300000          299966.21       0.08700         0.21500        
4threads             500000          499456.73       0.08700         0.24700        
4threads             700000          642911.41       0.08700         0.26300        
4threads             750000          675901.12       0.08700         0.25500


**Global lock - 8 threads**
taskset -c 0-7 ./build/RedisClone 
taskset -c 8,10,12,14 ./benchmark.sh rate-sweep 8threads-4cores
Config               Target RPS      Actual RPS      P50 (ms)        P99 (ms)       
-------------------- ----------      ----------      --------        --------              
8threads-4cores      100000          100013.84       0.08700         0.19900        
8threads-4cores      200000          200005.09       0.08700         0.16700        
8threads-4cores      300000          300008.29       0.08700         0.15900        
8threads-4cores      500000          499097.76       0.08700         0.15900        
8threads-4cores      700000          680093.62       0.08700         0.15100        
8threads-4cores      750000          664099.41       0.11100         0.20700


**Sharded locks - 8 threads, 16 shards**
taskset -c 0-7 ./build/RedisClone 
taskset -c 8,10,12,14 ./benchmark.sh rate-sweep 8threads-4cores
Config               Target RPS      Actual RPS      P50 (ms)        P99 (ms)       
-------------------- ----------      ----------      --------        --------       
8threads-4cores      100000          100014.15       0.08700         0.18300        
8threads-4cores      200000          200006.40       0.08700         0.16700        
8threads-4cores      300000          299979.84       0.08700         0.15900        
8threads-4cores      500000          499672.26       0.08700         0.15900        
8threads-4cores      700000          679319.36       0.08700         0.15900        
8threads-4cores      750000          678765.62       0.08700         0.19900


**Spinlock atomic flag - 8 threads, 16 shards**
Config               Target RPS      Actual RPS      P50 (ms)        P99 (ms)       
-------------------- ----------      ----------      --------        --------       
8threads-spinlock    100000          100004.00       0.08700         0.18300        
8threads-spinlock    200000          200007.87       0.08700         0.15900        
8threads-spinlock    300000          299977.26       0.08700         0.15900        
8threads-spinlock    500000          499432.24       0.08700         0.15100        
8threads-spinlock    700000          686082.78       0.08700         0.15100        
8threads-spinlock    750000          694080.30       0.08700         0.18300


**Spinlock - 16 threads, 16 shards**
Spinlock is eating away at CPU resources when one thread holds it for prolonged period, slowing execution of thread doing processing
Config               Target RPS      Actual RPS      P50 (ms)        P99 (ms)       
-------------------- ----------      ----------      --------        --------       
8threads-spinlock    100000          99980.90        0.09500         3.45500        
8threads-spinlock    200000          199562.73       0.08700         3.48700        
8threads-spinlock    300000          295515.49       0.09500         3.32700        
8threads-spinlock    500000          410915.76       0.09500         3.45500        
8threads-spinlock    700000          431231.84       0.09500         3.51900        
8threads-spinlock    750000          414914.37       0.09500         3.56700


**Mutex - 16 threads, 16 shards**
Spiked p99 latency has gone away now that processing threads can have full resources while competing threads sleep trying to acquire lock
Config               Target RPS      Actual RPS      P50 (ms)        P99 (ms)       
-------------------- ----------      ----------      --------        --------       
16threads-mutex      100000          100011.03       0.08700         0.27900        
16threads-mutex      200000          199992.09       0.08700         0.24700        
16threads-mutex      300000          299988.93       0.08700         0.23100        
16threads-mutex      500000          498803.89       0.08700         0.21500        
16threads-mutex      700000          696076.57       0.08700         0.21500        
16threads-mutex      750000          689217.18       0.10300         0.24700


**Optimization: TCP_NODELAY**
I think this will keep the number of send syscalls appx the same as well as throughput but reduce latency since packets are sent immediately from the kernel buffer, and the send syscall, which writes packets to the kernel buffer, will not write the packets any faster or spend any less time in the syscall, only the OS will send them faster once the bytes are written.

*Before*
taskset -c 0-7 ./build/RedisClone
taskset -c 8,10,12,14 memtier -s localhost -p 6379 -t 4 -c 20 --test-time=30s --ratio=1:1 --data-size=32 --key-pattern=R:R --hide-histogram --rate-limiting=30000
sudo strace -c -p $(pgrep RedisClone) -- sleep 15
% time     seconds  usecs/call     calls    errors syscall
 32.37   23.227157          27    858260    429133 recvfrom
 25.88   18.568373          67    276515           epoll_wait
 23.31   16.722769          38    429127           sendto
 18.13   13.008647          30    429134           epoll_ctl

taskset -c 8,10,12,14 ./benchmark.sh rate-sweep 8threads-sharded
Config               Target RPS      Actual RPS      P50 (ms)        P99 (ms)       
-------------------- ----------      ----------      --------        --------       
8threads-sharded     100000          100008.11       0.09500         0.18300        
8threads-sharded     200000          199982.44       0.09500         0.16700        
8threads-sharded     300000          299965.42       0.09500         0.15900        
8threads-sharded     500000          498861.03       0.09500         0.15900        
8threads-sharded     700000          661625.39       0.09500         0.15900        
8threads-sharded     750000          674602.86       0.09500         0.16700

On the above benchmark.sh run:
sudo perf record -F 999 -g -p $(pgrep RedisClone) -- sleep 15
*sys_sendto:* 32.96% sampling share

*After*
strace (as above):
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 32.43   25.918397          27    945193    472598 recvfrom
 25.81   20.627696          67    304951           epoll_wait
 23.24   18.575740          39    472595           sendto
 18.19   14.537242          30    472599           epoll_ctl

benchmark.sh:
Config               Target RPS      Actual RPS      P50 (ms)        P99 (ms)       
-------------------- ----------      ----------      --------        --------       
8threads-sharded     100000          99999.41        0.09500         0.15900        
8threads-sharded     200000          199978.73       0.09500         0.15900        
8threads-sharded     300000          299999.64       0.09500         0.15900        
8threads-sharded     500000          497364.55       0.09500         0.15900        
8threads-sharded     700000          651587.87       0.09500         0.15900        
8threads-sharded     750000          661955.91       0.09500         0.16700

*sys_sendto:* 32.72% sampling share

**Conclusion**
While throughput and send syscall share did remain effectively constant, p99 (and p50) latency remained near unchanged as well. This could be due to Redis's workload pattern where packets are usually only send after the client already acknowledges the previous packet, which makes the kernel flush its buffer immediately anyways if it has no packets to buffer during unacknowledge packets being in flight.


**Benchmark: replication**
Arena is size 65536

50k keys
    Replica    Sync (ms)    Keys/s      
    -------    ---------    ------      
    0          64ms         769230/s    
    1          64ms         769230/s    
    2          64ms         769230/s    
    3          64ms         769230/s

500k keys
    Replica    Sync (ms)    Keys/s      
    -------    ---------    ------      
    0          914ms        546448/s    
    1          970ms        514933/s    
    2          969ms        515463/s    
    3          970ms        514933/s 