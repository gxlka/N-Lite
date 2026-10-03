# N-Lite

A small, portable Windows system utility built with native Win32 C++. It has a resizable desktop window, stays in the notification area, and can optionally start with Windows. The release build uses the Windows SDK and a statically linked C++ runtime; it does not need .NET or another bundled runtime.

## Features

### Processes

- A searchable, expandable process tree with PID, sampled CPU use, and working-set memory.
- Per-process details, image path, and working-set reading.
- Change priority and toggle CPU affinity bits for processes Windows allows you to control.
- Save a Windows GPU preference for an executable: High performance or Power saving. Windows applies this preference when the target application is launched; it is not a live GPU-usage meter.
- End a process after a confirmation prompt.

### Memory

- Physical RAM in use, available memory, free pages, and the Windows standby list.
- System commit charge and its limit. Commit is backed by RAM and/or the page file; it is not a measurement of page-file disk I/O.
- Manual standby-list purge, or optional threshold-triggered purge with an adjustable threshold and polling interval.
- A timer-resolution request slider. The request is disabled by default and is released when N-Lite exits.
- Optional per-user Windows startup entry. Closing the window hides it in the tray; use the tray menu to exit.

## Build and download

Every push to `main` runs the Windows x64 Release build in GitHub Actions. Open the repository's **Actions** tab, choose the latest **Build N-Lite for Windows** run, and download the `N-Lite-Windows-x64` artifact. The artifact contains the portable `N-Lite.exe`. A tag beginning with `v` also runs the build.

To build from a Windows Developer Command Prompt with CMake and Visual Studio 2022 installed:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Notes

- The standby-list query and purge use the Windows native `NtQuerySystemInformation` and `NtSetSystemInformation` interfaces. Windows versions and security policy can reject a purge. If that happens, the manual cleaner can request a one-shot elevated run; the tray app itself does not need to run elevated.
- Automatic purging only attempts a purge after the selected size threshold is reached and the interval has elapsed. It waits for the standby list to drop below the threshold before arming the next attempt. Windows may repopulate standby memory immediately; standby memory is normally reclaimable cache, not memory permanently lost to applications.
- Process access, priority, affinity, and termination are subject to Windows permissions. Affinity uses the process's available processor mask and is limited to the current processor group.
- Timer requests can increase wakeups and power use. They are opt-in and active only while N-Lite is running.
- GPU preference is a Windows per-application graphics preference. It does not report GPU load, and Windows may ignore it on systems with only one GPU.
- The project targets low idle overhead and has no bundled UI framework or background service. Actual memory use depends on Windows and the number of processes shown.
