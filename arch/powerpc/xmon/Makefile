# SPDX-License-Identifier: GPL-2.0
# Makefile for xmon

GCOV_PROFILE := n
KCOV_INSTRUMENT := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n
KCSAN_SANITIZE := n

# Disable ftrace for the entire directory
ccflags-remove-$(CONFIG_FUNCTION_TRACER) += $(CC_FLAGS_FTRACE)

# Clang stores addresses on the stack causing the frame size to blow
# out. See https://github.com/ClangBuiltLinux/linux/issues/252
ccflags-$(CONFIG_CC_IS_CLANG) += -Wframe-larger-than=4096

obj-y			+= xmon.o nonstdio.o spr_access.o xmon_bpts.o

obj-$(CONFIG_XMON_DISASSEMBLY) += ppc-dis.o ppc-opc.o
