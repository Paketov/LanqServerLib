# Lanq(Lan Quick)
# Solodov A. N. (hotSAN)
# 2016
# Example module
# 
#

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

SOURCE_DIR 		:= $(LOCAL_PATH)/../Src
INCLUDE_DIRS 		:= $(LOCAL_PATH)/../Src $(LOCAL_PATH)/../../../Src

ifdef DEBUG
OPTIM_CFLAGS		:= $(LOCAL_CFLAGS) -O0 -g
else
OPTIM_CFLAGS		:= $(LOCAL_CFLAGS) -O3
endif

LOCAL_CFLAGS		:= $(OPTIM_CFLAGS) -w -std=c++0x -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -fpermissive
LOCAL_MODULE		:= remoteterm
LOCAL_SHARED_LIBRARIES := lanq_lib
LOCAL_LDFLAGS		:= $(OPTIM_CFLAGS) -shared -fvisibility=hidden -fvisibility-inlines-hidden -fpermissive 


rwildcard 		= $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

SOURCE_SUBDIRS 		:= $(sort $(dir $(call rwildcard,$(SOURCE_DIR)/,*.c) $(call rwildcard,$(SOURCE_DIR)/,*.cpp)))
INCLUDE_SUBDIRS 	:= $(sort $(dir $(call rwildcard,$(INCLUDE_DIRS)/,*)))

LOCAL_SRC_FILES 	:= $(sort $(call rwildcard,$(SOURCE_DIR)/,*.c) $(call rwildcard,$(SOURCE_DIR)/,*.cpp))
LOCAL_C_INCLUDES	:= $(INCLUDE_SUBDIRS)


include $(BUILD_SHARED_LIBRARY) 

include $(CLEAR_VARS) 
LOCAL_MODULE := lanq_lib 
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../Android/libs/armeabi-v7a/liblanq.so
include $(PREBUILT_SHARED_LIBRARY) 
