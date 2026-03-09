# Storage Tool

Easy Windows desktop app to find the biggest files on a drive.

## Features

- Clean UI with one-click drive buttons (`C:\`, `D:\`, etc.)
- Top-N selector (choose how many largest files to show)
- Fast scan with permission errors skipped automatically
- Results list with full path and human-readable size
- Native `.exe` output

## Build

```bat
build.bat
```

This compiles:

- `storage_tool.exe`

## Run

Double-click `storage_tool.exe`, then:

1. Set how many top files you want.
2. Click a drive button.
3. Review the largest files list.

## Notes

- Built as a native C++ Win32 app.
- No Python/runtime dependencies needed to run the `.exe`.
