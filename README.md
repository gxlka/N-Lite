# N-Lite

N-Lite is a lightweight native x64 Windows utility built with the Win32 API and C++.

## Features

- **Memory:** live RAM, available and free memory, standby-list size, system commit, and page-file usage. Manually purge the standby list or configure an editable threshold and automatic-clean interval.
- **Cleaner setup:** one administrator approval installs a protected helper and SYSTEM task. N-Lite stays unelevated, and later manual or automatic cleans do not prompt again.
- **Timer resolution:** select from the range Windows reports. The saved choice is applied at startup and released when N-Lite exits.
- **Processes:** searchable, sortable list with icons, PIDs, CPU, working set and private memory. Matching full executable paths share accurate memory totals; expanding a group shows each process separately.
- **Process actions:** end a process, open its file location, set priority, CPU affinity, or GPU preference. Priority and CPU-affinity choices are saved per executable and reapplied to later instances; GPU preference is saved for the current user.
- **Tray and startup:** close or minimize the window to the notification area, and optionally start N-Lite when you sign in.
- **In-app updates:** choose Install update to download the latest setup installer, verify its SHA-256 digest against the release metadata, install per-user, and restart N-Lite.
- **Appearance:** compact horizontal navigation with a saved light/dark theme.

Windows controls access to protected or elevated processes. The protected standby cleaner needs administrator approval once to register its helper; routine cleaning uses a restricted SYSTEM task without elevating the N-Lite interface.

## Build and install

GitHub Actions builds the x64 N-Lite app, its small cleaner helper, and the Inno Setup installer. The N-Lite-Windows-x64 Actions artifact contains all three files.

The setup installs per-user to %LOCALAPPDATA%/Programs/N-Lite, creates a Start Menu shortcut and offers optional Windows startup and desktop shortcut settings. Normal installation does not require administrator rights. The first manual or automatic standby clean asks once to install its protected helper; cancelling leaves N-Lite running without elevation.

Download the current setup from [Releases](https://github.com/gxlka/N-Lite/releases/latest). Older builds that only open GitHub for updates need this setup installed once before they can use the in-app installer.

## Requirements

Windows 10 version 1809 (build 17763) or later, x64.
