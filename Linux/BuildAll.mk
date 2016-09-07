

MODULE_DIRS     := $(sort $(dir $(wildcard ../Modules/*/Linux/)))
IS_CLEAR        :=



all: MAIN_LIB_BUILD $(MODULE_DIRS)

clear: MAIN_LIB_CLEAR $(MODULE_DIRS)

MAIN_LIB_BUILD: 
	make -f Makefile

$(MODULE_DIRS): 
	cd $@ && make 

$(MAIN_LIB_CLEAR):
	cd $@ && make 


.PHONY: all $(MODULE_DIRS)

