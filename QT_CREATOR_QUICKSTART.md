# Qt Creator Quick Start

## 1. Open the Project

Open [CMakeLists.txt](D:/CodeRepositories/edoyun/remote_control/CMakeLists.txt) in Qt Creator.

## 2. Choose the Kit

Use the Qt 6.7.3 desktop kit.

Recommended:

- `Desktop Qt 6.7.3 MSVC2022 64bit`

Do not use an LLVM, Clang, or MinGW-based kit for this project.
Use the regular MSVC kit entry, not a temporary preset entry.

## 3. Set the Build Directory

Recommended:

- `D:/CodeRepositories/edoyun/remote_control/build/qtcreator-debug`

For a release build:

- `D:/CodeRepositories/edoyun/remote_control/build/qtcreator-release`

## 4. Main Targets

- `remote_server_qt`
- `remote_client_qt`
- `remote_smoke_test`

## 5. Recommended Startup Setups

### Server

- Startup project: `remote_server_qt`
- Arguments: empty

### Server (background)

- Startup project: `remote_server_qt`
- Arguments: `--no-tray`

### Server (lock test)

- Startup project: `remote_server_qt`
- Arguments: `--no-tray --lock-test 2`

### Client

- Startup project: `remote_client_qt`
- Arguments: empty
- Default behavior: if `127.0.0.1:9527` is not listening yet, the client auto-starts the local server first

### One-click server + client

- Create a `Custom Executable` run configuration
- Program: `powershell.exe`
- Arguments: `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_stack.ps1" -NoTray`
- Working directory: `%{sourceDir}`
- Result: starts the server first and then opens the client

### Smoke Test

- Startup project: `remote_smoke_test`
- Arguments: `127.0.0.1 9527`

## 6. Simplest Daily Workflow

1. Open the project with the regular MSVC kit
2. Set the build directory to `build/qtcreator-debug`
3. Use the `run_stack.ps1` wrapper for one-click daily startup
4. Run `remote_server_qt` alone when you want server-only debugging
5. Run `remote_client_qt` alone when you want client-only debugging
6. When needed, run `remote_smoke_test`

For the common localhost workflow, setting `remote_client_qt` as the startup target is enough.
Press Run or Debug and the client will automatically start the local server in the background if needed.

## 7. If Qt DLLs Are Not Found

Use the wrapper scripts instead:

- [scripts/run_server.ps1](D:/CodeRepositories/edoyun/remote_control/scripts/run_server.ps1)
- [scripts/run_client.ps1](D:/CodeRepositories/edoyun/remote_control/scripts/run_client.ps1)
- [scripts/run_stack.ps1](D:/CodeRepositories/edoyun/remote_control/scripts/run_stack.ps1)
- [scripts/run_smoke_test.ps1](D:/CodeRepositories/edoyun/remote_control/scripts/run_smoke_test.ps1)

Create a `Custom Executable` run configuration:

- Program: `powershell.exe`
- Working directory: `%{sourceDir}`

Arguments examples:

- Server:
  `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_server.ps1"`
- Server no tray:
  `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_server.ps1" -NoTray`
- Client:
  `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_client.ps1"`
- Server + client:
  `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_stack.ps1" -NoTray`
- Smoke test:
  `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_smoke_test.ps1"`
