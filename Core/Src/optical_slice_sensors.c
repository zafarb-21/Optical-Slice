#include "optical_slice_sensors.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "optical_slice_config.h"
#include "sc18is604.h"
#include "usart.h"
#include "vl53l1x_bridge.h"

#define BH1750_CONTINUOUS_HIGH_RES_MODE 0x10U
#define WONDERCAM_REG_FW_VERSION        0x0000U
#define WONDERCAM_REG_LED               0x0030U
#define WONDERCAM_REG_FUNC              0x0035U
#define WONDERCAM_REG_CLASS_SUMM        0x0C00U
#define WONDERCAM_CLASS_SUMM_LEN        128U
#define WONDERCAM_APP_CLASSIFICATION    3U

typedef enum
{
  WONDERCAM_DIAG_NONE = 0,
  WONDERCAM_DIAG_NO_BRIDGE,
  WONDERCAM_DIAG_FW_READ_FAIL,
  WONDERCAM_DIAG_FUNC_READ_FAIL,
  WONDERCAM_DIAG_FUNC_WRITE_FAIL,
  WONDERCAM_DIAG_FUNC_VERIFY_FAIL,
  WONDERCAM_DIAG_LED_WRITE_FAIL,
  WONDERCAM_DIAG_SUMMARY_READ_FAIL,
  WONDERCAM_DIAG_ONLINE
} wondercam_diag_code_t;

static optical_sensor_status_t sensor_table[OPTICAL_SENSOR_COUNT];
static uint32_t last_bh1750_sample_ms;
static uint32_t last_wondercam_sample_ms;
static uint16_t cached_ambient_lux_x10;
static uint8_t cached_dark_detected;
static uint16_t cached_vl53_distance_mm;
static uint8_t cached_vl53_range_status;
static uint8_t cached_wondercam_online;
static uint8_t cached_wondercam_class_id;
static uint16_t cached_wondercam_conf_x10000;
static uint8_t wondercam_candidate_class_id;
static uint16_t wondercam_candidate_conf_x10000;
static uint8_t wondercam_candidate_streak;
#if OPTICAL_LASER_DEBUG_TOGGLE
static uint32_t last_laser_toggle_ms;
#endif
static uint8_t laser_tx_state;
static wondercam_diag_code_t wondercam_diag_code;

static void OpticalSensors_SetUpdateFlag(uint32_t *update_flags, uint32_t flag)
{
  if (update_flags != NULL)
  {
    *update_flags |= flag;
  }
}

static void OpticalSensors_ResetFrameFlags(optical_slice_frame_t *frame)
{
  frame->status_flags &= ~(OPTICAL_STATUS_BH1750_PRESENT |
                           OPTICAL_STATUS_BH1750_VALID |
                           OPTICAL_STATUS_VL53L1X_PRESENT |
                           OPTICAL_STATUS_CAMERA_PRESENT |
                           OPTICAL_STATUS_LASER_PRESENT |
                           OPTICAL_STATUS_SC18_PRESENT |
                           OPTICAL_STATUS_DARK |
                           OPTICAL_STATUS_LASER_SIGNAL_DETECTED);
}

static void OpticalSensors_LogWonderCamDiag(wondercam_diag_code_t code, HAL_StatusTypeDef status)
{
  char message[96];
  int length;
  const char *stage = "none";

  if (wondercam_diag_code == code)
  {
    return;
  }

  wondercam_diag_code = code;

  switch (code)
  {
    case WONDERCAM_DIAG_NO_BRIDGE:
      stage = "bridge_offline";
      break;
    case WONDERCAM_DIAG_FW_READ_FAIL:
      stage = "fw_read";
      break;
    case WONDERCAM_DIAG_FUNC_READ_FAIL:
      stage = "func_read";
      break;
    case WONDERCAM_DIAG_FUNC_WRITE_FAIL:
      stage = "func_write";
      break;
    case WONDERCAM_DIAG_FUNC_VERIFY_FAIL:
      stage = "func_verify";
      break;
    case WONDERCAM_DIAG_LED_WRITE_FAIL:
      stage = "led_write";
      break;
    case WONDERCAM_DIAG_SUMMARY_READ_FAIL:
      stage = "summary_read";
      break;
    case WONDERCAM_DIAG_ONLINE:
      stage = "online";
      break;
    default:
      stage = "none";
      break;
  }

  length = snprintf(message,
                    sizeof(message),
                    "WCAM | %s status=%d i2c=0x%02X(%s)\r\n",
                    stage,
                    status,
                    SC18IS604_GetLastI2cStatus(),
                    SC18IS604_I2cStatusText(SC18IS604_GetLastI2cStatus()));

  if ((length > 0) && ((size_t)length < sizeof(message)))
  {
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)message, (uint16_t)length, 50U);
  }
}

static void OpticalSensors_ScanBridgeI2cBus(void)
{
  char message[96];
  int length;
  uint8_t address;
  uint8_t found_any = 0U;
  uint8_t dummy_reg[2] = {0x00U, 0x00U};

  for (address = 0x08U; address <= 0x77U; ++address)
  {
    if (SC18IS604_I2cWrite(address, dummy_reg, (uint8_t)sizeof(dummy_reg)) == HAL_OK)
    {
      found_any = 1U;
      length = snprintf(message,
                        sizeof(message),
                        "SCAN | found 0x%02X i2c=0x%02X(%s)\r\n",
                        address,
                        SC18IS604_GetLastI2cStatus(),
                        SC18IS604_I2cStatusText(SC18IS604_GetLastI2cStatus()));
      if ((length > 0) && ((size_t)length < sizeof(message)))
      {
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)message, (uint16_t)length, 50U);
      }
    }
  }

  if (found_any == 0U)
  {
    length = snprintf(message, sizeof(message), "SCAN | no downstream I2C devices found\r\n");
    if ((length > 0) && ((size_t)length < sizeof(message)))
    {
      (void)HAL_UART_Transmit(&huart2, (uint8_t *)message, (uint16_t)length, 50U);
    }
  }
}

static HAL_StatusTypeDef OpticalSensors_WonderCamRead(uint16_t reg, uint8_t *data, uint8_t length)
{
  uint8_t reg_addr[2];
  HAL_StatusTypeDef status;

  if ((data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  reg_addr[0] = (uint8_t)(reg & 0xFFU);
  reg_addr[1] = (uint8_t)((reg >> 8) & 0xFFU);

  status = SC18IS604_I2cWrite(OPTICAL_ADDR_AI_CAMERA, reg_addr, (uint8_t)sizeof(reg_addr));
  if (status != HAL_OK)
  {
    return status;
  }

  return SC18IS604_I2cRead(OPTICAL_ADDR_AI_CAMERA, data, length);
}

static HAL_StatusTypeDef OpticalSensors_WonderCamWrite(uint16_t reg, const uint8_t *data, uint8_t length)
{
  uint8_t tx_data[18];

  if ((data == NULL) || (length == 0U) || ((uint16_t)length > (sizeof(tx_data) - 2U)))
  {
    return HAL_ERROR;
  }

  tx_data[0] = (uint8_t)(reg & 0xFFU);
  tx_data[1] = (uint8_t)((reg >> 8) & 0xFFU);
  memcpy(&tx_data[2], data, length);

  return SC18IS604_I2cWrite(OPTICAL_ADDR_AI_CAMERA, tx_data, (uint8_t)(length + 2U));
}

static HAL_StatusTypeDef OpticalSensors_StartWonderCam(optical_sensor_status_t *sensor)
{
  HAL_StatusTypeDef status;
  uint8_t current_func = 0U;
  uint8_t led_on = 1U;
  uint32_t start_ms;
  char fw_version[16];

  if (SC18IS604_IsReady() == 0U)
  {
    sensor->present = 0U;
    sensor->initialized = 0U;
    OpticalSensors_LogWonderCamDiag(WONDERCAM_DIAG_NO_BRIDGE, HAL_ERROR);
    return HAL_ERROR;
  }

  status = OpticalSensors_WonderCamRead(WONDERCAM_REG_FW_VERSION, (uint8_t *)fw_version, sizeof(fw_version));
  if (status != HAL_OK)
  {
    sensor->present = 0U;
    sensor->initialized = 0U;
    sensor->last_probe_ms = HAL_GetTick();
    OpticalSensors_LogWonderCamDiag(WONDERCAM_DIAG_FW_READ_FAIL, status);
    OpticalSensors_ScanBridgeI2cBus();
    return status;
  }

  sensor->present = 1U;

  status = OpticalSensors_WonderCamRead(WONDERCAM_REG_FUNC, &current_func, 1U);
  if (status != HAL_OK)
  {
    sensor->initialized = 0U;
    sensor->last_probe_ms = HAL_GetTick();
    OpticalSensors_LogWonderCamDiag(WONDERCAM_DIAG_FUNC_READ_FAIL, status);
    return status;
  }

  if (current_func != WONDERCAM_APP_CLASSIFICATION)
  {
    current_func = WONDERCAM_APP_CLASSIFICATION;
    status = OpticalSensors_WonderCamWrite(WONDERCAM_REG_FUNC, &current_func, 1U);
    if (status != HAL_OK)
    {
      sensor->initialized = 0U;
      sensor->last_probe_ms = HAL_GetTick();
      OpticalSensors_LogWonderCamDiag(WONDERCAM_DIAG_FUNC_WRITE_FAIL, status);
      return status;
    }

    start_ms = HAL_GetTick();
    do
    {
      HAL_Delay(50U);
      status = OpticalSensors_WonderCamRead(WONDERCAM_REG_FUNC, &current_func, 1U);
      if ((status == HAL_OK) && (current_func == WONDERCAM_APP_CLASSIFICATION))
      {
        break;
      }
    } while ((HAL_GetTick() - start_ms) < 4000U);

    if ((status != HAL_OK) || (current_func != WONDERCAM_APP_CLASSIFICATION))
    {
      sensor->initialized = 0U;
      sensor->last_probe_ms = HAL_GetTick();
      OpticalSensors_LogWonderCamDiag(WONDERCAM_DIAG_FUNC_VERIFY_FAIL, (status == HAL_OK) ? HAL_TIMEOUT : status);
      return (status == HAL_OK) ? HAL_TIMEOUT : status;
    }
  }

  status = OpticalSensors_WonderCamWrite(WONDERCAM_REG_LED, &led_on, 1U);
  if (status != HAL_OK)
  {
    sensor->initialized = 0U;
    sensor->last_probe_ms = HAL_GetTick();
    OpticalSensors_LogWonderCamDiag(WONDERCAM_DIAG_LED_WRITE_FAIL, status);
    return status;
  }

  sensor->initialized = 1U;
  sensor->last_probe_ms = HAL_GetTick();
  cached_wondercam_online = 1U;
  OpticalSensors_LogWonderCamDiag(WONDERCAM_DIAG_ONLINE, HAL_OK);
  return HAL_OK;
}

static void OpticalSensors_ApplyWonderCamClass(optical_slice_frame_t *frame, uint8_t class_id, uint16_t conf_x10000)
{
  uint16_t conf_x10 = (uint16_t)((conf_x10000 + 500U) / 1000U);

  cached_wondercam_class_id = class_id;
  cached_wondercam_conf_x10000 = conf_x10000;

  switch (class_id)
  {
    case OPTICAL_WONDERCAM_CLASS_SNOW:
      frame->precipitation_type = OPTICAL_PRECIP_SNOW;
      frame->precipitation_level_x10 = conf_x10;
      frame->package_detected = 0U;
      break;

    case OPTICAL_WONDERCAM_CLASS_ICE:
      frame->precipitation_type = OPTICAL_PRECIP_ICE;
      frame->precipitation_level_x10 = conf_x10;
      frame->package_detected = 0U;
      break;

    case OPTICAL_WONDERCAM_CLASS_PACKAGE:
      frame->precipitation_type = OPTICAL_PRECIP_NONE;
      frame->precipitation_level_x10 = 0U;
      frame->package_detected = 1U;
      break;

    case OPTICAL_WONDERCAM_CLASS_NONE:
      frame->precipitation_type = OPTICAL_PRECIP_NONE;
      frame->precipitation_level_x10 = 0U;
      frame->package_detected = 0U;
      break;

    default:
      frame->precipitation_type = OPTICAL_PRECIP_UNKNOWN;
      frame->precipitation_level_x10 = OPTICAL_READING_INVALID_U16;
      frame->package_detected = 0U;
      break;
  }
}

static HAL_StatusTypeDef OpticalSensors_SampleWonderCam(optical_slice_frame_t *frame, uint32_t *update_flags)
{
  optical_sensor_status_t *sensor = &sensor_table[OPTICAL_SENSOR_AI_CAMERA];
  uint8_t summary[WONDERCAM_CLASS_SUMM_LEN];
  uint8_t raw_class_id;
  uint8_t filtered_class_id;
  uint16_t conf_x10000;
  uint8_t previous_online = cached_wondercam_online;
  HAL_StatusTypeDef status;
  uint32_t now;

  if (SC18IS604_IsReady() == 0U)
  {
    sensor->present = 0U;
    sensor->initialized = 0U;
    cached_wondercam_online = 0U;
    wondercam_candidate_streak = 0U;
    if (previous_online != 0U)
    {
      OpticalSensors_SetUpdateFlag(update_flags, OPTICAL_SENSOR_UPDATE_WONDERCAM);
    }
    OpticalSensors_LogWonderCamDiag(WONDERCAM_DIAG_NO_BRIDGE, HAL_ERROR);
    return HAL_ERROR;
  }

  now = HAL_GetTick();
  if ((sensor->initialized == 0U) &&
      ((sensor->last_probe_ms == 0U) || ((now - sensor->last_probe_ms) >= OPTICAL_I2C_PROBE_PERIOD_MS)))
  {
    status = OpticalSensors_StartWonderCam(sensor);
    if (status != HAL_OK)
    {
      cached_wondercam_online = 0U;
      if (previous_online != 0U)
      {
        OpticalSensors_SetUpdateFlag(update_flags, OPTICAL_SENSOR_UPDATE_WONDERCAM);
      }
      return status;
    }
  }

  if (sensor->initialized == 0U)
  {
    cached_wondercam_online = 0U;
    return HAL_ERROR;
  }

  if ((now - last_wondercam_sample_ms) < OPTICAL_SAMPLE_PERIOD_MS)
  {
    frame->camera_online = cached_wondercam_online;
    OpticalSensors_ApplyWonderCamClass(frame, cached_wondercam_class_id, cached_wondercam_conf_x10000);
    return HAL_OK;
  }

  status = OpticalSensors_WonderCamRead(WONDERCAM_REG_CLASS_SUMM, summary, WONDERCAM_CLASS_SUMM_LEN);
  if (status != HAL_OK)
  {
    sensor->present = 0U;
    sensor->initialized = 0U;
    cached_wondercam_online = 0U;
    wondercam_candidate_streak = 0U;
    if (previous_online != 0U)
    {
      OpticalSensors_SetUpdateFlag(update_flags, OPTICAL_SENSOR_UPDATE_WONDERCAM);
    }
    OpticalSensors_LogWonderCamDiag(WONDERCAM_DIAG_SUMMARY_READ_FAIL, status);
    return status;
  }

  sensor->present = 1U;
  cached_wondercam_online = 1U;
  frame->camera_online = 1U;
  OpticalSensors_LogWonderCamDiag(WONDERCAM_DIAG_ONLINE, HAL_OK);
  raw_class_id = summary[1];
  conf_x10000 = (uint16_t)(((uint16_t)summary[3] << 8) | summary[2]);
  filtered_class_id = (conf_x10000 >= OPTICAL_WONDERCAM_MIN_CONF_X10000) ? raw_class_id : 0U;

  if (filtered_class_id != wondercam_candidate_class_id)
  {
    wondercam_candidate_class_id = filtered_class_id;
    wondercam_candidate_conf_x10000 = conf_x10000;
    wondercam_candidate_streak = 1U;
  }
  else
  {
    wondercam_candidate_conf_x10000 = conf_x10000;
    if (wondercam_candidate_streak < 255U)
    {
      ++wondercam_candidate_streak;
    }
  }

  if (wondercam_candidate_streak >= OPTICAL_WONDERCAM_STABLE_FRAMES)
  {
    cached_wondercam_class_id = wondercam_candidate_class_id;
    cached_wondercam_conf_x10000 = wondercam_candidate_conf_x10000;
  }

  OpticalSensors_ApplyWonderCamClass(frame, cached_wondercam_class_id, cached_wondercam_conf_x10000);
  last_wondercam_sample_ms = now;
  OpticalSensors_SetUpdateFlag(update_flags, OPTICAL_SENSOR_UPDATE_WONDERCAM);
  return HAL_OK;
}

static HAL_StatusTypeDef OpticalSensors_StartBh1750(optical_sensor_status_t *sensor)
{
  uint8_t mode = BH1750_CONTINUOUS_HIGH_RES_MODE;
  HAL_StatusTypeDef status;

  status = SC18IS604_I2cWrite(sensor->address_7bit, &mode, 1U);
  if (status == HAL_OK)
  {
    sensor->present = 1U;
    sensor->initialized = 1U;
  }
  else
  {
    sensor->present = 0U;
    sensor->initialized = 0U;
  }

  sensor->last_probe_ms = HAL_GetTick();
  return status;
}

static HAL_StatusTypeDef OpticalSensors_SampleBh1750(optical_slice_frame_t *frame, uint32_t *update_flags)
{
  optical_sensor_status_t *sensor = &sensor_table[OPTICAL_SENSOR_BH1750];
  uint8_t raw[2];
  uint16_t measurement;
  HAL_StatusTypeDef status;
  uint32_t now;

  if (SC18IS604_IsReady() == 0U)
  {
    sensor->present = 0U;
    sensor->initialized = 0U;
    return HAL_ERROR;
  }

  if (sensor->initialized == 0U)
  {
    status = OpticalSensors_StartBh1750(sensor);
    if (status != HAL_OK)
    {
      return status;
    }
  }

  now = HAL_GetTick();
  if ((now - last_bh1750_sample_ms) < OPTICAL_SAMPLE_PERIOD_MS)
  {
    frame->ambient_lux_x10 = cached_ambient_lux_x10;
    frame->dark_detected = cached_dark_detected;
    return HAL_OK;
  }

  status = SC18IS604_I2cRead(sensor->address_7bit, raw, sizeof(raw));
  if (status != HAL_OK)
  {
    sensor->present = 0U;
    sensor->initialized = 0U;
    return status;
  }

  sensor->present = 1U;
  measurement = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
  cached_ambient_lux_x10 = (uint16_t)((measurement * 10U) / 12U);
  cached_dark_detected = (uint8_t)((cached_ambient_lux_x10 <= OPTICAL_BH1750_DARK_THRESHOLD_X10) ? 1U : 0U);
  frame->ambient_lux_x10 = cached_ambient_lux_x10;
  frame->dark_detected = cached_dark_detected;
  last_bh1750_sample_ms = now;
  OpticalSensors_SetUpdateFlag(update_flags, OPTICAL_SENSOR_UPDATE_BH1750);

  return HAL_OK;
}

static HAL_StatusTypeDef OpticalSensors_SampleLaser(optical_slice_frame_t *frame)
{
  GPIO_PinState laser_rx_state;

#if OPTICAL_LASER_DEBUG_TOGGLE
  uint32_t now = HAL_GetTick();

  if ((now - last_laser_toggle_ms) >= OPTICAL_LASER_DEBUG_TOGGLE_MS)
  {
    last_laser_toggle_ms = now;
    laser_tx_state ^= 1U;
    HAL_GPIO_WritePin(LASER_TX_GPIO_Port,
                      LASER_TX_Pin,
                      (laser_tx_state != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }
#endif

  laser_rx_state = HAL_GPIO_ReadPin(LASER_RX_GPIO_Port, LASER_RX_Pin);
  frame->laser_signal_detected = (uint8_t)((laser_rx_state ==
                                            ((OPTICAL_LASER_ACTIVE_STATE != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET)) ? 1U : 0U);
  frame->laser_online = frame->laser_signal_detected;

  return HAL_OK;
}

HAL_StatusTypeDef OpticalSensors_Init(void)
{
  memset(sensor_table, 0, sizeof(sensor_table));

  sensor_table[OPTICAL_SENSOR_BH1750].id = OPTICAL_SENSOR_BH1750;
  sensor_table[OPTICAL_SENSOR_BH1750].name = "BH1750";
  sensor_table[OPTICAL_SENSOR_BH1750].address_7bit = OPTICAL_ADDR_BH1750_DEFAULT;
  sensor_table[OPTICAL_SENSOR_BH1750].present = 0U;
  sensor_table[OPTICAL_SENSOR_BH1750].initialized = 0U;

  sensor_table[OPTICAL_SENSOR_VL53L1X].id = OPTICAL_SENSOR_VL53L1X;
  sensor_table[OPTICAL_SENSOR_VL53L1X].name = "VL53L1X";
  sensor_table[OPTICAL_SENSOR_VL53L1X].address_7bit = OPTICAL_ADDR_VL53L1X_DEFAULT;
  sensor_table[OPTICAL_SENSOR_VL53L1X].present = 0U;
  sensor_table[OPTICAL_SENSOR_VL53L1X].initialized = 0U;

  sensor_table[OPTICAL_SENSOR_SC18IS604].id = OPTICAL_SENSOR_SC18IS604;
  sensor_table[OPTICAL_SENSOR_SC18IS604].name = "SC18IS604";
  sensor_table[OPTICAL_SENSOR_SC18IS604].address_7bit = 0U;

  sensor_table[OPTICAL_SENSOR_AI_CAMERA].id = OPTICAL_SENSOR_AI_CAMERA;
  sensor_table[OPTICAL_SENSOR_AI_CAMERA].name = "WonderCam";
  sensor_table[OPTICAL_SENSOR_AI_CAMERA].address_7bit = OPTICAL_ADDR_AI_CAMERA;
  sensor_table[OPTICAL_SENSOR_AI_CAMERA].present = 0U;
  sensor_table[OPTICAL_SENSOR_AI_CAMERA].initialized = 0U;

  sensor_table[OPTICAL_SENSOR_LASER_FRONT_END].id = OPTICAL_SENSOR_LASER_FRONT_END;
  sensor_table[OPTICAL_SENSOR_LASER_FRONT_END].name = "Laser Receiver via PA12";
  sensor_table[OPTICAL_SENSOR_LASER_FRONT_END].address_7bit = 0U;

  last_bh1750_sample_ms = 0U;
  last_wondercam_sample_ms = 0U;
#if OPTICAL_LASER_DEBUG_TOGGLE
  last_laser_toggle_ms = HAL_GetTick();
#endif
  laser_tx_state = OPTICAL_LASER_TX_DEFAULT_STATE;
  cached_ambient_lux_x10 = 0U;
  cached_dark_detected = 0U;
  cached_vl53_distance_mm = OPTICAL_READING_INVALID_U16;
  cached_vl53_range_status = 0xFFU;
  cached_wondercam_online = 0U;
  cached_wondercam_class_id = 0U;
  cached_wondercam_conf_x10000 = 0U;
  wondercam_candidate_class_id = 0U;
  wondercam_candidate_conf_x10000 = 0U;
  wondercam_candidate_streak = 0U;
  wondercam_diag_code = WONDERCAM_DIAG_NONE;

#if OPTICAL_ENABLE_LASER_FRONT_END
  HAL_GPIO_WritePin(LASER_TX_GPIO_Port,
                    LASER_TX_Pin,
                    (laser_tx_state != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);

  sensor_table[OPTICAL_SENSOR_LASER_FRONT_END].present = 1U;
  sensor_table[OPTICAL_SENSOR_LASER_FRONT_END].initialized = 1U;
#else
  HAL_GPIO_WritePin(LASER_TX_GPIO_Port, LASER_TX_Pin, GPIO_PIN_RESET);
  laser_tx_state = 0U;
#endif

#if OPTICAL_ENABLE_BH1750 || OPTICAL_ENABLE_VL53L1X || OPTICAL_ENABLE_WONDERCAM
  if (SC18IS604_Init() == HAL_OK)
  {
    sensor_table[OPTICAL_SENSOR_SC18IS604].present = 1U;
    sensor_table[OPTICAL_SENSOR_SC18IS604].initialized = 1U;
#if OPTICAL_ENABLE_BH1750
    (void)OpticalSensors_StartBh1750(&sensor_table[OPTICAL_SENSOR_BH1750]);
#endif
#if OPTICAL_ENABLE_VL53L1X
    if (VL53L1XBridge_Init() == HAL_OK)
    {
      sensor_table[OPTICAL_SENSOR_VL53L1X].present = 1U;
      sensor_table[OPTICAL_SENSOR_VL53L1X].initialized = 1U;
    }
#endif
#if OPTICAL_ENABLE_WONDERCAM
    (void)OpticalSensors_StartWonderCam(&sensor_table[OPTICAL_SENSOR_AI_CAMERA]);
#endif
  }
  else
  {
    sensor_table[OPTICAL_SENSOR_SC18IS604].present = 0U;
    sensor_table[OPTICAL_SENSOR_SC18IS604].initialized = 0U;
  }
#endif

  return HAL_OK;
}

HAL_StatusTypeDef OpticalSensors_Poll(optical_slice_frame_t *frame, uint32_t *update_flags)
{
  if (update_flags != NULL)
  {
    *update_flags = OPTICAL_SENSOR_UPDATE_NONE;
  }

  OpticalSensors_ResetFrameFlags(frame);

  frame->timestamp_ms = HAL_GetTick();
  frame->object_distance_mm = cached_vl53_distance_mm;
  frame->tof_range_status = cached_vl53_range_status;
  frame->snow_height_mm = OPTICAL_READING_INVALID_U16;
  frame->precipitation_level_x10 = OPTICAL_READING_INVALID_U16;
  frame->ambient_lux_x10 = cached_ambient_lux_x10;
  frame->precipitation_type = OPTICAL_PRECIP_UNKNOWN;
  frame->motion_detected = 0U;
  frame->package_detected = 0U;
  frame->dark_detected = cached_dark_detected;
  frame->camera_online = cached_wondercam_online;
  frame->laser_online = 0U;
  frame->laser_signal_detected = 0U;

  if (SC18IS604_IsReady() != 0U)
  {
    frame->status_flags |= OPTICAL_STATUS_SC18_PRESENT;
    sensor_table[OPTICAL_SENSOR_SC18IS604].present = 1U;
  }

#if OPTICAL_ENABLE_VL53L1X
  if (VL53L1XBridge_GetInfo()->present != 0U)
  {
    frame->status_flags |= OPTICAL_STATUS_VL53L1X_PRESENT;
    sensor_table[OPTICAL_SENSOR_VL53L1X].present = 1U;
  }
#endif

#if OPTICAL_ENABLE_BH1750
  if (OpticalSensors_SampleBh1750(frame, update_flags) == HAL_OK)
  {
    frame->status_flags |= OPTICAL_STATUS_BH1750_PRESENT | OPTICAL_STATUS_BH1750_VALID;
  }
#endif

#if OPTICAL_ENABLE_WONDERCAM
  if (OpticalSensors_SampleWonderCam(frame, update_flags) == HAL_OK)
  {
    frame->status_flags |= OPTICAL_STATUS_CAMERA_PRESENT;
  }
#endif

  if (frame->dark_detected != 0U)
  {
    frame->status_flags |= OPTICAL_STATUS_DARK;
  }

#if OPTICAL_ENABLE_LASER_FRONT_END
  if (OpticalSensors_SampleLaser(frame) == HAL_OK)
  {
    sensor_table[OPTICAL_SENSOR_LASER_FRONT_END].present = 1U;
    frame->status_flags |= OPTICAL_STATUS_LASER_PRESENT;
    if (frame->laser_signal_detected != 0U)
    {
      frame->status_flags |= OPTICAL_STATUS_LASER_SIGNAL_DETECTED;
    }
  }
  else
  {
    sensor_table[OPTICAL_SENSOR_LASER_FRONT_END].present = 0U;
  }
#endif

#if OPTICAL_ENABLE_VL53L1X
  if (VL53L1XBridge_Poll(&cached_vl53_distance_mm, &cached_vl53_range_status) == HAL_OK)
  {
    frame->object_distance_mm = cached_vl53_distance_mm;
    frame->tof_range_status = cached_vl53_range_status;
    OpticalSensors_SetUpdateFlag(update_flags, OPTICAL_SENSOR_UPDATE_VL53L1X);
  }
#endif

  if (frame->package_detected != 0U)
  {
    frame->status_flags |= OPTICAL_STATUS_PACKAGE_DETECTED;
  }

  return HAL_OK;
}

const optical_sensor_status_t *OpticalSensors_GetTable(uint8_t *count)
{
  if (count != NULL)
  {
    *count = (uint8_t)OPTICAL_SENSOR_COUNT;
  }

  return sensor_table;
}
