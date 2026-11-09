# Android

Builds `cuffc` (the runner) and `cuffsh` (the interactive shell) as command-line executables for Android. The engine is header-only C++17, so nothing outside this folder is needed or changed.

| File | Purpose |
| :--- | :--- |
| `jni/Android.mk`, `jni/Application.mk` | `ndk-build` modules and settings (arm64-v8a, armeabi-v7a, x86_64, x86; API level 21; static `libc++`) |
| `CMakeLists.txt` | The same two executables for CMake and Gradle's `externalNativeBuild` |
| `build.sh`, `build.bat` | Run `ndk-build` in this folder (`ndk-build -C android` does the same); `build.sh` is for Linux, macOS and Git Bash or WSL, `build.bat` for Windows |

## Build

```bash
ANDROID_NDK_HOME=/path/to/android-ndk android/build.sh
```

On Windows (Command Prompt):

```bat
set ANDROID_NDK_HOME=C:\path\to\android-ndk
android\build.bat
```

The executables are written to `android/libs/<abi>/`. With CMake:

```bash
cmake -S android -B build-android -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21
cmake --build build-android
```

## Run on a device

```bash
adb push android/libs/arm64-v8a/cuffc /data/local/tmp/
adb push examples/01_hello.cuff /data/local/tmp/
adb shell /data/local/tmp/cuffc /data/local/tmp/01_hello.cuff
```

Android does not allow execution from shared storage (`/sdcard`), so use `/data/local/tmp` or an app's own directory. `cuffsh` needs a terminal, so run it from `adb shell` or a terminal app.

## Notes

- Needs an NDK with C++17 `<filesystem>` (r23 or newer) and API level 21 or higher.
- The network library speaks plain HTTP over BSD sockets, so an app that uses it needs the `INTERNET` permission.
- Not yet tested on a device.
