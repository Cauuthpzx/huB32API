# hub32api

A production-grade REST API server exposing [Hub32](https://hub32.io/) educational monitoring capabilities via HTTP.

## Architecture

See [docs/architecture/overview.md](docs/architecture/overview.md).

## API Reference

- [v1 API](docs/api/v1.md)
- [v2 API](docs/api/v2.md)

## Build

### Prerequisites

- Windows 10/11
- MSYS2 MinGW64 (CMake 3.20+, Ninja, GCC 12+)
- Hub32 installed at `C:\Program Files\Hub32\`

### Configure and Build

```bash
# Debug build (with tests)
cmake --preset debug
cmake --build --preset debug

# Release build
cmake --preset release
cmake --build --preset release
```

### Run (console mode)

```bash
./build/release/bin/hub32api-service.exe --console --config conf/development.json
```

### Install as Windows Service

```bash
./hub32api-service.exe --install --config "C:\ProgramData\hub32api\config.json"
```

## Client SDKs

- Python: [clients/python/hub32api_client.py](clients/python/hub32api_client.py)

## License

GPL-2.0 (same as Hub32)
