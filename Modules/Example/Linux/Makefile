# Lanq(Lan Quick)
# Solodov A. N. (hotSAN)
# 2016
# Example module
# 
#

CURRENT_DIR         := $(shell pwd)
CC                  := g++ $(COMPILE_FLAGS)
rwildcard           = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

ifdef DEBUG
CFLAGS              := -O0 -g
else
CFLAGS              := -O3
endif

CFLAGS              := $(CFLAGS) -w -std=c++0x -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -fpermissive -fno-stack-protector
LDFLAGS             := -shared  -L../../../Linux -fvisibility=hidden -fvisibility-inlines-hidden -fpermissive -fno-stack-protector
LDLIBS              := -llanq

PROGRAM             := libexample.so
SOURCE_DIR          := ../Src
INCLUDE_DIRS        := ../Src ../../../Src
OBJECT_DIR          := Obj




SOURCE_SUBDIRS      := $(sort $(dir $(call rwildcard,$(SOURCE_DIR)/,*.c) $(call rwildcard,$(SOURCE_DIR)/,*.cpp)))
INCLUDE_SUBDIRS     := $(sort $(dir $(call rwildcard,$(INCLUDE_DIRS)/,*) ))

SOURCES             := $(sort $(call rwildcard,$(SOURCE_DIR)/,*.c) $(call rwildcard,$(SOURCE_DIR)/,*.cpp))

OBJECTS_SUBDIRS     := $(sort $(dir $(SOURCES)))
OBJECTS_SUBDIRS     := $(OBJECTS_SUBDIRS:$(SOURCE_DIR)%=%)
OBJECTS_SUBDIRS     := $(addprefix $(OBJECT_DIR),$(OBJECTS_SUBDIRS))

OBJECTS             := $(SOURCES:$(SOURCE_DIR)%=%)
OBJECTS             := $(addprefix $(OBJECT_DIR),$(OBJECTS))

OBJECTS             := $(OBJECTS:%.cpp=%.o)
OBJECTS             := $(OBJECTS:%.c=%.o)



all : $(PROGRAM)

$(PROGRAM) : obj_dirs $(OBJECTS)
	@echo " Link module" $(PROGRAM)
	@$(CC) -o $@ $(OBJECTS) $(LDFLAGS) $(LDLIBS)

	
obj_dirs :
	@echo " Create object subdirs"
	@mkdir -p $(OBJECTS_SUBDIRS)


$(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.cpp
	@echo " Compile " $<
	@$(CC) -o $@ -c $< $(CFLAGS) $(addprefix -I, $(INCLUDE_SUBDIRS))

$(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.c
	@echo " Compile " $<
	@$(CC) -o $@ -c $< $(CFLAGS) $(addprefix -I, $(INCLUDE_SUBDIRS))

clear :
	@echo " Remove object subdirs" $(OBJECTS_SUBDIRS) " remove module" $(PROGRAM)
	@rm -rf $(OBJECTS_SUBDIRS) $(PROGRAM)

include $(wildcard $(addsuffix /*.d, $(OBJECTS_SUBDIRS)))

