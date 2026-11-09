@echo off
REM Builds cuffc and cuffsh for Android with ndk-build (jni\Android.mk, jni\Application.mk).
REM Nothing outside android\ is modified. Run from anywhere.
REM Usage: set ANDROID_NDK_HOME=C:\path\to\ndk   then   android\build.bat [extra ndk-build arguments]
REM The executables land in android\libs\<abi>\ (push them with adb to /data/local/tmp).

setlocal
cd /d "%~dp0"

if not defined ANDROID_NDK_HOME set "ANDROID_NDK_HOME=%ANDROID_NDK_ROOT%"
if not defined ANDROID_NDK_HOME (
    echo Set ANDROID_NDK_HOME to an Android NDK ^(r23 or newer^).
    exit /b 1
)
if not exist "%ANDROID_NDK_HOME%\ndk-build.cmd" (
    echo ndk-build.cmd not found in "%ANDROID_NDK_HOME%".
    exit /b 1
)

call "%ANDROID_NDK_HOME%\ndk-build.cmd" -C . %*
if %ERRORLEVEL% NEQ 0 (
    echo Build failed.
    exit /b 1
)

echo Built android\libs\^<abi^>\cuffc and android\libs\^<abi^>\cuffsh
