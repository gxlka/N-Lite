# N-Lite

N-Lite is a lightweight native x64 Windows utility built with the Win32 API and C++.

## Features

- **Memory:** live RAM, available and free memory, standby-list size, system commit, and page-file usage. Manually purge the standby list or configure an editable threshold and automatic-clean interval.
- **Timer resolution:** select from the range Windows reports. The saved choice is applied at startup and released when N-Lite exits.
- **Processes:** searchable, sortable list with icons, PIDs, CPU, working set and private memory. Collapsed executable groups show aggregate usage; expanded rows show each process separately.
- **Process actions:** end a process, open its file location, set priority, CPU affinity, or GPU preference. Priority and CPU-affinity choices are saved per executable and reapplied to later instances; GPU preference is saved for the current user.
- **Tray and startup:** close or minimize the window to the notification area, and optionally start N-Lite when you sign in.
- **In-app updates:** choose Install update to download the latest setup installer, verify its SHA-256 digest against the release metadata, install per-user, and restart N-Lite.

Windows controls access to protected or elevated processes. Standby-list purge is also protected by Windows; automatic cleaning uses a scheduled task and may require administrator approval when it is configured.

## Build and install

GitHub Actions builds an x64 portable executable and the Inno Setup installer. The N-Lite-Windows-x64 Actions artifact contains both files.

The setup installs per-user to %LOCALAPPDATA%/Programs/N-Lite, creates a Start Menu shortcut and offers optional Windows startup and desktop shortcut settings. Normal installation does not require administrator rights.

Download the current setup from [Releases](https://github.com/gxlka/N-Lite/releases/latest). Older builds that only open GitHub for updates need this setup installed once before they can use the in-app installer.

## Requirements

Windows 10 version 1809 (build 17763) or later, x64.