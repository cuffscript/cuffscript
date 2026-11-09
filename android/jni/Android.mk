# ndk-build module definitions: the runner (cuffc) and the interactive shell (cuffsh).
# Both are plain command-line executables; the engine is header-only, so each is a single translation unit.
LOCAL_PATH := $(call my-dir)
ENGINE_ROOT := $(LOCAL_PATH)/../..

include $(CLEAR_VARS)
LOCAL_MODULE := cuffc
LOCAL_SRC_FILES := ../../main.cpp
LOCAL_C_INCLUDES := $(ENGINE_ROOT)
LOCAL_CPPFLAGS := -O3 -DNDEBUG -Wall -Wextra
LOCAL_CPP_FEATURES := exceptions rtti
LOCAL_LDFLAGS := -Wl,--gc-sections
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := cuffsh
LOCAL_SRC_FILES := ../../cli/main.cpp
LOCAL_C_INCLUDES := $(ENGINE_ROOT)
LOCAL_CPPFLAGS := -O2 -DNDEBUG -Wall -Wextra
LOCAL_CPP_FEATURES := exceptions rtti
LOCAL_LDFLAGS := -Wl,--gc-sections
include $(BUILD_EXECUTABLE)
