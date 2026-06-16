// ============================================================================
//  board_pins.h  -  LilyGo T-Display S3 Pro (K231 / XY231020) GPIO map
//
//  VERIFIED against the official LilyGo repo. Do NOT invent pin numbers.
//  Source: examples/*/utilities.h
//    https://github.com/Xinyuan-LilyGO/T-Display-S3-Pro
//
//  Original macro names from the repo are shown in comments for traceability.
// ============================================================================
#pragma once

// --- Shared I2C bus (CST226SE touch, LTR-553 light sensor, SY6970 PMU) ------
#define TDS3_PIN_I2C_SDA      5    // BOARD_I2C_SDA
#define TDS3_PIN_I2C_SCL      6    // BOARD_I2C_SCL

// --- Shared SPI bus (TFT + microSD) -----------------------------------------
#define TDS3_PIN_MISO         8    // BOARD_SPI_MISO
#define TDS3_PIN_MOSI         17   // BOARD_SPI_MOSI
#define TDS3_PIN_SCK          18   // BOARD_SPI_SCK

// --- TFT (ST7796) -----------------------------------------------------------
#define TDS3_PIN_TFT_CS       39   // BOARD_TFT_CS
#define TDS3_PIN_TFT_RST      47   // BOARD_TFT_RST
#define TDS3_PIN_TFT_DC       9    // BOARD_TFT_DC
#define TDS3_PIN_TFT_BL       48   // BOARD_TFT_BL (backlight, PWM)
#define TDS3_TFT_WIDTH        222  // BOARD_TFT_WIDTH  (native portrait W)
#define TDS3_TFT_HEIGHT       480  // BOARD_TFT_HEIHT  (native portrait H)

// --- Touch (CST226SE, on shared I2C) ----------------------------------------
#define TDS3_PIN_TOUCH_RST    13   // BOARD_TOUCH_RST

// --- Light / proximity sensor (LTR-553ALS-01) -------------------------------
#define TDS3_PIN_SENSOR_IRQ   21   // BOARD_SENSOR_IRQ

// --- microSD ----------------------------------------------------------------
#define TDS3_PIN_SD_CS        14   // BOARD_SD_CS

// --- On-board user buttons --------------------------------------------------
#define TDS3_PIN_BTN_BOOT     0    // BOARD_USER_BUTTON[0]  (Boot / IO0)
#define TDS3_PIN_BTN_IO12     12   // BOARD_USER_BUTTON[1]
#define TDS3_PIN_BTN_IO16     16   // BOARD_USER_BUTTON[2]

// --- Backlight PWM (LovyanGFX Light_PWM) ------------------------------------
#define TDS3_TFT_BL_FREQ      44100
#define TDS3_TFT_BL_CHANNEL   7    // LEDC channel reserved for backlight
