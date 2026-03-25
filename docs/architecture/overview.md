# hub32api Architecture Overview

## Layer Diagram

```
┌──────────────────────────────────────────────────────────────┐
│                    HTTP Clients                              │
│         (Python SDK / C# SDK / Browser / curl)              │
└────────────────────────┬─────────────────────────────────────┘
                         │ HTTP/REST
┌────────────────────────▼─────────────────────────────────────┐
│                 HttpServer (cpp-httplib)                     │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                    Router                            │   │
│  │  /api/v1/* → v1 Controllers                         │   │
│  │  /api/v2/* → v2 Controllers                         │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌─────────────────┐  ┌───────────────┐  ┌─────────────┐   │
│  │  AuthMiddleware  │  │ CorsMiddleware │  │ RateLimit   │   │
│  └─────────────────┘  └───────────────┘  └─────────────┘   │
│                                                              │
│  v1 Controllers:    Auth | Computer | Feature | Framebuffer  │
│  v2 Controllers:    Batch | Location | Metrics               │
└────────────────────────┬─────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────────┐
│              Plugin Registry (hub32api-core)               │
│  ┌──────────────────┐ ┌─────────────────┐ ┌──────────────┐  │
│  │  ComputerPlugin  │ │  FeaturePlugin  │ │ SessionPlugin│  │
│  └────────┬─────────┘ └────────┬────────┘ └──────┬───────┘  │
└───────────┼──────────────────-─┼─────────────────┼──────────┘
            │                   │                  │
┌───────────▼───────────────────▼──────────────────▼──────────┐
│              Hub32CoreWrapper (Qt singleton)                 │
│  NetworkObjectDirectoryManager | FeatureManager             │
│  ComputerControlInterface | PlatformPluginInterface         │
└────────────────────────────────────────────────────────────-┘
```

## Key Design Decisions

1. **No Qt in public API** — Hub32CoreWrapper hides Qt types behind Pimpl
2. **Result<T> everywhere** — no exceptions cross controller boundaries
3. **Stateless HTTP** — JWT Bearer tokens, no session cookies
4. **Plugin isolation** — each plugin can be tested independently with mocks
5. **Windows-first** — WinServiceAdapter, Registry config, Windows SDK linking
