#!/usr/bin/env bash
# Builds cuffc and cuffsh for Android with ndk-build (jni/Android.mk, jni/Application.mk). Nothing outside android/ is modified.
# Usage: ANDROID_NDK_HOME=/path/to/ndk android/build.sh [extra ndk-build arguments]
# The executables land in android/libs/<abi>/ (push them with adb to a directory that allows execution, such as /data/local/tmp).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [ -z "$NDK" ] || [ ! -x "$NDK/ndk-build" ]; then
    echo "Set ANDROID_NDK_HOME to an Android NDK (r23 or newer)." >&2
    exit 1
fi

"$NDK/ndk-build" -C . "$@"

echo "Built android/libs/<abi>/cuffc and android/libs/<abi>/cuffsh"
