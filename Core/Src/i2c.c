/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.c
  * @brief   This file provides code for the configuration
  *          of the I2C instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "i2c.h"

/* USER CODE BEGIN 0 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "optical_slice_config.h"
#include "optical_slice_sensors.h"

#define OPTICAL_MASTER_LINK_CMD_STATUS          0x00U
#define OPTICAL_MASTER_LINK_CMD_CONFIG          0x01U
#define OPTICAL_MASTER_LINK_CMD_DIAGNOSTICS     0x02U
#define OPTICAL_MASTER_LINK_CMD_CAPTURE_BASELINE 0x10U
#define OPTICAL_MASTER_LINK_CMD_CLEAR_BASELINE  0x11U
#define OPTICAL_MASTER_LINK_CMD_PROFILE_DEFAULT 0x20U
#define OPTICAL_MASTER_LINK_CMD_PROFILE_FAST    0x21U
#define OPTICAL_MASTER_LINK_CMD_PROFILE_STABLE  0x22U
#define OPTICAL_MASTER_LINK_CMD_RESET_DIAGNOSTICS 0x30U
#define OPTICAL_MASTER_LINK_STATUS_PACKET_LEN   384U
#define OPTICAL_MASTER_LINK_CONFIG_PACKET_LEN   320U
#define OPTICAL_MASTER_LINK_DIAG_PACKET_LEN     640U

static optical_slice_frame_t master_link_latest_frame;
static char master_link_status_packet[OPTICAL_MASTER_LINK_STATUS_PACKET_LEN];
static char master_link_config_packet[OPTICAL_MASTER_LINK_CONFIG_PACKET_LEN];
static char master_link_diag_packet[OPTICAL_MASTER_LINK_DIAG_PACKET_LEN];
static uint16_t master_link_status_length;
static uint16_t master_link_config_length;
static uint16_t master_link_diag_length;
static uint8_t master_link_rx_command;
static uint8_t master_link_selected_command = OPTICAL_MASTER_LINK_CMD_STATUS;
static uint8_t master_link_initialized;
static uint8_t master_link_listen_enabled;
static uint32_t master_link_last_activity_ms;
static uint32_t master_link_tx_count;
static uint32_t master_link_rx_count;
static uint32_t master_link_error_count;

uint8_t OpticalMasterLink_IsHealthy(void);
uint8_t OpticalMasterLink_HasRecentActivity(void);
void respondToLoaf(void);

static const char *OpticalMasterLink_PrecipTypeText(uint8_t precip_type)
{
  switch (precip_type)
  {
    case OPTICAL_PRECIP_NONE:
      return "none";

    case OPTICAL_PRECIP_SNOW:
      return "snow";

    case OPTICAL_PRECIP_ICE:
      return "ice";

    default:
      return "unknown";
  }
}

static const char *OpticalMasterLink_LaserProfileText(uint8_t profile)
{
  return OpticalSensors_GetLaserProfileName(profile);
}

static const char *OpticalMasterLink_FormatOptionalU16(char *buffer, size_t length, uint16_t value)
{
  if ((buffer == NULL) || (length == 0U))
  {
    return "";
  }

  if (value == OPTICAL_READING_INVALID_U16)
  {
    (void)snprintf(buffer, length, "\"unavailable\"");
    return buffer;
  }

  (void)snprintf(buffer, length, "%u", value);
  return buffer;
}

static uint16_t OpticalMasterLink_StoreAsciiPacket(char *packet,
                                                   size_t packet_len,
                                                   const char *format,
                                                   ...)
{
  va_list args;
  int written;

  if ((packet == NULL) || (packet_len < 2U) || (format == NULL))
  {
    return 0U;
  }

  va_start(args, format);
  written = vsnprintf(packet, packet_len, format, args);
  va_end(args);

  if (written < 0)
  {
    packet[0] = '\n';
    packet[1] = '\0';
    return 1U;
  }

  if ((size_t)written >= packet_len)
  {
    packet[packet_len - 2U] = '\n';
    packet[packet_len - 1U] = '\0';
    return (uint16_t)(packet_len - 1U);
  }

  return (uint16_t)written;
}

static HAL_StatusTypeDef OpticalMasterLink_EnableListen(void)
{
  HAL_StatusTypeDef status;

  status = HAL_I2C_EnableListen_IT(&hi2c1);
  master_link_listen_enabled = (uint8_t)(status == HAL_OK);
  return status;
}

static void OpticalMasterLink_RebuildPackets(void)
{
  optical_runtime_config_t runtime_config;
  optical_runtime_diag_t runtime_diag;
  char snow_baseline[20];

  OpticalSensors_GetRuntimeConfig(&runtime_config);
  OpticalSensors_GetDiagnostics(&runtime_diag);

  respondToLoaf();

  master_link_config_length = OpticalMasterLink_StoreAsciiPacket(master_link_config_packet,
                                                                 sizeof(master_link_config_packet),
                                                                 ">packet:\"config\""
                                                                 ">timestamp_ms:%lu"
                                                                 ">snow_baseline_mm:%s"
                                                                 ">laser_presence_assert_ms:%u"
                                                                 ">laser_presence_release_ms:%u"
                                                                 ">laser_motion_hold_ms:%u"
                                                                 ">laser_profile:\"%s\""
                                                                 ">sample_period_ms:%u"
                                                                 ">report_period_ms:%u"
                                                                 ">health_report_ms:%u"
                                                                 ">master_link_healthy:%u"
                                                                 ">master_link_recent:%u\n",
                                                                 (unsigned long)master_link_latest_frame.timestamp_ms,
                                                                 OpticalMasterLink_FormatOptionalU16(snow_baseline,
                                                                                                     sizeof(snow_baseline),
                                                                                                     runtime_config.snow_baseline_mm),
                                                                 (unsigned int)runtime_config.laser_presence_assert_ms,
                                                                 (unsigned int)runtime_config.laser_presence_release_ms,
                                                                 (unsigned int)runtime_config.laser_motion_hold_ms,
                                                                 OpticalMasterLink_LaserProfileText(runtime_config.laser_profile),
                                                                 (unsigned int)OPTICAL_SAMPLE_PERIOD_MS,
                                                                 (unsigned int)OPTICAL_REPORT_PERIOD_MS,
                                                                 (unsigned int)OPTICAL_HEALTH_REPORT_MS,
                                                                 (unsigned int)OpticalMasterLink_IsHealthy(),
                                                                 (unsigned int)OpticalMasterLink_HasRecentActivity());

  master_link_diag_length = OpticalMasterLink_StoreAsciiPacket(master_link_diag_packet,
                                                               sizeof(master_link_diag_packet),
                                                               ">packet:\"diagnostics\""
                                                               ">timestamp_ms:%lu"
                                                               ">tx_count:%lu"
                                                               ">rx_count:%lu"
                                                               ">error_count:%lu"
                                                               ">bridge_recovery_count:%lu"
                                                               ">bh1750_stale_count:%lu"
                                                               ">vl53_stale_count:%lu"
                                                               ">wondercam_stale_count:%lu"
                                                               ">wondercam_online_count:%lu"
                                                               ">health_event_count:%lu"
                                                               ">fault_event_count:%lu"
                                                               ">last_health_ms:%lu"
                                                               ">last_fault_ms:%lu"
                                                               ">last_health_code:%u"
                                                               ">last_fault_code:%u"
                                                               ">wondercam_raw_class_id:%u"
                                                               ">wondercam_filtered_class_id:%u"
                                                               ">wondercam_candidate_streak:%u"
                                                               ">wondercam_conf_x10000:%u\n",
                                                               (unsigned long)master_link_latest_frame.timestamp_ms,
                                                               (unsigned long)master_link_tx_count,
                                                               (unsigned long)master_link_rx_count,
                                                               (unsigned long)master_link_error_count,
                                                               (unsigned long)runtime_diag.bridge_recovery_count,
                                                               (unsigned long)runtime_diag.bh1750_stale_count,
                                                               (unsigned long)runtime_diag.vl53_stale_count,
                                                               (unsigned long)runtime_diag.wondercam_stale_count,
                                                               (unsigned long)runtime_diag.wondercam_online_count,
                                                               (unsigned long)runtime_diag.health_event_count,
                                                               (unsigned long)runtime_diag.fault_event_count,
                                                               (unsigned long)runtime_diag.last_health_ms,
                                                               (unsigned long)runtime_diag.last_fault_ms,
                                                               (unsigned int)runtime_diag.last_health_code,
                                                               (unsigned int)runtime_diag.last_fault_code,
                                                               (unsigned int)runtime_diag.wondercam_raw_class_id,
                                                               (unsigned int)runtime_diag.wondercam_filtered_class_id,
                                                               (unsigned int)runtime_diag.wondercam_candidate_streak,
                                                               (unsigned int)runtime_diag.wondercam_conf_x10000);
}

static void OpticalMasterLink_HandleCommand(uint8_t command)
{
  switch (command)
  {
    case OPTICAL_MASTER_LINK_CMD_STATUS:
    case OPTICAL_MASTER_LINK_CMD_CONFIG:
    case OPTICAL_MASTER_LINK_CMD_DIAGNOSTICS:
      master_link_selected_command = command;
      break;

    case OPTICAL_MASTER_LINK_CMD_CAPTURE_BASELINE:
      if (OpticalSensors_CaptureSnowBaseline() == HAL_OK)
      {
        master_link_selected_command = OPTICAL_MASTER_LINK_CMD_CONFIG;
      }
      else
      {
        ++master_link_error_count;
      }
      break;

    case OPTICAL_MASTER_LINK_CMD_CLEAR_BASELINE:
      OpticalSensors_ClearSnowBaseline();
      master_link_selected_command = OPTICAL_MASTER_LINK_CMD_CONFIG;
      break;

    case OPTICAL_MASTER_LINK_CMD_PROFILE_DEFAULT:
      if (OpticalSensors_SetLaserProfile(OPTICAL_LASER_PROFILE_DEFAULT) == HAL_OK)
      {
        master_link_selected_command = OPTICAL_MASTER_LINK_CMD_CONFIG;
      }
      else
      {
        ++master_link_error_count;
      }
      break;

    case OPTICAL_MASTER_LINK_CMD_PROFILE_FAST:
      if (OpticalSensors_SetLaserProfile(OPTICAL_LASER_PROFILE_FAST) == HAL_OK)
      {
        master_link_selected_command = OPTICAL_MASTER_LINK_CMD_CONFIG;
      }
      else
      {
        ++master_link_error_count;
      }
      break;

    case OPTICAL_MASTER_LINK_CMD_PROFILE_STABLE:
      if (OpticalSensors_SetLaserProfile(OPTICAL_LASER_PROFILE_STABLE) == HAL_OK)
      {
        master_link_selected_command = OPTICAL_MASTER_LINK_CMD_CONFIG;
      }
      else
      {
        ++master_link_error_count;
      }
      break;

    case OPTICAL_MASTER_LINK_CMD_RESET_DIAGNOSTICS:
      master_link_tx_count = 0U;
      master_link_rx_count = 0U;
      master_link_error_count = 0U;
      OpticalSensors_ResetDiagnostics();
      master_link_selected_command = OPTICAL_MASTER_LINK_CMD_DIAGNOSTICS;
      break;

    default:
      ++master_link_error_count;
      break;
  }

  OpticalMasterLink_RebuildPackets();
}

void respondToLoaf(void)
{
  char ambient_lux[20];
  char object_distance[20];
  char snow_height[20];
  char precip_level[20];

  /*
   * The shared loaf-side skeleton uses UART, but our upstream transport is
   * still I2C1 slave mode. We keep the existing I2C callbacks and assemble
   * the same `>name:value` payload here for the next master read. The
   * skeleton's LED toggle / HAL_Delay do not fit this interrupt-driven path.
   */
  master_link_status_length = OpticalMasterLink_StoreAsciiPacket(master_link_status_packet,
                                                                 sizeof(master_link_status_packet),
                                                                 ">packet:\"status\""
                                                                 ">slice:\"optical\""
                                                                 ">timestamp_ms:%lu"
                                                                 ">status_flags:%lu"
                                                                 ">ambient_lux_x10:%s"
                                                                 ">object_distance_mm:%s"
                                                                 ">snow_height_mm:%s"
                                                                 ">precipitation_level_x10:%s"
                                                                 ">precipitation_type:\"%s\""
                                                                 ">motion_detected:%u"
                                                                 ">package_detected:%u"
                                                                 ">presence_detected:%u"
                                                                 ">dark_detected:%u"
                                                                 ">camera_online:%u"
                                                                 ">laser_online:%u"
                                                                 ">laser_signal_detected:%u"
                                                                 ">tof_range_status:%u\n",
                                                                 (unsigned long)master_link_latest_frame.timestamp_ms,
                                                                 (unsigned long)master_link_latest_frame.status_flags,
                                                                 OpticalMasterLink_FormatOptionalU16(ambient_lux,
                                                                                                     sizeof(ambient_lux),
                                                                                                     master_link_latest_frame.ambient_lux_x10),
                                                                 OpticalMasterLink_FormatOptionalU16(object_distance,
                                                                                                     sizeof(object_distance),
                                                                                                     master_link_latest_frame.object_distance_mm),
                                                                 OpticalMasterLink_FormatOptionalU16(snow_height,
                                                                                                     sizeof(snow_height),
                                                                                                     master_link_latest_frame.snow_height_mm),
                                                                 OpticalMasterLink_FormatOptionalU16(precip_level,
                                                                                                     sizeof(precip_level),
                                                                                                     master_link_latest_frame.precipitation_level_x10),
                                                                 OpticalMasterLink_PrecipTypeText(master_link_latest_frame.precipitation_type),
                                                                 (unsigned int)master_link_latest_frame.motion_detected,
                                                                 (unsigned int)master_link_latest_frame.package_detected,
                                                                 (unsigned int)master_link_latest_frame.presence_detected,
                                                                 (unsigned int)master_link_latest_frame.dark_detected,
                                                                 (unsigned int)master_link_latest_frame.camera_online,
                                                                 (unsigned int)master_link_latest_frame.laser_online,
                                                                 (unsigned int)master_link_latest_frame.laser_signal_detected,
                                                                 (unsigned int)master_link_latest_frame.tof_range_status);
}

/* USER CODE END 0 */

I2C_HandleTypeDef hi2c1;

/* I2C1 init function */
void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00201D2B;
  hi2c1.Init.OwnAddress1 = (uint32_t)(OPTICAL_MASTER_LINK_ADDR_7BIT << 1);
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

void HAL_I2C_MspInit(I2C_HandleTypeDef* i2cHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(i2cHandle->Instance==I2C1)
  {
  /* USER CODE BEGIN I2C1_MspInit 0 */

  /* USER CODE END I2C1_MspInit 0 */

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**I2C1 GPIO Configuration
    PB6     ------> I2C1_SCL
    PB7     ------> I2C1_SDA
    */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* I2C1 clock enable */
    __HAL_RCC_I2C1_CLK_ENABLE();

    /* I2C1 interrupt Init */
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 0, 1);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
  /* USER CODE BEGIN I2C1_MspInit 1 */

  /* USER CODE END I2C1_MspInit 1 */
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* i2cHandle)
{

  if(i2cHandle->Instance==I2C1)
  {
  /* USER CODE BEGIN I2C1_MspDeInit 0 */

  /* USER CODE END I2C1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_I2C1_CLK_DISABLE();

    /**I2C1 GPIO Configuration
    PB6     ------> I2C1_SCL
    PB7     ------> I2C1_SDA
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_7);

    /* I2C1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
  /* USER CODE BEGIN I2C1_MspDeInit 1 */

  /* USER CODE END I2C1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

HAL_StatusTypeDef OpticalMasterLink_Init(void)
{
  memset(&master_link_latest_frame, 0, sizeof(master_link_latest_frame));
  master_link_rx_command = OPTICAL_MASTER_LINK_CMD_STATUS;
  master_link_selected_command = OPTICAL_MASTER_LINK_CMD_STATUS;
  master_link_last_activity_ms = 0U;
  master_link_tx_count = 0U;
  master_link_rx_count = 0U;
  master_link_error_count = 0U;
  master_link_initialized = 1U;
  OpticalMasterLink_RebuildPackets();
  return OpticalMasterLink_EnableListen();
}

void OpticalMasterLink_UpdateFrame(const optical_slice_frame_t *frame)
{
  if (frame == NULL)
  {
    return;
  }

  master_link_latest_frame = *frame;
  OpticalMasterLink_RebuildPackets();
}

uint8_t OpticalMasterLink_IsHealthy(void)
{
  return (uint8_t)((master_link_initialized != 0U) && (master_link_listen_enabled != 0U));
}

uint8_t OpticalMasterLink_HasRecentActivity(void)
{
  if ((master_link_last_activity_ms == 0U) ||
      ((HAL_GetTick() - master_link_last_activity_ms) > OPTICAL_MASTER_LINK_ACTIVITY_MS))
  {
    return 0U;
  }

  return 1U;
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode)
{
  HAL_StatusTypeDef status;
  uint8_t *tx_data = (uint8_t *)master_link_status_packet;
  uint16_t tx_length = master_link_status_length;

  (void)AddrMatchCode;

  if (hi2c != &hi2c1)
  {
    return;
  }

  if (TransferDirection == I2C_DIRECTION_RECEIVE)
  {
    switch (master_link_selected_command)
    {
      case OPTICAL_MASTER_LINK_CMD_CONFIG:
        tx_data = (uint8_t *)master_link_config_packet;
        tx_length = master_link_config_length;
        break;

      case OPTICAL_MASTER_LINK_CMD_DIAGNOSTICS:
        tx_data = (uint8_t *)master_link_diag_packet;
        tx_length = master_link_diag_length;
        break;

      case OPTICAL_MASTER_LINK_CMD_STATUS:
      default:
        tx_data = (uint8_t *)master_link_status_packet;
        tx_length = master_link_status_length;
        break;
    }

    status = HAL_I2C_Slave_Seq_Transmit_IT(hi2c,
                                           tx_data,
                                           tx_length,
                                           I2C_FIRST_AND_LAST_FRAME);
  }
  else
  {
    status = HAL_I2C_Slave_Seq_Receive_IT(hi2c,
                                          &master_link_rx_command,
                                          1U,
                                          I2C_FIRST_AND_LAST_FRAME);
  }

  if (status != HAL_OK)
  {
    ++master_link_error_count;
    master_link_listen_enabled = 0U;
  }
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c != &hi2c1)
  {
    return;
  }

  ++master_link_tx_count;
  master_link_last_activity_ms = HAL_GetTick();
  OpticalMasterLink_RebuildPackets();
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c != &hi2c1)
  {
    return;
  }

  ++master_link_rx_count;
  master_link_last_activity_ms = HAL_GetTick();
  OpticalMasterLink_HandleCommand(master_link_rx_command);
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c != &hi2c1)
  {
    return;
  }

  (void)OpticalMasterLink_EnableListen();
  OpticalMasterLink_RebuildPackets();
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c != &hi2c1)
  {
    return;
  }

  ++master_link_error_count;
  master_link_listen_enabled = 0U;
  (void)OpticalMasterLink_EnableListen();
  OpticalMasterLink_RebuildPackets();
}

/* USER CODE END 1 */

