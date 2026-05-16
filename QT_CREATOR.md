# Qt Creator

1. Open [CMakeLists.txt](D:/CodeRepositories/edoyun/remote_control/CMakeLists.txt) in Qt Creator.
2. Pick your Qt 6.7.3 desktop kit, preferably the MSVC 2022 64-bit kit.
   Do not use an LLVM, Clang, or MinGW-based kit for this project.
3. Set the build directory manually:
   - Debug: `D:/CodeRepositories/edoyun/remote_control/build/qtcreator-debug`
   - Release: `D:/CodeRepositories/edoyun/remote_control/build/qtcreator-release`
4. Build targets:
   - `remote_client_qt`
   - `remote_server_qt`
   - `remote_smoke_test`

Recommended startup targets:

- Daily client debugging: `remote_client_qt`
- Daily server debugging: `remote_server_qt`
- Regression check: `remote_smoke_test`

When `remote_client_qt` starts with its default settings, it automatically checks `127.0.0.1:9527`.
If no local server is listening yet, it starts `remote_server_qt` first and then shows the client window.

Recommended executable wrappers:

- Client wrapper: [scripts/run_client.ps1](D:/CodeRepositories/edoyun/remote_control/scripts/run_client.ps1)
- Server wrapper: [scripts/run_server.ps1](D:/CodeRepositories/edoyun/remote_control/scripts/run_server.ps1)
- Server + client wrapper: [scripts/run_stack.ps1](D:/CodeRepositories/edoyun/remote_control/scripts/run_stack.ps1)
- Smoke test wrapper: [scripts/run_smoke_test.ps1](D:/CodeRepositories/edoyun/remote_control/scripts/run_smoke_test.ps1)

Recommended run arguments:

- Server normal mode: no arguments
- Server background test mode: `--no-tray`
- Server short lock test: `--no-tray --lock-test 2`
- Smoke test: `127.0.0.1 9527`

Suggested workflow in Qt Creator:

1. Use the combined wrapper when you want one click to launch both server and client.
2. Use `remote_client_qt` alone when you want one-click localhost run/debug from the Qt Creator toolbar.
3. Use `remote_server_qt` alone when you need to debug the server.
4. Use `remote_smoke_test` for quick protocol regression checks.

Exact Run Settings to create in Qt Creator:

1. `remote_client_qt`
   - Executable: `%{buildDir}/remote_client_qt.exe`
   - Command line arguments: empty
   - Working directory: `%{sourceDir}`

2. `remote_server_qt`
   - Executable: `%{buildDir}/remote_server_qt.exe`
   - Command line arguments: empty
   - Working directory: `%{sourceDir}`

3. `remote_server_qt (--no-tray)`
   - Executable: `%{buildDir}/remote_server_qt.exe`
   - Command line arguments: `--no-tray`
   - Working directory: `%{sourceDir}`

4. `remote_server_qt (lock test)`
   - Executable: `%{buildDir}/remote_server_qt.exe`
   - Command line arguments: `--no-tray --lock-test 2`
   - Working directory: `%{sourceDir}`

5. `remote_smoke_test`
   - Executable: `%{buildDir}/remote_smoke_test.exe`
   - Command line arguments: `127.0.0.1 9527`
   - Working directory: `%{sourceDir}`

6. `remote stack`
   - Program: `powershell.exe`
   - Command line arguments: `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_stack.ps1" -NoTray`
   - Working directory: `%{sourceDir}`
   - Result: starts the server first and then launches the client

If you prefer not to manage PATH manually in Qt Creator, use the PowerShell wrappers instead:

1. Create a Custom Executable run configuration.
2. Program:
   - `powershell.exe`
3. Arguments examples:
   - Client: `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_client.ps1"`
   - Server: `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_server.ps1"`
   - Server no tray: `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_server.ps1" -NoTray`
   - Server + client: `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_stack.ps1" -NoTray`
   - Smoke test: `-ExecutionPolicy Bypass -File "%{sourceDir}/scripts/run_smoke_test.ps1"`
4. Working directory:
   - `%{sourceDir}`

Notes:

- Qt Creator stores run/debug details in local `*.creator.user` files, which are intentionally ignored by Git.
- The project no longer relies on Qt Creator CMake preset mode, because that mode caused crashes in this environment.
- The scripts above inject the Qt 6.7.3 `bin` directory automatically, which is handy if your Kit environment is incomplete.
