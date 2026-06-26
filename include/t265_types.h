#ifndef T265_TYPES_H
#define T265_TYPES_H

#include <stdint.h>

typedef struct t265_runtime t265_runtime;
typedef struct t265_context t265_context;

#define T265_MAX_DEVICES 16
#define T265_MAX_ROLES 16

typedef struct t265_device_info {
    int index;
    uint8_t bus_number;
    uint8_t device_address;
    uint16_t vid;
    uint16_t pid;
    char serial[64];
    char label[128];
} t265_device_info;

typedef struct t265_role_binding {
    char role[64];
    char serial[64];
} t265_role_binding;

typedef enum {
    T265_OK = 0,
    T265_ERR_USB = -1,
    T265_ERR_INVALID_STATE = -2,
    T265_ERR_TIMEOUT = -3,
    T265_ERR_QUEUE_FULL = -4,
    T265_ERR_QUEUE_EMPTY = -5,
    T265_ERR_NOT_FOUND = -6
} t265_error_t;

typedef struct t265_pose_sample {
    float x;
    float y;
    float z;
    float quat_i;
    float quat_j;
    float quat_k;
    float quat_r;
    uint64_t timestamp_ns;
    uint32_t tracker_confidence;
    uint32_t mapper_confidence;
    uint32_t tracker_state;
} t265_pose_sample;

typedef struct t265_imu_sample {
    uint8_t sensor_id;
    uint64_t timestamp_ns;
    uint64_t arrival_timestamp_ns;
    uint32_t frame_id;
    uint32_t metadata_length;
    float temperature;
    uint32_t frame_length;
    float x;
    float y;
    float z;
} t265_imu_sample;

typedef struct t265_fisheye_frame {
    uint8_t sensor_id;
    uint64_t timestamp_ns;
    uint64_t arrival_timestamp_ns;
    uint32_t frame_id;
    uint32_t metadata_length;
    uint32_t exposure_time;
    float gain;
    uint32_t frame_length;
    uint32_t width;
    uint32_t height;
    /*
     * Non-owning pointer to the decoded frame payload.
     *
     * Important:
     * - In callbacks, this pointer is only valid for the duration of the
     *   callback unless the implementation explicitly copies the payload.
     * - Do not store this pointer without copying the image data first.
     * - Public ownership rules for queued image payloads are part of the
     *   Phase 3 image ownership design.
     */
    const unsigned char *frame_data;
} t265_fisheye_frame;

/* Sensor IDs */
#define T265_SENSOR_ID_FISHEYE0 0x03
#define T265_SENSOR_ID_FISHEYE1 0x23
#define T265_SENSOR_ID_GYRO 0x04
#define T265_SENSOR_ID_ACCEL 0x05

#endif // T265_TYPES_H
