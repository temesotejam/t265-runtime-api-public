#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef struct libusb_context libusb_context;
typedef struct libusb_device libusb_device;
typedef struct libusb_device_handle libusb_device_handle;

#define LIBUSB_SUCCESS 0
#define T265_BOOT_VID 0x03e7
#define T265_BOOT_PID 0x2150
#define T265_BOOT_MAX_DEVICES 16
#define T265_BOOT_TIMEOUT_MS 30000

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

typedef struct boot_device_info {
    int index;
    uint8_t bus_number;
    uint8_t device_address;
} boot_device_info;

int libusb_init(libusb_context **ctx);
void libusb_exit(libusb_context *ctx);
ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list);
void libusb_free_device_list(libusb_device **list, int unref_devices);
int libusb_get_device_descriptor(libusb_device *dev,
                                 struct libusb_device_descriptor *desc);
uint8_t libusb_get_bus_number(libusb_device *dev);
uint8_t libusb_get_device_address(libusb_device *dev);
int libusb_open(libusb_device *dev, libusb_device_handle **dev_handle);
void libusb_close(libusb_device_handle *dev_handle);
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
const char *libusb_error_name(int errcode);

extern const uint8_t _binary_tools_t265_fw_target_bin_start[];
extern const uint8_t _binary_tools_t265_fw_target_bin_end[];

#define fw_target_data _binary_tools_t265_fw_target_bin_start

static unsigned fw_target_size(void)
{
    return (unsigned)(_binary_tools_t265_fw_target_bin_end -
                      _binary_tools_t265_fw_target_bin_start);
}

static void print_libusb_error(const char *what, int rc)
{
    fprintf(stderr, "%s failed: %s (%d)\n", what, libusb_error_name(rc), rc);
}

static int is_boot_device(const struct libusb_device_descriptor *desc)
{
    return desc && desc->idVendor == T265_BOOT_VID && desc->idProduct == T265_BOOT_PID;
}

static int list_boot_devices(libusb_context *ctx, boot_device_info *devices, int max_devices)
{
    libusb_device **list = NULL;
    ssize_t list_count;
    ssize_t i;
    int found = 0;

    list_count = libusb_get_device_list(ctx, &list);
    if (list_count < 0) {
        print_libusb_error("libusb_get_device_list", (int)list_count);
        return -1;
    }

    for (i = 0; i < list_count; ++i) {
        struct libusb_device_descriptor desc;
        int rc = libusb_get_device_descriptor(list[i], &desc);
        if (rc != LIBUSB_SUCCESS || !is_boot_device(&desc)) {
            continue;
        }

        if (devices && found < max_devices) {
            devices[found].index = found;
            devices[found].bus_number = libusb_get_bus_number(list[i]);
            devices[found].device_address = libusb_get_device_address(list[i]);
        }
        found++;
    }

    libusb_free_device_list(list, 1);
    return found;
}

static libusb_device_handle *open_boot_device(libusb_context *ctx,
                                              const boot_device_info *target)
{
    libusb_device **list = NULL;
    ssize_t list_count;
    ssize_t i;
    libusb_device_handle *handle = NULL;

    list_count = libusb_get_device_list(ctx, &list);
    if (list_count < 0) {
        print_libusb_error("libusb_get_device_list", (int)list_count);
        return NULL;
    }

    for (i = 0; i < list_count; ++i) {
        struct libusb_device_descriptor desc;
        int rc = libusb_get_device_descriptor(list[i], &desc);
        if (rc != LIBUSB_SUCCESS || !is_boot_device(&desc)) {
            continue;
        }
        if (libusb_get_bus_number(list[i]) != target->bus_number ||
            libusb_get_device_address(list[i]) != target->device_address) {
            continue;
        }
        rc = libusb_open(list[i], &handle);
        if (rc != LIBUSB_SUCCESS) {
            print_libusb_error("libusb_open", rc);
            handle = NULL;
        }
        break;
    }

    libusb_free_device_list(list, 1);
    return handle;
}

static int boot_one_device(const boot_device_info *target)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    int rc;
    int transferred = 0;

    rc = libusb_init(&ctx);
    if (rc != LIBUSB_SUCCESS) {
        print_libusb_error("libusb_init", rc);
        return 1;
    }

    handle = open_boot_device(ctx, target);
    if (!handle) {
        fprintf(stderr, "03e7:2150 device bus=%u address=%u was not found\n",
                (unsigned int)target->bus_number,
                (unsigned int)target->device_address);
        libusb_exit(ctx);
        return 1;
    }

    if (libusb_kernel_driver_active(handle, 0) == 1) {
        rc = libusb_detach_kernel_driver(handle, 0);
        if (rc != LIBUSB_SUCCESS) {
            print_libusb_error("libusb_detach_kernel_driver", rc);
        }
    }

    rc = libusb_claim_interface(handle, 0);
    if (rc != LIBUSB_SUCCESS) {
        print_libusb_error("libusb_claim_interface", rc);
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }

    printf("boot device[%d] bus=%u address=%u: sending %u bytes to endpoint 0x01\n",
           target->index,
           (unsigned int)target->bus_number,
           (unsigned int)target->device_address,
           fw_target_size());
    rc = libusb_bulk_transfer(handle, 0x01, (unsigned char *)fw_target_data,
                              (int)fw_target_size(), &transferred,
                              T265_BOOT_TIMEOUT_MS);
    if (rc != LIBUSB_SUCCESS) {
        print_libusb_error("libusb_bulk_transfer", rc);
        fprintf(stderr, "transferred=%d\n", transferred);
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        libusb_exit(ctx);
        return 2;
    }

    printf("boot device[%d]: bulk transfer succeeded, transferred=%d\n",
           target->index, transferred);
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    return 0;
}

int main(void)
{
    libusb_context *ctx = NULL;
    boot_device_info devices[T265_BOOT_MAX_DEVICES];
    int rc = libusb_init(&ctx);
    int count;
    int attempted = 0;
    int succeeded = 0;

    if (rc != LIBUSB_SUCCESS) {
        print_libusb_error("libusb_init", rc);
        return 1;
    }

    memset(devices, 0, sizeof(devices));
    count = list_boot_devices(ctx, devices, T265_BOOT_MAX_DEVICES);
    if (count <= 0) {
        if (count == 0) {
            fprintf(stderr, "03e7:2150 device was not found\n");
        }
        libusb_exit(ctx);
        return 1;
    }

    if (count > T265_BOOT_MAX_DEVICES) {
        fprintf(stderr, "Found %d boot devices; booting first %d only.\n",
                count, T265_BOOT_MAX_DEVICES);
        count = T265_BOOT_MAX_DEVICES;
    }

    printf("boot devices: %d\n", count);
    for (int i = 0; i < count; ++i) {
        attempted++;
        if (boot_one_device(&devices[i]) == 0) {
            succeeded++;
        }
    }

    libusb_exit(ctx);
    if (succeeded == attempted) {
        return 0;
    }
    if (succeeded > 0) {
        return 3;
    }
    return 2;
}
