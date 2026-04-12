################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/BSP/X-NUCLEO-53L1A1/X-NUCLEO-53L1A1.c 

OBJS += \
./Drivers/BSP/X-NUCLEO-53L1A1/X-NUCLEO-53L1A1.o 

C_DEPS += \
./Drivers/BSP/X-NUCLEO-53L1A1/X-NUCLEO-53L1A1.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/BSP/X-NUCLEO-53L1A1/%.o Drivers/BSP/X-NUCLEO-53L1A1/%.su Drivers/BSP/X-NUCLEO-53L1A1/%.cyclo: ../Drivers/BSP/X-NUCLEO-53L1A1/%.c Drivers/BSP/X-NUCLEO-53L1A1/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F401xE -c -I../Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I../Drivers/BSP/X-NUCLEO-53L1A1 -I../Drivers/BSP/Components/vl53l1x_uld -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Drivers-2f-BSP-2f-X-2d-NUCLEO-2d-53L1A1

clean-Drivers-2f-BSP-2f-X-2d-NUCLEO-2d-53L1A1:
	-$(RM) ./Drivers/BSP/X-NUCLEO-53L1A1/X-NUCLEO-53L1A1.cyclo ./Drivers/BSP/X-NUCLEO-53L1A1/X-NUCLEO-53L1A1.d ./Drivers/BSP/X-NUCLEO-53L1A1/X-NUCLEO-53L1A1.o ./Drivers/BSP/X-NUCLEO-53L1A1/X-NUCLEO-53L1A1.su

.PHONY: clean-Drivers-2f-BSP-2f-X-2d-NUCLEO-2d-53L1A1

