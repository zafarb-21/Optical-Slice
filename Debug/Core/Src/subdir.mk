################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/VL53L1X_api.c \
../Core/Src/VL53L1X_calibration.c \
../Core/Src/dma.c \
../Core/Src/gpio.c \
../Core/Src/main.c \
../Core/Src/optical_slice_app.c \
../Core/Src/optical_slice_sensors.c \
../Core/Src/optical_slice_validation.c \
../Core/Src/sc18is604.c \
../Core/Src/spi.c \
../Core/Src/stm32f3xx_hal_msp.c \
../Core/Src/stm32f3xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f3xx.c \
../Core/Src/usart.c \
../Core/Src/vl53l1_platform.c \
../Core/Src/vl53l1x_bridge.c 

OBJS += \
./Core/Src/VL53L1X_api.o \
./Core/Src/VL53L1X_calibration.o \
./Core/Src/dma.o \
./Core/Src/gpio.o \
./Core/Src/main.o \
./Core/Src/optical_slice_app.o \
./Core/Src/optical_slice_sensors.o \
./Core/Src/optical_slice_validation.o \
./Core/Src/sc18is604.o \
./Core/Src/spi.o \
./Core/Src/stm32f3xx_hal_msp.o \
./Core/Src/stm32f3xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f3xx.o \
./Core/Src/usart.o \
./Core/Src/vl53l1_platform.o \
./Core/Src/vl53l1x_bridge.o 

C_DEPS += \
./Core/Src/VL53L1X_api.d \
./Core/Src/VL53L1X_calibration.d \
./Core/Src/dma.d \
./Core/Src/gpio.d \
./Core/Src/main.d \
./Core/Src/optical_slice_app.d \
./Core/Src/optical_slice_sensors.d \
./Core/Src/optical_slice_validation.d \
./Core/Src/sc18is604.d \
./Core/Src/spi.d \
./Core/Src/stm32f3xx_hal_msp.d \
./Core/Src/stm32f3xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f3xx.d \
./Core/Src/usart.d \
./Core/Src/vl53l1_platform.d \
./Core/Src/vl53l1x_bridge.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F303x8 -c -I../Core/Inc -I../Drivers/STM32F3xx_HAL_Driver/Inc -I../Drivers/STM32F3xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F3xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/VL53L1X_api.cyclo ./Core/Src/VL53L1X_api.d ./Core/Src/VL53L1X_api.o ./Core/Src/VL53L1X_api.su ./Core/Src/VL53L1X_calibration.cyclo ./Core/Src/VL53L1X_calibration.d ./Core/Src/VL53L1X_calibration.o ./Core/Src/VL53L1X_calibration.su ./Core/Src/dma.cyclo ./Core/Src/dma.d ./Core/Src/dma.o ./Core/Src/dma.su ./Core/Src/gpio.cyclo ./Core/Src/gpio.d ./Core/Src/gpio.o ./Core/Src/gpio.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/optical_slice_app.cyclo ./Core/Src/optical_slice_app.d ./Core/Src/optical_slice_app.o ./Core/Src/optical_slice_app.su ./Core/Src/optical_slice_sensors.cyclo ./Core/Src/optical_slice_sensors.d ./Core/Src/optical_slice_sensors.o ./Core/Src/optical_slice_sensors.su ./Core/Src/optical_slice_validation.cyclo ./Core/Src/optical_slice_validation.d ./Core/Src/optical_slice_validation.o ./Core/Src/optical_slice_validation.su ./Core/Src/sc18is604.cyclo ./Core/Src/sc18is604.d ./Core/Src/sc18is604.o ./Core/Src/sc18is604.su ./Core/Src/spi.cyclo ./Core/Src/spi.d ./Core/Src/spi.o ./Core/Src/spi.su ./Core/Src/stm32f3xx_hal_msp.cyclo ./Core/Src/stm32f3xx_hal_msp.d ./Core/Src/stm32f3xx_hal_msp.o ./Core/Src/stm32f3xx_hal_msp.su ./Core/Src/stm32f3xx_it.cyclo ./Core/Src/stm32f3xx_it.d ./Core/Src/stm32f3xx_it.o ./Core/Src/stm32f3xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f3xx.cyclo ./Core/Src/system_stm32f3xx.d ./Core/Src/system_stm32f3xx.o ./Core/Src/system_stm32f3xx.su ./Core/Src/usart.cyclo ./Core/Src/usart.d ./Core/Src/usart.o ./Core/Src/usart.su ./Core/Src/vl53l1_platform.cyclo ./Core/Src/vl53l1_platform.d ./Core/Src/vl53l1_platform.o ./Core/Src/vl53l1_platform.su ./Core/Src/vl53l1x_bridge.cyclo ./Core/Src/vl53l1x_bridge.d ./Core/Src/vl53l1x_bridge.o ./Core/Src/vl53l1x_bridge.su

.PHONY: clean-Core-2f-Src

