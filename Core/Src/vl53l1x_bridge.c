#include "vl53l1x_bridge.h"

#include <stddef.h>
#include <string.h>

#include "VL53L1X_api.h"
#include "optical_slice_config.h"
#include "optical_slice_types.h"

#define VL53L1X_DEV_ADDR_8BIT        0x52U
#define VL53L1X_BOOT_TIMEOUT_MS      500U
#define VL53L1X_DEFAULT_DISTANCE_MODE 2U
#define VL53L1X_DEFAULT_TIMING_BUDGET_MS 100U
#define VL53L1X_DEFAULT_INTERMEAS_MS 100U

static vl53l1x_bridge_info_t vl53_info;

HAL_StatusTypeDef VL53L1XBridge_Init(void)
{
  HAL_StatusTypeDef status = HAL_ERROR;
  VL53L1X_ERROR api_status;
  uint32_t start_ms;
  uint8_t state = 0U;

  memset(&vl53_info, 0, sizeof(vl53_info));
  vl53_info.uld_enabled = (uint8_t)OPTICAL_ENABLE_VL53L1X_ULD;
  vl53_info.last_api_status = 0;
  start_ms = HAL_GetTick();

  do
  {
    vl53_info.init_stage = 1U;
    api_status = VL53L1X_BootState(VL53L1X_DEV_ADDR_8BIT, &state);
    vl53_info.last_api_status = (int16_t)api_status;
    if ((api_status == VL53L1X_ERROR_NONE) && (state != 0U))
    {
      break;
    }
    HAL_Delay(2U);
  } while ((HAL_GetTick() - start_ms) < VL53L1X_BOOT_TIMEOUT_MS);

  if (state == 0U)
  {
    return HAL_TIMEOUT;
  }

  vl53_info.boot_state = state;

  vl53_info.init_stage = 2U;
  api_status = VL53L1X_GetSensorId(VL53L1X_DEV_ADDR_8BIT, &vl53_info.sensor_id);
  vl53_info.last_api_status = (int16_t)api_status;
  if (api_status != VL53L1X_ERROR_NONE)
  {
    return HAL_ERROR;
  }

  vl53_info.model_id = (uint8_t)(vl53_info.sensor_id >> 8);
  vl53_info.module_type = (uint8_t)(vl53_info.sensor_id & 0xFFU);
  vl53_info.present = 1U;

#if OPTICAL_ENABLE_VL53L1X_ULD
  vl53_info.init_stage = 3U;
  api_status = VL53L1X_SensorInit(VL53L1X_DEV_ADDR_8BIT);
  vl53_info.last_api_status = (int16_t)api_status;
  if (api_status != VL53L1X_ERROR_NONE)
  {
    return HAL_ERROR;
  }

  vl53_info.init_stage = 4U;
  api_status = VL53L1X_SetDistanceMode(VL53L1X_DEV_ADDR_8BIT, VL53L1X_DEFAULT_DISTANCE_MODE);
  vl53_info.last_api_status = (int16_t)api_status;
  if (api_status != VL53L1X_ERROR_NONE)
  {
    return HAL_ERROR;
  }

  vl53_info.init_stage = 5U;
  api_status = VL53L1X_SetTimingBudgetInMs(VL53L1X_DEV_ADDR_8BIT, VL53L1X_DEFAULT_TIMING_BUDGET_MS);
  vl53_info.last_api_status = (int16_t)api_status;
  if (api_status != VL53L1X_ERROR_NONE)
  {
    return HAL_ERROR;
  }

  vl53_info.init_stage = 6U;
  api_status = VL53L1X_SetInterMeasurementInMs(VL53L1X_DEV_ADDR_8BIT, VL53L1X_DEFAULT_INTERMEAS_MS);
  vl53_info.last_api_status = (int16_t)api_status;
  if (api_status != VL53L1X_ERROR_NONE)
  {
    return HAL_ERROR;
  }

  vl53_info.init_stage = 7U;
  api_status = VL53L1X_SetInterruptPolarity(VL53L1X_DEV_ADDR_8BIT, 0U);
  vl53_info.last_api_status = (int16_t)api_status;
  if (api_status != VL53L1X_ERROR_NONE)
  {
    return HAL_ERROR;
  }

  vl53_info.init_stage = 8U;
  api_status = VL53L1X_StartRanging(VL53L1X_DEV_ADDR_8BIT);
  vl53_info.last_api_status = (int16_t)api_status;
  if (api_status != VL53L1X_ERROR_NONE)
  {
    return HAL_ERROR;
  }

  vl53_info.initialized = 1U;
  vl53_info.ranging_active = 1U;
#endif

  vl53_info.init_stage = 0U;
  status = HAL_OK;
  return status;
}

HAL_StatusTypeDef VL53L1XBridge_Poll(uint16_t *distance_mm, uint8_t *range_status)
{
  VL53L1X_ERROR api_status;
  uint8_t data_ready = 0U;

  if (vl53_info.present == 0U)
  {
    return HAL_ERROR;
  }

#if !OPTICAL_ENABLE_VL53L1X_ULD
  return HAL_ERROR;
#else
  if (vl53_info.ranging_active == 0U)
  {
    return HAL_ERROR;
  }

  api_status = VL53L1X_CheckForDataReady(VL53L1X_DEV_ADDR_8BIT, &data_ready);
  if (api_status != VL53L1X_ERROR_NONE)
  {
    return HAL_ERROR;
  }

  if (data_ready == 0U)
  {
    return HAL_BUSY;
  }

  if (range_status != NULL)
  {
    api_status = VL53L1X_GetRangeStatus(VL53L1X_DEV_ADDR_8BIT, range_status);
    if (api_status != VL53L1X_ERROR_NONE)
    {
      return HAL_ERROR;
    }
  }

  if (distance_mm != NULL)
  {
    api_status = VL53L1X_GetDistance(VL53L1X_DEV_ADDR_8BIT, distance_mm);
    if (api_status != VL53L1X_ERROR_NONE)
    {
      return HAL_ERROR;
    }
  }

  api_status = VL53L1X_ClearInterrupt(VL53L1X_DEV_ADDR_8BIT);
  if (api_status != VL53L1X_ERROR_NONE)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
#endif
}

const vl53l1x_bridge_info_t *VL53L1XBridge_GetInfo(void)
{
  return &vl53_info;
}
