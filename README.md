# Optical Slice Firmware

Current firmware status for the STM32-based optical slice node.

## Architecture

- `STM32F303 <-> SPI1 <-> SC18IS604 <-> downstream I2C sensors`
- `STM32F303 <-> I2C1 <-> master board`

The STM32 does not use its native `I2C1` to talk directly to the optical sensors in the final design. Sensor access goes through `SPI1` to the `SC18IS604` bridge. `I2C1` is kept for the upstream master-board link.

## Working Now

- `SC18IS604` SPI transport and bridge register access
- downstream bridge-backed I2C transactions
- `BH1750` ambient light sampling through the bridge
- `VL53L1X` identification, init, start ranging, data-ready polling, distance read, and interrupt clear through the bridge
- laser transmitter driven directly from `PB0`
- laser receiver read directly on `PA12`
- live debug/status frame over `USART2`

## Important VL53L1X Note

The `VL53L1X` now works through the `SC18IS604`, but only when register reads use split transactions:

1. write 16-bit register address
2. perform a separate read transaction

The `SC18IS604` combined read-after-write command was bench-tested and does not reliably work with the `VL53L1X` path on this hardware. The production platform shim therefore uses discrete write/read transactions for `VL53L1X` register access.

The official ST `VL53L1X` ULD package used by the project is under:

- `STSW-IMG009/STSW-IMG009_v3.5.5`

## Current Sensor State

- `BH1750`: working
- `VL53L1X`: working
- laser path: integrated and visible in the debug frame
- `WonderCam`: not integrated yet

The UART live frame currently reports:

- bridge health
- ambient light
- dark/light threshold state
- raw `VL53L1X` distance and range status
- laser TX state, raw RX level, and interpreted beam state
- camera placeholder status

Note: the ambient light value printed in the debug frame is currently `lux_x10`, not plain lux. For example, `39` means `3.9 lux`.

## Known Sensor Addresses

- `BH1750` = `0x23`
- `BH1750` alternate = `0x5C`
- `VL53L1X` = `0x29`
- `WonderCam` = `0x32`

## Laser Path

The laser front end is not on I2C.

- `PB0` drives the transmitter
- `PA12` reads the receiver

The receiver is handled as a digital GPIO input in firmware and is included in the live status frame.

## STM32 Pin Map

- `PB3` = `SPI1_SCK`
- `PB4` = `SPI1_MISO`
- `PB5` = `SPI1_MOSI`
- `PA4` = `SC18_CS`
- `PA8` = `SC18_RESET`
- `PA9` = `SC18_INT`
- `PB0` = `LASER_TX`
- `PA12` = `LASER_RX`
- `PB6` = `I2C1_SCL` to master board
- `PB7` = `I2C1_SDA` to master board

## What Is Still Pending

1. `WonderCam` command/data integration through the bridge
2. snow-height conversion from mounted baseline distance
3. calibration workflow for deployed `VL53L1X` measurements
4. final master-board `I2C1` protocol and slave address
5. cleanup or formalization of the temporary `USART2` debug output

## Current Bring-Up Outcome

The firmware can now boot, initialize the bridge, read ambient light, read the laser receiver state, initialize the `VL53L1X`, and stream a stable debug frame over UART. The system is now past basic bus bring-up and into application-layer integration work.
