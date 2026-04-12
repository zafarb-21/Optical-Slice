#ifndef VL53L1X_BRIDGE_H
#define VL53L1X_BRIDGE_H

#include <stdint.h>

#include "main.h"

typedef struct
{
  uint8_t present;
  uint8_t initialized;
  uint8_t ranging_active;
  uint8_t boot_state;
  uint16_t sensor_id;
  uint8_t model_id;
  uint8_t module_type;
  uint8_t mask_rev;
  uint8_t uld_enabled;
  uint8_t init_stage;
  int16_t last_api_status;
} vl53l1x_bridge_info_t;

HAL_StatusTypeDef VL53L1XBridge_Init(void);
HAL_StatusTypeDef VL53L1XBridge_Poll(uint16_t *distance_mm, uint8_t *range_status);
const vl53l1x_bridge_info_t *VL53L1XBridge_GetInfo(void);

#endif /* VL53L1X_BRIDGE_H */
