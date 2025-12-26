# XFCE4 Floating Pomodoro — Development Plan

This document defines the project goals, architecture, visual language, coding standards, and a PR‑by‑PR roadmap. It intentionally contains no implementation code.

## 1) Project Goal

Build a low‑power Pomodoro timer for Debian XFCE (X11) with:

- A modern, well‑designed main window for tasks and settings.
- An always‑visible, draggable, resizable, half‑transparent floating ball overlay with progress + remaining time.
- System tray integration for quick control.
- Focus guard features: blacklist app warnings + Chrome page relevance checks using Ollama.
- Installable via `.deb` and manageable via apt.

This project optimizes for the current machine: XFCE panel + Xfwm4 compositor enabled, X11 session.

## 2) Target Environment

- OS/Desktop: Debian (XFCE 4.20), X11
- Compositor: Xfwm4 (compositing enabled)
- Toolkit: GTK4
- Language: C
- Tray: StatusNotifierItem (SNI) over D‑Bus (modern approach)

Constraints:

- Transparency depends on compositor (present on this machine).
- “Always on top” is a WM hint and may be overridden; must degrade gracefully.
- CPU/battery usage must be minimal.

## 3) Product Scope

### Core features
- Pomodoro timer with focus, short break, long break, and pause.
- Task list with reusable history (reactivate completed tasks).
- Configurable archive strategies: default “3 days”, “archive immediately”, “keep latest N”.
- Floating ball overlay: always visible, draggable, resizable, adjustable transparency.
- Hover expansion animation: shows current and next task.
- Tray icon: left‑click opens main window; right‑click menu actions.
- Right‑click menu on overlay mirrors tray actions.

### Focus Guard
- Blacklist apps: repeated warnings when blacklisted app is active.
- Chrome relevance check: when Chrome is active and timer running, use CDP to inspect the active tab and compare against current task; warn when off‑task.
- Warning style: in‑app red/pulsing overlay + nice animation (not OS notification styling).
- Usage stats: track time spent per app and show in UI.

### Non‑goals (initially)
- Cross‑desktop support beyond XFCE X11.
- Multiple browser support beyond Chrome.
- Cloud sync or multi‑device.

## 4) Architecture Overview

### High‑level modules
- **App Core**
  - App lifecycle
  - Settings management
  - Persistence (tasks, settings, stats)
- **Timer Engine**
  - State machine (focus/break/paused)
  - Ticks and notifications
- **Task Manager**
  - Tasks, completion history, archiving rules
- **Overlay Window**
  - Floating ball UI
  - Progress rendering
  - Hover expansion + animation
  - Right‑click context menu
- **Tray Integration (SNI)**
  - D‑Bus status notifier item
  - Menu actions
- **Focus Guard**
  - Active window detection (X11)
  - Blacklist warnings
  - App usage stats
  - Chrome + Ollama integration
- **UI/UX**
  - GTK4 UI and CSS
  - Modern typography and color system

### Primary data flows
- Timer tick → overlay progress update → (optional) warning checks
- Active window change → blacklist check → warning overlay + stats update
- Chrome active → CDP query → Ollama relevance → warning overlay

## 5) Project Structure (proposed)

```
.
├─ data/
│  ├─ icons/
│  ├─ desktop/
│  └─ styles/
├─ docs/
│  └─ DEV_PLAN.md
├─ src/
│  ├─ app/
│  ├─ core/
│  ├─ ui/
│  ├─ overlay/
│  ├─ tray/
│  ├─ focus/
│  ├─ storage/
│  └─ utils/
├─ packaging/
│  └─ debian/
├─ tests/
└─ meson.build
```

Notes:
- The exact structure may change after initial scaffolding, but separation between core, UI, overlay, tray, and focus guard is required.
- Keep platform‑specific X11 code isolated in `focus/` or `utils/x11/`.
- Current structure includes `src/app/` (app init + state) and `src/ui/` (main window, dialogs, task list) with `src/main.c` acting as a thin entry point.

## 6) Coding Standards

- Language: C (C11).
- Use clear module boundaries and minimal global state.
- Prefer small, testable functions.
- Avoid unnecessary allocations in hot paths.
- Polling is avoided; use event‑driven signals where possible.
- Logging must be structured and quiet by default.
- UI thread must never block (especially during Ollama calls).
- Error handling: fail gracefully and notify the user where applicable.
- UI ownership & lifetime guardrails:
  - Store only top-level windows (or other long-lived widgets) in shared app state.
  - Per-window widgets live inside a window-scoped struct owned by that window and stored via `g_object_set_data_full`.
  - Avoid holding pointers to child widgets across windows; pass data, not widgets.
  - Prefer GTK helpers that manage model ownership (e.g., `gtk_drop_down_new_from_strings`) and avoid manual ref/unref unless you clearly own the object.
  - Signals should use window-scoped user data so teardown can’t touch stale pointers.

### Power/Battery Guidelines
- Timer tick at 1 Hz; avoid higher frequency animations.
- Warning overlays should be lightweight.
- Chrome/Ollama checks must be rate‑limited.
- No busy loops.

## 7) Visual Design Language

### Palette (initial proposal)
- Background: warm ivory `#F6F1E7`
- Primary: deep teal `#0F4C5C`
- Accent: warm orange `#E36414`
- Success: soft green `#3D8361`
- Warning: amber `#F2A900`
- Error: red `#D7263D`
- Text: charcoal `#1E1E1E`
- Secondary text: slate `#5B5B5B`

### Typography
- Modern sans serif; default choice: Manrope or Source Sans 3.
- Use one font family for performance; vary weights for hierarchy.

### Overlay design
- Circular with subtle radial gradient.
- Progress ring with smooth easing.
- Hover expansion: quick, clean, 200–250ms, not “bouncy”.
- Warning mode: red pulsing halo; readable, not aggressive.

### Main window
- Minimal, modern layout.
- Clear separation between tasks, timer, and settings.
- Flat components with subtle shadows and consistent spacing.

## 8) Testing Strategy (non‑code)

- Manual functional tests on this machine for overlay, tray, focus guard, and transparency.
- Basic regression checklist before each PR merge.
- For focus guard: verify repeated warnings and stats logging.
- For Chrome/Ollama: verify “no debug port” gracefully and “model list refresh” behavior.

## 9) PR‑by‑PR Roadmap

Each PR includes motivation, implementation instructions, and testable standards.

### PR 1 — Project scaffolding
**Status:** Complete (2025-12-23).  
**Motivation:** Establish a clean GTK4+C base that builds and runs.  
**Instructions:** Set up Meson; app ID; basic window; logging; README.  
**Testable standard:** Build passes; running binary opens a window without errors.

### PR 2 — Visual system & fonts
**Status:** Complete (2025-12-23).  
**Motivation:** Avoid default GTK look; lock a modern visual identity.  
**Instructions:** Create CSS theme, palette, typography; bundle/declare font.  
**Testable standard:** UI uses new colors and font; no default theme styling visible.

### PR 3 — Task model & persistence
**Motivation:** Core feature: reusable tasks with archive rules.  
**Instructions:** Data model; save/load; archive strategies (3 days default, immediate, keep N).  
**Testable standard:** Tasks persist across restarts; reactivation works; archive behavior is configurable.

### PR 4 — Timer engine + main controls
**Motivation:** Deliver working Pomodoro with minimal UX.  
**Instructions:** Timer state machine; start/pause/skip; durations in settings.  
**Testable standard:** Timer transitions correct; 1 Hz updates; no UI stalls.

### PR 5 — Floating ball overlay
**Motivation:** Signature overlay experience.  
**Instructions:** Frameless overlay; circular style; opacity slider; drag/resize; hover expansion; right‑click menu; X11 keep‑above hint.  
**Testable standard:** Always visible; opacity works; drag/resize works; hover animation smooth; menu actions work; stays above on this machine.

### PR 6 — Tray integration (SNI)
**Motivation:** Fast control without opening main window.  
**Instructions:** Implement StatusNotifierItem; left‑click main window; right‑click menu; mirror overlay actions.  
**Testable standard:** Tray icon appears in XFCE panel; actions are functional; no crashes on panel restart.

### PR 7 — Focus Guard v1 (blacklist + stats)
**Motivation:** Reduce distraction and track app usage.  
**Instructions:** Active window tracking; blacklist warnings; stats per app; configurable warning interval (default 1–2 seconds).  
**Testable standard:** Blacklisted app triggers repeated warnings; stats update; disabling warnings stops them.

### PR 8 — Chrome + Ollama relevance checks
**Motivation:** Intelligent off‑task detection.  
**Instructions:** Detect Chrome active; CDP read active tab; Ollama relevance check; model dropdown from `ollama list` with auto‑refresh; warning overlay on mismatch.  
**Testable standard:** Works when Chrome debug port is enabled; shows clear message when it’s not; model list refreshes.

### PR 9 — Packaging (.deb)
**Motivation:** Installable via apt on this machine.  
**Instructions:** Debian packaging metadata; desktop entry; icons; fonts; dependencies.  
**Testable standard:** `dpkg -i` installs and runs cleanly; menu entry exists; uninstall works.

## 10) Risks and Mitigations

- **Always‑on‑top hint ignored by WM**: Use best‑effort hint; warn user in settings if not respected.
- **System notifications styling**: Use custom in‑app warning overlay for “red blinking” design.
- **Chrome debug port not enabled**: Detect and show guidance; degrade gracefully.
- **Ollama latency**: Rate‑limit checks and run off the UI thread.

## 11) Decision Log

- GTK4 + C selected for performance and future‑proofing.
- Overlay always visible; no click‑through.
- Only Chrome supported for relevance checks (for now).
- Modern visual design prioritized over default GTK styling.
- 2025-12-26: Refactored `src/main.c` into `src/app/` and `src/ui/` modules to enforce separation of concerns; `src/main.c` now only boots the app.
