CC ?= gcc
CXX ?= g++
CFLAGS ?= -Wall -Wextra -Iinclude -Isrc
CXXFLAGS ?= -Wall -Wextra -Iinclude -Isrc
LIBUSB_SO ?= /lib/x86_64-linux-gnu/libusb-1.0.so.0
LDLIBS ?= $(LIBUSB_SO) -lpthread

COMMON_SRC = \
	src/t265_runtime.c \
	src/t265_runtime_threads.c \
	src/t265_runtime_queue.c \
	src/t265_runtime_multi_queue.c \
	src/t265_syncer.c \
	src/t265_image.c

EXAMPLES = \
	example_list_devices \
	example_latest_state \
	example_multi_latest_state \
	example_open_by_serial \
	example_open_by_role \
	example_simple_reader \
	example_multi_queue \
	example_image_queue

TOOLS = \
	t265_boot_libusb \
	t265_multi_latest_long_run \
	t265_runtime_sync_probe \
	t265_runtime_image_queue_long_run_probe \
	t265_runtime_image_save_worker_probe \
	t265_image_save_worker \
	t265_runtime_pose_timestamp_reader_probe

.PHONY: all examples tools opencv optional clean help

all: examples tools

examples: $(EXAMPLES)

tools: $(TOOLS)

example_list_devices: examples/example_list_devices.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

example_latest_state: examples/example_latest_state.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

example_multi_latest_state: examples/example_multi_latest_state.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

example_open_by_serial: examples/example_open_by_serial.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

example_open_by_role: examples/example_open_by_role.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

example_simple_reader: examples/example_simple_reader.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

example_multi_queue: examples/example_multi_queue.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

example_image_queue: examples/example_image_queue.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

tools/t265_fw_target.o: tools/t265_fw_target.bin
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@
	objcopy --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=contents,readonly $@

t265_boot_libusb: tools/t265_boot_libusb.c tools/t265_fw_target.o
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

t265_multi_latest_long_run: tools/t265_multi_latest_long_run.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

t265_runtime_sync_probe: tools/t265_runtime_sync_probe.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

t265_runtime_image_queue_long_run_probe: tools/t265_runtime_image_queue_long_run_probe.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

t265_runtime_image_save_worker_probe: tools/t265_runtime_image_save_worker_probe.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

t265_image_save_worker: tools/t265_image_save_worker.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

t265_runtime_pose_timestamp_reader_probe: tools/t265_runtime_pose_timestamp_reader_probe.c $(COMMON_SRC)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

opencv optional:
	./build_opencv_examples.sh

clean:
	rm -f $(EXAMPLES) $(TOOLS) tools/t265_fw_target.o example_opencv_fisheye_view

help:
	@echo "Targets:"
	@echo "  make all       Build core examples and selected tools"
	@echo "  make examples  Build public API examples"
	@echo "  make tools     Build selected verification tools"
	@echo "  make opencv    Build optional OpenCV example if OpenCV is available"
	@echo "  make clean     Remove generated binaries"
