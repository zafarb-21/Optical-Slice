/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
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
#include "usart.h"

/* USER CODE BEGIN 0 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "optical_slice_config.h"
#include "optical_slice_sensors.h"

#define OPTICAL_MASTER_LINK_SLOT_ID            0x03U
#define OPTICAL_MASTER_LINK_RX_CMD_LEN         64U
#define OPTICAL_MASTER_LINK_STATUS_PACKET_LEN  384U
#define OPTICAL_MASTER_LINK_CONFIG_PACKET_LEN  320U
#define OPTICAL_MASTER_LINK_DIAG_PACKET_LEN    640U

typedef enum
{
  OPTICAL_MASTER_LINK_VIEW_STATUS = 0,
  OPTICAL_MASTER_LINK_VIEW_CONFIG,
  OPTICAL_MASTER_LINK_VIEW_DIAGNOSTICS
} optical_master_link_view_t;

static optical_slice_frame_t master_link_latest_frame;
static char master_link_status_packet[OPTICAL_MASTER_LINK_STATUS_PACKET_LEN];
static char master_link_config_packet[OPTICAL_MASTER_LINK_CONFIG_PACKET_LEN];
static char master_link_diag_packet[OPTICAL_MASTER_LINK_DIAG_PACKET_LEN];
static uint16_t master_link_status_length;
static uint16_t master_link_config_length;
static uint16_t master_link_diag_length;
static uint8_t master_link_initialized;
static uint8_t master_link_rx_enabled;
static uint8_t master_link_rx_byte;
static uint8_t master_link_waiting_for_slot_id;
static uint32_t master_link_last_activity_ms;
static uint32_t master_link_tx_count;
static uint32_t master_link_rx_count;
static uint32_t master_link_error_count;
static optical_master_link_view_t master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_STATUS;
static char master_link_command[OPTICAL_MASTER_LINK_RX_CMD_LEN];
static uint8_t master_link_command_length;

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

static HAL_StatusTypeDef OpticalMasterLink_EnableReceive(void)
{
  HAL_StatusTypeDef status;

  status = HAL_UART_Receive_IT(&huart1, &master_link_rx_byte, 1U);
  master_link_rx_enabled = (uint8_t)(status == HAL_OK);
  return status;
}

static void OpticalMasterLink_RebuildPackets(void)
{
  optical_runtime_config_t runtime_config;
  optical_runtime_diag_t runtime_diag;
  char ambient_lux[20];
  char object_distance[20];
  char snow_height[20];
  char precip_level[20];
  char snow_baseline[20];

  OpticalSensors_GetRuntimeConfig(&runtime_config);
  OpticalSensors_GetDiagnostics(&runtime_diag);

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
                                                                 OpticalSensors_GetLaserProfileName(runtime_config.laser_profile),
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

static void OpticalMasterLink_HandleCommand(const char *command)
{
  if (command == NULL)
  {
    return;
  }

  if ((strcmp(command, "status") == 0) || (strcmp(command, "packet:status") == 0))
  {
    master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_STATUS;
  }
  else if ((strcmp(command, "config") == 0) || (strcmp(command, "packet:config") == 0))
  {
    master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_CONFIG;
  }
  else if ((strcmp(command, "diagnostics") == 0) || (strcmp(command, "packet:diagnostics") == 0))
  {
    master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_DIAGNOSTICS;
  }
  else if ((strcmp(command, "baseline:capture") == 0) || (strcmp(command, "capture_baseline") == 0))
  {
    if (OpticalSensors_CaptureSnowBaseline() != HAL_OK)
    {
      ++master_link_error_count;
    }
    master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_CONFIG;
  }
  else if ((strcmp(command, "baseline:clear") == 0) || (strcmp(command, "clear_baseline") == 0))
  {
    OpticalSensors_ClearSnowBaseline();
    master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_CONFIG;
  }
  else if ((strcmp(command, "laser_profile:default") == 0) || (strcmp(command, "profile:default") == 0))
  {
    if (OpticalSensors_SetLaserProfile(OPTICAL_LASER_PROFILE_DEFAULT) != HAL_OK)
    {
      ++master_link_error_count;
    }
    master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_CONFIG;
  }
  else if ((strcmp(command, "laser_profile:fast") == 0) || (strcmp(command, "profile:fast") == 0))
  {
    if (OpticalSensors_SetLaserProfile(OPTICAL_LASER_PROFILE_FAST) != HAL_OK)
    {
      ++master_link_error_count;
    }
    master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_CONFIG;
  }
  else if ((strcmp(command, "laser_profile:stable") == 0) || (strcmp(command, "profile:stable") == 0))
  {
    if (OpticalSensors_SetLaserProfile(OPTICAL_LASER_PROFILE_STABLE) != HAL_OK)
    {
      ++master_link_error_count;
    }
    master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_CONFIG;
  }
  else if ((strcmp(command, "diagnostics:reset") == 0) || (strcmp(command, "reset_diagnostics") == 0))
  {
    master_link_tx_count = 0U;
    master_link_rx_count = 0U;
    master_link_error_count = 0U;
    OpticalSensors_ResetDiagnostics();
    master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_DIAGNOSTICS;
  }
  else
  {
    ++master_link_error_count;
  }
}

static void OpticalMasterLink_ProcessByte(uint8_t byte)
{
  if (master_link_waiting_for_slot_id != 0U)
  {
    master_link_waiting_for_slot_id = 0U;
    if (byte == OPTICAL_MASTER_LINK_SLOT_ID)
    {
      ++master_link_rx_count;
      master_link_last_activity_ms = HAL_GetTick();
      respondToLoaf();
    }
    return;
  }

  if (byte == 0x99U)
  {
    master_link_waiting_for_slot_id = 1U;
    return;
  }

  if (byte == '>')
  {
    master_link_command_length = 0U;
    return;
  }

  if (byte == '\r')
  {
    return;
  }

  if (byte == '\n')
  {
    if (master_link_command_length != 0U)
    {
      master_link_command[master_link_command_length] = '\0';
      ++master_link_rx_count;
      master_link_last_activity_ms = HAL_GetTick();
      OpticalMasterLink_HandleCommand(master_link_command);
      master_link_command_length = 0U;
    }
    return;
  }

  if (master_link_command_length < (OPTICAL_MASTER_LINK_RX_CMD_LEN - 1U))
  {
    master_link_command[master_link_command_length++] = (char)byte;
  }
  else
  {
    master_link_command_length = 0U;
    ++master_link_error_count;
  }
}

/* USER CODE END 0 */

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_rx;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 38400;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}
/* USART2 init function */

void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 38400;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */
    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PB6     ------> USART1_TX
    PB7     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* USART1 DMA Init */
    /* USART1_RX Init */
    hdma_usart1_rx.Instance = DMA1_Channel5;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_NORMAL;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart1_rx);

    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
  else if(uartHandle->Instance==USART2)
  {
  /* USER CODE BEGIN USART2_MspInit 0 */

  /* USER CODE END USART2_MspInit 0 */
    /* USART2 clock enable */
    __HAL_RCC_USART2_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA15     ------> USART2_RX
    */
    GPIO_InitStruct.Pin = VCP_TX_Pin|VCP_RX_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN USART2_MspInit 1 */

  /* USER CODE END USART2_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PB6     ------> USART1_TX
    PB7     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6|GPIO_PIN_7);

    /* USART1 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);

    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
  else if(uartHandle->Instance==USART2)
  {
  /* USER CODE BEGIN USART2_MspDeInit 0 */

  /* USER CODE END USART2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART2_CLK_DISABLE();

    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA15     ------> USART2_RX
    */
    HAL_GPIO_DeInit(GPIOA, VCP_TX_Pin|VCP_RX_Pin);

  /* USER CODE BEGIN USART2_MspDeInit 1 */

  /* USER CODE END USART2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

HAL_StatusTypeDef OpticalMasterLink_Init(void)
{
  memset(&master_link_latest_frame, 0, sizeof(master_link_latest_frame));
  master_link_selected_view = OPTICAL_MASTER_LINK_VIEW_STATUS;
  master_link_initialized = 1U;
  master_link_rx_enabled = 0U;
  master_link_waiting_for_slot_id = 0U;
  master_link_last_activity_ms = 0U;
  master_link_tx_count = 0U;
  master_link_rx_count = 0U;
  master_link_error_count = 0U;
  master_link_command_length = 0U;
  master_link_status_length = 0U;
  master_link_config_length = 0U;
  master_link_diag_length = 0U;
  OpticalMasterLink_RebuildPackets();
  return OpticalMasterLink_EnableReceive();
}

void OpticalMasterLink_UpdateFrame(const optical_slice_frame_t *frame)
{
  if (frame == NULL)
  {
    return;
  }

  master_link_latest_frame = *frame;
}

uint8_t OpticalMasterLink_IsHealthy(void)
{
  return (uint8_t)((master_link_initialized != 0U) && (master_link_rx_enabled != 0U));
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

void respondToLoaf(void)
{
  const char *tx_data = master_link_status_packet;
  uint16_t tx_length = master_link_status_length;

  OpticalMasterLink_RebuildPackets();

  switch (master_link_selected_view)
  {
    case OPTICAL_MASTER_LINK_VIEW_CONFIG:
      tx_data = master_link_config_packet;
      tx_length = master_link_config_length;
      break;

    case OPTICAL_MASTER_LINK_VIEW_DIAGNOSTICS:
      tx_data = master_link_diag_packet;
      tx_length = master_link_diag_length;
      break;

    case OPTICAL_MASTER_LINK_VIEW_STATUS:
    default:
      tx_data = master_link_status_packet;
      tx_length = master_link_status_length;
      break;
  }

  if ((tx_length == 0U) ||
      (HAL_UART_Transmit(&huart1, (uint8_t *)tx_data, tx_length, HAL_MAX_DELAY) != HAL_OK))
  {
    ++master_link_error_count;
    return;
  }

  ++master_link_tx_count;
  master_link_last_activity_ms = HAL_GetTick();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart != &huart1)
  {
    return;
  }

  OpticalMasterLink_ProcessByte(master_link_rx_byte);
  (void)OpticalMasterLink_EnableReceive();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart != &huart1)
  {
    return;
  }

  ++master_link_error_count;
  (void)HAL_UART_AbortReceive(huart);
  (void)OpticalMasterLink_EnableReceive();
}

/* USER CODE END 1 */

