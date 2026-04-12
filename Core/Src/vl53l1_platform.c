#include "vl53l1_platform.h"

#include <string.h>

#include "main.h"
#include "sc18is604.h"

static uint8_t VL53L1_PlatformAddr7(uint16_t dev)
{
  return (uint8_t)(dev >> 1);
}

int8_t VL53L1_WriteMulti(uint16_t dev, uint16_t index, uint8_t *pdata, uint32_t count)
{
  uint8_t payload[260];

  if ((pdata == NULL) || (count == 0U) || (count > 255U))
  {
    return -1;
  }

  payload[0] = (uint8_t)(index >> 8);
  payload[1] = (uint8_t)(index & 0xFFU);
  memcpy(&payload[2], pdata, count);

  return (SC18IS604_I2cWrite(VL53L1_PlatformAddr7(dev), payload, (uint8_t)(count + 2U)) == HAL_OK) ? 0 : -1;
}

int8_t VL53L1_ReadMulti(uint16_t dev, uint16_t index, uint8_t *pdata, uint32_t count)
{
  uint8_t reg_addr[2];
  HAL_StatusTypeDef write_status;

  if ((pdata == NULL) || (count == 0U) || (count > 255U))
  {
    return -1;
  }

  reg_addr[0] = (uint8_t)(index >> 8);
  reg_addr[1] = (uint8_t)(index & 0xFFU);

  /*
   * The SC18IS604 combined read-after-write command does not reliably work
   * with this VL53L1X path on the final hardware, but discrete write/read
   * transactions do. Keep the VL53 platform on the split transaction path.
   */
  write_status = SC18IS604_I2cWrite(VL53L1_PlatformAddr7(dev),
                                    reg_addr,
                                    (uint8_t)sizeof(reg_addr));
  if (write_status != HAL_OK)
  {
    return -1;
  }

  return (SC18IS604_I2cRead(VL53L1_PlatformAddr7(dev),
                            pdata,
                            (uint8_t)count) == HAL_OK) ? 0 : -1;
}

int8_t VL53L1_WrByte(uint16_t dev, uint16_t index, uint8_t data)
{
  return VL53L1_WriteMulti(dev, index, &data, 1U);
}

int8_t VL53L1_WrWord(uint16_t dev, uint16_t index, uint16_t data)
{
  uint8_t value[2];

  value[0] = (uint8_t)(data >> 8);
  value[1] = (uint8_t)(data & 0xFFU);

  return VL53L1_WriteMulti(dev, index, value, 2U);
}

int8_t VL53L1_WrDWord(uint16_t dev, uint16_t index, uint32_t data)
{
  uint8_t value[4];

  value[0] = (uint8_t)(data >> 24);
  value[1] = (uint8_t)((data >> 16) & 0xFFU);
  value[2] = (uint8_t)((data >> 8) & 0xFFU);
  value[3] = (uint8_t)(data & 0xFFU);

  return VL53L1_WriteMulti(dev, index, value, 4U);
}

int8_t VL53L1_RdByte(uint16_t dev, uint16_t index, uint8_t *pdata)
{
  return VL53L1_ReadMulti(dev, index, pdata, 1U);
}

int8_t VL53L1_RdWord(uint16_t dev, uint16_t index, uint16_t *pdata)
{
  uint8_t value[2];

  if (pdata == NULL)
  {
    return -1;
  }

  if (VL53L1_ReadMulti(dev, index, value, 2U) != 0)
  {
    return -1;
  }

  *pdata = (uint16_t)(((uint16_t)value[0] << 8) | value[1]);
  return 0;
}

int8_t VL53L1_RdDWord(uint16_t dev, uint16_t index, uint32_t *pdata)
{
  uint8_t value[4];

  if (pdata == NULL)
  {
    return -1;
  }

  if (VL53L1_ReadMulti(dev, index, value, 4U) != 0)
  {
    return -1;
  }

  *pdata = ((uint32_t)value[0] << 24) |
           ((uint32_t)value[1] << 16) |
           ((uint32_t)value[2] << 8) |
           value[3];
  return 0;
}

int8_t VL53L1_WaitMs(uint16_t dev, int32_t wait_ms)
{
  (void)dev;
  if (wait_ms < 0)
  {
    return -1;
  }

  HAL_Delay((uint32_t)wait_ms);
  return 0;
}
