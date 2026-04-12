#ifndef SC18IS604_H
#define SC18IS604_H

#include <stdint.h>

#include "main.h"

HAL_StatusTypeDef SC18IS604_Init(void);
uint8_t SC18IS604_IsReady(void);
uint8_t SC18IS604_GetLastI2cStatus(void);
uint8_t SC18IS604_GetLastI2cRawStatus(void);
const char *SC18IS604_I2cStatusText(uint8_t status);
HAL_StatusTypeDef SC18IS604_ReadVersion(char *buffer, uint8_t length);
HAL_StatusTypeDef SC18IS604_ReadGpioState(uint8_t *state);
HAL_StatusTypeDef SC18IS604_I2cWrite(uint8_t address_7bit, const uint8_t *data, uint8_t length);
HAL_StatusTypeDef SC18IS604_I2cRead(uint8_t address_7bit, uint8_t *data, uint8_t length);
HAL_StatusTypeDef SC18IS604_I2cWriteRead(uint8_t address_7bit,
                                         const uint8_t *write_data,
                                         uint8_t write_length,
                                         uint8_t *read_data,
                                         uint8_t read_length);

#endif /* SC18IS604_H */
