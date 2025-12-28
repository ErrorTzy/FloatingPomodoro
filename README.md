# XFCE4 Floating Pomodoro

Low-power Pomodoro timer for Debian XFCE (X11), built with GTK4 and C. It combines a modern main window, a floating always-visible overlay, tray controls, and an optional focus guard that can warn when you drift off-task.

## Highlights

- Pomodoro timer with focus, short break, long break, pause, skip, and stop
- Task list with repeat cycles, inline editing, and task history
- Automatic task completion and configurable archiving rules
- Floating ball overlay with progress ring, hover details, opacity control, and right-click menu
- StatusNotifierItem (SNI) tray icon with quick actions
- Focus guard: blacklist warnings plus app usage stats
- Optional Chrome + Ollama relevance checks for off-task detection

## Screenshots

Add images here:
- Main window
- Floating overlay
- Settings (timer + focus guard)
- Usage stats

## Feature tour

### Timer and task workflow

- Default cycle: 25 min focus, 5 min short break, 15 min long break every 4 focus sessions
- Start/pause/resume/skip/stop from the main window, tray, or overlay
- Timer controls are disabled until an active task exists
- Focus time and completed break count are tracked per run (reset on Stop)

Task behavior:
- Tasks have a "Cycles" count (1-99) that represents how many focus sessions the task needs
- When a focus session ends (the timer switches to a break), the active task is decremented
- When the cycle count reaches zero, the task is marked completed automatically
- Tasks can be set to Active, Pending, or Completed manually via the status button
- Inline edit, archive/restore, and delete actions are available per task

### Task archiving

Archived tasks are kept in a separate window and can be restored later. Archive behavior is configurable:
- After N days (default 3)
- Archive immediately after completion
- Keep latest N completed tasks (default 5)

You can also archive all tasks or delete archived tasks from the Settings window.

### Floating overlay ("floating ball")

An always-visible overlay shows the current timer at a glance:
- Circular progress ring with phase color changes (focus / break / long break)
- Hover to expand and show current and next task
- Opacity slider (0.3 to 1.0, default ~0.65)
- Right-click menu for start/pause, skip, stop, open app, hide, quit
- Draggable by left-click
- Best-effort "always on top", skip taskbar, skip pager hints (X11)

When focus guard warnings trigger, the overlay turns red and displays a warning message. If the overlay was hidden, it is automatically shown.

### Tray integration (SNI)

Uses StatusNotifierItem over D-Bus:
- Left-click (Activate) opens the main window
- Menu actions: Start/Pause/Resume, Skip, Stop, Toggle Floating Ball, Open App, Quit

### Focus guard (blacklist + usage stats)

Focus guard works only during an active focus session with an active task:
- Active window detection via X11 (_NET_ACTIVE_WINDOW + WM_CLASS)
- Blacklist matches are case-insensitive substring checks
- Warnings repeat on a configurable interval (default 1 second)
- Per-task stats are collected during focus sessions
- Global usage stats (optional) track active app usage while the app runs
- Usage stats are shown in the main window (top 5 apps for the current day)
- Click a task row to see per-task usage stats
- Stats are stored in SQLite and pruned after 35 days

### Chrome + Ollama relevance checks (optional)

If built with libsoup + json-glib and Ollama is installed, an additional settings page appears:
- Only runs when Chrome/Chromium is the active app
- Chrome must be launched with a remote debugging port (default 9222)
- Fetches the active tab via CDP and extracts title, URL, and page text (innerText capped at 8000 chars)
- Sends a structured prompt to Ollama and expects one of: "directly relevant", "not sure", "clearly irrelevant"
- Only "clearly irrelevant" triggers warnings
- Relevance checks are rate-limited (every 15 seconds)

The Chrome relevance toggle is disabled until a model is selected from `ollama list`. A refresh button reloads models.

## Settings overview

Timer:
- Focus minutes (1-120)
- Short break minutes (1-30)
- Long break minutes (1-60)
- Long break interval (1-12)

Startup and tray:
- Autostart on login
- Start in tray when autostarting
- Minimize to tray
- Close to tray

Focus guard:
- Warnings on/off
- Global usage stats on/off
- Detection interval (1-60 seconds)
- Blacklist management (manual entry or "Use active app")

Chrome relevance (only when Ollama is available):
- Enable relevance checks
- Select Ollama model
- Refresh model list
- Chrome debug port

Maintenance:
- Reset settings to defaults
- Archive all tasks
- Delete archived tasks
- Delete usage stats

## System requirements

- Debian XFCE, X11 session
- Xfwm4 compositor enabled (for overlay transparency)
- GTK4 (>= 4.8)
- X11 development headers for build

Wayland is not supported.

## Build

Required dependencies:
- GTK4 development packages
- fontconfig
- sqlite3
- pkg-config
- Meson + Ninja
- X11 development headers

Optional (enables Chrome/Ollama features and the integration test):
- libsoup-3.0
- json-glib
- python3 + `trafilatura` (optional; the app detects and reports availability in settings)

Typical Debian packages (may vary by release):
- `libgtk-4-dev`
- `libfontconfig1-dev`
- `libsqlite3-dev`
- `libx11-dev`
- `libsoup-3.0-dev` (optional)
- `libjson-glib-dev` (optional)
- `python3` and `python3-trafilatura` (optional)
- `meson`, `ninja-build`, `pkg-config`

Build commands:

```sh
meson setup build
ninja -C build
```

## Run

```sh
./build/xfce4-floating-pomodoro
```

Autostart launch (used by the autostart .desktop file):

```sh
./build/xfce4-floating-pomodoro --autostart
```

## Data locations

All user data is stored under the XDG data dir:
- Settings: `~/.local/share/xfce4-floating-pomodoro/settings.ini`
- Tasks: `~/.local/share/xfce4-floating-pomodoro/tasks.ini`
- Usage stats: `~/.local/share/xfce4-floating-pomodoro/usage_stats.sqlite3`

Bundled fonts are extracted to:
- `~/.cache/xfce4-floating-pomodoro/fonts/`

Autostart file:
- `~/.config/autostart/xfce4-floating-pomodoro.desktop`

## Logging and debug

Logging is quiet by default. Set:

```sh
POMODORO_LOG_LEVEL=info ./build/xfce4-floating-pomodoro
```

Accepted values: `warn` (default), `info`, `debug`.

Fast timer mode for UI testing:

```sh
POMODORO_TEST_TIMER=1 ./build/xfce4-floating-pomodoro
```

## Chrome and Ollama setup (optional)

1) Ensure Ollama is installed and running. The app detects `ollama` from PATH.
   Optional: install `python3` + `trafilatura`. The app reports its availability in
   Settings but it is not required for Chrome relevance checks.
2) Pull a model:

```sh
ollama pull llama3
```

3) Start Chrome with remote debugging enabled (default port 9222):

```sh
google-chrome --remote-debugging-port=9222
```

4) Open Settings -> Focus guard -> Chrome relevance:
- Refresh model list
- Select a model
- Enable relevance checks

If Chrome is active during a focus session and the page is clearly irrelevant, the overlay will warn.

## Tests

If built with libsoup/json-glib, an integration test is available:

```sh
meson test -C build
```

The Chrome/Ollama test can be configured with:

```sh
POMODORO_TEST_OLLAMA_MODEL=your-model-name meson test -C build
```

## App ID

`com.scott.Xfce4FloatingPomodoro`
