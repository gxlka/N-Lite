# N-Lite

N-Lite is a small native Windows system utility built with the Win32 API and C++.

## Features

- Process view with executable icons, process tree expansion, CPU and memory columns, search, and row context actions.
- Custom process actions for ending a process, changing priority, CPU affinity, GPU preference, and opening its file location.
- Memory dashboard for RAM in use, available memory, free pages, standby list, system commit charge, and page-file usage.
- Editable standby threshold (64–131072 MB) and automatic standby cleaning through a scheduled task. Windows asks for administrator approval once when enabling the cleaner.
- Timer-resolution control using the lowest resolution Windows reports as supported. Its enable state and chosen value are saved and reapplied at app launch.
- Tray mode, optional Windows startup, and an update notification that checks GitHub Releases at launch and every six hours.

Automatic standby cleaning requires an elevated scheduled task because Windows protects standby-list purging. The task checks once per minute and honors the configured threshold and interval. A failed purge is retried.

## Build

Use the repository's GitHub Actions workflow to build the x64 portable executable. Each build is published as the N-Lite-Windows-x64 Actions artifact.

To publish an update, push a version tag such as v0.3.0. The workflow builds the tagged version and attaches dist/N-Lite.exe to a GitHub Release. N-Lite checks the latest release and shows an update button in the app when its version is newer.

## Runtime behavior

The application starts hidden in the tray when launched with the Windows startup entry. Closing or minimizing the main window keeps N-Lite in the tray; use the tray menu to exit.
