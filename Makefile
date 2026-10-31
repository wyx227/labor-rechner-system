# Top-level build for the project-specific TRDP design.
#
# Sources placed below src/ are compiled into build/libtrdp-design.a.  The
# checked-out upstream stack remains isolated in trdp-src/.

TRDP_DIR       := trdp-src/trdp
BUILD_DIR      := build
TARGET          := $(BUILD_DIR)/libtrdp-design.a
PD_DEMO         := $(BUILD_DIR)/pd-demo
PD_SENDER       := $(BUILD_DIR)/pd-sender
PD_RECEIVER     := $(BUILD_DIR)/pd-receiver
MD_NOTIFICATION := $(BUILD_DIR)/md-notification
MD_NOTIFICATION_RECEIVER := $(BUILD_DIR)/md-notification-receiver
SEND_DIAG_DATA	:= $(BUILD_DIR)/send_diag_data
MD_MRMP         := $(BUILD_DIR)/md-mrmp
TRDP_ARCH        := linux
TRDP_LIB         := $(TRDP_DIR)/bld/output/$(TRDP_ARCH)-rel/libtrdp.a

CC              ?= cc
AR              ?= ar
PYTHON          ?= python3
CPPFLAGS        += -DMD_SUPPORT=1 -Iinclude -I$(TRDP_DIR)/src/api -I$(TRDP_DIR)/src/vos/api -I$(TRDP_DIR)/src/common
CFLAGS          ?= -Wall -Wextra -Wpedantic -O2

# Standalone examples have their own targets and are not part of the design library.
SOURCES         := $(filter-out src/pd_demo.c src/pd_sender.c src/pd_receiver.c src/md_notification.c src/md_notification_receiver.c src/md_mrmp.c src/pd_teaching_simulator.c src/md_console_switch_demo.c src/pd_console_temperature_demo.c,$(shell find src -type f -name '*.c' 2>/dev/null))
OBJECTS         := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS            := $(OBJECTS:.o=.d)

.DEFAULT_GOAL := all

.PHONY: all clean trdp pd-demo pd-sender pd-receiver pd-teaching-simulator pd-console-temperature-demo md-notification md-notification-receiver md-mrmp md-console-switch-demo consist-info-template help

ifeq ($(strip $(SOURCES)),)
all:
	@echo "No C sources found under src/; nothing to compile."
else
all: $(TARGET)
endif

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(AR) rcs $@ $^

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

# Build the upstream TRDP static library with a selected upstream profile.
# Example: make trdp TRDP_CONFIG=OSX_X86_64_config
TRDP_CONFIG ?= LINUX_config
trdp:
	$(MAKE) -C $(TRDP_DIR) $(TRDP_CONFIG)
	$(MAKE) -C $(TRDP_DIR) SOA_SUPPORT=0 libtrdp

pd-demo: $(PD_DEMO)

$(PD_DEMO): trdp src/pd_demo.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/pd_demo.c $(TRDP_LIB) -lpthread -o $@

pd-sender: $(PD_SENDER)

$(PD_SENDER): trdp $(TARGET) src/pd_sender.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/pd_sender.c $(TARGET) $(TRDP_LIB) -lpthread -o $@

pd-receiver: $(PD_RECEIVER)

$(PD_RECEIVER): trdp $(TARGET) src/pd_receiver.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/pd_receiver.c $(TARGET) $(TRDP_LIB) -lpthread -o $@

send_diag_data: $(SEND_DIAG_DATA)

$(SEND_DIAG_DATA): trdp $(TARGET) src/send_diag_data.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/send_diag_data.c $(TARGET) $(TRDP_LIB) -lpthread -o $@

md-notification: $(MD_NOTIFICATION)

$(MD_NOTIFICATION): trdp $(TARGET) src/md_notification.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/md_notification.c $(TARGET) $(TRDP_LIB) -lpthread -o $@

md-notification-receiver: $(MD_NOTIFICATION_RECEIVER)

$(MD_NOTIFICATION_RECEIVER): trdp $(TARGET) src/md_notification_receiver.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/md_notification_receiver.c $(TARGET) $(TRDP_LIB) -lpthread -o $@

md-mrmp: $(MD_MRMP)

$(MD_MRMP): trdp $(TARGET) src/md_mrmp.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/md_mrmp.c $(TARGET) $(TRDP_LIB) -lpthread -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(TRDP_DIR)/bld

help:
	@echo "make              Build src/ into $(TARGET)"
	@echo "make trdp         Build the upstream TRDP library"
	@echo "make pd-demo      Build the PD telegram sender demo"
	@echo "make pd-sender    Build the PD wrapper sender example"
	@echo "make pd-receiver  Build the PD wrapper receiver example"
	@echo "make send_diag_data  Build the unicast PD teaching simulator"
	@echo "make md-notification  Build the MD notification example"
	@echo "make md-notification-receiver  Build the MD notification receiver example"
	@echo "make md-mrmp      Build the MD request/reply example"
	@echo "make clean        Remove project build artifacts"

-include $(DEPS)
