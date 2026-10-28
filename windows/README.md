# SimpleSecCamViewer

## Configuration

1. Install a 32-bit VLC 3 version compatible with Windows XP.
2. Copy `iniciar.bat.example` to `iniciar.bat` alongside `SimpleSecCamViewer.exe`.
3. Edit the NVR IP address, username, password and VLC path. The example IP address is a placeholder.
4. Open `SimpleSecCamViewer.exe` and configure the cameras from its menus.

The settings file contains the password in plain text. Do not share or publish it. Passwords containing special characters and different NVR models require compatibility testing.

## Build

With MinGW-w64 i686 and binutils on your PATH, run the following from this directory:

```sh
i686-w64-mingw32-windres -I . -i camxp.rc -o camxp-resource.o
i686-w64-mingw32-gcc -std=c99 -Os -march=pentium-m -mwindows -nostdlib -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -Wl,--entry,_WinMainCRTStartup@0 -Wl,--subsystem,windows:5.1 -Wl,--major-os-version,5,--minor-os-version,1 -o SimpleSecCamViewer.exe camxp.c camxp-resource.o -lkernel32 -luser32 -lgdi32 -lgcc
i686-w64-mingw32-strip SimpleSecCamViewer.exe
```

The executable requires a compatible 32-bit VLC installation and its plugins. `-march=pentium-m` targets the original device; other older processors may require a different target and testing.

## Local tests

```sh
cc -std=c99 -I. tests/test-layout.c -o /tmp/camxp-test-layout && /tmp/camxp-test-layout
cc -std=c99 -I. tests/test-watchdog.c -o /tmp/camxp-test-watchdog && /tmp/camxp-test-watchdog
```

These tests cover layout and recovery rules, but do not replace testing on Windows XP with VLC and an NVR.
