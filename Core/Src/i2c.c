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
#include <string.h>

#include "optical_slice_config.h"
#include "optical_slice_sensors.h"

#define OPTICAL_MASTER_LINK_START0              0xA5U
#define OPTICAL_MASTER_LINK_START1              0x5AU
#define OPTICAL_MASTER_LINK_PKT_STATUS          0x01U
#define OPTICAL_MASTER_LINK_PKT_CONFIG          0x02U
#define OPTICAL_MASTER_LINK_PKT_DIAGNOSTICS     0x03U
#define OPTICAL_MASTER_LINK_CMD_STATUS          0x00U
#define OPTICAL_MASTER_LINK_CMD_CONFIG          0x01U
#define OPTICAL_MASTER_LINK_CMD_DIAGNOSTICS     0x02U
#define OPTICAL_MASTER_LINK_CMD_CAPTURE_BASELINE 0x10U
#define OPTICAL_MASTER_LINK_CMD_CLEAR_BASELINE  0x11U
#define OPTICAL_MASTER_LINK_CMD_PROFILE_DEFAULT 0x20U
#define OPTICAL_MASTER_LINK_CMD_PROFILE_FAST    0x21U
#define OPTICAL_MASTER_LINK_CMD_PROFILE_STABLE  0x22U
#define OPTICAL_MASTER_LINK_CMD_RESET_DIAGNOSTICS 0x30U
#define OPTICAL_MASTER_LINK_STATUS_PACKET_LEN   33U
#define OPTICAL_MASTER_LINK_CONFIG_PACKET_LEN   34U
#define OPTICAL_MASTER_LINK_DIAG_PACKET_LEN     68U

static optical_slice_frame_t master_link_latest_frame;
static uint8_t master_link_status_packet[OPTICAL_MASTER_LINK_STATUS_PACKET_LEN];
static uint8_t master_link_config_packet[OPTICAL_MASTER_LINK_CONFIG_PACKET_LEN];
static uint8_t master_link_diag_packet[OPTICAL_MASTER_LINK_DIAG_PACKET_LEN];
static uint8_t master_link_rx_command;
static uint8_t master_link_selected_command = OPTICAL_MASTER_LINK_CMD_STATUS;
static uint8_t master_link_sequence;
static uint8_t master_link_initialized;
static uint8_t master_link_listen_enabled;
static uint32_t master_link_last_activity_ms;
static uint32_t master_link_tx_count;
static uint32_t master_link_rx_count;
static uint32_t master_link_error_count;

uint8_t OpticalMasterLink_IsHealthy(void);
uint8_t OpticalMasterLink_HasRecentActivity(void);

static void OpticalMasterLink_WriteU16(uint8_t *buffer, uint16_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)(value >> 8);
}

static void OpticalMasterLink_WriteU32(uint8_t *buffer, uint32_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
  buffer[2] = (uint8_t)((value >> 16) & 0xFFU);
  buffer[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint16_t OpticalMasterLink_Crc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t index;
  uint8_t bit;

  if (data == NULL)
  {
    return 0U;
  }

  for (index = 0U; index < length; ++index)
  {
    crc ^= (uint16_t)data[index] << 8;
    for (bit = 0U; bit < 8U; ++bit)
    {
      if ((crc & 0x8000U) != 0U)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      }
      else
      {
        crc <<= 1;
      }
    }
  }

  return crc;
}

static void OpticalMasterLink_InitPacket(uint8_t *packet,
                                         uint16_t packet_len,
                                         uint8_t packet_type,
                                         uint8_t sequence)
{
  if ((packet == NULL) || (packet_len < 7U))
  {
    return;
  }

  memset(packet, 0, packet_len);
  packet[0] = OPTICAL_MASTER_LINK_START0;
  packet[1] = OPTICAL_MASTER_LINK_START1;
  packet[2] = OPTICAL_MASTER_LINK_PROTOCOL_VER;
  packet[3] = packet_type;
  packet[4] = (uint8_t)(packet_len - 7U);
  packet[5] = sequence;
}

static void OpticalMasterLink_FinalizePacket(uint8_t *packet, uint16_t packet_len)
{
  uint16_t crc;

  if ((packet == NULL) || (packet_len < 7U))
  {
    return;
  }

  crc = OpticalMasterLink_Crc16(packet, (uint16_t)(packet_len - 2U));
  OpticalMasterLink_WriteU16(&packet[packet_len - 2U], crc);
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
  uint8_t config_flags = 0U;

  OpticalSensors_GetRuntimeConfig(&runtime_config);
  OpticalSensors_GetDiagnostics(&runtime_diag);

  OpticalMasterLink_InitPacket(master_link_status_packet,
                               (uint16_t)sizeof(master_link_status_packet),
                               OPTICAL_MASTER_LINK_PKT_STATUS,
                               master_link_sequence);
  OpticalMasterLink_WriteU32(&master_link_status_packet[6], master_link_latest_frame.timestamp_ms);
  OpticalMasterLink_WriteU32(&master_link_status_packet[10], master_link_latest_frame.status_flags);
  OpticalMasterLink_WriteU16(&master_link_status_packet[14], master_link_latest_frame.ambient_lux_x10);
  OpticalMasterLink_WriteU16(&master_link_status_packet[16], master_link_latest_frame.object_distance_mm);
  OpticalMasterLink_WriteU16(&master_link_status_packet[18], master_link_latest_frame.snow_height_mm);
  OpticalMasterLink_WriteU16(&master_link_status_packet[20], master_link_latest_frame.precipitation_level_x10);
  master_link_status_packet[22] = master_link_latest_frame.precipitation_type;
  master_link_status_packet[23] = master_link_latest_frame.motion_detected;
  master_link_status_packet[24] = master_link_latest_frame.package_detected;
  master_link_status_packet[25] = master_link_latest_frame.presence_detected;
  master_link_status_packet[26] = master_link_latest_frame.dark_detected;
  master_link_status_packet[27] = master_link_latest_frame.camera_online;
  master_link_status_packet[28] = master_link_latest_frame.laser_online;
  master_link_status_packet[29] = master_link_latest_frame.laser_signal_detected;
  master_link_status_packet[30] = master_link_latest_frame.tof_range_status;
  OpticalMasterLink_FinalizePacket(master_link_status_packet, (uint16_t)sizeof(master_link_status_packet));

  if (OpticalSensors_HasSnowBaseline() != 0U)
  {
    config_flags |= 0x01U;
  }
  if (OpticalMasterLink_IsHealthy() != 0U)
  {
    config_flags |= 0x02U;
  }
  if (OpticalMasterLink_HasRecentActivity() != 0U)
  {
    config_flags |= 0x04U;
  }

  OpticalMasterLink_InitPacket(master_link_config_packet,
                               (uint16_t)sizeof(master_link_config_packet),
                               OPTICAL_MASTER_LINK_PKT_CONFIG,
                               master_link_sequence);
  OpticalMasterLink_WriteU32(&master_link_config_packet[6], master_link_latest_frame.timestamp_ms);
  OpticalMasterLink_WriteU16(&master_link_config_packet[10], runtime_config.snow_baseline_mm);
  OpticalMasterLink_WriteU16(&master_link_config_packet[12], runtime_config.laser_presence_assert_ms);
  OpticalMasterLink_WriteU16(&master_link_config_packet[14], runtime_config.laser_presence_release_ms);
  OpticalMasterLink_WriteU16(&master_link_config_packet[16], runtime_config.laser_motion_hold_ms);
  master_link_config_packet[18] = runtime_config.laser_profile;
  master_link_config_packet[19] = config_flags;
  OpticalMasterLink_WriteU16(&master_link_config_packet[20], OPTICAL_SAMPLE_PERIOD_MS);
  OpticalMasterLink_WriteU16(&master_link_config_packet[22], OPTICAL_REPORT_PERIOD_MS);
  OpticalMasterLink_WriteU16(&master_link_config_packet[24], OPTICAL_HEALTH_REPORT_MS);
  OpticalMasterLink_WriteU16(&master_link_config_packet[26], OPTICAL_BH1750_STALE_MS);
  OpticalMasterLink_WriteU16(&master_link_config_packet[28], OPTICAL_VL53_STALE_MS);
  OpticalMasterLink_WriteU16(&master_link_config_packet[30], OPTICAL_WONDERCAM_STALE_MS);
  OpticalMasterLink_FinalizePacket(master_link_config_packet, (uint16_t)sizeof(master_link_config_packet));

  OpticalMasterLink_InitPacket(master_link_diag_packet,
                               (uint16_t)sizeof(master_link_diag_packet),
                               OPTICAL_MASTER_LINK_PKT_DIAGNOSTICS,
                               master_link_sequence);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[6], master_link_latest_frame.timestamp_ms);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[10], master_link_tx_count);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[14], master_link_rx_count);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[18], master_link_error_count);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[22], runtime_diag.bridge_recovery_count);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[26], runtime_diag.bh1750_stale_count);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[30], runtime_diag.vl53_stale_count);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[34], runtime_diag.wondercam_stale_count);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[38], runtime_diag.wondercam_online_count);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[42], runtime_diag.health_event_count);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[46], runtime_diag.fault_event_count);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[50], runtime_diag.last_health_ms);
  OpticalMasterLink_WriteU32(&master_link_diag_packet[54], runtime_diag.last_fault_ms);
  master_link_diag_packet[58] = runtime_diag.last_health_code;
  master_link_diag_packet[59] = runtime_diag.last_fault_code;
  master_link_diag_packet[60] = runtime_diag.wondercam_raw_class_id;
  master_link_diag_packet[61] = runtime_diag.wondercam_filtered_class_id;
  master_link_diag_packet[62] = runtime_diag.wondercam_candidate_streak;
  master_link_diag_packet[63] = runtime_config.laser_profile;
  OpticalMasterLink_WriteU16(&master_link_diag_packet[64], runtime_diag.wondercam_conf_x10000);
  OpticalMasterLink_FinalizePacket(master_link_diag_packet, (uint16_t)sizeof(master_link_diag_packet));
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
  master_link_sequence = 0U;
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
  ++master_link_sequence;
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
  uint8_t *tx_data = master_link_status_packet;
  uint16_t tx_length = (uint16_t)sizeof(master_link_status_packet);

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
        tx_data = master_link_config_packet;
        tx_length = (uint16_t)sizeof(master_link_config_packet);
        break;

      case OPTICAL_MASTER_LINK_CMD_DIAGNOSTICS:
        tx_data = master_link_diag_packet;
        tx_length = (uint16_t)sizeof(master_link_diag_packet);
        break;

      case OPTICAL_MASTER_LINK_CMD_STATUS:
      default:
        tx_data = master_link_status_packet;
        tx_length = (uint16_t)sizeof(master_link_status_packet);
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

