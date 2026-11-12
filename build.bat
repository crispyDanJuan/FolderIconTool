@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

where gcc >nul 2>nul
if errorlevel 1 (
    echo [ERROR] gcc not found. Install MinGW-w64 / MSYS2 and add its bin folder to PATH.
    exit /b 1
)
where windres >nul 2>nul
if errorlevel 1 (
    echo [ERROR] windres not found. It ships with MinGW-w64 binutils.
    exit /b 1
)

if not exist dist mkdir dist

pushd src
echo [1/2] Compiling resources...
windres app.rc -O coff -o app_res.o
if errorlevel 1 (
    popd
    echo [ERROR] Resource compilation failed.
    exit /b 1
)

echo [2/2] Building executable...
gcc -Os -Wall -Wextra -municode -static -static-libgcc -s -mwindows ^
    -o "..\dist\FolderIconTool.exe" ^
    folder_icon_tool.c app_res.o ^
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32 -lkernel32
if errorlevel 1 (
    popd
    echo [ERROR] Compilation failed.
    exit /b 1
)
del app_res.o
popd

for %%F in ("dist\FolderIconTool.exe") do echo Done: dist\FolderIconTool.exe  ^(%%~zF bytes^)
endlocal
