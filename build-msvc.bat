@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
  echo ERROR: cl.exe not found.
  echo Open an MSVC Developer Command Prompt or load your portable MSVC environment first.
  exit /b 1
)
cl /nologo /std:c++17 /EHsc /W4 /O2 /DNDEBUG /DUNICODE /D_UNICODE ClipDrag.cpp /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib
if errorlevel 1 exit /b 1
echo Build successful: ClipDrag.exe
