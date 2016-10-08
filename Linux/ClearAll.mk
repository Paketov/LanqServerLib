

MODULE_DIRS     := $(sort $(dir $(wildcard ../Modules/*/Linux/)))
IS_CLEAR        :=



all: MAIN_LIB_BUILD $(MODULE_DIRS)

MAIN_LIB_BUILD:
	make --file=Makefile clear

$(MODULE_DIRS): 
	cd $@ && make clear

.PHONY: all $(MODULE_DIRS)

