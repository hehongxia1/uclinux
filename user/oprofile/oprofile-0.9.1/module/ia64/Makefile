#
# module/ia64/Makefile.in
# 
# Will Cohen
# 7/29/2002

OPROFILE_DIR=/home/ksh/cvs/commit/11-23/uClinux-dist/user/blkfin-apps/oprofile
VERSION=0.9.1
KVERS=
KSRC=

EXTRA_CFLAGS= -DEXPECT_OK -D__NO_VERSION__ \
	-I$(OPROFILE_DIR)/ -I$(OPROFILE_DIR)/libutil -I$(OPROFILE_DIR)/libop -I$(OPROFILE_DIR)/module
 
USE_STANDARD_AS_RULE=1
 
# NB: do not add a file here without adding it to EXTRA_DIST in ../Makefile.am

O_TARGET := arch.o
 
obj-y := cpu_type.o op_pmu.o op_syscalls.o oprofile_stubs.o
obj-m := $(O_TARGET)
O_OBJS := $(obj-y)
M_OBJS := $(O_TARGET)
 
TOPDIR := $(KSRC)
CONFIG_SHELL := TOPDIR=$(KSRC) /bin/bash
include $(KSRC)/Rules.make

# FIXME: dependencies 
