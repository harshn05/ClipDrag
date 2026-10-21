# ClipDrag

A lightweight, portable Windows screenshot clipping utility built with native C++ and Win32 APIs.

## Usage

1. Run `ClipDrag.exe`.
2. Press `Ctrl+Shift+X`.
3. Drag with the left mouse button to select a rectangular screen region.
4. Release to copy the selected image to the clipboard.
5. Paste with `Ctrl+V`. Press `Esc` to cancel.

## Build with CMake and MinGW-w64

Run these commands from an MSYS2 MinGW/UCRT shell with CMake and Ninja installed:

```bash
cmake -S . -B build_mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_mingw
```

The executable is `build_mingw/ClipDrag.exe`.

## Build with MSVC

From a configured MSVC Developer Command Prompt or your portable WinDev MSVC environment:

```bat
cl /nologo /std:c++17 /EHsc /W4 /O2 /DNDEBUG /DUNICODE /D_UNICODE ClipDrag.cpp /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib
```

## Portability

No installer, administrator rights, registry changes, or third-party libraries are required. The app registers a global hotkey only while it is running.

## Limitations

Protected video, secure desktop prompts, and some hardware-accelerated surfaces may not be capturable. The clipboard currently receives a `CF_BITMAP` format.
