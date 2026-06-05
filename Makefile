# ─────────────────────────────────────────────────────────────────────────────
# Makefile — dualLooper for Daisy Seed
# ─────────────────────────────────────────────────────────────────────────────

# Project name (produces dualLooper.bin / dualLooper.elf)
TARGET = dualLooper

# Enable DaisySP LGPL modules (required for varSpeedLooper / Oscillator etc.)
USE_DAISYSP_LGPL = 1

# Compiler optimisation
OPT = -O1

# Source files
CPP_SOURCES = dualLooper.cpp

# ── Library paths ─────────────────────────────────────────────────────────────
# Adjust these to match where you cloned libDaisy and DaisySP.
# Default assumes this project sits inside a folder next to both libraries:
#
#   workspace/
#     libDaisy/
#     DaisySP/
#     dualLooper/       ← this Makefile lives here
#
LIBDAISY_DIR = ../libDaisy
DAISYSP_DIR  = ../DaisySP

# ── Core Makefile (do not edit below this line) ───────────────────────────────
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
