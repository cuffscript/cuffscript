@echo off
REM Builds cuffsh.exe (the interactive shell) against the CuffScript engine
REM exactly as shipped in ..\engine - nothing under the project root is
REM modified or overwritten by this script. Run from anywhere; paths below
REM are relative to this file's own folder.
REM Requires a MinGW-w64 g++ (or MSVC's own toolchain adapted similarly) on
REM PATH; set CXX to use another compiler. Produces cuffsh.exe in this same
REM cli\ folder.

setlocal
cd /d "%~dp0"

if not defined CXX set "CXX=g++"
"%CXX%" -std=c++17 -Wall -Wextra -O2 -Wl,--stack,8388608 -static ^
    -o cuffsh.exe main.cpp -lws2_32 -lshell32

if %ERRORLEVEL% NEQ 0 (
    echo Build failed.
    exit /b 1
)

echo Built cli\cuffsh.exe
endlocal
