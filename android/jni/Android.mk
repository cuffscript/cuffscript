# ndk-build module definitions: the runner (cuffc) and the interactive shell (cuffsh).
# Both are plain command-line executables; the engine is header-only, so each is a single translation unit.
LOCAL_PATH := $(call my-dir)
ENGINE_ROOT := $(LOCAL_PATH)/../..

# Libraries to leave out: `ndk-build EXCLUDE_DLC="network hardware"` (or android/build.sh EXCLUDE_DLC="..."), as in the
# root Makefile. DLC:convert cannot be left out.
DLC_NAMES := math string time datetime random list map json network filesystem csv hash path hardware
ifneq ($(filter-out $(DLC_NAMES),$(EXCLUDE_DLC)),)
$(error EXCLUDE_DLC cannot leave out $(filter-out $(DLC_NAMES),$(EXCLUDE_DLC)) (those that can be left out are: $(DLC_NAMES)))
endif
dlc_upper = $(subst a,A,$(subst b,B,$(subst c,C,$(subst d,D,$(subst e,E,$(subst f,F,$(subst g,G,$(subst h,H,$(subst i,I,$(subst j,J,$(subst k,K,$(subst l,L,$(subst m,M,$(subst n,N,$(subst o,O,$(subst p,P,$(subst q,Q,$(subst r,R,$(subst s,S,$(subst t,T,$(subst u,U,$(subst v,V,$(subst w,W,$(subst x,X,$(subst y,Y,$(subst z,Z,$(1)))))))))))))))))))))))))))
DLC_FLAGS := $(foreach d,$(EXCLUDE_DLC),-DCUFF_EXCLUDE_DLC_$(call dlc_upper,$(d)))

include $(CLEAR_VARS)
LOCAL_MODULE := cuffc
LOCAL_SRC_FILES := ../../main.cpp
LOCAL_C_INCLUDES := $(ENGINE_ROOT)
LOCAL_CPPFLAGS := -O3 -DNDEBUG -Wall -Wextra $(DLC_FLAGS)
LOCAL_CPP_FEATURES := exceptions rtti
LOCAL_LDFLAGS := -Wl,--gc-sections
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := cuffsh
LOCAL_SRC_FILES := ../../cli/main.cpp
LOCAL_C_INCLUDES := $(ENGINE_ROOT)
LOCAL_CPPFLAGS := -O2 -DNDEBUG -Wall -Wextra $(DLC_FLAGS)
LOCAL_CPP_FEATURES := exceptions rtti
LOCAL_LDFLAGS := -Wl,--gc-sections
include $(BUILD_EXECUTABLE)
