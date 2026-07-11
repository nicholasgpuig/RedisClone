#!/bin/bash
# Usage:
#   ./benchmark.sh rate-sweep <label>   # latency vs load curve
#   ./benchmark.sh saturation <label>   # find max throughput
#
# Label tags results (e.g. "single-thread", "global-lock", "sharded")
# Results appended to bench/results.csv
#
# Recommended setup before running (reduces scheduler noise):
#   sudo cpupower frequency-set -g performance
#   sudo cpupower idle-set -D 0
#
# Core pinning (optional — scheduler is usually fine on idle machines):
#   i7-12700 has HT pairs (0,1)(2,3)...(14,15) + E-cores (16-19).
#   If pinning, own full physical cores to avoid sibling interference:
#     taskset -c 0,1 ./build/RedisClone &          # server owns P-core 0
#     taskset -c 2,4,6,8 bash bench/benchmark.sh   # 4 threads → 4 P-cores

set -euo pipefail

MODE="${1:-rate-sweep}"
LABEL="${2:-unnamed}"

HOST=127.0.0.1
PORT=6379
THREADS=4
CLIENTS=20          # per thread → 80 total connections
DURATION=20         # seconds per run
DATA_SIZE=32        # bytes per value
RESULTS_CSV="$(dirname "$0")/results.csv"

# ---------------------------------------------------------------------------

die() { echo "ERROR: $*" >&2; exit 1; }

check_server() {
    redis-cli -h "$HOST" -p "$PORT" ping &>/dev/null \
        || die "Server not responding on $HOST:$PORT — start it first"
}

# Run memtier and extract: actual_rps p50_ms p99_ms avg_ms
# args: rate_limit (0=unlimited) [clients_per_thread (default: $CLIENTS)]
run_memtier() {
    local rate_limit=$1
    local clients=${2:-$CLIENTS}
    local rate_flag=""
    if [ "$rate_limit" -gt 0 ]; then
        rate_flag="--rate-limiting=$rate_limit"
    fi

    local raw
    raw=$(memtier_benchmark \
        -s "$HOST" -p "$PORT" \
        -t "$THREADS" -c "$clients" \
        --test-time="$DURATION" \
        --ratio=1:1 \
        --data-size="$DATA_SIZE" \
        --key-pattern=R:R \
        --hide-histogram \
        $rate_flag \
        2>&1)

    # Parse the Totals line — columns: Type Ops/sec Hits/sec Misses/sec Avg.Lat p50 p99 p99.9 KB/sec
    local totals
    totals=$(echo "$raw" | grep "^Totals" | tail -1)
    [ -z "$totals" ] && { echo "$raw" >&2; die "memtier produced no Totals line"; }

    echo "$totals" | awk '{printf "%s %s %s %s", $2, $6, $7, $5}'
}

print_header() {
    printf "\n%-20s %-15s %-15s %-15s %-15s\n" \
        "Config" "Target RPS" "Actual RPS" "P50 (ms)" "P99 (ms)"
    printf "%-20s %-15s %-15s %-15s %-15s\n" \
        "--------------------" "----------" "----------" "--------" "--------"
}

emit_csv_header() {
    if [ ! -f "$RESULTS_CSV" ]; then
        echo "timestamp,label,mode,target_rps,actual_rps,p50_ms,p99_ms,avg_ms" > "$RESULTS_CSV"
    fi
}

emit_row() {
    local target_label=$1 target_rps=$2 actual_rps=$3 p50=$4 p99=$5 avg=$6
    printf "%-20s %-15s %-15s %-15s %-15s\n" \
        "$LABEL" "$target_label" "$actual_rps" "$p50" "$p99"
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ),$LABEL,$MODE,$target_rps,$actual_rps,$p50,$p99,$avg" \
        >> "$RESULTS_CSV"
}

# ---------------------------------------------------------------------------
# MODE: rate-sweep
# Fixes concurrency at THREADS*CLIENTS, varies per-connection rate limit.
# Shows how latency degrades as you approach saturation.
# ---------------------------------------------------------------------------
mode_rate_sweep() {
    local total_conns=$((THREADS * CLIENTS))

    # Target total RPS values (0 = unlimited)
    local targets=(25000 50000 100000 200000 250000 300000)

    print_header
    emit_csv_header

    for target in "${targets[@]}"; do
        local rate_per_conn=0
        local target_label
        if [ "$target" -eq 0 ]; then
            target_label="unlimited"
            rate_per_conn=0
        else
            target_label="$target"
            # ceiling division: rate per connection
            rate_per_conn=$(( (target + total_conns - 1) / total_conns ))
        fi

        read -r actual p50 p99 avg <<< "$(run_memtier "$rate_per_conn")"
        emit_row "$target_label" "$target" "$actual" "$p50" "$p99" "$avg"
    done
}

# ---------------------------------------------------------------------------
# MODE: saturation
# Unlimited rate, sweeps client count to find throughput ceiling.
# When actual RPS stops growing, you've hit the bottleneck.
# ---------------------------------------------------------------------------
mode_saturation() {
    local client_counts=(1 2 4 8 16 32 50)

    print_header
    emit_csv_header

    for c in "${client_counts[@]}"; do
        read -r actual p50 p99 avg <<< "$(run_memtier 0 "$c")"
        local total_clients=$((THREADS * c))
        emit_row "${total_clients}conns" "$total_clients" "$actual" "$p50" "$p99" "$avg"
    done
}

# ---------------------------------------------------------------------------

check_server

echo "==> Mode: $MODE | Label: $LABEL | ${THREADS}t x ${CLIENTS}c | ${DURATION}s per run"
echo "    Results appended to: $RESULTS_CSV"

case "$MODE" in
    rate-sweep)  mode_rate_sweep ;;
    saturation)  mode_saturation ;;
    *)           die "Unknown mode '$MODE'. Use: rate-sweep | saturation" ;;
esac

echo ""
echo "Done. Full results in $RESULTS_CSV"
