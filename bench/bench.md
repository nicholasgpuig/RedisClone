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

