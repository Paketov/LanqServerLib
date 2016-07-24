# Lanq(Lan Quick)
# Solodov A. N. (hotSAN)
# 2016
# Build main library
# 
#

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

SOURCE_DIR 			:= $(LOCAL_PATH)/../Src
INCLUDE_DIRS 		:= $(LOCAL_PATH)/../Src



LOCAL_CFLAGS := -w -std=c++0x -DLANQBUILD -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -fexceptions -fpermissive -fno-stack-protector

ifdef DEBUG
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -O0 -g
else
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -O3
endif

LOCAL_LDFLAGS := -rdynamic -fvisibility=hidden -fvisibility-inlines-hidden
LOCAL_MODULE := lanq
LOCAL_SHARED_LIBRARIES := 


rwildcard 			= $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

SOURCE_SUBDIRS 		:= $(sort $(dir $(call rwildcard,$(SOURCE_DIR)/,*.c) $(call rwildcard,$(SOURCE_DIR)/,*.cpp)))
INCLUDE_SUBDIRS 	:= $(sort $(dir $(call rwildcard,$(INCLUDE_DIRS)/,*)))

LOCAL_SRC_FILES 	:= $(sort $(call rwildcard,$(SOURCE_DIR)/,*.c) $(call rwildcard,$(SOURCE_DIR)/,*.cpp))
LOCAL_C_INCLUDES	:= $(INCLUDE_SUBDIRS)


include $(BUILD_SHARED_LIBRARY)
