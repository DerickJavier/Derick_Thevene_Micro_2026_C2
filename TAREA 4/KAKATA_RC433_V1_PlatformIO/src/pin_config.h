#pragma once

#include "driver/gpio.h"
#include "hal/adc_types.h"

// ============================================================
// I2C Bus (OLED SSD1306 + MPU6050 shared)
// ============================================================
#define PIN_I2C_SDA         GPIO_NUM_6
#define PIN_I2C_SCL         GPIO_NUM_7
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         400000

// ============================================================
// I2C Device Addresses
// ============================================================
#define SSD1306_I2C_ADDR    0x3C
#define MPU6050_I2C_ADDR    0x68

// ============================================================
// Joystick ADC Channels (ADC1)
// ============================================================
#define PIN_JOY0_X          GPIO_NUM_4   // ADC1_CH3 - Left joystick X
#define PIN_JOY0_Y          GPIO_NUM_5   // ADC1_CH4 - Left joystick Y
#define PIN_JOY1_X          GPIO_NUM_2   // ADC1_CH1 - Right joystick X
#define PIN_JOY1_Y          GPIO_NUM_1   // ADC1_CH0 - Right joystick Y

#define ADC_JOY0_X_CH       ADC_CHANNEL_3
#define ADC_JOY0_Y_CH       ADC_CHANNEL_4
#define ADC_JOY1_X_CH       ADC_CHANNEL_1
#define ADC_JOY1_Y_CH       ADC_CHANNEL_0

// ============================================================
// Joystick Buttons
// ============================================================
#define PIN_JOY0_BTN        GPIO_NUM_3
#define PIN_JOY1_BTN        GPIO_NUM_46

// ============================================================
// Face Buttons (4 bottom buttons)
// ============================================================
#define PIN_BTN_0           GPIO_NUM_9
#define PIN_BTN_1           GPIO_NUM_11
#define PIN_BTN_2           GPIO_NUM_10
#define PIN_BTN_3           GPIO_NUM_12

// ============================================================
// Lateral Side Buttons (4 side trigger buttons)
// ============================================================
#define PIN_BTN_L1          GPIO_NUM_42
#define PIN_BTN_L2          GPIO_NUM_41
#define PIN_BTN_L3          GPIO_NUM_40
#define PIN_BTN_L4          GPIO_NUM_39

// ============================================================
// MPU-6050 Interrupt
// ============================================================
#define PIN_MPU_INT         GPIO_NUM_16

// ============================================================
// 433MHz RF Module
// ============================================================
#define PIN_RF_DATA_RX      GPIO_NUM_18
#define PIN_RF_DATA_TX      GPIO_NUM_17
#define PIN_RF_ENABLE       GPIO_NUM_15

// ============================================================
// Status LEDs
// ============================================================
#define PIN_LED_1           GPIO_NUM_45
#define PIN_LED_2           GPIO_NUM_48
#define PIN_LED_3           GPIO_NUM_47
#define PIN_LED_4           GPIO_NUM_21
#define PIN_LED_5           GPIO_NUM_14
#define PIN_LED_6           GPIO_NUM_13

// ============================================================
// Battery Voltage ADC
// ============================================================
#define PIN_VBAT            GPIO_NUM_8
#define ADC_VBAT_CH         ADC_CHANNEL_7

// ============================================================
// ADC Configuration
// ============================================================
#define ADC_ATTEN           ADC_ATTEN_DB_12
#define ADC_MAX_VALUE       4095
#define ADC_CENTER_VALUE    2048

// ============================================================
// Joystick Mapping Range
// ============================================================
#define JOY_MIN             (-100)
#define JOY_MAX             100
#define JOY_DEADZONE        15

// ============================================================
// Button Active Level (active low with pull-up)
// ============================================================
#define BTN_ACTIVE_LOW      1

// ============================================================
// Joystick Offset Structure
// ============================================================
typedef struct {
    int16_t raw_x;
    int16_t raw_y;
} joystick_offsets_t;
