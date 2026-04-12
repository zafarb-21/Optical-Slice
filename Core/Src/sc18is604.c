#include "sc18is604.h"

#include <string.h>

#include "optical_slice_config.h"
#include "spi.h"

#define SC18_CMD_I2C_WRITE            0x00U
#define SC18_CMD_I2C_READ             0x01U
#define SC18_CMD_I2C_READ_AFTER_WRITE 0x02U
#define SC18_CMD_READ_BUFFER          0x06U
#define SC18_CMD_WRITE_REGISTER       0x20U
#define SC18_CMD_READ_REGISTER        0x21U
#define SC18_CMD_READ_VERSION         0xFEU

#define SC18_REG_IOCONFIG             0x00U
#define SC18_REG_IOSTATE              0x01U
#define SC18_REG_I2CCLOCK             0x02U
#define SC18_REG_I2CTO                0x03U
#define SC18_REG_I2CSTAT              0x04U

#define SC18_I2C_STATUS_SUCCESS       0xF0U
#define SC18_I2C_STATUS_ADDR_NACK     0xF1U
#define SC18_I2C_STATUS_DATA_NACK     0xF2U
#define SC18_I2C_STATUS_BUSY          0xF3U
#define SC18_I2C_STATUS_TIMEOUT       0xF8U
#define SC18_SPI_INTERBYTE_DELAY_US   10U

static uint8_t sc18_ready;
static uint8_t sc18_last_i2c_status = SC18_I2C_STATUS_SUCCESS;
static uint8_t sc18_last_i2c_raw_status = SC18_I2C_STATUS_SUCCESS;

static void SC18IS604_DelayUs(uint32_t delay_us)
{
  uint32_t cycles;
  uint32_t start_cycles;

  if (delay_us == 0U)
  {
    return;
  }

  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
  {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  }

  if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
  {
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  }

  cycles = (SystemCoreClock / 1000000U) * delay_us;
  start_cycles = DWT->CYCCNT;
  while ((DWT->CYCCNT - start_cycles) < cycles)
  {
  }
}

static void SC18IS604_Select(void)
{
  HAL_GPIO_WritePin(SC18_CS_GPIO_Port, SC18_CS_Pin, GPIO_PIN_RESET);
}

static void SC18IS604_Deselect(void)
{
  HAL_GPIO_WritePin(SC18_CS_GPIO_Port, SC18_CS_Pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef SC18IS604_SpiTransfer(const uint8_t *tx_data, uint8_t *rx_data, uint16_t length)
{
  HAL_StatusTypeDef status = HAL_OK;
  uint16_t index;

  SC18IS604_Select();
  for (index = 0U; index < length; ++index)
  {
    status = HAL_SPI_TransmitReceive(&hspi1,
                                     (uint8_t *)&tx_data[index],
                                     &rx_data[index],
                                     1U,
                                     100U);
    if (status != HAL_OK)
    {
      break;
    }

    if ((index + 1U) < length)
    {
      SC18IS604_DelayUs(SC18_SPI_INTERBYTE_DELAY_US);
    }
  }
  SC18IS604_Deselect();
  return status;
}

static HAL_StatusTypeDef SC18IS604_SendCommand(const uint8_t *data, uint16_t length)
{
  uint8_t rx_data[260];

  if ((data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  memset(rx_data, 0, length);
  return SC18IS604_SpiTransfer(data, rx_data, length);
}

static HAL_StatusTypeDef SC18IS604_WriteRegister(uint8_t reg, uint8_t value)
{
  uint8_t tx_data[3] = {SC18_CMD_WRITE_REGISTER, reg, value};
  uint8_t rx_data[3];

  return SC18IS604_SpiTransfer(tx_data, rx_data, sizeof(tx_data));
}

static HAL_StatusTypeDef SC18IS604_ReadRegister(uint8_t reg, uint8_t *value)
{
  uint8_t tx_data[3] = {SC18_CMD_READ_REGISTER, reg, 0xFFU};
  uint8_t rx_data[3];
  HAL_StatusTypeDef status;

  if (value == NULL)
  {
    return HAL_ERROR;
  }

  status = SC18IS604_SpiTransfer(tx_data, rx_data, sizeof(tx_data));
  if (status == HAL_OK)
  {
    *value = rx_data[2];
  }

  return status;
}

static HAL_StatusTypeDef SC18IS604_WaitForI2cCompletion(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint8_t status_value = SC18_I2C_STATUS_BUSY;
  HAL_StatusTypeDef status;

  do
  {
    status = SC18IS604_ReadRegister(SC18_REG_I2CSTAT, &status_value);
    if (status != HAL_OK)
    {
      return status;
    }

    sc18_last_i2c_raw_status = status_value;
    sc18_last_i2c_status = status_value;

    if (status_value == SC18_I2C_STATUS_SUCCESS)
    {
      return HAL_OK;
    }

    if (status_value != SC18_I2C_STATUS_BUSY)
    {
      return HAL_ERROR;
    }

    HAL_Delay(1U);
  } while ((HAL_GetTick() - start) < timeout_ms);

  sc18_last_i2c_status = SC18_I2C_STATUS_TIMEOUT;
  return HAL_TIMEOUT;
}

static HAL_StatusTypeDef SC18IS604_ReadBuffer(uint8_t *data, uint8_t length)
{
  uint8_t tx_data[256];
  uint8_t rx_data[256];
  uint16_t transfer_length;
  HAL_StatusTypeDef status;

  if ((data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  transfer_length = (uint16_t)length + 1U;
  memset(tx_data, 0xFF, transfer_length);
  memset(rx_data, 0, transfer_length);
  tx_data[0] = SC18_CMD_READ_BUFFER;

  status = SC18IS604_SpiTransfer(tx_data, rx_data, transfer_length);
  if (status == HAL_OK)
  {
    memcpy(data, &rx_data[1], length);
  }

  return status;
}

HAL_StatusTypeDef SC18IS604_Init(void)
{
  uint8_t register_value;

  HAL_GPIO_WritePin(SC18_RESET_GPIO_Port, SC18_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(1U);
  HAL_GPIO_WritePin(SC18_RESET_GPIO_Port, SC18_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(5U);

  SC18IS604_Deselect();

  /*
   * Treat successful internal register access as the bridge presence check.
   * The I2CStat reset value is not reliable enough to use as a strict gate
   * during bring-up, but read/write access to the control registers is.
   */
  if (SC18IS604_ReadRegister(SC18_REG_I2CCLOCK, &register_value) != HAL_OK)
  {
    sc18_ready = 0U;
    return HAL_ERROR;
  }

  if (SC18IS604_WriteRegister(SC18_REG_I2CCLOCK, OPTICAL_SC18_I2C_CLOCK_DIVIDER) != HAL_OK)
  {
    sc18_ready = 0U;
    return HAL_ERROR;
  }

  if (SC18IS604_ReadRegister(SC18_REG_I2CCLOCK, &register_value) != HAL_OK)
  {
    sc18_ready = 0U;
    return HAL_ERROR;
  }

  if (register_value != OPTICAL_SC18_I2C_CLOCK_DIVIDER)
  {
    sc18_ready = 0U;
    return HAL_ERROR;
  }

  if (SC18IS604_WriteRegister(SC18_REG_I2CTO, 0x00U) != HAL_OK)
  {
    sc18_ready = 0U;
    return HAL_ERROR;
  }

  if (SC18IS604_ReadRegister(SC18_REG_I2CTO, &register_value) != HAL_OK)
  {
    sc18_ready = 0U;
    return HAL_ERROR;
  }

  if (register_value != 0x00U)
  {
    sc18_ready = 0U;
    return HAL_ERROR;
  }

  if (SC18IS604_WriteRegister(SC18_REG_IOCONFIG, 0x00U) != HAL_OK)
  {
    sc18_ready = 0U;
    return HAL_ERROR;
  }

  if (SC18IS604_ReadRegister(SC18_REG_IOCONFIG, &register_value) != HAL_OK)
  {
    sc18_ready = 0U;
    return HAL_ERROR;
  }

  if (register_value != 0x00U)
  {
    sc18_ready = 0U;
    return HAL_ERROR;
  }

  sc18_ready = 1U;
  sc18_last_i2c_status = SC18_I2C_STATUS_SUCCESS;
  sc18_last_i2c_raw_status = SC18_I2C_STATUS_SUCCESS;
  return HAL_OK;
}

uint8_t SC18IS604_IsReady(void)
{
  return sc18_ready;
}

uint8_t SC18IS604_GetLastI2cStatus(void)
{
  return sc18_last_i2c_status;
}

uint8_t SC18IS604_GetLastI2cRawStatus(void)
{
  return sc18_last_i2c_raw_status;
}

const char *SC18IS604_I2cStatusText(uint8_t status)
{
  switch (status)
  {
    case SC18_I2C_STATUS_SUCCESS:
      return "OK";
    case SC18_I2C_STATUS_ADDR_NACK:
      return "ADDR_NACK";
    case SC18_I2C_STATUS_DATA_NACK:
      return "DATA_NACK";
    case SC18_I2C_STATUS_BUSY:
      return "BUSY";
    case SC18_I2C_STATUS_TIMEOUT:
      return "TIMEOUT";
    default:
      return "UNKNOWN";
  }
}

HAL_StatusTypeDef SC18IS604_ReadVersion(char *buffer, uint8_t length)
{
  uint8_t version_buffer[16];
  uint8_t command = SC18_CMD_READ_VERSION;
  HAL_StatusTypeDef status;

  if ((buffer == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  status = SC18IS604_SendCommand(&command, 1U);
  if (status != HAL_OK)
  {
    return status;
  }

  HAL_Delay(1U);
  status = SC18IS604_ReadBuffer(version_buffer, sizeof(version_buffer));
  if (status != HAL_OK)
  {
    return status;
  }

  memset(buffer, 0, length);
  strncpy(buffer, (const char *)version_buffer, length - 1U);
  return HAL_OK;
}

HAL_StatusTypeDef SC18IS604_ReadGpioState(uint8_t *state)
{
  return SC18IS604_ReadRegister(SC18_REG_IOSTATE, state);
}

HAL_StatusTypeDef SC18IS604_I2cWrite(uint8_t address_7bit, const uint8_t *data, uint8_t length)
{
  uint8_t tx_data[258];
  HAL_StatusTypeDef status;

  if ((data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  tx_data[0] = SC18_CMD_I2C_WRITE;
  tx_data[1] = length;
  tx_data[2] = (uint8_t)(address_7bit << 1);
  memcpy(&tx_data[3], data, length);

  status = SC18IS604_SendCommand(tx_data, (uint16_t)length + 3U);
  if (status != HAL_OK)
  {
    return status;
  }

  return SC18IS604_WaitForI2cCompletion(50U);
}

HAL_StatusTypeDef SC18IS604_I2cRead(uint8_t address_7bit, uint8_t *data, uint8_t length)
{
  uint8_t tx_data[3];
  HAL_StatusTypeDef status;

  if ((data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  tx_data[0] = SC18_CMD_I2C_READ;
  tx_data[1] = length;
  tx_data[2] = (uint8_t)((address_7bit << 1) | 0x01U);

  status = SC18IS604_SendCommand(tx_data, sizeof(tx_data));
  if (status != HAL_OK)
  {
    return status;
  }

  status = SC18IS604_WaitForI2cCompletion(50U);
  if (status != HAL_OK)
  {
    return status;
  }

  return SC18IS604_ReadBuffer(data, length);
}

HAL_StatusTypeDef SC18IS604_I2cWriteRead(uint8_t address_7bit,
                                         const uint8_t *write_data,
                                         uint8_t write_length,
                                         uint8_t *read_data,
                                         uint8_t read_length)
{
  uint8_t tx_data[260];
  uint16_t offset;
  HAL_StatusTypeDef status;

  if ((write_data == NULL) || (read_data == NULL) || (write_length == 0U) || (read_length == 0U))
  {
    return HAL_ERROR;
  }

  tx_data[0] = SC18_CMD_I2C_READ_AFTER_WRITE;
  tx_data[1] = write_length;
  tx_data[2] = (uint8_t)(address_7bit << 1);
  memcpy(&tx_data[3], write_data, write_length);
  offset = (uint16_t)write_length + 3U;
  tx_data[offset] = read_length;
  tx_data[offset + 1U] = (uint8_t)((address_7bit << 1) | 0x01U);

  status = SC18IS604_SendCommand(tx_data, offset + 2U);
  if (status != HAL_OK)
  {
    return status;
  }

  status = SC18IS604_WaitForI2cCompletion(50U);
  if (status != HAL_OK)
  {
    return status;
  }

  return SC18IS604_ReadBuffer(read_data, read_length);
}
