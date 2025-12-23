# XFCE4 Floating Pomodoro

Low-power Pomodoro timer for Debian XFCE (X11), built with GTK4 and C.

## Build

Dependencies:
- GTK4 development packages
- Meson + Ninja
- pkg-config

Debian package name for GTK4: `libgtk-4-dev`.

```sh
meson setup build
ninja -C build
```

## Run

```sh
./build/xfce4-floating-pomodoro
```

## Logging

Logging is quiet by default. Set the log level with:

```sh
POMODORO_LOG_LEVEL=info ./build/xfce4-floating-pomodoro
```

Accepted values: `warn` (default), `info`, `debug`.

## App ID

`com.scott.Xfce4FloatingPomodoro`
