################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/BSP/Components/vl53l1x_uld/VL53L1X_api.c \
../Drivers/BSP/Components/vl53l1x_uld/VL53L1X_calibration.c 

OBJS += \
./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_api.o \
./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_calibration.o 

C_DEPS += \
./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_api.d \
./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_calibration.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/BSP/Components/vl53l1x_uld/%.o Drivers/BSP/Components/vl53l1x_uld/%.su Drivers/BSP/Components/vl53l1x_uld/%.cyclo: ../Drivers/BSP/Components/vl53l1x_uld/%.c Drivers/BSP/Components/vl53l1x_uld/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F401xE -c -I../Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I../Drivers/BSP/X-NUCLEO-53L1A1 -I../Drivers/BSP/Components/vl53l1x_uld -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-BSP-2f-Components-2f-vl53l1x_uld

clean-Drivers-2f-BSP-2f-Components-2f-vl53l1x_uld:
	-$(RM) ./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_api.cyclo ./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_api.d ./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_api.o ./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_api.su ./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_calibration.cyclo ./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_calibration.d ./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_calibration.o ./Drivers/BSP/Components/vl53l1x_uld/VL53L1X_calibration.su

.PHONY: clean-Drivers-2f-BSP-2f-Components-2f-vl53l1x_uld

