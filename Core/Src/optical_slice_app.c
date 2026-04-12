#include "optical_slice_app.h"

#include <stdio.h>
#include <string.h>

#include "optical_slice_config.h"
#include "sc18is604.h"
#include "optical_slice_sensors.h"
#include "usart.h"

static optical_slice_frame_t latest_frame;
static optical_slice_frame_t last_reported_frame;
static uint32_t last_report_ms;
static uint8_t last_report_valid;
static volatile uint8_t sc18_irq_pending;

#define OPTICAL_VL53_PROBE_ADDR_7BIT 0x29U
#define OPTICAL_VL53_IDENT_REG       0x010FU
#define OPTICAL_HEIGHT_ALERT_MM      100U
#define OPTICAL_HEIGHT_TRACK_MM      300U
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
  char message[256];
  int length;

  bh1750 = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_BH1750);
  sc18 = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_SC18IS604);
  vl53 = OpticalSlice_GetSensorStatus(OPTICAL_SENSOR_VL53L1X);

  length = snprintf(message,
                    sizeof(message),
                    "BOOT | SC18 ready=%u present=%u init=%u i2c=0x%02X(%s) | BH en=%u init=%u | VL53 en=%u present=%u init=%u | LAS en=%u tx=%u raw=%u beam=%u | CAM en=%u\r\n",
                    SC18IS604_IsReady(),
                    (sc18 != NULL) ? sc18->present : 0U,
                    (sc18 != NULL) ? sc18->initialized : 0U,
                    SC18IS604_GetLastI2cStatus(),
                    SC18IS604_I2cStatusText(SC18IS604_GetLastI2cStatus()),
                    OPTICAL_ENABLE_BH1750,
                    (bh1750 != NULL) ? bh1750->initialized : 0U,
                    OPTICAL_ENABLE_VL53L1X,
                    (vl53 != NULL) ? vl53->present : 0U,
                    (vl53 != NULL) ? vl53->initialized : 0U,
                    OPTICAL_ENABLE_LASER_FRONT_END,
                    (HAL_GPIO_ReadPin(LASER_TX_GPIO_Port, LASER_TX_Pin) == GPIO_PIN_SET) ? 1U : 0U,
                    OpticalSlice_ReadLaserRaw(),
                    OpticalSlice_ReadLaserBeam(),
                    OPTICAL_ENABLE_WONDERCAM);

  if ((length > 0) && ((size_t)length < sizeof(message)))
  {
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)message, (uint16_t)length, 50U);
  }
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
                      "VL53PROBE | split ACK at 0x%02X, ident[0x%04X]=0x%02X 0x%02X\r\n",
                      OPTICAL_VL53_PROBE_ADDR_7BIT,
                      OPTICAL_VL53_IDENT_REG,
                      ident_split[0],
                      ident_split[1]);
  }
  else
  {
    length = snprintf(message,
                      sizeof(message),
                      "VL53PROBE | split write=%u read=%u i2c=0x%02X(%s) raw=0x%02X(%s)\r\n",
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
  OPTICAL_HEIGHT_LEVEL_TRACK,
  OPTICAL_HEIGHT_LEVEL_ALERT
} optical_height_level_t;

static optical_height_level_t OpticalSlice_GetHeightLevel(const optical_slice_frame_t *frame)
{
  if ((frame == NULL) ||
      (frame->object_distance_mm == OPTICAL_READING_INVALID_U16) ||
      (frame->tof_range_status == 0xFFU))
  {
    return OPTICAL_HEIGHT_LEVEL_UNKNOWN;
  }

  if (frame->object_distance_mm < OPTICAL_HEIGHT_ALERT_MM)
  {
    return OPTICAL_HEIGHT_LEVEL_ALERT;
  }

  if (frame->object_distance_mm < OPTICAL_HEIGHT_TRACK_MM)
  {
    return OPTICAL_HEIGHT_LEVEL_TRACK;
  }

  return OPTICAL_HEIGHT_LEVEL_CLEAR;
}

static const char *OpticalSlice_HeightLevelText(optical_height_level_t level)
{
  switch (level)
  {
    case OPTICAL_HEIGHT_LEVEL_ALERT:
      return "Alert";

    case OPTICAL_HEIGHT_LEVEL_TRACK:
      return "Tracking";

    case OPTICAL_HEIGHT_LEVEL_CLEAR:
      return "Clear";

    default:
      return "Unknown";
  }
}

static const char *OpticalSlice_YesNoText(uint8_t state)
{
  return (state != 0U) ? "Yes" : "No";
}

static const char *OpticalSlice_CameraText(uint8_t state)
{
  return (state != 0U) ? "Online" : "Offline";
}

static const char *OpticalSlice_PackageText(const optical_slice_frame_t *frame)
{
  if ((frame == NULL) || (frame->camera_online == 0U))
  {
    return "Unknown";
  }

  return (frame->package_detected != 0U) ? "Detected" : "Not detected";
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
  else
  {
    (void)snprintf(buffer,
                   length,
                   "%u mm (%s)",
                   latest_frame.object_distance_mm,
                   OpticalSlice_HeightLevelText(level));
  }
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
      (void)snprintf(buffer,
                     length,
                     "Snow (score %u/%u)",
                     (unsigned int)latest_frame.precipitation_level_x10,
                     10U);
      break;

    case OPTICAL_PRECIP_ICE:
      (void)snprintf(buffer,
                     length,
                     "Ice (score %u/%u)",
                     (unsigned int)latest_frame.precipitation_level_x10,
                     10U);
      break;

    default:
      (void)snprintf(buffer, length, "Unknown");
      break;
  }
}

static void OpticalSlice_ReportFrame(void)
{
  char summary[320];
  char vl53_text[24];
  char precip_text[48];
  int summary_length;
  HAL_StatusTypeDef status;

  status = HAL_UART_Transmit(&huart2, (uint8_t *)"", 0U, 50U);
  if (status != HAL_OK)
  {
    latest_frame.status_flags &= ~OPTICAL_STATUS_MASTER_LINK_OK;
  }
  else
  {
    latest_frame.status_flags |= OPTICAL_STATUS_MASTER_LINK_OK;
  }

  OpticalSlice_FormatVl53Text(vl53_text, sizeof(vl53_text));
  OpticalSlice_FormatPrecipitationText(precip_text, sizeof(precip_text));
  summary_length = snprintf(summary,
                            sizeof(summary),
                            "LIVE %lums | Ambient %u.%u lx | Dark %s | VL53 height/range %s\r\n"
                            "Laser signal %s | Presence %s | Motion %s | Package %s | Precipitation %s | Camera %s\r\n",
                            (unsigned long)latest_frame.timestamp_ms,
                            (unsigned int)(latest_frame.ambient_lux_x10 / 10U),
                            (unsigned int)(latest_frame.ambient_lux_x10 % 10U),
                            OpticalSlice_YesNoText(latest_frame.dark_detected),
                            vl53_text,
                            OpticalSlice_YesNoText(latest_frame.laser_signal_detected),
                            OpticalSlice_YesNoText(latest_frame.presence_detected),
                            OpticalSlice_YesNoText(latest_frame.motion_detected),
                            OpticalSlice_PackageText(&latest_frame),
                            precip_text,
                            OpticalSlice_CameraText(latest_frame.camera_online));

  if ((summary_length > 0) && ((size_t)summary_length < sizeof(summary)))
  {
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)summary, (uint16_t)summary_length, 50U);
  }

  last_reported_frame = latest_frame;
  last_report_valid = 1U;
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
  last_report_valid = 0U;
  sc18_irq_pending = 0U;

  (void)OpticalSensors_Init();
  (void)HAL_UART_Transmit(&huart2,
                          (uint8_t *)"\r\nOPTICAL boot\r\n",
                          (uint16_t)strlen("\r\nOPTICAL boot\r\n"),
                          50U);
  OpticalSlice_ReportBootDiagnostics();
  if ((SC18IS604_IsReady() != 0U) &&
      (SC18IS604_ReadVersion(version, (uint8_t)sizeof(version)) == HAL_OK))
  {
    (void)HAL_UART_Transmit(&huart2,
                            (uint8_t *)version,
                            (uint16_t)strlen(version),
                            50U);
    (void)HAL_UART_Transmit(&huart2,
                            (uint8_t *)"\r\n",
                            2U,
                            50U);
  }
  else
  {
    (void)HAL_UART_Transmit(&huart2,
                            (uint8_t *)"SC18 | version read failed\r\n",
                            (uint16_t)strlen("SC18 | version read failed\r\n"),
                            50U);
  }
  OpticalSlice_ReportVl53BridgeProbe();

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

  if (last_report_valid == 0U)
  {
    should_report = 1U;
  }

  current_height_level = OpticalSlice_GetHeightLevel(&latest_frame);
  previous_height_level = OpticalSlice_GetHeightLevel(&last_reported_frame);

  if ((latest_frame.dark_detected != last_reported_frame.dark_detected) ||
      (latest_frame.laser_signal_detected != last_reported_frame.laser_signal_detected) ||
      (latest_frame.presence_detected != last_reported_frame.presence_detected) ||
      (latest_frame.camera_online != last_reported_frame.camera_online) ||
      (latest_frame.package_detected != last_reported_frame.package_detected) ||
      (latest_frame.precipitation_type != last_reported_frame.precipitation_type))
  {
    should_report = 1U;
  }

  if ((current_height_level == OPTICAL_HEIGHT_LEVEL_ALERT) &&
      ((latest_frame.object_distance_mm != last_reported_frame.object_distance_mm) ||
       (previous_height_level != OPTICAL_HEIGHT_LEVEL_ALERT)))
  {
    should_report = 1U;
  }

  if ((previous_height_level == OPTICAL_HEIGHT_LEVEL_ALERT) &&
      (current_height_level != OPTICAL_HEIGHT_LEVEL_ALERT))
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
