# Lanq(Lan Quick)
# Solodov A. N. (hotSAN)
# 2016
# Build main library
# 
#

CURRENT_DIR 		:= $(shell pwd)
CC 			        := g++
rwildcard 			= $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

ifdef DEBUG
CFLAGS := -O0 -g
else
CFLAGS := -O3
endif

CFLAGS				:= $(CFLAGS) -w -DLANQBUILD -std=c++0x -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -fpermissive
LDFLAGS				:= -shared -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -fpermissive

LDLIBS				:= -lpthread -ldl
PROGRAM 			:= liblanq.so
SOURCE_DIR 			:= ../Src
INCLUDE_DIRS 		:= ../Src
OBJECT_DIR			:= Obj




SOURCE_SUBDIRS 		:= $(sort $(dir $(call rwildcard,$(SOURCE_DIR)/,*.c) $(call rwildcard,$(SOURCE_DIR)/,*.cpp)))
INCLUDE_SUBDIRS 	:= $(sort $(dir $(call rwildcard,$(INCLUDE_DIRS)/,*)))

SOURCES 			:= $(sort $(call rwildcard,$(SOURCE_DIR)/,*.c) $(call rwildcard,$(SOURCE_DIR)/,*.cpp))

OBJECTS_SUBDIRS		:= $(sort $(dir $(SOURCES)))
OBJECTS_SUBDIRS		:= $(OBJECTS_SUBDIRS:$(SOURCE_DIR)%=%)
OBJECTS_SUBDIRS		:= $(addprefix $(OBJECT_DIR),$(OBJECTS_SUBDIRS))

OBJECTS 			:= $(SOURCES:$(SOURCE_DIR)%=%)
OBJECTS 			:= $(addprefix $(OBJECT_DIR),$(OBJECTS))

OBJECTS  			:= $(OBJECTS:%.cpp=%.o)
OBJECTS  			:= $(OBJECTS:%.c=%.o)



all : $(PROGRAM)

$(PROGRAM) : obj_dirs $(OBJECTS)
	@echo " Link module"
	@$(CC) -o $@ $(OBJECTS) $(LDFLAGS) $(LDLIBS)
	@rm -f ./CodeBlocks/$(PROGRAM)
	@echo " Create link to main module"
	@ln -s -r $(PROGRAM) ./CodeBlocks/$(PROGRAM)
	@echo " Make console shell module"
	@cd ../Modules/HttpConsoleShell/Linux && make
	@echo " Make auto load module"
	@cd ../Modules/MdlAutoLoad/Linux && make
	
obj_dirs :
	@echo " Create object subdirs"
	@mkdir -p $(OBJECTS_SUBDIRS)

$(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.cpp
	@echo " Compile " $<
	@$(CC) -o $@ -c $< $(CFLAGS) $(addprefix -I, $(INCLUDE_SUBDIRS))

$(OBJECT_DIR)/%.o: $(SOURCE_DIR)/%.c
	@echo " Compile " $<
	@$(CC) -o $@ -c $< $(CFLAGS) $(addprefix -I, $(INCLUDE_SUBDIRS))

clean :
	@echo " Remove object subdirs" $(OBJECTS_SUBDIRS) " remove module" $(PROGRAM)
	@rm -rf $(OBJECTS_SUBDIRS) $(PROGRAM)
	@cd ../Modules/HttpConsoleShell/Linux && make clean

include $(wildcard $(addsuffix /*.d, $(OBJECTS_SUBDIRS)))

