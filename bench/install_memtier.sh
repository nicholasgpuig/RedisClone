#!/bin/bash
set -euo pipefail

echo "==> Installing memtier_benchmark dependencies..."
sudo apt-get install -y \
    build-essential autoconf automake \
    libpcre3-dev libevent-dev \
    pkg-config zlib1g-dev libssl-dev git

echo "==> Cloning memtier_benchmark..."
cd /tmp
rm -rf memtier_benchmark
git clone --depth=1 https://github.com/RedisLabs/memtier_benchmark.git
cd memtier_benchmark

echo "==> Building..."
autoreconf -ivf
./configure
make -j"$(nproc)"
sudo make install

echo "==> Done: $(memtier_benchmark --version)"
