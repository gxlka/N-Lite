# Topline N-Lite UI Design

## User-approved direction

Implement **Topline** from the selected preview: compact horizontal navigation across the top, a memory overview beside the standby cleaner, and a timer row below. Keep the existing Win32/GDI application and its existing feature set. Use the current dark palette as the default, add a persisted light/dark switch, and use flat fills with no gradients.

## Screen structure

- The top application bar contains the N-Lite mark, Memory / Processes / Startup / Settings tabs, health state, and the appearance toggle.
- Memory has a memory-use hero with an in-use/standby/free bar and labeled available/page-file/commit values. The adjacent cleaner panel contains status, threshold, check interval, automatic-clean toggle, setup/clean action, and a concise status line. Timer resolution stays in one full-width row below.
- Processes keeps search, refresh, sortable columns, process actions, and expansion. Rows group only processes with the same normalized full executable path. Unknown paths remain individual rows. Group totals equal the sum of the listed instances; same-name executables in different folders stay separate.
- Startup and Settings retain their current controls and update behavior under the top tabs. Theme preference persists per user.

## Layout behavior

- Design target: 1240x830 client area; minimum client size remains 960x620.
- At the minimum width, cleaner fields wrap or stack instead of shrinking into unreadable columns. Process columns retain a flexible name column and fixed numeric columns; search and refresh stay within the content width.
- The process list preserves the top visible process/group across refreshes and keeps a group at the same screen row when it is expanded or collapsed. No content is drawn outside the client area.
- Text stays on the existing Segoe UI family. Continue the current restrained indigo, green, and amber accents and use theme-aware surfaces, text, borders, hover, and selection colors.

## Acceptance checks

1. Topline navigation replaces the vertical sidebar on every page.
2. At 1240x830, memory hero and cleaner are side by side with the timer below; at 960x620 all controls remain visible and usable.
3. Switching themes updates every surface and control and remains selected after restart.
4. Two process rows with the same name but different full paths never share a group or total; equal normalized paths may group.
5. Expanding a group and periodic refreshes keep the anchor row stationary; switching pages and all existing controls still work.
