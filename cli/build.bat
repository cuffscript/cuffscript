@echo off
REM Builds cuffsh.exe (the interactive shell) against the CuffScript engine
REM exactly as shipped in ..\engine - nothing under the project root is
REM modified or overwritten by this script. Run from anywhere; paths below
REM are relative to this file's own folder.
REM Requires a MinGW-w64 g++ (or MSVC's own toolchain adapted similarly) on
REM PATH; set CXX to use another compiler. Produces cuffsh.exe in this same
REM cli\ folder.

setlocal EnableDelayedExpansion
cd /d "%~dp0"

if not defined CXX set "CXX=g++"

REM set EXCLUDE_DLC=network hardware   leaves those libraries out, as make EXCLUDE_DLC=... does for cuffc.
set "DLC_NAMES= math string time datetime random list map json network filesystem csv hash path hardware "
set "DLC_FLAGS="
for %%d in (%EXCLUDE_DLC%) do (
    if "!DLC_NAMES: %%d =!"=="!DLC_NAMES!" (
        echo EXCLUDE_DLC cannot leave out %%d ^(those that can be left out are:!DLC_NAMES!^)
        exit /b 1
    )
    set "dlc=%%d"
    for %%L in (A B C D E F G H I J K L M N O P Q R S T U V W X Y Z) do set "dlc=!dlc:%%L=%%L!"
    set "DLC_FLAGS=!DLC_FLAGS! -DCUFF_EXCLUDE_DLC_!dlc!"
)

"%CXX%" -std=c++17 -Wall -Wextra -O2 -Wl,--stack,8388608 -static !DLC_FLAGS! ^
    -o cuffsh.exe main.cpp -lws2_32 -lshell32

if %ERRORLEVEL% NEQ 0 (
    echo Build failed.
    exit /b 1
)

echo Built cli\cuffsh.exe
endlocal
