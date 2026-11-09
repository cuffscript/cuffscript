# Build settings shared by every module (used by ndk-build; see android/README.md).
APP_ABI := arm64-v8a armeabi-v7a x86_64 x86
APP_PLATFORM := android-21
APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -fexceptions -frtti
APP_OPTIM := release
