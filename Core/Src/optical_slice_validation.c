#include "optical_slice_validation.h"

#include <stdio.h>
#include <string.h>
#include "optical_slice_sensors.h"
#include "usart.h"

static void OpticalValidation_SetFlag(uint32_t *pass_flags, uint32_t *fail_flags, uint32_t flag, uint8_t passed)
{
  if (passed != 0U)
  {
    *pass_flags |= flag;
    *fail_flags &= ~flag;
  }
  else
  {
    *pass_flags &= ~flag;
    *fail_flags |= flag;
  }
}

HAL_StatusTypeDef OpticalValidation_RunSnapshot(optical_validation_report_t *report)
{
  uint8_t sensor_count = 0U;
  HAL_StatusTypeDef status;

  if (report == NULL)
  {
    return HAL_ERROR;
  }

  memset(report, 0, sizeof(*report));
  report->timestamp_ms = HAL_GetTick();
  report->frame.object_distance_mm = OPTICAL_READING_INVALID_U16;
  report->frame.snow_height_mm = OPTICAL_READING_INVALID_U16;
  report->frame.precipitation_level_x10 = OPTICAL_READING_INVALID_U16;
  report->frame.precipitation_type = OPTICAL_PRECIP_UNKNOWN;
  report->frame.tof_range_status = 0xFFU;

  status = OpticalSensors_Poll(&report->frame, NULL);
  OpticalValidation_SetFlag(&report->pass_flags, &report->fail_flags, OPTICAL_VALIDATION_APP_POLL_OK, (uint8_t)(status == HAL_OK));
  OpticalValidation_SetFlag(&report->pass_flags,
                            &report->fail_flags,
                            OPTICAL_VALIDATION_SC18_READY,
                            (uint8_t)((report->frame.status_flags & OPTICAL_STATUS_SC18_PRESENT) != 0U));
  OpticalValidation_SetFlag(&report->pass_flags,
                            &report->fail_flags,
                            OPTICAL_VALIDATION_BH1750_READY,
                            (uint8_t)((report->frame.status_flags & OPTICAL_STATUS_BH1750_VALID) != 0U));
  OpticalValidation_SetFlag(&report->pass_flags,
                            &report->fail_flags,
                            OPTICAL_VALIDATION_VL53_PRESENT,
                            (uint8_t)((report->frame.status_flags & OPTICAL_STATUS_VL53L1X_PRESENT) != 0U));
  OpticalValidation_SetFlag(&report->pass_flags,
                            &report->fail_flags,
                            OPTICAL_VALIDATION_VL53_RANGE_VALID,
                            (uint8_t)((report->frame.status_flags & OPTICAL_STATUS_VL53_RANGE_VALID) != 0U));
  OpticalValidation_SetFlag(&report->pass_flags,
                            &report->fail_flags,
                            OPTICAL_VALIDATION_LASER_GPIO_READY,
                            report->frame.laser_online);
  OpticalValidation_SetFlag(&report->pass_flags,
                            &report->fail_flags,
                            OPTICAL_VALIDATION_LASER_SIGNAL_SEEN,
                            report->frame.laser_signal_detected);
  OpticalValidation_SetFlag(&report->pass_flags,
                            &report->fail_flags,
                            OPTICAL_VALIDATION_CAMERA_READY,
                            (uint8_t)((report->frame.status_flags & OPTICAL_STATUS_CAMERA_PRESENT) != 0U));
  OpticalValidation_SetFlag(&report->pass_flags,
                            &report->fail_flags,
                            OPTICAL_VALIDATION_SNOW_HEIGHT_VALID,
                            (uint8_t)((report->frame.status_flags & OPTICAL_STATUS_SNOW_HEIGHT_VALID) != 0U));
  OpticalValidation_SetFlag(&report->pass_flags,
                            &report->fail_flags,
                            OPTICAL_VALIDATION_SNOW_BASELINE_SET,
                            OpticalSensors_HasSnowBaseline());
  OpticalValidation_SetFlag(&report->pass_flags,
                            &report->fail_flags,
                            OPTICAL_VALIDATION_MASTER_LINK_READY,
                            OpticalMasterLink_IsHealthy());

  (void)OpticalSensors_GetTable(&sensor_count);
  OpticalSensors_GetRuntimeConfig(&report->config);
  OpticalSensors_GetDiagnostics(&report->diagnostics);
  report->sensor_count = sensor_count;
  report->timestamp_ms = report->frame.timestamp_ms;

  return status;
}

int OpticalValidation_FormatReport(const optical_validation_report_t *report, char *buffer, size_t buffer_len)
{
  if ((report == NULL) || (buffer == NULL) || (buffer_len == 0U))
  {
    return -1;
  }

  return snprintf(buffer,
                  buffer_len,
                  "VALIDATION,t=%lu,pass=0x%08lX,fail=0x%08lX,sensors=%u,lux_x10=%u,tof_mm=%u,snow_mm=%u,tof_status=%u,cam=%u,laser_rx=%u,laser_sig=%u,presence=%u,motion=%u,baseline_mm=%u,laser_profile=%u,bridge_rec=%lu,bh_stale=%lu,vl53_stale=%lu,wcam_stale=%lu,health=%lu,fault=%lu\r\n",
                  (unsigned long)report->timestamp_ms,
                  (unsigned long)report->pass_flags,
                  (unsigned long)report->fail_flags,
                  report->sensor_count,
                  report->frame.ambient_lux_x10,
                  report->frame.object_distance_mm,
                  report->frame.snow_height_mm,
                  report->frame.tof_range_status,
                  report->frame.camera_online,
                  report->frame.laser_online,
                  report->frame.laser_signal_detected,
                  report->frame.presence_detected,
                  report->frame.motion_detected,
                  report->config.snow_baseline_mm,
                  report->config.laser_profile,
                  (unsigned long)report->diagnostics.bridge_recovery_count,
                  (unsigned long)report->diagnostics.bh1750_stale_count,
                  (unsigned long)report->diagnostics.vl53_stale_count,
                  (unsigned long)report->diagnostics.wondercam_stale_count,
                  (unsigned long)report->diagnostics.health_event_count,
                  (unsigned long)report->diagnostics.fault_event_count);
}
