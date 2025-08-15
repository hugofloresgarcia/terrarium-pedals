# Project Name
TARGET = your_pedal

# Sources
CPP_SOURCES = wigglrs.cpp 

# Library Locations
LIBDAISY_DIR = ../libDaisy
DAISYSP_DIR = ../DaisySP

# Core location, and generic Makefile.
# Include terrarium.h
C_INCLUDES += -I../Terrarium
C_INCLUDES += -I../flib

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

LDFLAGS += -u _printf_float
