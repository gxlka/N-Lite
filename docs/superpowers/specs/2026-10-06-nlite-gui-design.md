# N-Lite GUI redesign

**Date:** 2026-10-06  
**Status:** Design direction approved by the user; implementation pending review of this spec.

## Purpose

Refresh the complete N-Lite interface so it feels compact, polished, and responsive while keeping its current Windows-native footprint and working features. The user asked for fixed-pixel sizing, sharp high-quality emoji and icons, smooth scrolling, and smooth animations. The previously accepted Topline layout remains the base.

## User requirements

- Keep the compact Topline layout with horizontal navigation and the Memory, Processes, Startup, and Settings pages.
- Preserve existing cleaner, process, startup, timer, update, theme, and tray behavior.
- Use fixed, intentional pixel dimensions and consistent spacing.
- Keep emoji, icons, and images sharp at their display size.
- Make list scrolling and interface animations smooth.
- Use a clean visual style without unnecessary controls or decoration.

## Design

### Visual system and layout

- Keep the current 1240×830 requested window size and four-page horizontal navigation.
- Keep Memory and Standby Cleaner side by side on the Memory page.
- Use an 8-pixel spacing grid, whole-pixel control bounds, consistent text sizes, and larger click targets than decorative details.
- Retain the restrained dark and light palettes. Use flat surfaces and clear contrast; no gradients or extra status banners.
- Use fixed logical-pixel control dimensions at the 96-DPI baseline. When Windows display scaling changes, rerender text and vector geometry at the target device scale and snap edges to physical pixels. Stretch only the content regions that need available space.
- Preserve all existing controls, labels, page order, and current click actions. This work changes presentation and motion, not cleaner or startup policy.

### Icons and emoji

- Draw app chrome and action icons as small native vector shapes so they remain crisp at their target sizes.
- Add a consistent 20-logical-pixel emoji accent beside each page label and the Memory/Cleaner section headings. Render these glyphs with the Windows color emoji font through a small Direct2D/DirectWrite helper; do not scale a low-resolution emoji bitmap.
- Keep executable icons at native shell-provided sizes where available. If an emoji or icon renderer is unavailable, use a simple vector fallback instead of a blurry stretched image.
- Do not add decorative imagery that competes with the data or adds new controls.

### Scrolling and animation

- Replace row-at-a-time wheel changes in Processes and Startup with pixel-offset scrolling. Animate from the current offset to the requested offset using elapsed time and easing; clamp to the actual content bounds.
- Keep painted rows, selection, hover, and hit testing aligned to the same scroll offset so clicks remain accurate during and after movement.
- Use a short, subtle page transition and brief hover/switch transitions. Use a double-buffered paint surface to prevent flicker.
- Run animation frames only while a transition is active and the window is visible. Respect the Windows animation preference by skipping transitions when animations are disabled.
- Keep the existing data refresh timers and cleaner cadence independent from UI animation timers.

## Implementation boundaries

- Keep the app as the existing single-process C++17 Win32 application.
- Keep the current page model and command IDs; do not change cleaner, startup, update, process, or timer semantics.
- Keep the current GDI page renderer. Use Direct2D/DirectWrite only to render the small emoji glyphs into the existing paint surface; draw all other new app icons with GDI vector primitives.
- Put deterministic easing and scroll-bound calculations in a small testable UI helper. Keep drawing and animation scheduling in the existing UI layer unless implementation inspection finds a concrete reason for a separate renderer module.
- Avoid adding a large framework or runtime dependency. Any Windows rendering API added for color emoji must remain part of the native Windows stack.

## Failure and fallback behavior

- If color emoji cannot be rendered on the running Windows version, display the specified vector fallback without failing page paint.
- If animation setup is unavailable or disabled by Windows, immediately draw the final state; controls must remain usable.
- If a list has no overflow, scrolling remains at zero and does not start an animation timer.
- Any transient back-buffer or icon creation failure falls back to the existing direct paint path and keeps interaction available.

## Verification

- Add focused tests for easing endpoints, intermediate progress, and scroll clamping.
- Run the full existing CMake/CTest suite and build both app executables on Windows.
- Verify visual sharpness and click alignment at 100%, 125%, and 150% Windows scaling, and at the normal window size plus a narrower resized window.
- Manually verify wheel scrolling in Processes and Startup, row toggles and delete hit targets, page navigation, timer slider, theme toggle, and cleaner controls.
- Confirm animations stop when idle and do not increase the existing refresh/clean cadence.

## Explicitly out of scope

- Changing cleaner privileges, task-scheduler setup, startup item discovery or write policy, process-cleaning behavior, timer resolution behavior, automatic update behavior, or tray behavior.
- Adding pages, extra toggles, gradients, background artwork, or another app process.
