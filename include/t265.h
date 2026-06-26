#ifndef T265_H
#define T265_H

#include "t265_types.h"
#include "t265_latest.h"
#include "t265_multi_queue.h"
#include "t265_image.h"
#include "t265_syncer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * T265 Runtime Public API
 *
 * Recommended user include:
 *   #include "t265.h"
 *
 * Phase 7 API policy:
 * - latest state API is the stable control path.
 * - simple reader API is the high-level example / quick-start path.
 * - callback -> multi-queue is the recommended logging / analysis path.
 * - image payload APIs are usable but require ownership / lifetime care.
 * - syncer is a timestamp association helper; the default threshold is 20 ms.
 * - OpenCV integration is optional and intentionally kept outside core headers.
 * - Low-level USB/protocol helpers are intentionally not exposed here.
 */

/* Multi-device context.
 *
 * A context owns libusb state and a refreshed snapshot of connected T265
 * runtime devices. Use it when more than one T265 may be connected.
 * Close all runtimes opened from the context before destroying the context.
 */
t265_context* t265_context_create(void);
void t265_context_destroy(t265_context *ctx);
int t265_context_refresh_devices(t265_context *ctx);
int t265_context_get_device_count(const t265_context *ctx);
int t265_context_get_device_info(const t265_context *ctx, int index, t265_device_info *out_info);
t265_runtime* t265_context_open_device(t265_context *ctx, int index);
t265_runtime* t265_context_open_device_by_bus_address(
    t265_context *ctx,
    uint8_t bus_number,
    uint8_t device_address
);
t265_runtime* t265_context_open_device_by_serial(
    t265_context *ctx,
    const char *serial
);
int t265_context_load_roles(t265_context *ctx, const char *path);
int t265_context_set_role(t265_context *ctx, const char *role, const char *serial);
int t265_context_get_role_serial(const t265_context *ctx, const char *role, char *serial, int serial_length);
t265_runtime* t265_context_open_device_by_role(t265_context *ctx, const char *role);

/* Convenience enumeration for simple programs.
 *
 * Returns the number of connected T265 runtime devices, or a negative T265_ERR_*
 * value on failure. If devices is NULL or max_devices is 0, only the count is
 * returned.
 */
int t265_list_devices(t265_device_info *devices, int max_devices);

/* Device lifecycle.
 *
 * Ownership:
 * - t265_open() returns a runtime handle owned by the caller.
 * - t265_open() is the compatibility path and opens device index 0 from an
 *   internally owned context.
 * - The caller must stop active readers/streams before t265_close().
 * - If the device appears as bootloader 03e7:2150, load runtime firmware
 *   before using this API.
 */
t265_runtime* t265_open(void);
void t265_close(t265_runtime *dev);
int t265_get_device_info(t265_runtime *dev, t265_device_info *out_info);
int t265_configure_streams(t265_runtime *dev, int fisheye, int imu, int pose);
int t265_start(t265_runtime *dev);
int t265_stop(t265_runtime *dev);

/* Reader management.
 *
 * t265_reader_create(dev) attaches to an already opened/configured runtime.
 * In Phase 7 this is treated as a non-owning reference to dev: stop/destroy the
 * reader before stopping/closing dev.
 *
 * Current ownership note:
 * - t265_reader_init(&reader, dev) is valid for stack-allocated readers.
 * - t265_reader_create(dev) allocates a reader context, but dev remains owned by
 *   the caller.
 * - Do not make t265_reader_destroy() close dev for non-owning readers.
 * - Heap-reader free semantics are being finalized separately from stack-reader
 *   cleanup so existing probe code remains safe.
 */
typedef struct t265_reader_context t265_reader_context;

t265_reader_context* t265_reader_create(t265_runtime *dev);
void t265_reader_destroy(t265_reader_context *ctx);
void t265_reader_free(t265_reader_context *ctx);
int t265_reader_start(t265_reader_context *ctx);
int t265_reader_stop(t265_reader_context *ctx);
int t265_reader_set_multi_queue(t265_reader_context *ctx, t265_multi_queue *mq);

/* High-level factory API.
 *
 * t265_create_simple_reader(NULL, ...) is the short-example path: it opens,
 * configures, starts, and attaches a reader to an internally created runtime.
 *
 * Phase 7 ownership direction:
 * - low-level readers do not own the supplied runtime.
 * - simple readers created with dev == NULL should own the internally created
 *   runtime and eventually stop/close it through a dedicated cleanup path.
 * - simple readers created with a non-NULL dev should not close that external
 *   runtime.
 *
 * Cleanup:
 * - Use t265_destroy_simple_reader() for readers returned by this factory.
 * - Do not call t265_destroy_simple_reader() for stack-allocated readers.
 */
t265_reader_context* t265_create_simple_reader(t265_runtime *dev, int fisheye, int imu, int pose, t265_multi_queue *mq);
t265_reader_context* t265_create_simple_reader_for_device(
    t265_context *ctx,
    int device_index,
    int fisheye,
    int imu,
    int pose,
    t265_multi_queue *mq
);
void t265_destroy_simple_reader(t265_reader_context *ctx);

#ifdef __cplusplus
}
#endif

#endif // T265_H
