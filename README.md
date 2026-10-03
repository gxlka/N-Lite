# N-Lite

N-Lite is a lightweight native Windows utility built with the Win32 API and C++.

## Pages and features

- **Memory** shows RAM in use, available memory, standby-list size, page-file usage and system commit. The standby cleaner has an editable threshold, selectable check interval, automatic purge and a manual clean action. Timer resolution can be selected down to the lowest value Windows reports and is restored whenever N-Lite starts.
- **Processes** lists executable icons, PID, CPU, working set and private memory. Process groups start collapsed and show the combined working set and private bytes of their child processes; expanding a group shows each process separately. Columns can be sorted and processes can be searched.
- **Process actions** include ending a process, opening its file location, changing priority, setting CPU affinity and choosing a GPU preference. N-Lite saves priority and CPU-affinity choices per executable and reapplies them to later instances. GPU preferences are stored in the current Windows user profile.
- **Startup** controls whether N-Lite launches quietly in the notification area when you sign in.
- **Settings** shows the installed version and lets you check for updates or open the GitHub project. Update checks also run at launch and every six hours.

Automatic standby cleaning uses an elevated scheduled task because Windows protects standby-list purging. The task checks once per minute and honors the configured threshold and interval.

## Build and install

GitHub Actions builds the x64 portable executable and the Windows setup installer. The N-Lite-Windows-x64 Actions artifact contains N-Lite.exe and N-Lite-Setup-x64.exe.

The installer installs per-user under LocalAppData, creates a Start Menu shortcut and offers optional Windows startup and desktop shortcut settings. It does not require administrator rights.

To publish an update, push a version tag such as v0.3.0. The workflow builds that version and attaches both the portable executable and setup installer to a GitHub Release. N-Lite shows an in-app update button when a release is newer than the installed version.

## Runtime behavior

Closing or minimizing the main window keeps N-Lite in the notification area. Use the tray menu to reopen the window or exit.
