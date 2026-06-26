#include "t265_internal.h"
#include "t265_runtime_threads.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>

#define LIBUSB_SUCCESS 0

struct libusb_device_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
};

int libusb_init(libusb_context **ctx);
void libusb_exit(libusb_context *ctx);
libusb_device_handle *libusb_open_device_with_vid_pid(libusb_context *ctx,
                                                       uint16_t vendor_id,
                                                       uint16_t product_id);
ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list);
void libusb_free_device_list(libusb_device **list, int unref_devices);
int libusb_get_device_descriptor(libusb_device *dev,
                                 struct libusb_device_descriptor *desc);
uint8_t libusb_get_bus_number(libusb_device *dev);
uint8_t libusb_get_device_address(libusb_device *dev);
int libusb_open(libusb_device *dev, libusb_device_handle **dev_handle);
void libusb_close(libusb_device_handle *dev_handle);
int libusb_get_string_descriptor_ascii(libusb_device_handle *dev_handle,
                                       uint8_t desc_index,
                                       unsigned char *data,
                                       int length);
int libusb_kernel_driver_active(libusb_device_handle *dev_handle,
                                int interface_number);
int libusb_detach_kernel_driver(libusb_device_handle *dev_handle,
                                int interface_number);
int libusb_claim_interface(libusb_device_handle *dev_handle,
                           int interface_number);
int libusb_release_interface(libusb_device_handle *dev_handle,
                             int interface_number);
int libusb_bulk_transfer(libusb_device_handle *dev_handle,
                         unsigned char endpoint,
                         unsigned char *data,
                         int length,
                         int *actual_length,
                         unsigned int timeout);
int libusb_interrupt_transfer(libusb_device_handle *dev_handle,
                              unsigned char endpoint,
                              unsigned char *data,
                              int length,
                              int *actual_length,
                              unsigned int timeout);
const char *libusb_error_name(int errcode);

uint16_t t265_read_le16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t t265_read_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t t265_read_le64(const unsigned char *p)
{
    return (uint64_t)t265_read_le32(p) |
           ((uint64_t)t265_read_le32(p + 4) << 32);
}

float t265_read_le_float(const unsigned char *p)
{
    uint32_t bits = t265_read_le32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void t265_write_le16(unsigned char *p, uint16_t value)
{
    p[0] = value & 0xff;
    p[1] = (value >> 8) & 0xff;
}

void t265_write_le32(unsigned char *p, uint32_t value)
{
    p[0] = value & 0xff;
    p[1] = (value >> 8) & 0xff;
    p[2] = (value >> 16) & 0xff;
    p[3] = (value >> 24) & 0xff;
}

const char *t265_libusb_error_name(int rc)
{
    return libusb_error_name(rc);
}

const char *t265_message_name(uint16_t id)
{
    switch (id) {
    case T265_DEV_SAMPLE:
        return "DEV_SAMPLE";
    case T265_DEV_GET_POSE:
        return "DEV_GET_POSE";
    case 0x0014:
        return "DEV_STATUS";
    case T265_DEV_START:
        return "DEV_START";
    case T265_DEV_STOP:
        return "DEV_STOP";
    case T265_DEV_RAW_STREAMS_CONTROL:
        return "DEV_RAW_STREAMS_CONTROL";
    case T265_DEV_SET_LOW_POWER_MODE:
        return "DEV_SET_LOW_POWER_MODE";
    case T265_SLAM_6DOF_CONTROL:
        return "SLAM_6DOF_CONTROL";
    default:
        return "UNKNOWN";
    }
}

static void print_libusb_error(const char *what, int rc)
{
    fprintf(stderr, "%s failed: %s (%d)\n", what, libusb_error_name(rc), rc);
}

static void t265_fill_device_label(t265_device_info *info)
{
    if (!info) {
        return;
    }
    if (info->serial[0] != '\0') {
        snprintf(info->label, sizeof(info->label),
                 "T265[%d] serial=%s bus=%u address=%u",
                 info->index, info->serial,
                 (unsigned int)info->bus_number,
                 (unsigned int)info->device_address);
    } else {
        snprintf(info->label, sizeof(info->label), "T265[%d] bus=%u address=%u",
                 info->index, (unsigned int)info->bus_number,
                 (unsigned int)info->device_address);
    }
}

static int t265_is_runtime_device(const struct libusb_device_descriptor *desc)
{
    return desc && desc->idVendor == T265_USB_VID && desc->idProduct == T265_USB_PID;
}

static void t265_read_device_serial(libusb_device *usb_dev,
                                    const struct libusb_device_descriptor *desc,
                                    char *serial,
                                    size_t serial_size)
{
    libusb_device_handle *handle = NULL;
    int rc;

    if (!usb_dev || !desc || !serial || serial_size == 0) {
        return;
    }

    serial[0] = '\0';
    if (desc->iSerialNumber == 0) {
        return;
    }

    rc = libusb_open(usb_dev, &handle);
    if (rc != LIBUSB_SUCCESS || !handle) {
        return;
    }

    rc = libusb_get_string_descriptor_ascii(handle, desc->iSerialNumber,
                                            (unsigned char *)serial,
                                            (int)serial_size);
    if (rc < 0) {
        serial[0] = '\0';
    } else if ((size_t)rc >= serial_size) {
        serial[serial_size - 1] = '\0';
    } else {
        serial[rc] = '\0';
    }

    libusb_close(handle);
}

static int t265_refresh_devices_into(libusb_context *ctx,
                                     t265_device_info *devices,
                                     int max_devices,
                                     int *out_count)
{
    libusb_device **list = NULL;
    ssize_t list_count;
    int found = 0;
    ssize_t i;

    if (!ctx || !out_count || max_devices < 0) {
        return T265_ERR_INVALID_STATE;
    }

    list_count = libusb_get_device_list(ctx, &list);
    if (list_count < 0) {
        print_libusb_error("libusb_get_device_list", (int)list_count);
        return T265_ERR_USB;
    }

    for (i = 0; i < list_count; ++i) {
        struct libusb_device_descriptor desc;
        int rc = libusb_get_device_descriptor(list[i], &desc);
        if (rc != LIBUSB_SUCCESS) {
            continue;
        }
        if (!t265_is_runtime_device(&desc)) {
            continue;
        }
        if (devices && found < max_devices) {
            t265_device_info *info = &devices[found];
            memset(info, 0, sizeof(*info));
            info->index = found;
            info->bus_number = libusb_get_bus_number(list[i]);
            info->device_address = libusb_get_device_address(list[i]);
            info->vid = desc.idVendor;
            info->pid = desc.idProduct;
            t265_read_device_serial(list[i], &desc, info->serial,
                                    sizeof(info->serial));
            t265_fill_device_label(info);
        }
        found++;
    }

    libusb_free_device_list(list, 1);
    *out_count = found;
    return T265_OK;
}

static int t265_claim_runtime_interface(t265_runtime *dev)
{
    int rc;

    if (!dev || !dev->handle) {
        return T265_ERR_INVALID_STATE;
    }

    rc = libusb_kernel_driver_active(dev->handle, 0);
    if (rc == 1) {
        rc = libusb_detach_kernel_driver(dev->handle, 0);
        if (rc != LIBUSB_SUCCESS) {
            print_libusb_error("libusb_detach_kernel_driver", rc);
            return T265_ERR_USB;
        }
    }

    rc = libusb_claim_interface(dev->handle, 0);
    if (rc != LIBUSB_SUCCESS) {
        print_libusb_error("libusb_claim_interface", rc);
        return T265_ERR_USB;
    }

    dev->claimed = 1;
    dev->interrupt_transferred = 0;
    dev->interrupt_offset = 0;
    return T265_OK;
}

static char *t265_trim(char *text)
{
    char *end;

    if (!text) {
        return text;
    }
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static int t265_debug_interrupt_ids_enabled(void)
{
    const char *value = getenv("T265_DEBUG_INTERRUPT_IDS");
    return value && value[0] != '\0' && strcmp(value, "0") != 0;
}

static int t265_debug_pose_raw_offsets_enabled(void)
{
    const char *value = getenv("T265_DEBUG_POSE_RAW_OFFSETS");
    return value && value[0] != '\0' && strcmp(value, "0") != 0;
}

static int t265_debug_pose_raw_limit(void)
{
    const char *value = getenv("T265_DEBUG_POSE_RAW_LIMIT");
    int limit = value ? atoi(value) : 5;
    return limit > 0 ? limit : 5;
}

static void t265_debug_print_pose_raw_offsets(const unsigned char *buffer, int length)
{
    static int printed = 0;
    static const int offsets[] = {8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 84, 88, 96};
    int limit;
    size_t i;

    if (!t265_debug_pose_raw_offsets_enabled()) {
        return;
    }

    limit = t265_debug_pose_raw_limit();
    if (printed >= limit) {
        return;
    }

    fprintf(stderr, "T265_DEBUG_POSE_RAW_OFFSETS sample=%d len=%d\n", printed + 1, length);
    for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        int off = offsets[i];
        if (off + 8 <= length) {
            fprintf(stderr, "  offset %-3d: %llu\n", off,
                    (unsigned long long)t265_read_le64(buffer + off));
        }
    }
    printed++;
}

t265_context* t265_context_create(void)
{
    int rc;
    t265_context *ctx = calloc(1, sizeof(t265_context));
    if (!ctx) return NULL;

    rc = libusb_init(&ctx->ctx);
    if (rc != LIBUSB_SUCCESS) {
        print_libusb_error("libusb_init", rc);
        free(ctx);
        return NULL;
    }

    return ctx;
}

void t265_context_destroy(t265_context *ctx)
{
    if (!ctx) {
        return;
    }
    if (ctx->ctx) {
        libusb_exit(ctx->ctx);
        ctx->ctx = NULL;
    }
    free(ctx);
}

int t265_context_refresh_devices(t265_context *ctx)
{
    int count = 0;
    int rc;

    if (!ctx || !ctx->ctx) {
        return T265_ERR_INVALID_STATE;
    }

    memset(ctx->devices, 0, sizeof(ctx->devices));
    ctx->device_count = 0;

    rc = t265_refresh_devices_into(ctx->ctx, ctx->devices, T265_MAX_DEVICES, &count);
    if (rc != T265_OK) {
        return rc;
    }

    ctx->device_count = count > T265_MAX_DEVICES ? T265_MAX_DEVICES : count;
    return count;
}

int t265_context_get_device_count(const t265_context *ctx)
{
    if (!ctx) {
        return T265_ERR_INVALID_STATE;
    }
    return ctx->device_count;
}

int t265_context_get_device_info(const t265_context *ctx, int index, t265_device_info *out_info)
{
    if (!ctx || !out_info || index < 0 || index >= ctx->device_count) {
        return T265_ERR_INVALID_STATE;
    }
    *out_info = ctx->devices[index];
    return T265_OK;
}

static t265_runtime* t265_context_open_matching_device(t265_context *ctx,
                                                       const t265_device_info *target)
{
    libusb_device **list = NULL;
    ssize_t list_count;
    ssize_t i;
    t265_runtime *dev = NULL;

    if (!ctx || !ctx->ctx || !target) {
        return NULL;
    }

    list_count = libusb_get_device_list(ctx->ctx, &list);
    if (list_count < 0) {
        print_libusb_error("libusb_get_device_list", (int)list_count);
        return NULL;
    }

    for (i = 0; i < list_count; ++i) {
        struct libusb_device_descriptor desc;
        int rc = libusb_get_device_descriptor(list[i], &desc);
        if (rc != LIBUSB_SUCCESS || !t265_is_runtime_device(&desc)) {
            continue;
        }
        if (libusb_get_bus_number(list[i]) != target->bus_number ||
            libusb_get_device_address(list[i]) != target->device_address) {
            continue;
        }

        dev = calloc(1, sizeof(t265_runtime));
        if (!dev) {
            break;
        }
        dev->ctx = ctx->ctx;
        dev->owner_context = ctx;
        dev->info = *target;

        rc = libusb_open(list[i], &dev->handle);
        if (rc != LIBUSB_SUCCESS || !dev->handle) {
            print_libusb_error("libusb_open", rc);
            free(dev);
            dev = NULL;
            break;
        }

        if (t265_claim_runtime_interface(dev) != T265_OK) {
            t265_close(dev);
            dev = NULL;
        }
        break;
    }

    libusb_free_device_list(list, 1);
    return dev;
}

t265_runtime* t265_context_open_device(t265_context *ctx, int index)
{
    if (!ctx || index < 0) {
        return NULL;
    }
    if (ctx->device_count == 0) {
        int count = t265_context_refresh_devices(ctx);
        if (count < 0) {
            return NULL;
        }
    }
    if (index >= ctx->device_count) {
        fprintf(stderr, "T265 device index %d was not found\n", index);
        return NULL;
    }

    return t265_context_open_matching_device(ctx, &ctx->devices[index]);
}

t265_runtime* t265_context_open_device_by_bus_address(t265_context *ctx,
                                                      uint8_t bus_number,
                                                      uint8_t device_address)
{
    int i;
    if (!ctx) {
        return NULL;
    }
    if (ctx->device_count == 0) {
        int count = t265_context_refresh_devices(ctx);
        if (count < 0) {
            return NULL;
        }
    }

    for (i = 0; i < ctx->device_count; ++i) {
        if (ctx->devices[i].bus_number == bus_number &&
            ctx->devices[i].device_address == device_address) {
            return t265_context_open_matching_device(ctx, &ctx->devices[i]);
        }
    }

    fprintf(stderr, "T265 bus=%u address=%u was not found\n",
            (unsigned int)bus_number, (unsigned int)device_address);
    return NULL;
}

t265_runtime* t265_context_open_device_by_serial(t265_context *ctx,
                                                 const char *serial)
{
    int i;
    if (!ctx || !serial || serial[0] == '\0') {
        return NULL;
    }
    if (ctx->device_count == 0) {
        int count = t265_context_refresh_devices(ctx);
        if (count < 0) {
            return NULL;
        }
    }

    for (i = 0; i < ctx->device_count; ++i) {
        if (strcmp(ctx->devices[i].serial, serial) == 0) {
            return t265_context_open_matching_device(ctx, &ctx->devices[i]);
        }
    }

    fprintf(stderr, "T265 serial=%s was not found\n", serial);
    return NULL;
}

int t265_context_set_role(t265_context *ctx, const char *role, const char *serial)
{
    int i;

    if (!ctx || !role || !serial || role[0] == '\0' || serial[0] == '\0') {
        return T265_ERR_INVALID_STATE;
    }

    for (i = 0; i < ctx->role_count; ++i) {
        if (strcmp(ctx->roles[i].role, role) == 0) {
            snprintf(ctx->roles[i].serial, sizeof(ctx->roles[i].serial), "%s", serial);
            return T265_OK;
        }
    }

    if (ctx->role_count >= T265_MAX_ROLES) {
        return T265_ERR_QUEUE_FULL;
    }

    snprintf(ctx->roles[ctx->role_count].role, sizeof(ctx->roles[ctx->role_count].role),
             "%s", role);
    snprintf(ctx->roles[ctx->role_count].serial, sizeof(ctx->roles[ctx->role_count].serial),
             "%s", serial);
    ctx->role_count++;
    return T265_OK;
}

int t265_context_load_roles(t265_context *ctx, const char *path)
{
    FILE *fp;
    char line[256];
    int loaded = 0;

    if (!ctx || !path) {
        return T265_ERR_INVALID_STATE;
    }

    fp = fopen(path, "r");
    if (!fp) {
        return T265_ERR_NOT_FOUND;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *text = t265_trim(line);
        char *eq;
        char *role;
        char *serial;

        if (text[0] == '\0' || text[0] == '#') {
            continue;
        }

        eq = strchr(text, '=');
        if (!eq) {
            continue;
        }

        *eq = '\0';
        role = t265_trim(text);
        serial = t265_trim(eq + 1);

        if (t265_context_set_role(ctx, role, serial) == T265_OK) {
            loaded++;
        }
    }

    fclose(fp);
    return loaded;
}

int t265_context_get_role_serial(const t265_context *ctx, const char *role,
                                 char *serial, int serial_length)
{
    int i;

    if (!ctx || !role || !serial || serial_length <= 0) {
        return T265_ERR_INVALID_STATE;
    }

    for (i = 0; i < ctx->role_count; ++i) {
        if (strcmp(ctx->roles[i].role, role) == 0) {
            snprintf(serial, (size_t)serial_length, "%s", ctx->roles[i].serial);
            return T265_OK;
        }
    }

    return T265_ERR_NOT_FOUND;
}

t265_runtime* t265_context_open_device_by_role(t265_context *ctx, const char *role)
{
    char serial[64];

    if (t265_context_get_role_serial(ctx, role, serial, (int)sizeof(serial)) != T265_OK) {
        fprintf(stderr, "T265 role=%s was not found\n", role ? role : "<null>");
        return NULL;
    }

    return t265_context_open_device_by_serial(ctx, serial);
}

int t265_list_devices(t265_device_info *devices, int max_devices)
{
    t265_context *ctx;
    int count;

    if (max_devices < 0) {
        return T265_ERR_INVALID_STATE;
    }

    ctx = t265_context_create();
    if (!ctx) {
        return T265_ERR_USB;
    }

    count = t265_refresh_devices_into(ctx->ctx, devices, max_devices, &ctx->device_count);
    if (count == T265_OK) {
        count = ctx->device_count;
    }

    t265_context_destroy(ctx);
    return count;
}

t265_runtime* t265_open(void)
{
    t265_context *ctx = t265_context_create();
    t265_runtime *dev;
    int count;

    if (!ctx) {
        return NULL;
    }

    count = t265_context_refresh_devices(ctx);
    if (count <= 0) {
        if (count == 0) {
            fprintf(stderr, "8087:0b37 runtime T265 device was not found\n");
        }
        t265_context_destroy(ctx);
        return NULL;
    }

    dev = t265_context_open_device(ctx, 0);
    if (!dev) {
        t265_context_destroy(ctx);
        return NULL;
    }

    dev->owns_context = 1;
    return dev;
}

void t265_close(t265_runtime *dev)
{
    if (!dev) return;
    if (dev->started) {
        t265_stop(dev);
    }
    if (dev->handle && dev->claimed) {
        libusb_release_interface(dev->handle, 0);
        dev->claimed = 0;
    }
    if (dev->handle) {
        libusb_close(dev->handle);
        dev->handle = NULL;
    }
    if (dev->owns_context && dev->owner_context) {
        t265_context_destroy(dev->owner_context);
        dev->owner_context = NULL;
        dev->ctx = NULL;
    }
    free(dev);
}

int t265_get_device_info(t265_runtime *dev, t265_device_info *out_info)
{
    if (!dev || !out_info) {
        return T265_ERR_INVALID_STATE;
    }
    *out_info = dev->info;
    return T265_OK;
}

int t265_command(t265_runtime *dev, uint16_t message_id,
                 const unsigned char *payload, int payload_length,
                 unsigned char *response, int response_length,
                 int *transferred)
{
    unsigned char request[T265_RESPONSE_BUFFER_SIZE];
    int request_length = 6 + payload_length;
    int written = 0;
    int rc;

    if (request_length > (int)sizeof(request)) {
        fprintf(stderr, "request too large: %d\n", request_length);
        return -1;
    }

    memset(request, 0, sizeof(request));
    t265_write_le32(request, (uint32_t)request_length);
    t265_write_le16(request + 4, message_id);
    if (payload_length > 0 && payload) {
        memcpy(request + 6, payload, (size_t)payload_length);
    }

    rc = libusb_bulk_transfer(dev->handle, T265_EP_MSG_OUT, request,
                              request_length, &written, 10000);
    if (rc != LIBUSB_SUCCESS) {
        print_libusb_error("command write", rc);
        return rc;
    }
    if (written != request_length) {
        fprintf(stderr, "short command write: %d/%d\n", written,
                request_length);
        return -1;
    }

    memset(response, 0, (size_t)response_length);
    rc = libusb_bulk_transfer(dev->handle, T265_EP_MSG_IN, response,
                              response_length, transferred, 10000);
    if (rc != LIBUSB_SUCCESS) {
        print_libusb_error("command read", rc);
        return rc;
    }

    return T265_OK;
}

int t265_command_expect_ok(t265_runtime *dev, uint16_t message_id,
                           const unsigned char *payload, int payload_length)
{
    unsigned char response[T265_RESPONSE_BUFFER_SIZE];
    int transferred = 0;
    uint16_t response_id;
    uint16_t status;
    int rc = t265_command(dev, message_id, payload, payload_length, response,
                          (int)sizeof(response), &transferred);
    if (rc != T265_OK) {
        return rc;
    }
    if (transferred < 8) {
        fprintf(stderr, "%s response too short: %d\n",
                t265_message_name(message_id), transferred);
        return -1;
    }

    response_id = t265_read_le16(response + 4);
    status = t265_read_le16(response + 6);
    printf("%s: response id=0x%04x status=0x%04x transferred=%d\n",
           t265_message_name(message_id), response_id, status, transferred);

    if (response_id != message_id || status != T265_MESSAGE_STATUS_SUCCESS) {
        return -1;
    }

    return T265_OK;
}

int t265_set_low_power_mode(t265_runtime *dev, int enabled)
{
    unsigned char payload[2] = {0, 0};
    payload[0] = enabled ? 1 : 0;
    return t265_command_expect_ok(dev, T265_DEV_SET_LOW_POWER_MODE, payload,
                                  (int)sizeof(payload));
}

int t265_get_supported_raw_streams(t265_runtime *dev, unsigned char *streams,
                                   uint16_t *stream_count)
{
    unsigned char response[T265_RESPONSE_BUFFER_SIZE];
    int transferred = 0;
    uint16_t count;
    int expected;
    int rc = t265_command(dev, T265_DEV_GET_SUPPORTED_RAW_STREAMS, NULL, 0,
                          response, (int)sizeof(response), &transferred);
    if (rc != T265_OK) {
        return rc;
    }
    if (transferred < 12 ||
        t265_read_le16(response + 4) != T265_DEV_GET_SUPPORTED_RAW_STREAMS ||
        t265_read_le16(response + 6) != T265_MESSAGE_STATUS_SUCCESS) {
        fprintf(stderr, "DEV_GET_SUPPORTED_RAW_STREAMS failed\n");
        return -1;
    }

    count = t265_read_le16(response + 8);
    if (count > T265_MAX_STREAMS) {
        fprintf(stderr, "too many streams: %u\n", count);
        return -1;
    }

    expected = 12 + (int)count * T265_STREAM_ENTRY_SIZE;
    if (transferred < expected) {
        fprintf(stderr, "short stream response: %d/%d\n", transferred,
                expected);
        return -1;
    }

    memcpy(streams, response + 12, (size_t)count * T265_STREAM_ENTRY_SIZE);
    *stream_count = count;
    printf("supported raw streams: %u\n", count);
    return T265_OK;
}

int t265_filter_exposed_streams(const unsigned char *streams,
                                uint16_t stream_count,
                                unsigned char *active_streams,
                                uint16_t *active_count,
                                int enable_fisheye, int enable_imu)
{
    uint16_t out = 0;

    for (uint16_t i = 0; i < stream_count; ++i) {
        const unsigned char *source = streams + i * T265_STREAM_ENTRY_SIZE;
        uint8_t sensor_id = source[0];
        uint16_t fps = t265_read_le16(source + 10);
        int exposed = 0;
        int output = 0;

        if (sensor_id == T265_SENSOR_ID_FISHEYE0 || sensor_id == T265_SENSOR_ID_FISHEYE1) {
            exposed = 1;
            output = enable_fisheye;
        } else if (sensor_id == T265_SENSOR_ID_GYRO && fps == 200) {
            exposed = 1;
            output = enable_imu;
        } else if (sensor_id == T265_SENSOR_ID_ACCEL && fps == 62) {
            exposed = 1;
            output = enable_imu;
        }

        if (exposed && output) {
            memcpy(active_streams + out * T265_STREAM_ENTRY_SIZE, source,
                   T265_STREAM_ENTRY_SIZE);
            active_streams[out * T265_STREAM_ENTRY_SIZE + 7] = 1;
            printf("  stream 0x%02x fps=%u output=1\n", sensor_id, fps);
            out++;
        }
    }

    *active_count = out;
    printf("raw streams passed to DEV_RAW_STREAMS_CONTROL: %u\n", out);
    return out > 0 ? T265_OK : T265_ERR_NOT_FOUND;
}

int t265_raw_streams_control(t265_runtime *dev,
                             const unsigned char *active_streams,
                             uint16_t active_count)
{
    unsigned char payload[2 + T265_MAX_STREAMS * T265_STREAM_ENTRY_SIZE];
    int payload_length = 2 + (int)active_count * T265_STREAM_ENTRY_SIZE;

    t265_write_le16(payload, active_count);
    memcpy(payload + 2, active_streams,
           (size_t)active_count * T265_STREAM_ENTRY_SIZE);

    return t265_command_expect_ok(dev, T265_DEV_RAW_STREAMS_CONTROL, payload,
                                  payload_length);
}

int t265_slam_6dof_control(t265_runtime *dev, int enabled)
{
    unsigned char payload[2] = {0, 0};
    payload[0] = enabled ? 1 : 0;
    payload[1] = 0;
    return t265_command_expect_ok(dev, T265_SLAM_6DOF_CONTROL, payload,
                                  (int)sizeof(payload));
}

int t265_configure_streams(t265_runtime *dev, int enable_fisheye,
                           int enable_imu, int enable_pose)
{
    unsigned char streams[T265_MAX_STREAMS * T265_STREAM_ENTRY_SIZE];
    unsigned char active[T265_MAX_STREAMS * T265_STREAM_ENTRY_SIZE];
    uint16_t stream_count = 0;
    uint16_t active_count = 0;
    int rc;

    if (!dev) {
        return T265_ERR_INVALID_STATE;
    }

    if (!enable_fisheye && !enable_imu && !enable_pose) {
        fprintf(stderr, "t265_configure_streams: all streams disabled\n");
        return T265_ERR_INVALID_STATE;
    }

    t265_set_low_power_mode(dev, 0);

    rc = t265_get_supported_raw_streams(dev, streams, &stream_count);
    if (rc != T265_OK) {
        return rc;
    }

    rc = t265_filter_exposed_streams(streams, stream_count, active,
                                     &active_count, enable_fisheye, enable_imu);
    if (rc != T265_OK) {
        return rc;
    }

    rc = t265_raw_streams_control(dev, active, active_count);
    if (rc != T265_OK) {
        return rc;
    }

    return t265_slam_6dof_control(dev, enable_pose ? 1 : 0);
}

int t265_start(t265_runtime *dev)
{
    int rc;
    if (!dev) {
        return T265_ERR_INVALID_STATE;
    }
    rc = t265_command_expect_ok(dev, T265_DEV_START, NULL, 0);
    if (rc == T265_OK) {
        dev->started = 1;
    }
    return rc;
}

int t265_stop(t265_runtime *dev)
{
    int rc;

    if (!dev) {
        return T265_ERR_INVALID_STATE;
    }
    if (!dev->handle) {
        dev->started = 0;
        return T265_OK;
    }

    rc = t265_command_expect_ok(dev, T265_DEV_STOP, NULL, 0);
    dev->started = 0;
    return rc;
}

int t265_bulk_read(t265_runtime *dev, unsigned char endpoint,
                  unsigned char *buffer, int buffer_length,
                  int *transferred, unsigned int timeout_ms)
{
    int rc = libusb_bulk_transfer(dev->handle, endpoint, buffer, buffer_length,
                                  transferred, timeout_ms);
    if (rc != LIBUSB_SUCCESS) {
        print_libusb_error("bulk read", rc);
        return rc;
    }
    return T265_OK;
}

int t265_interrupt_read(t265_runtime *dev, unsigned char endpoint,
                        unsigned char *buffer, int buffer_length,
                        int *transferred, unsigned int timeout_ms)
{
    int rc = libusb_interrupt_transfer(dev->handle, endpoint, buffer,
                                       buffer_length, transferred,
                                       timeout_ms);
    if (rc != LIBUSB_SUCCESS) {
        print_libusb_error("interrupt read", rc);
        return rc;
    }
    return T265_OK;
}

int t265_decode_pose(const unsigned char *buffer, int length, t265_pose_sample *pose)
{
    const unsigned char *p;
    if (!buffer || !pose) {
        return -1;
    }
    if (length < 104 || t265_read_le16(buffer + 4) != T265_DEV_GET_POSE) {
        return -1;
    }
    t265_debug_print_pose_raw_offsets(buffer, length);
    p = buffer + 8;
    pose->x = t265_read_le_float(p);
    pose->y = t265_read_le_float(p + 4);
    pose->z = t265_read_le_float(p + 8);
    pose->quat_i = t265_read_le_float(p + 12);
    pose->quat_j = t265_read_le_float(p + 16);
    pose->quat_k = t265_read_le_float(p + 20);
    pose->quat_r = t265_read_le_float(p + 24);
    pose->timestamp_ns = t265_read_le64(p + 76);
    pose->tracker_confidence = t265_read_le32(p + 84);
    pose->mapper_confidence = t265_read_le32(p + 88);
    pose->tracker_state = t265_read_le32(p + 92);
    return T265_OK;
}

int t265_decode_imu_sample(const unsigned char *buffer, int length, t265_imu_sample *imu)
{
    if (!buffer || !imu) {
        return -1;
    }
    if (length < 52 || t265_read_le16(buffer + 4) != T265_DEV_SAMPLE) {
        return -1;
    }
    imu->sensor_id = buffer[6];
    if (imu->sensor_id != T265_SENSOR_ID_GYRO && imu->sensor_id != T265_SENSOR_ID_ACCEL) {
        return -1;
    }
    imu->timestamp_ns = t265_read_le64(buffer + 8);
    imu->arrival_timestamp_ns = t265_read_le64(buffer + 16);
    imu->frame_id = t265_read_le32(buffer + 24);
    imu->metadata_length = t265_read_le32(buffer + 28);
    imu->temperature = t265_read_le_float(buffer + 32);
    imu->frame_length = t265_read_le32(buffer + 36);
    imu->x = t265_read_le_float(buffer + 40);
    imu->y = t265_read_le_float(buffer + 44);
    imu->z = t265_read_le_float(buffer + 48);
    return T265_OK;
}

int t265_decode_fisheye_frame(const unsigned char *buffer, int length, t265_fisheye_frame *frame)
{
    if (!buffer || !frame) {
        return -1;
    }
    if (length < 44 || t265_read_le16(buffer + 4) != T265_DEV_SAMPLE) {
        return -1;
    }
    frame->sensor_id = buffer[6];
    if (frame->sensor_id != T265_SENSOR_ID_FISHEYE0 &&
        frame->sensor_id != T265_SENSOR_ID_FISHEYE1) {
        return -1;
    }
    frame->frame_length = t265_read_le32(buffer + 40);
    if (frame->frame_length == 0 || frame->frame_length > (uint32_t)(length - 44)) {
        return -1;
    }
    frame->timestamp_ns = t265_read_le64(buffer + 8);
    frame->arrival_timestamp_ns = t265_read_le64(buffer + 16);
    frame->frame_id = t265_read_le32(buffer + 24);
    frame->metadata_length = t265_read_le32(buffer + 28);
    frame->exposure_time = t265_read_le32(buffer + 32);
    frame->gain = t265_read_le_float(buffer + 36);
    frame->width = 848;
    frame->height = 800;
    frame->frame_data = buffer + 44;
    return T265_OK;
}

int t265_decode_interrupt_message(const unsigned char *buffer, int length,
                                  t265_interrupt_sample *sample)
{
    uint16_t message_id;

    if (!buffer || !sample) {
        return -1;
    }

    if (length < 6) {
        return -1;
    }

    message_id = t265_read_le16(buffer + 4);
    if (message_id != T265_DEV_SAMPLE && t265_debug_interrupt_ids_enabled()) {
        fprintf(stderr, "DEBUG: Received interrupt message_id: 0x%04x, len=%d\n",
                message_id, length);
    }

    if (message_id == T265_DEV_GET_POSE) {
        if (t265_decode_pose(buffer, length, &sample->data.pose) == T265_OK) {
            sample->type = T265_SAMPLE_POSE;
            return T265_OK;
        }
    } else if (message_id == T265_DEV_SAMPLE) {
        if (t265_decode_imu_sample(buffer, length, &sample->data.imu) == T265_OK) {
            if (sample->data.imu.sensor_id == T265_SENSOR_ID_GYRO) {
                sample->type = T265_SAMPLE_GYRO;
            } else if (sample->data.imu.sensor_id == T265_SENSOR_ID_ACCEL) {
                sample->type = T265_SAMPLE_ACCEL;
            } else {
                sample->type = T265_SAMPLE_UNKNOWN;
            }
            return T265_OK;
        }
    }

    sample->type = T265_SAMPLE_UNKNOWN;
    return -1;
}

int t265_next_interrupt_message(const unsigned char *buffer, int transferred,
                                int *offset, const unsigned char **msg_out,
                                int *len_out)
{
    uint32_t message_length;

    if (!buffer || !offset || !msg_out || !len_out) {
        return 0;
    }

    if (*offset < 0 || *offset + 6 > transferred) {
        return 0;
    }

    message_length = t265_read_le32(buffer + *offset);

    if (message_length < 6) {
        return 0;
    }

    if (message_length > (uint32_t)(transferred - *offset)) {
        return 0;
    }

    *msg_out = buffer + *offset;
    *len_out = (int)message_length;
    *offset += (int)message_length;

    return 1;
}

int t265_read_next_interrupt_sample(t265_runtime *dev,
                                    t265_interrupt_sample *sample,
                                    unsigned int timeout_ms)
{
    const unsigned char *msg;
    int msg_len;
    int read_attempts = 0;
    const int max_read_attempts = 2;

    if (!dev || !sample) {
        return -1;
    }

    sample->type = T265_SAMPLE_UNKNOWN;

    while (read_attempts < max_read_attempts) {
        if (dev->interrupt_offset >= dev->interrupt_transferred) {
            int rc;
            dev->interrupt_transferred = 0;
            dev->interrupt_offset = 0;
            rc = t265_interrupt_read(dev, T265_EP_INT_IN,
                                     dev->interrupt_buffer,
                                     sizeof(dev->interrupt_buffer),
                                     &dev->interrupt_transferred,
                                     timeout_ms);
            if (rc != T265_OK) {
                return rc;
            }
            if (dev->interrupt_transferred == 0) {
                return -1;
            }
            read_attempts++;
        }

        while (t265_next_interrupt_message(dev->interrupt_buffer,
                                           dev->interrupt_transferred,
                                           &dev->interrupt_offset,
                                           &msg, &msg_len)) {
            if (t265_decode_interrupt_message(msg, msg_len, sample) == T265_OK) {
                if (sample->type == T265_SAMPLE_POSE ||
                    sample->type == T265_SAMPLE_GYRO ||
                    sample->type == T265_SAMPLE_ACCEL) {
                    return T265_OK;
                }
            }
        }

        dev->interrupt_offset = dev->interrupt_transferred;
    }

    return -1;
}

int t265_read_next_fisheye_frame(t265_runtime *dev,
                                 t265_fisheye_frame *frame,
                                 unsigned int timeout_ms)
{
    int transferred = 0;
    int rc;
    uint16_t message_id;

    if (!dev || !frame) {
        return -1;
    }

    rc = t265_bulk_read(dev, T265_EP_BULK_IN, dev->bulk_buffer,
                        (int)sizeof(dev->bulk_buffer), &transferred,
                        timeout_ms);
    if (rc != T265_OK) {
        return rc;
    }

    if (transferred < 44) {
        return -1;
    }

    message_id = t265_read_le16(dev->bulk_buffer + 4);
    if (message_id != T265_DEV_SAMPLE) {
        return -1;
    }

    rc = t265_decode_fisheye_frame(dev->bulk_buffer, transferred, frame);
    if (rc != T265_OK) {
        return rc;
    }

    if (frame->sensor_id == T265_SENSOR_ID_FISHEYE0 ||
        frame->sensor_id == T265_SENSOR_ID_FISHEYE1) {
        return T265_OK;
    }

    return -1;
}

t265_reader_context* t265_create_simple_reader(t265_runtime *dev_in, int fisheye, int imu, int pose, t265_multi_queue *mq)
{
    int owns_runtime = 0;
    int runtime_started_by_reader = 0;
    t265_runtime *dev = dev_in;

    if (!dev) {
        dev = t265_open();
        if (!dev) return NULL;
        owns_runtime = 1;
    }
    
    if (t265_configure_streams(dev, fisheye, imu, pose) != T265_OK) {
        if (owns_runtime) {
            t265_close(dev);
        }
        return NULL;
    }
    
    if (t265_start(dev) != T265_OK) {
        if (owns_runtime) {
            t265_close(dev);
        }
        return NULL;
    }
    runtime_started_by_reader = 1;
    
    t265_reader_context *ctx = t265_reader_create(dev);
    if (!ctx) {
        if (runtime_started_by_reader) {
            t265_stop(dev);
        }
        if (owns_runtime) {
            t265_close(dev);
        }
        return NULL;
    }
    ctx->owns_runtime = owns_runtime;
    ctx->runtime_started_by_reader = runtime_started_by_reader;
    
    if (mq) {
        t265_reader_set_multi_queue(ctx, mq);
    }
    
    if (t265_reader_start(ctx) != T265_OK) {
        t265_reader_free(ctx);
        if (runtime_started_by_reader) {
            t265_stop(dev);
        }
        if (owns_runtime) {
            t265_close(dev);
        }
        return NULL;
    }

    return ctx;
}

t265_reader_context* t265_create_simple_reader_for_device(
    t265_context *ctx,
    int device_index,
    int fisheye,
    int imu,
    int pose,
    t265_multi_queue *mq
)
{
    t265_runtime *dev;
    t265_reader_context *reader;

    if (!ctx || device_index < 0) {
        return NULL;
    }

    dev = t265_context_open_device(ctx, device_index);
    if (!dev) {
        return NULL;
    }

    reader = t265_create_simple_reader(dev, fisheye, imu, pose, mq);
    if (!reader) {
        t265_close(dev);
        return NULL;
    }
    reader->owns_runtime = 1;
    return reader;
}

void t265_destroy_simple_reader(t265_reader_context *ctx)
{
    t265_runtime *dev;
    int owns_runtime;
    int runtime_started_by_reader;

    if (!ctx) {
        return;
    }

    dev = ctx->dev;
    owns_runtime = ctx->owns_runtime;
    runtime_started_by_reader = ctx->runtime_started_by_reader;

    t265_reader_destroy(ctx);

    if (dev && runtime_started_by_reader) {
        t265_stop(dev);
    }

    if (dev && owns_runtime) {
        t265_close(dev);
    }

    free(ctx);
}
