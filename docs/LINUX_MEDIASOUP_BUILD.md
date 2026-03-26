# Building hub32api with mediasoup on Linux

## Prerequisites

```bash
# Ubuntu 22.04+
sudo apt install build-essential cmake ninja-build python3-pip
sudo apt install libssl-dev libsqlite3-dev
pip3 install meson

# Build mediasoup worker as static library
cd third_party/mediasoup/worker
meson setup builddir --default-library=static
ninja -C builddir
```

## Generate FlatBuffers headers

```bash
# Install flatc (Ubuntu 22.04+)
sudo apt install flatbuffers-compiler

# Generate C++ headers from mediasoup FBS schemas
cd third_party/mediasoup-server-ref/worker/fbs
flatc --cpp *.fbs --gen-all
# This creates FBS/ directory with *_generated.h files required by
# WorkerMessageBuilder.hpp (message, request, response, notification,
# worker, router, webRtcTransport, transport, sctpParameters).
```

## Configure hub32api with mediasoup

```bash
cmake --preset debug -DHUB32_WITH_MEDIASOUP=ON \
    -Dmediasoup_WORKER_LIB=third_party/mediasoup/worker/builddir/libmediasoup-worker.a

cmake --build build/debug
```

## Run

```bash
# Config must set sfuBackend to "mediasoup":
# "sfuBackend": "mediasoup",
# "sfuWorkerCount": 0,  // auto = CPU cores
./build/debug/bin/hub32api-service --console --config conf/default.json
```

## Verify worker startup

Look for log messages:
```
[MediasoupSfuBackend] spawning 4 workers (rtcMinPort=40000, rtcMaxPort=49999)
[MediasoupSfuBackend] worker 0 starting
[MediasoupSfuBackend] worker 1 starting
...
```
