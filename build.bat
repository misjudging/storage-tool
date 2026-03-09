@echo off
setlocal

set ZIG_EXE=%LOCALAPPDATA%\Microsoft\WinGet\Packages\zig.zig_Microsoft.Winget.Source_8wekyb3d8bbwe\zig-x86_64-windows-0.15.2\zig.exe

if not exist "%ZIG_EXE%" (
  echo Zig compiler not found at:
  echo %ZIG_EXE%
  echo Install Zig or update ZIG_EXE in build.bat.
  exit /b 1
)

"%ZIG_EXE%" c++ -std=c++17 -O2 -municode -mwindows storage_tool.cpp -o storage_tool.exe -lcomctl32 -luser32 -lgdi32 -lshell32 -lole32 -ladvapi32
if errorlevel 1 exit /b 1

echo Built storage_tool.exe
