#ifndef OPTICAL_SLICE_CONFIG_H
#define OPTICAL_SLICE_CONFIG_H

#include <stdint.h>

#define OPTICAL_ADDR_BH1750_DEFAULT       0x23U
#define OPTICAL_ADDR_BH1750_ALT           0x5CU
#define OPTICAL_ADDR_VL53L1X_DEFAULT      0x29U
#define OPTICAL_ADDR_WONDERCAM_DEFAULT    0x32U

/*
 * Sensor-side addresses below are the 7-bit addresses used on the downstream
 * I2C bus controlled by the SC18IS604 SPI-to-I2C bridge.
 *
 * The STM32's native I2C peripheral is reserved for communication with the
 * master board and should not be used for direct sensor access in the final
 * design.
 *
 * The KY-008 laser transmitter and the common laser receiver module are not
 * I2C peripherals, so they do not have I2C bus addresses. In the final board
 * setup, the laser front end is wired directly to STM32 GPIO:
 * - PB0 drives the transmitter
 * - PA12 reads the receiver
 */
#define OPTICAL_ADDR_AI_CAMERA            OPTICAL_ADDR_WONDERCAM_DEFAULT
#define OPTICAL_ADDR_LASER_FRONT_END      0x00U

#define OPTICAL_ENABLE_BH1750             1U
#define OPTICAL_ENABLE_LASER_FRONT_END    1U
#define OPTICAL_ENABLE_VL53L1X            1U
#define OPTICAL_ENABLE_WONDERCAM          1U
#define OPTICAL_SC18_I2C_CLOCK_DIVIDER    19U
#define OPTICAL_LASER_ACTIVE_STATE        0U
#define OPTICAL_LASER_TX_DEFAULT_STATE    1U
#define OPTICAL_LASER_DEBUG_TOGGLE        0U
#define OPTICAL_LASER_DEBUG_TOGGLE_MS     1000U
#define OPTICAL_LASER_PRESENCE_ASSERT_MS  75U
#define OPTICAL_LASER_PRESENCE_RELEASE_MS 150U
#define OPTICAL_LASER_MOTION_HOLD_MS      300U
#define OPTICAL_ENABLE_VL53L1X_ULD        1U

#define OPTICAL_WONDERCAM_CLASS_NONE      1U
#define OPTICAL_WONDERCAM_CLASS_ICE       5U
#define OPTICAL_WONDERCAM_CLASS_PACKAGE   10U
#define OPTICAL_WONDERCAM_CLASS_SNOW      12U
#define OPTICAL_WONDERCAM_MIN_CONF_X10000 8500U
#define OPTICAL_WONDERCAM_STABLE_FRAMES   4U

#define OPTICAL_BH1750_DARK_THRESHOLD_X10 100U
#define OPTICAL_SAMPLE_PERIOD_MS          200U
#define OPTICAL_REPORT_PERIOD_MS          1000U
#define OPTICAL_I2C_PROBE_PERIOD_MS       1000U

#endif /* OPTICAL_SLICE_CONFIG_H */
