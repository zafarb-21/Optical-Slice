#ifndef OPTICAL_SLICE_TYPES_H
#define OPTICAL_SLICE_TYPES_H

#include <stdint.h>

#define OPTICAL_READING_INVALID_U16 0xFFFFU

typedef enum
{
  OPTICAL_PRECIP_NONE = 0,
  OPTICAL_PRECIP_SNOW = 1,
  OPTICAL_PRECIP_ICE = 2,
  OPTICAL_PRECIP_UNKNOWN = 255
} optical_precip_type_t;

enum
{
  OPTICAL_STATUS_BH1750_PRESENT = (1UL << 0),
  OPTICAL_STATUS_BH1750_VALID = (1UL << 1),
  OPTICAL_STATUS_VL53L1X_PRESENT = (1UL << 2),
  OPTICAL_STATUS_CAMERA_PRESENT = (1UL << 3),
  OPTICAL_STATUS_LASER_PRESENT = (1UL << 4),
  OPTICAL_STATUS_SC18_PRESENT = (1UL << 5),
  OPTICAL_STATUS_MOTION_DETECTED = (1UL << 6),
  OPTICAL_STATUS_PACKAGE_DETECTED = (1UL << 7),
  OPTICAL_STATUS_DARK = (1UL << 8),
  OPTICAL_STATUS_MASTER_LINK_OK = (1UL << 9),
  OPTICAL_STATUS_LASER_SIGNAL_DETECTED = (1UL << 10),
  OPTICAL_STATUS_PRESENCE_DETECTED = (1UL << 11)
};

typedef struct
{
  uint32_t timestamp_ms;
  uint32_t status_flags;
  uint16_t ambient_lux_x10;
  uint16_t object_distance_mm;
  uint16_t snow_height_mm;
  uint16_t precipitation_level_x10;
  uint8_t precipitation_type;
  uint8_t motion_detected;
  uint8_t package_detected;
  uint8_t presence_detected;
  uint8_t dark_detected;
  uint8_t camera_online;
  uint8_t laser_online;
  uint8_t laser_signal_detected;
  uint8_t tof_range_status;
} optical_slice_frame_t;

#endif /* OPTICAL_SLICE_TYPES_H */
