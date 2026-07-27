# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

### Prerequisites
- Windows, Visual Studio Build Tools with MSVC C++ toolchain, CMake 3.25+, Ninja, Qt 5.15 or Qt 6 MSVC kit
- Set `QTDIR` environment variable or let `Build.ps1` auto-detect from common install paths

### Commands

```powershell
# Incremental Debug build (configures and deploys when needed)
.\scripts\Build.ps1

# Build only one target group
.\scripts\Build.ps1 -Target client
.\scripts\Build.ps1 -Target server
.\scripts\Build.ps1 -Target tests

# Build Release
.\scripts\Build.ps1 -Action build -Config Release

# Configure only (generates compile_commands.json for clangd)
.\scripts\Build.ps1 -Action configure

# Clean
.\scripts\Build.ps1 -Action clean -Config Debug

# Refresh local toolchain presets or force runtime deployment
.\scripts\Build.ps1 -RefreshPresets
.\scripts\Build.ps1 -Deploy
```

```powershell
# Run server (listens on all interfaces, port 9527 by default)
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug

# Run client
.\scripts\Run.ps1 -Target client -BuildDir .\build\msvc-debug

# Run smoke test (requires running server)
.\scripts\Run.ps1 -Target smoke -BuildDir .\build\msvc-debug

# Server without tray icon
.\scripts\Run.ps1 -Target server -BuildDir .\build\msvc-debug -NoTray
```

### Tests

```powershell
# Unit tests (pure protocol tests, no server needed — registered with CTest)
ctest --test-dir .\build\msvc-debug --output-on-failure -R RemoteControlProtocolTests

# Integration test (requires running server at 127.0.0.1:9527)
.\scripts\Run.ps1 -Target smoke -BuildDir .\build\msvc-debug
```

### Static Analysis

```powershell
# Format check
$formatFiles = rg --files include src -g '*.h' -g '*.cpp'
clang-format --dry-run --Werror --style=file $formatFiles

# clang-tidy (run from repo root; needs compile_commands.json from a prior build)
$tidyFiles = rg --files src -g '*.cpp'
clang-tidy -p build/msvc-debug `
    --extra-arg=-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH `
    --quiet $tidyFiles
```

## Architecture

A Windows remote desktop/control application built with Qt Widgets and TCP.

### Layers

```
include/                    src/
  common/                     common/        RemoteControlCommon (static lib)
    Protocol.h — command enum, FileEntry,      Protocol.cpp — wire serialization
    MouseEventPacket, UTF-8 helpers             Packet.cpp — framing (0xFEFF header, size, checksum)
    Packet.h — binary packet framing
  client/                     client/         RemoteControlClient (executable)
    RemoteClient.h — network facade             ClientMain.cpp — entry point
    MainWindow.h — main UI                      MainWindow.cpp/.ui — drive tree, file table
    WatchWindow.h — remote screen view          RemoteClient.cpp — async network ops
    WatchConnectionWorker.h — screen frames     WatchConnectionWorker.cpp — persistent screen socket
    ControlConnectionWorker.h — mouse/lock      ControlConnectionWorker.cpp — coalesced control socket
    DownloadWorker.h — file download            DownloadWorker.cpp — streamed download to QSaveFile
  server/                     server/          RemoteControlServer (executable)
    RemoteServer.h — TCP listener               ServerMain.cpp — entry point, CLI parsing
    RemoteSession.h — per-client dispatch       RemoteServer.cpp — accept, create sessions
    CommandService.h — lightweight commands     RemoteSession.cpp — packet routing
    FileRequestPool.h — bounded worker pool     CommandService.cpp — drives, run, test
    FileRequestWorker.h — file ops worker       FileRequestPool.cpp — 2-4 reusable workers
    ControlStreamThread.h — persistent control  FileRequestWorker.cpp — dir, download, delete
    WatchStreamThread.h — persistent screen     ControlStreamThread.cpp — mouse; queue lock/unlock
    PlatformIntegration.h — Windows API         WatchStreamThread.cpp — GDI capture → PNG stream
    ServerTrayController.h — system tray        PlatformIntegration.cpp — SendInput, UAC, startup
    LockWindow.h — full-screen lock overlay     ServerTrayController.cpp — tray icon + menu
```

### Connection Model

- **Short-lived connections:** `TestConnection`, `ListDrives`, `RunFile` — asynchronous on the client event loop and handled by `CommandService` on the server thread
- **File task connections:** `ListDirectory`, `DownloadFile`, `DeleteFile` — socket transferred to a worker in `FileRequestPool` (bounded pool, 2-4 threads)
- **Control long connection:** `ControlChannel` — dedicated `ControlStreamThread` for mouse events, lock, unlock (mouse moves are coalesced: only the latest queued position is kept)
- **Watch long connection:** `WatchScreen` — dedicated `WatchStreamThread` streams PNG screenshots; single-frame flow control (client requests next frame only after receiving the previous one)

Separate connections for screen and control avoid head-of-line blocking (screenshot data doesn't delay mouse input).

### Key Design Decisions

- `PlatformIntegration` encapsulates all Windows-specific code (GDI screen capture, `SendInput` mouse injection, UAC elevation via `ShellExecuteEx`, startup registry entries)
- File workers are reusable and support cooperative cancellation via an atomic flag
- The client owns three persistent worker threads for downloads, screen frames, and ordered control commands; cross-thread work is submitted with queued invocation
- Directory items use one `DirectoryLoadState` (`Unloaded`, `Loading`, `Loaded`, or `Refreshing`) plus a separate cached `QList<FileEntry>`
- The lock overlay (`LockWindow`) intercepts keyboard input, blocks close requests, and blocks focus-out to prevent bypass
- Server supports headless mode (`--no-tray`), startup-entry maintenance (`--install-startup`, `--remove-startup`), UAC relaunch (`--elevate`), and a timed auto-unlock test (`--lock-test`)
- Packet validation bounds payloads to 64 MiB and uses a 16-bit additive payload checksum; it is corruption detection, not authentication or cryptographic integrity

## Coding Conventions

This project follows strict C++17 conventions. The authoritative reference is `.claude/skills/coding-style-review/SKILL.md`. Key rules:

- **C++ standard:** C++17, no extensions
- **Headers:** `#pragma once`; project headers use full module path (`#include "common/Packet.h"`); double quotes for project, angle brackets for Qt/STL/Windows
- **Naming:** `PascalCase` for types/enums/constants; `camelCase` for functions, locals, data members; `_camelCase` parameters; `m_camelCase` private members; `g_` globals
- **Const:** east-const style (`int const` not `const int`)
- **Initialization:** use braces for explicit initialization; initialize scalar, pointer, and enum members with meaningful values; do not add redundant empty `{}` to safely default-constructible class members
- **Functions:** `[[nodiscard]]` on getters; `noexcept`, `override`, `= default`, `= delete` where correct; `explicit` on single-arg constructors; pass-by-value for primitives/enums, `T const&` for Qt/STL types
- **Types:** `using` not `typedef`; `nullptr` not `NULL`; no C-style casts — `static_cast` preferred, `reinterpret_cast` only at verified Windows/binary boundaries
- **Ownership:** Qt parent-child for `QObject`s; `std::unique_ptr` otherwise; no `malloc`/`free`
- **Formatting:** clang-format (Google-based, 4-space indent, 100-char column limit, custom brace wrapping, left pointer alignment); clang-tidy with curated checks and naming enforcement
- **Comments:** English, `//` for implementation, Doxygen `/** ... */` with `@param`/`@return` for function documentation

## Testing

- **`RemoteControlProtocolTests`** — pure tests for malformed packet-length recovery, split TCP headers, long UTF-8 `FileEntry` round-trips, invalid entry payloads, and status payloads. No network required. Registered with CTest.
- **`RemoteControlSmokeTest`** — end-to-end integration test: connection, drive listing, valid/invalid and Unicode directory listing, file download (large/concurrent/missing/empty), file/directory deletion, persistent screen capture (PNG decode verification), persistent mouse control, and file execution. Requires a running server.

Always run protocol tests after changing `src/common/`. Run smoke tests after changing server or protocol code.

## Security

The current protocol has no authentication or TLS encryption, and the server listens on all interfaces by default. The README states it must not be exposed to the public internet or untrusted networks. This is a learning/testing tool intended for authorized controlled environments only.
