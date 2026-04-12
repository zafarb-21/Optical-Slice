#ifndef OPTICAL_SLICE_VALIDATION_H
#define OPTICAL_SLICE_VALIDATION_H

#include <stddef.h>
#include <stdint.h>

#include "main.h"
#include "optical_slice_types.h"

enum
{
  OPTICAL_VALIDATION_APP_POLL_OK = (1UL << 0),
  OPTICAL_VALIDATION_SC18_READY = (1UL << 1),
  OPTICAL_VALIDATION_BH1750_READY = (1UL << 2),
  OPTICAL_VALIDATION_VL53_PRESENT = (1UL << 3),
  OPTICAL_VALIDATION_VL53_RANGE_VALID = (1UL << 4),
  OPTICAL_VALIDATION_LASER_GPIO_READY = (1UL << 5),
  OPTICAL_VALIDATION_LASER_SIGNAL_SEEN = (1UL << 6)
};

typedef struct
{
  uint32_t timestamp_ms;
  uint32_t pass_flags;
  uint32_t fail_flags;
  optical_slice_frame_t frame;
  uint8_t sensor_count;
} optical_validation_report_t;

HAL_StatusTypeDef OpticalValidation_RunSnapshot(optical_validation_report_t *report);
int OpticalValidation_FormatReport(const optical_validation_report_t *report, char *buffer, size_t buffer_len);

#endif /* OPTICAL_SLICE_VALIDATION_H */
