# Standby Cleaner Helper Design

## User-approved behavior

N-Lite stays unelevated during normal use. Windows approval happens when the restricted cleaner helper is first installed, or when its helper version changes. Manual and automatic standby purges then run through the installed helper without another UAC prompt. The helper only reads validated cleaner settings and requests a standby-list purge.

## Why the current cleaner fails

`src/main.cpp::PurgeStandby` calls `NtSetSystemInformation(SystemMemoryListInformation, MemoryPurgeStandbyList)` after trying to enable `SeProfileSingleProcessPrivilege`. `AdjustTokenPrivileges` cannot grant a privilege missing from the caller's token. The fallback relaunches N-Lite with `runas`, so each clean can trigger UAC, and an elevated user token may still lack the required right.

## Architecture

- Build `N-Lite-Cleaner.exe` as a separate, small Windows helper. Its accepted commands are install, run the configured clean/check task, and uninstall a user's cleaner registration. It has no arbitrary command, path, or process-action interface.
- On setup, the unelevated app launches the packaged helper with `runas` once. The elevated helper copies itself under `%ProgramData%\N-Lite` into an Administrators/SYSTEM-owned directory, creates per-user settings and status files, and registers a SYSTEM task. The task runs every minute; the helper only auto-purges when that user's validated settings enable it and the standby threshold/interval policy allows it.
- The task action points only at the protected helper and a validated target-user SID. Its DACL grants SYSTEM and Administrators full control and Users read/execute only. The unelevated app can run the task, but cannot change its action, task principal, or protected executable.
- The helper writes a protected version marker only after registration and task ACL verification succeed. N-Lite uses this durable marker and its per-user config/status files to recognize setup; a transient Task Scheduler query failure must not trigger the elevated setup flow again.
- Manual cleaning records a monotonically increasing request ID in the user's settings file and starts the same task. The helper records completion and NTSTATUS in a separate read-only-to-user status file; the UI polls it and shows completion or a specific failure.
- Settings are bounded to 64–131072 MB and 60–7200 seconds. Invalid or unreadable files fail closed. The auto-clean policy retains the current below-threshold re-arm and interval behavior.
- Uninstall disables auto-clean before asking the protected helper to remove that user's task and files. The shared protected helper is removed only when no other N-Lite user registrations remain.

## Security and failure behavior

- Reject malformed SIDs, unknown helper commands, missing config fields, out-of-range values, reparse-point config files, and concurrent cleaner runs.
- Helper install/update fails visibly if the current user cancels UAC, the ProgramData directory cannot be secured, task registration/DACL verification fails, or the task cannot be started. Never fall back to elevating the full UI on a clean click.
- Preserve manual clean, threshold, interval, auto-clean, and per-user installation behavior. No driver or third-party service is added.
- Use Windows Task Scheduler's SYSTEM service account and an explicit task DACL. Microsoft documents that task file execute permission allows `IRegisteredTask::Run`, and that registration accepts an SDDL ACL: <https://learn.microsoft.com/en-us/windows/win32/taskschd/security-contexts-for-running-tasks> and <https://learn.microsoft.com/en-us/windows/win32/taskschd/taskfolder-registertaskdefinition>.

## Acceptance checks

1. The ordinary N-Lite process contains no `runas` fallback for manual or automatic clean.
2. Setup performs the only normal UAC elevation; subsequent manual/automatic tasks run while the UI remains unelevated.
3. The protected task runs under SYSTEM, its action points to the protected ProgramData helper, and standard Users can run it but cannot change it.
4. Helper input accepts only the bounded settings format; below-threshold, disabled, unarmed, or cooldown states do not purge.
5. Task/helper failures reach the UI with a failure state and never appear as successful cleaning.
