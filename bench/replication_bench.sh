#!/bin/bash
# Profiles full-sync replication time: N keys → M replicas
#
# Usage:
#   ./bench/replication_bench.sh                      # default: sweep key counts, 1 replica
#   ./bench/replication_bench.sh sweep 1              # sweep N_KEYS, M replicas
#   ./bench/replication_bench.sh single 100000 2      # 100k keys, 2 replicas
#
# Timing: measures from replica process start → master logs "Full sync done replica N"
# (includes ~50-100ms startup overhead; negligible for large key counts)
#
# Requires: python3, redis-cli, ./build/RedisClone

set -euo pipefail

MODE="${1:-sweep}"
MASTER_PORT=6379
BASE_REPLICA_PORT=6380
DATA_SIZE="${DATA_SIZE:-32}"
TIMEOUT_S=120
BINARY="./build/RedisClone"
RESULTS_CSV="$(dirname "$0")/replication_results.csv"

PIDS=()
TMPFILES=()

# ---------------------------------------------------------------------------

die()     { echo "ERROR: $*" >&2; exit 1; }
log()     { echo "==> $*"; }
sublog()  { echo "    $*"; }

cleanup() {
    for pid in "${PIDS[@]:-}"; do
        kill "$pid" 2>/dev/null || true
    done
    for f in "${TMPFILES[@]:-}"; do
        rm -f "$f"
    done
}
trap cleanup EXIT INT TERM

mktemp_tracked() {
    local f
    f=$(mktemp "$@")
    TMPFILES+=("$f")
    echo "$f"
}

check_prereqs() {
    [ -f "$BINARY" ] || die "Binary not found: $BINARY — run: cmake --build build"
    command -v redis-cli &>/dev/null || die "redis-cli not found"
    command -v python3   &>/dev/null || die "python3 not found (needed for fast key generation)"
}

# ---------------------------------------------------------------------------

start_master() {
    local logfile
    logfile=$(mktemp_tracked /tmp/rc-master-XXXX.log)

    "$BINARY" -p "$MASTER_PORT" &>"$logfile" &
    PIDS+=($!)
    MASTER_LOG="$logfile"

    local i
    for i in $(seq 1 30); do
        redis-cli -p "$MASTER_PORT" ping &>/dev/null && return 0
        sleep 0.1
    done
    die "Master didn't become ready within 3s"
}

stop_master() {
    [ -z "${MASTER_PID:-}" ] && return
    kill "$MASTER_PID" 2>/dev/null || true
    # Remove from PIDS so cleanup doesn't double-kill
    PIDS=("${PIDS[@]/$MASTER_PID/}")
    MASTER_PID=""
}

# Load N keys via direct TCP socket — avoids redis-cli --pipe's sentinel
# which prepends \r\n to its final PING and trips our parser.
load_keys() {
    local n=$1
    local start_ms end_ms
    start_ms=$(date +%s%3N)

    python3 - "$n" "$DATA_SIZE" "$MASTER_PORT" <<'PYEOF'
import socket, sys

n, size, port = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
v = b'x' * size
BATCH = 1000

sock = socket.create_connection(('127.0.0.1', port))
sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

buf = []
to_recv = 0
recv_buf = b''

for i in range(1, n + 1):
    k = f'key:{i}'.encode()
    buf.append(b'*3\r\n$3\r\nSET\r\n$' + str(len(k)).encode() + b'\r\n' + k +
               b'\r\n$' + str(size).encode() + b'\r\n' + v + b'\r\n')
    to_recv += 1
    if len(buf) == BATCH:
        sock.sendall(b''.join(buf))
        buf.clear()
        # drain responses: each SET returns "+OK\r\n" (one \n per response)
        while recv_buf.count(b'\n') < to_recv:
            recv_buf += sock.recv(65536)
        to_recv = 0
        recv_buf = b''

if buf:
    sock.sendall(b''.join(buf))
    while recv_buf.count(b'\n') < to_recv:
        recv_buf += sock.recv(65536)

sock.close()
PYEOF

    end_ms=$(date +%s%3N)
    echo $((end_ms - start_ms))
}

start_replica() {
    local port=$1 idx=$2
    local logfile
    logfile=$(mktemp /tmp/rc-replica-${port}-XXXX.log)
    TMPFILES+=("$logfile")
    # replica mode needs no server socket — just connect to master
    "$BINARY" --replica 127.0.0.1 "$MASTER_PORT" &>"$logfile" &
    PIDS+=($!)
    sublog "Replica $idx started (PID $!) log=$logfile"
}

emit_csv_header() {
    if [ ! -f "$RESULTS_CSV" ]; then
        echo "timestamp,n_keys,m_replicas,data_size,load_ms,replica_id,sync_ms,keys_per_sec" \
            > "$RESULTS_CSV"
    fi
}

emit_csv_row() {
    local n=$1 m=$2 load_ms=$3 replica_id=$4 sync_ms=$5
    local kps=$(( n * 1000 / (sync_ms + 1) ))
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ),$n,$m,$DATA_SIZE,$load_ms,$replica_id,$sync_ms,$kps" \
        >> "$RESULTS_CSV"
}

# ---------------------------------------------------------------------------
# Core benchmark: one (N, M) pair
# ---------------------------------------------------------------------------

run_one() {
    local n_keys=$1
    local m_replicas=$2

    log "N=$n_keys keys | M=$m_replicas replica(s) | value=${DATA_SIZE}B"

    # Fresh master for each run
    start_master
    MASTER_PID="${PIDS[-1]}"

    sublog "Loading $n_keys keys..."
    local load_ms
    load_ms=$(load_keys "$n_keys")
    sublog "Loaded in ${load_ms}ms ($((n_keys * 1000 / (load_ms + 1))) keys/s)"

    # Start all replicas simultaneously, record start time per replica
    declare -a replica_start_ms
    local i
    for i in $(seq 0 $((m_replicas - 1))); do
        start_replica $(( BASE_REPLICA_PORT + i )) "$i"
        replica_start_ms+=("$(date +%s%3N)")
    done

    # Round-robin poll — detect each replica's completion as soon as possible
    declare -a done_flags sync_ms_arr
    local remaining=$m_replicas
    local deadline=$(( $(date +%s%3N) + TIMEOUT_S * 1000 ))
    for i in $(seq 0 $((m_replicas - 1))); do
        done_flags[$i]=0; sync_ms_arr[$i]=0
    done

    while [ "$remaining" -gt 0 ]; do
        [ "$(date +%s%3N)" -gt "$deadline" ] && die "Sync timed out after ${TIMEOUT_S}s"
        for i in $(seq 0 $((m_replicas - 1))); do
            [ "${done_flags[$i]}" -eq 1 ] && continue
            if grep -q "Full sync done replica $i" "$MASTER_LOG" 2>/dev/null; then
                sync_ms_arr[$i]=$(( $(date +%s%3N) - ${replica_start_ms[$i]} ))
                done_flags[$i]=1
                (( remaining-- )) || true
            fi
        done
        [ "$remaining" -gt 0 ] && sleep 0.05
    done

    printf "\n    %-10s %-12s %-12s\n" "Replica" "Sync (ms)" "Keys/s"
    printf "    %-10s %-12s %-12s\n" "-------" "---------" "------"
    for i in $(seq 0 $((m_replicas - 1))); do
        local sync_ms=${sync_ms_arr[$i]}
        local kps=$(( n_keys * 1000 / (sync_ms + 1) ))
        printf "    %-10s %-12s %-12s\n" "$i" "${sync_ms}ms" "${kps}/s"
        emit_csv_row "$n_keys" "$m_replicas" "$load_ms" "$i" "$sync_ms"
    done

    echo ""

    # Tear down this run before next
    stop_master
    unset done_flags sync_ms_arr replica_start_ms
    sleep 0.5
}

# ---------------------------------------------------------------------------

check_prereqs
emit_csv_header

case "$MODE" in
    single)
        n_keys="${2:-10000}"
        m_replicas="${3:-1}"
        run_one "$n_keys" "$m_replicas"
        ;;
    sweep)
        m_replicas="${2:-1}"
        for n in 1000 10000 100000 500000; do
            run_one "$n" "$m_replicas"
        done
        ;;
    *)
        die "Unknown mode '$MODE'. Use: single <N> <M>  |  sweep <M>"
        ;;
esac

log "Results appended to $RESULTS_CSV"
