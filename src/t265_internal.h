/*
 * T265 Runtime Helper Layer
 * 
 * 責務と担当範囲:
 * このレイヤーは、Intel RealSense T265 の runtime 状態 (VID: 0x8087, PID: 0x0b37) 
 * における USB 通信を抽象化し、librealsense に依存せずにデバイスを制御するための
 * 低〜中レベル API を提供します。libusb の初期化・終了、ベンダー固有コマンドの送受信、
 * ストリーム（Fisheye, IMU, Pose）の構成、およびデータの読み出しを担います。
 * 
 * 主な公開API:
 * - ライフサイクル: t265_open(), t265_close()
 * - ストリーム構成: t265_configure_streams()
 * - ストリーム制御: t265_start(), t265_stop()
 * - データ読み出し: t265_bulk_read(), t265_interrupt_read()
 */
#ifndef T265_INTERNAL_H
#define T265_INTERNAL_H

#include <stdint.h>

typedef struct libusb_context libusb_context;
typedef struct libusb_device_handle libusb_device_handle;
typedef struct libusb_device libusb_device;

#define T265_USB_VID 0x8087
#define T265_USB_PID 0x0b37

#define T265_MESSAGE_STATUS_SUCCESS 0x0000

#define T265_DEV_GET_SUPPORTED_RAW_STREAMS 0x0004
#define T265_DEV_RAW_STREAMS_CONTROL 0x0005
#define T265_DEV_SAMPLE 0x0011
#define T265_DEV_START 0x0012
#define T265_DEV_STOP 0x0013
#define T265_DEV_GET_POSE 0x0015
#define T265_DEV_SET_LOW_POWER_MODE 0x0025
#define T265_SLAM_6DOF_CONTROL 0x1006

#include "../include/t265_types.h"

#define T265_EP_MSG_OUT 0x02
#define T265_EP_MSG_IN 0x82
#define T265_EP_BULK_IN 0x81
#define T265_EP_INT_IN 0x83

#define T265_RESPONSE_BUFFER_SIZE 1024
#define T265_BULK_BUFFER_SIZE (1024 * 1024)
#define T265_STREAM_ENTRY_SIZE 12
#define T265_MAX_STREAMS 32

typedef struct t265_runtime {
    libusb_context *ctx;
    t265_context *owner_context;
    libusb_device_handle *handle;
    t265_device_info info;
    int owns_context;
    int claimed;
    int started;
    unsigned char interrupt_buffer[T265_RESPONSE_BUFFER_SIZE];
    int interrupt_transferred;
    int interrupt_offset;
    unsigned char bulk_buffer[T265_BULK_BUFFER_SIZE];
} t265_runtime;

struct t265_context {
    libusb_context *ctx;
    t265_device_info devices[T265_MAX_DEVICES];
    int device_count;
    t265_role_binding roles[T265_MAX_ROLES];
    int role_count;
};

typedef enum {
    T265_SAMPLE_UNKNOWN = 0,
    T265_SAMPLE_POSE,
    T265_SAMPLE_GYRO,
    T265_SAMPLE_ACCEL
} t265_sample_type;

typedef struct t265_interrupt_sample {
    t265_sample_type type;
    union {
        t265_pose_sample pose;
        t265_imu_sample imu;
    } data;
} t265_interrupt_sample;

uint16_t t265_read_le16(const unsigned char *p);
uint32_t t265_read_le32(const unsigned char *p);
uint64_t t265_read_le64(const unsigned char *p);
float t265_read_le_float(const unsigned char *p);
void t265_write_le16(unsigned char *p, uint16_t value);
void t265_write_le32(unsigned char *p, uint32_t value);

const char *t265_libusb_error_name(int rc);
const char *t265_message_name(uint16_t id);

t265_runtime* t265_open(void);
void t265_close(t265_runtime *dev);

int t265_command(t265_runtime *dev, uint16_t message_id,
                 const unsigned char *payload, int payload_length,
                 unsigned char *response, int response_length,
                 int *transferred);
int t265_command_expect_ok(t265_runtime *dev, uint16_t message_id,
                           const unsigned char *payload, int payload_length);

int t265_set_low_power_mode(t265_runtime *dev, int enabled);
int t265_get_supported_raw_streams(t265_runtime *dev, unsigned char *streams,
                                   uint16_t *stream_count);
int t265_filter_exposed_streams(const unsigned char *streams,
                                uint16_t stream_count,
                                unsigned char *active_streams,
                                uint16_t *active_count,
                                int enable_fisheye, int enable_imu);
int t265_raw_streams_control(t265_runtime *dev,
                             const unsigned char *active_streams,
                             uint16_t active_count);
int t265_slam_6dof_control(t265_runtime *dev, int enabled);

/*
 * T265 のストリーム構成を行います。
 * 
 * 仕様:
 * - この関数は T265 のstream構成だけを行います。
 * - t265_start() は呼びません。設定成功後に呼び出し側が明示的に t265_start() を呼ぶ必要があります。
 * - 内部で t265_set_low_power_mode(dev, 0) を呼びます。
 * - 内部で t265_get_supported_raw_streams() を呼びます。
 * - 内部で t265_filter_exposed_streams() を呼びます。
 * - 内部で t265_raw_streams_control() を呼びます。
 * - 内部で t265_slam_6dof_control(dev, enable_pose ? 1 : 0) を呼びます。
 * - enable_fisheye, enable_imu, enable_pose がすべて0の場合はエラーになります。
 */
int t265_configure_streams(t265_runtime *dev, int enable_fisheye,
                           int enable_imu, int enable_pose);

int t265_start(t265_runtime *dev);
int t265_stop(t265_runtime *dev);

int t265_bulk_read(t265_runtime *dev, unsigned char endpoint,
                  unsigned char *buffer, int buffer_length,
                  int *transferred, unsigned int timeout_ms);
int t265_interrupt_read(t265_runtime *dev, unsigned char endpoint,
                        unsigned char *buffer, int buffer_length,
                        int *transferred, unsigned int timeout_ms);

int t265_decode_pose(const unsigned char *buffer, int length, t265_pose_sample *pose);
int t265_decode_imu_sample(const unsigned char *buffer, int length, t265_imu_sample *imu);
int t265_decode_fisheye_frame(const unsigned char *buffer, int length, t265_fisheye_frame *frame);

int t265_decode_interrupt_message(const unsigned char *buffer, int length,
                                  t265_interrupt_sample *sample);
int t265_next_interrupt_message(const unsigned char *buffer, int transferred,
                                int *offset, const unsigned char **msg_out,
                                int *len_out);

/*
 * interrupt endpoint 0x83 IN から Pose / Gyro / Accelerometer sample を1つ返します。
 * 1回のUSB readに複数messageが含まれる可能性があるため、t265_runtime内部に読み残しbufferを持ちます。
 * Fisheye frame は対象外で、bulk endpoint 0x81 IN を使います。
 * この関数は t265_start() を呼びません。
 * 呼び出し前に t265_configure_streams() と t265_start() が成功している必要があります。
 */
int t265_read_next_interrupt_sample(t265_runtime *dev,
                                    t265_interrupt_sample *sample,
                                    unsigned int timeout_ms);

/*
 * bulk endpoint 0x81 IN から Fisheye frame を1つ読み込み、デコードして返します。
 * この関数は内部の bulk_buffer を使用してデータを読み込みます。
 * この関数は t265_start() を呼びません。
 * 呼び出し前に t265_configure_streams() と t265_start() が成功している必要があります。
 */
int t265_read_next_fisheye_frame(t265_runtime *dev,
                                 t265_fisheye_frame *frame,
                                 unsigned int timeout_ms);

#endif // T265_INTERNAL_H
