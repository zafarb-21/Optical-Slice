#ifndef OPTICAL_SLICE_SENSORS_H
#define OPTICAL_SLICE_SENSORS_H

#include <stdint.h>

#include "main.h"
#include "optical_slice_types.h"

typedef enum
{
  OPTICAL_SENSOR_BH1750 = 0,
  OPTICAL_SENSOR_VL53L1X,
  OPTICAL_SENSOR_SC18IS604,
  OPTICAL_SENSOR_AI_CAMERA,
  OPTICAL_SENSOR_LASER_FRONT_END,
  OPTICAL_SENSOR_COUNT
} optical_sensor_id_t;

typedef struct
{
  optical_sensor_id_t id;
  const char *name;
  uint8_t address_7bit;
  uint8_t present;
  uint8_t initialized;
  uint32_t last_probe_ms;
} optical_sensor_status_t;

enum
{
  OPTICAL_SENSOR_UPDATE_NONE = 0UL,
  OPTICAL_SENSOR_UPDATE_BH1750 = (1UL << 0),
  OPTICAL_SENSOR_UPDATE_VL53L1X = (1UL << 1),
  OPTICAL_SENSOR_UPDATE_WONDERCAM = (1UL << 2)
};

HAL_StatusTypeDef OpticalSensors_Init(void);
HAL_StatusTypeDef OpticalSensors_Poll(optical_slice_frame_t *frame, uint32_t *update_flags);
const optical_sensor_status_t *OpticalSensors_GetTable(uint8_t *count);
void OpticalSensors_GetRuntimeConfig(optical_runtime_config_t *config);
void OpticalSensors_GetDiagnostics(optical_runtime_diag_t *diag);
uint8_t OpticalSensors_HasSnowBaseline(void);
uint16_t OpticalSensors_GetSnowBaselineMm(void);
HAL_StatusTypeDef OpticalSensors_CaptureSnowBaseline(void);
void OpticalSensors_ClearSnowBaseline(void);
HAL_StatusTypeDef OpticalSensors_SetLaserProfile(optical_laser_profile_t profile);
const char *OpticalSensors_GetLaserProfileName(uint8_t profile);
void OpticalSensors_ResetDiagnostics(void);

#endif /* OPTICAL_SLICE_SENSORS_H */
