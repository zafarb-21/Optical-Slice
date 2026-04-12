#include "optical_slice_app.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "i2c.h"
#include "optical_slice_config.h"
#include "sc18is604.h"
#include "optical_slice_sensors.h"
#include "usart.h"

static optical_slice_frame_t latest_frame;
static optical_slice_frame_t last_reported_frame;
static uint32_t last_report_ms;
static uint32_t last_health_report_ms;
static uint8_t last_report_valid;
static volatile uint8_t sc18_irq_pending;

#define OPTICAL_VL53_PROBE_ADDR_7BIT 0x29U
#define OPTICAL_VL53_IDENT_REG       0x010FU
#define OPTICAL_REPORT_DEBOUNCE_MS   100U

static uint8_t OpticalSlice_ReadLaserRaw(void)
{
  return (uint8_t)((HAL_GPIO_ReadPin(LASER_RX_GPIO_Port, LASER_RX_Pin) == GPIO_PIN_SET) ? 1U : 0U);
}

static uint8_t OpticalSlice_ReadLaserBeam(void)
{
  return (uint8_t)((OpticalSlice_ReadLaserRaw() ==
                    ((OPTICAL_LASER_ACTIVE_STATE != 0U) ? 1U : 0U)) ? 1U : 0U);
}

static void OpticalSlice_UartPrint(const char *format, ...)
{
  va_list args;
  char message[256];
  int length;

  if (format == NULL)
  {
    return;
  }

  va_start(args, format);
  length = vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  if ((length > 0) && ((size_t)length < sizeof(message)))
  {
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)message, (uint16_t)length, 50U);
  }
}

static const optical_sensor_status_t *OpticalSlice_GetSensorStatus(optical_sensor_id_t id)
{
  const optical_sensor_status_t *table;
  uint8_t count = 0U;
  uint8_t index;

  table = OpticalSensors_GetTable(&count);
  for (index = 0U; index < count; ++index)
  {
    if (table[index].id == id)
    {
      return &table[index];
    }
  }

  return NULL;
}

static void OpticalSlice_ReportBootDiagnostics(void)
{
  const optical_sensor_status_t *bh1750;
  const optical_sensor_status_t *sc18;
  const optical_sensor_status_t *vl53;

  bh1750 = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_BH1750);
  sc18 = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_SC18IS604);
  vl53 = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_VL53L1X);

  OpticalSlice_UartPrint("BOOT | SC18 rdy=%u prs=%u init=%u i2c=0x%02X(%s)\r\n",
                         SC18IS604_IsReady(),
                         (sc18 != NULL) ? sc18->present : 0U,
                         (sc18 != NULL) ? sc18->initialized : 0U,
                         SC18IS604_GetLastI2cStatus(),
                         SC18IS604_I2cStatusText(SC18IS604_GetLastI2cStatus()));
  OpticalSlice_UartPrint("BOOT | BH en=%u init=%u | VL53 en=%u prs=%u init=%u | CAM en=%u\r\n",
                         OPTICAL_ENABLE_BH1750,
                         (bh1750 != NULL) ? bh1750->initialized : 0U,
                         OPTICAL_ENABLE_VL53L1X,
                         (vl53 != NULL) ? vl53->present : 0U,
                         (vl53 != NULL) ? vl53->initialized : 0U,
                         OPTICAL_ENABLE_WONDERCAM);
  OpticalSlice_UartPrint("BOOT | LAS en=%u tx=%u raw=%u beam=%u\r\n",
                         OPTICAL_ENABLE_LASER_FRONT_END,
                         (HAL_GPIO_ReadPin(LASER_TX_GPIO_Port, LASER_TX_Pin) == GPIO_PIN_SET) ? 1U : 0U,
                         OpticalSlice_ReadLaserRaw(),
                         OpticalSlice_ReadLaserBeam());
}

static void OpticalSlice_ReportVl53BridgeProbe(void)
{
  uint8_t reg_addr[2];
  uint8_t ident_split[2] = {0U, 0U};
  char message[160];
  int length;
  HAL_StatusTypeDef split_write_status;
  HAL_StatusTypeDef split_read_status;

  if (SC18IS604_IsReady() == 0U)
  {
    return;
  }

  reg_addr[0] = (uint8_t)(OPTICAL_VL53_IDENT_REG >> 8);
  reg_addr[1] = (uint8_t)(OPTICAL_VL53_IDENT_REG & 0xFFU);

  split_write_status = SC18IS604_I2cWrite(OPTICAL_VL53_PROBE_ADDR_7BIT,
                                          reg_addr,
                                          (uint8_t)sizeof(reg_addr));
  if (split_write_status == HAL_OK)
  {
    split_read_status = SC18IS604_I2cRead(OPTICAL_VL53_PROBE_ADDR_7BIT,
                                          ident_split,
                                          (uint8_t)sizeof(ident_split));
  }
  else
  {
    split_read_status = HAL_ERROR;
  }

  if ((split_write_status == HAL_OK) && (split_read_status == HAL_OK))
  {
    length = snprintf(message,
                      sizeof(message),
                      "BOOT | VL53 split ok addr=0x%02X ident[0x%04X]=0x%02X 0x%02X\r\n",
                      OPTICAL_VL53_PROBE_ADDR_7BIT,
                      OPTICAL_VL53_IDENT_REG,
                      ident_split[0],
                      ident_split[1]);
  }
  else
  {
    length = snprintf(message,
                      sizeof(message),
                      "FAULT | VL53 probe wr=%u rd=%u i2c=0x%02X(%s) raw=0x%02X(%s)\r\n",
                      (split_write_status == HAL_OK) ? 1U : 0U,
                      (split_read_status == HAL_OK) ? 1U : 0U,
                      SC18IS604_GetLastI2cStatus(),
                      SC18IS604_I2cStatusText(SC18IS604_GetLastI2cStatus()),
                      SC18IS604_GetLastI2cRawStatus(),
                      SC18IS604_I2cStatusText(SC18IS604_GetLastI2cRawStatus()));
  }

  if ((length > 0) && ((size_t)length < sizeof(message)))
  {
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)message, (uint16_t)length, 50U);
  }
}

typedef enum
{
  OPTICAL_HEIGHT_LEVEL_UNKNOWN = 0,
  OPTICAL_HEIGHT_LEVEL_CLEAR,
  OPTICAL_HEIGHT_LEVEL_SNOW,
  OPTICAL_HEIGHT_LEVEL_OBSTRUCTION
} optical_height_level_t;

static optical_height_level_t OpticalSlice_GetHeightLevel(const optical_slice_frame_t *frame)
{
  if ((frame == NULL) ||
      ((frame->status_flags & OPTICAL_STATUS_VL53_RANGE_VALID) == 0U))
  {
    return OPTICAL_HEIGHT_LEVEL_UNKNOWN;
  }

  if ((frame->status_flags & OPTICAL_STATUS_OBSTRUCTION_DETECTED) != 0U)
  {
    return OPTICAL_HEIGHT_LEVEL_OBSTRUCTION;
  }

  if (frame->object_distance_mm < OPTICAL_SNOW_BASELINE_MM)
  {
    return OPTICAL_HEIGHT_LEVEL_SNOW;
  }

  return OPTICAL_HEIGHT_LEVEL_CLEAR;
}

static const char *OpticalSlice_HeightLevelText(optical_height_level_t level)
{
  switch (level)
  {
    case OPTICAL_HEIGHT_LEVEL_OBSTRUCTION:
      return "Obstruction";

    case OPTICAL_HEIGHT_LEVEL_SNOW:
      return "Snow";

    case OPTICAL_HEIGHT_LEVEL_CLEAR:
      return "Clear";

    default:
      return "Unknown";
  }
}

static const char *OpticalSlice_YNText(uint8_t state)
{
  return (state != 0U) ? "Y" : "N";
}

static const char *OpticalSlice_CameraShortText(uint8_t state)
{
  return (state != 0U) ? "On" : "Off";
}

static void OpticalSlice_FormatAmbientText(char *buffer, size_t length)
{
  if ((buffer == NULL) || (length == 0U))
  {
    return;
  }

  if ((latest_frame.status_flags & OPTICAL_STATUS_BH1750_VALID) == 0U)
  {
    (void)snprintf(buffer, length, "unavailable");
    return;
  }

  (void)snprintf(buffer,
                 length,
                 "%u.%u lx",
                 (unsigned int)(latest_frame.ambient_lux_x10 / 10U),
                 (unsigned int)(latest_frame.ambient_lux_x10 % 10U));
}

static const char *OpticalSlice_PackageShortText(const optical_slice_frame_t *frame)
{
  if ((frame == NULL) || (frame->camera_online == 0U))
  {
    return "Unk";
  }

  return (frame->package_detected != 0U) ? "Yes" : "No";
}

static void OpticalSlice_FormatVl53Text(char *buffer, size_t length)
{
  optical_height_level_t level;

  if ((buffer == NULL) || (length == 0U))
  {
    return;
  }

  level = OpticalSlice_GetHeightLevel(&latest_frame);
  if (level == OPTICAL_HEIGHT_LEVEL_UNKNOWN)
  {
    (void)snprintf(buffer, length, "unavailable");
  }
  else if (level == OPTICAL_HEIGHT_LEVEL_OBSTRUCTION)
  {
    (void)snprintf(buffer, length, "%u mm (%s)",
                   latest_frame.object_distance_mm,
                   OpticalSlice_HeightLevelText(level));
  }
  else
  {
    (void)snprintf(buffer,
                   length,
                   "%u mm (%s)",
                   latest_frame.object_distance_mm,
                   OpticalSlice_HeightLevelText(level));
  }
}

static void OpticalSlice_FormatSnowHeightText(char *buffer, size_t length)
{
  if ((buffer == NULL) || (length == 0U))
  {
    return;
  }

  if ((latest_frame.status_flags & OPTICAL_STATUS_VL53_RANGE_VALID) == 0U)
  {
    (void)snprintf(buffer, length, "unavailable");
    return;
  }

  if ((latest_frame.status_flags & OPTICAL_STATUS_OBSTRUCTION_DETECTED) != 0U)
  {
    (void)snprintf(buffer, length, "obstruction");
    return;
  }

  if ((latest_frame.status_flags & OPTICAL_STATUS_SNOW_HEIGHT_VALID) == 0U)
  {
    if (OpticalSensors_HasSnowBaseline() == 0U)
    {
      (void)snprintf(buffer, length, "uncalibrated");
    }
    else
    {
      (void)snprintf(buffer, length, "unavailable");
    }
    return;
  }

  (void)snprintf(buffer, length, "%u mm", latest_frame.snow_height_mm);
}

static void OpticalSlice_FormatPrecipitationText(char *buffer, size_t length)
{
  if ((buffer == NULL) || (length == 0U))
  {
    return;
  }

  if (latest_frame.camera_online == 0U)
  {
    (void)snprintf(buffer, length, "Unavailable");
    return;
  }

  switch (latest_frame.precipitation_type)
  {
    case OPTICAL_PRECIP_NONE:
      (void)snprintf(buffer, length, "None");
      break;

    case OPTICAL_PRECIP_SNOW:
      (void)snprintf(buffer, length, "Snow %u/%u", (unsigned int)latest_frame.precipitation_level_x10, 10U);
      break;

    case OPTICAL_PRECIP_ICE:
      (void)snprintf(buffer, length, "Ice %u/%u", (unsigned int)latest_frame.precipitation_level_x10, 10U);
      break;

    default:
      (void)snprintf(buffer, length, "Unknown");
      break;
  }
}

static void OpticalSlice_ReportFrame(void)
{
  char summary[320];
  char ambient_text[24];
  char vl53_text[24];
  char snow_text[24];
  char precip_text[48];
  int summary_length;

  OpticalSlice_FormatAmbientText(ambient_text, sizeof(ambient_text));
  OpticalSlice_FormatVl53Text(vl53_text, sizeof(vl53_text));
  OpticalSlice_FormatSnowHeightText(snow_text, sizeof(snow_text));
  OpticalSlice_FormatPrecipitationText(precip_text, sizeof(precip_text));
  summary_length = snprintf(summary,
                            sizeof(summary),
                            "LIVE %lums | Lux %s | Dark %s\r\n"
                            "ToF %s | Snow %s\r\n"
                            "Laser %s | Pres %s | Mot %s | Pkg %s | Prec %s | Cam %s\r\n",
                            (unsigned long)latest_frame.timestamp_ms,
                            ambient_text,
                            OpticalSlice_YNText(latest_frame.dark_detected),
                            vl53_text,
                            snow_text,
                            OpticalSlice_YNText(latest_frame.laser_signal_detected),
                            OpticalSlice_YNText(latest_frame.presence_detected),
                            OpticalSlice_YNText(latest_frame.motion_detected),
                            OpticalSlice_PackageShortText(&latest_frame),
                            precip_text,
                            OpticalSlice_CameraShortText(latest_frame.camera_online));

  if ((summary_length > 0) && ((size_t)summary_length < sizeof(summary)))
  {
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)summary, (uint16_t)summary_length, 50U);
  }

  last_reported_frame = latest_frame;
  last_report_valid = 1U;
}

static void OpticalSlice_ReportHealth(void)
{
  const optical_sensor_status_t *bh1750 = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_BH1750);
  const optical_sensor_status_t *sc18 = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_SC18IS604);
  const optical_sensor_status_t *vl53 = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_VL53L1X);
  const optical_sensor_status_t *wcam = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_AI_CAMERA);
  const optical_sensor_status_t *laser = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_LASER_FRONT_END);
  optical_runtime_config_t runtime_config;
  const char *master_text = "fault";
  uint16_t baseline_mm;

  if (OpticalMasterLink_IsHealthy() != 0U)
  {
    master_text = (OpticalMasterLink_HasRecentActivity() != 0U) ? "ok" : "idle";
  }

  OpticalSensors_GetRuntimeConfig(&runtime_config);
  baseline_mm = OpticalSensors_GetSnowBaselineMm();

  OpticalSlice_UartPrint("HEALTH | SC18 %u/%u | BH %u/%u | VL53 %u/%u | CAM %u/%u | LAS %u/%u | BL %s | LP %s | M %s\r\n",
                         (sc18 != NULL) ? sc18->present : 0U,
                         (sc18 != NULL) ? sc18->initialized : 0U,
                         (bh1750 != NULL) ? bh1750->present : 0U,
                         (bh1750 != NULL) ? bh1750->initialized : 0U,
                         (vl53 != NULL) ? vl53->present : 0U,
                         (vl53 != NULL) ? vl53->initialized : 0U,
                         (wcam != NULL) ? wcam->present : 0U,
                         (wcam != NULL) ? wcam->initialized : 0U,
                         (laser != NULL) ? laser->present : 0U,
                         (laser != NULL) ? laser->initialized : 0U,
                         (baseline_mm != OPTICAL_READING_INVALID_U16) ? "set" : "none",
                         OpticalSensors_GetLaserProfileName(runtime_config.laser_profile),
                         master_text);
}

void OpticalSlice_Init(void)
{
  char version[24];

  memset(&latest_frame, 0, sizeof(latest_frame));
  latest_frame.object_distance_mm = OPTICAL_READING_INVALID_U16;
  latest_frame.snow_height_mm = OPTICAL_READING_INVALID_U16;
  latest_frame.precipitation_level_x10 = OPTICAL_READING_INVALID_U16;
  latest_frame.precipitation_type = OPTICAL_PRECIP_UNKNOWN;
  latest_frame.tof_range_status = 0xFFU;
  memset(&last_reported_frame, 0, sizeof(last_reported_frame));
  last_report_ms = 0U;
  last_health_report_ms = 0U;
  last_report_valid = 0U;
  sc18_irq_pending = 0U;

  (void)OpticalSensors_Init();
  if (OpticalMasterLink_Init() == HAL_OK)
  {
    OpticalSlice_UartPrint("BOOT | I2C1 addr=0x%02X listen=ok\r\n", OPTICAL_MASTER_LINK_ADDR_7BIT);
  }
  else
  {
    OpticalSlice_UartPrint("FAULT | I2C1 addr=0x%02X listen start failed\r\n", OPTICAL_MASTER_LINK_ADDR_7BIT);
  }
  if (OpticalMasterLink_IsHealthy() != 0U)
  {
    latest_frame.status_flags |= OPTICAL_STATUS_MASTER_LINK_OK;
  }
  else
  {
    latest_frame.status_flags &= ~OPTICAL_STATUS_MASTER_LINK_OK;
  }
  OpticalMasterLink_UpdateFrame(&latest_frame);
  OpticalSlice_UartPrint("\r\nBOOT | OPTICAL start\r\n");
  OpticalSlice_ReportBootDiagnostics();
  if ((SC18IS604_IsReady() != 0U) &&
      (SC18IS604_ReadVersion(version, (uint8_t)sizeof(version)) == HAL_OK))
  {
    OpticalSlice_UartPrint("BOOT | SC18 ver %s\r\n", version);
  }
  else
  {
    OpticalSlice_UartPrint("FAULT | SC18 version read failed\r\n");
  }
  OpticalSlice_ReportVl53BridgeProbe();
  OpticalSlice_ReportHealth();
}

void OpticalSlice_Run(void)
{
  uint32_t sensor_update_flags = OPTICAL_SENSOR_UPDATE_NONE;
  uint32_t now = HAL_GetTick();
  uint8_t bridge_irq_seen;
  uint8_t should_report = 0U;
  optical_height_level_t current_height_level;
  optical_height_level_t previous_height_level;

  __disable_irq();
  bridge_irq_seen = sc18_irq_pending;
  sc18_irq_pending = 0U;
  __enable_irq();

  (void)OpticalSensors_Poll(&latest_frame, &sensor_update_flags);
  if (OpticalMasterLink_IsHealthy() != 0U)
  {
    latest_frame.status_flags |= OPTICAL_STATUS_MASTER_LINK_OK;
  }
  else
  {
    latest_frame.status_flags &= ~OPTICAL_STATUS_MASTER_LINK_OK;
  }
  OpticalMasterLink_UpdateFrame(&latest_frame);

  if (last_report_valid == 0U)
  {
    should_report = 1U;
  }

  current_height_level = OpticalSlice_GetHeightLevel(&latest_frame);
  previous_height_level = OpticalSlice_GetHeightLevel(&last_reported_frame);

  if ((latest_frame.dark_detected != last_reported_frame.dark_detected) ||
      (latest_frame.laser_signal_detected != last_reported_frame.laser_signal_detected) ||
      (latest_frame.presence_detected != last_reported_frame.presence_detected) ||
      ((latest_frame.status_flags & OPTICAL_STATUS_MASTER_LINK_OK) !=
       (last_reported_frame.status_flags & OPTICAL_STATUS_MASTER_LINK_OK)) ||
      (latest_frame.camera_online != last_reported_frame.camera_online) ||
      (latest_frame.package_detected != last_reported_frame.package_detected) ||
      (latest_frame.precipitation_type != last_reported_frame.precipitation_type))
  {
    should_report = 1U;
  }

  if ((current_height_level == OPTICAL_HEIGHT_LEVEL_OBSTRUCTION) &&
      ((latest_frame.object_distance_mm != last_reported_frame.object_distance_mm) ||
       (previous_height_level != OPTICAL_HEIGHT_LEVEL_OBSTRUCTION)))
  {
    should_report = 1U;
  }

  if ((previous_height_level == OPTICAL_HEIGHT_LEVEL_OBSTRUCTION) &&
      (current_height_level != OPTICAL_HEIGHT_LEVEL_OBSTRUCTION))
  {
    should_report = 1U;
  }

  if ((bridge_irq_seen != 0U) &&
      ((sensor_update_flags & OPTICAL_SENSOR_UPDATE_WONDERCAM) != 0U) &&
      ((latest_frame.package_detected != last_reported_frame.package_detected) ||
       (latest_frame.precipitation_type != last_reported_frame.precipitation_type) ||
       (latest_frame.camera_online != last_reported_frame.camera_online)))
  {
    should_report = 1U;
  }

  if ((should_report != 0U) &&
      ((last_report_valid == 0U) || ((now - last_report_ms) >= OPTICAL_REPORT_DEBOUNCE_MS)))
  {
    last_report_ms = now;
    OpticalSlice_ReportFrame();
  }

  if ((last_health_report_ms == 0U) ||
      ((now - last_health_report_ms) >= OPTICAL_HEALTH_REPORT_MS))
  {
    last_health_report_ms = now;
    OpticalSlice_ReportHealth();
  }
}

const optical_slice_frame_t *OpticalSlice_GetLatestFrame(void)
{
  return &latest_frame;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == SC18_INT_Pin)
  {
    sc18_irq_pending = 1U;
  }
}
