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

typedef enum
{
  OPTICAL_LASER_PROFILE_DEFAULT = 0,
  OPTICAL_LASER_PROFILE_FAST = 1,
  OPTICAL_LASER_PROFILE_STABLE = 2
} optical_laser_profile_t;

typedef enum
{
  OPTICAL_EVENT_NONE = 0,
  OPTICAL_EVENT_BRIDGE_RECOVERY_REQUESTED = 1,
  OPTICAL_EVENT_BRIDGE_RECOVERED = 2,
  OPTICAL_EVENT_BH1750_STALE = 3,
  OPTICAL_EVENT_VL53_STALE = 4,
  OPTICAL_EVENT_WONDERCAM_STALE = 5,
  OPTICAL_EVENT_WONDERCAM_ONLINE = 6,
  OPTICAL_EVENT_BASELINE_CAPTURED = 7,
  OPTICAL_EVENT_BASELINE_CLEARED = 8,
  OPTICAL_EVENT_LASER_PROFILE_DEFAULT = 9,
  OPTICAL_EVENT_LASER_PROFILE_FAST = 10,
  OPTICAL_EVENT_LASER_PROFILE_STABLE = 11,
  OPTICAL_EVENT_DIAGNOSTICS_RESET = 12
} optical_event_code_t;

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
  OPTICAL_STATUS_PRESENCE_DETECTED = (1UL << 11),
  OPTICAL_STATUS_VL53_RANGE_VALID = (1UL << 12),
  OPTICAL_STATUS_SNOW_HEIGHT_VALID = (1UL << 13),
  OPTICAL_STATUS_OBSTRUCTION_DETECTED = (1UL << 14)
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

typedef struct
{
  uint16_t snow_baseline_mm;
  uint16_t laser_presence_assert_ms;
  uint16_t laser_presence_release_ms;
  uint16_t laser_motion_hold_ms;
  uint8_t laser_profile;
  uint8_t reserved;
} optical_runtime_config_t;

typedef struct
{
  uint32_t bridge_recovery_count;
  uint32_t bh1750_stale_count;
  uint32_t vl53_stale_count;
  uint32_t wondercam_stale_count;
  uint32_t wondercam_online_count;
  uint32_t health_event_count;
  uint32_t fault_event_count;
  uint32_t last_health_ms;
  uint32_t last_fault_ms;
  uint16_t wondercam_conf_x10000;
  uint8_t last_health_code;
  uint8_t last_fault_code;
  uint8_t wondercam_raw_class_id;
  uint8_t wondercam_filtered_class_id;
  uint8_t wondercam_candidate_streak;
  uint8_t reserved;
} optical_runtime_diag_t;

#endif /* OPTICAL_SLICE_TYPES_H */
