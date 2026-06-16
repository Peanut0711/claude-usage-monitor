// ============================================================================
//  LGFX_TDisplayS3Pro.hpp  -  LovyanGFX panel/bus config for the ST7796 LCD
//
//  This file is ONLY the LovyanGFX device description. It is kept separate from
//  the higher-level drawing code (DisplayHAL) so that a future LVGL layer can
//  reuse the same LGFX device for its flush callback without touching UI code.
//
//  Bus/panel/backlight values verified against the board-specific LovyanGFX
//  configuration: https://github.com/lovyan03/LovyanGFX/discussions/674
//  Pin numbers come from board_pins.h (official LilyGo utilities.h).
// ============================================================================
#pragma once

#ifndef LGFX_USE_V1
#define LGFX_USE_V1     // also set globally in platformio.ini build_flags
#endif
#include <LovyanGFX.hpp>
#include "../board_pins.h"

class LGFX_TDisplayS3Pro : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;

public:
    LGFX_TDisplayS3Pro() {
        // ---- SPI bus -------------------------------------------------------
        {
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;   // 40 MHz write
            cfg.freq_read   = 16000000;   // 16 MHz read
            cfg.spi_3wire   = true;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = TDS3_PIN_SCK;
            cfg.pin_mosi    = TDS3_PIN_MOSI;
            cfg.pin_miso    = TDS3_PIN_MISO;
            cfg.pin_dc      = TDS3_PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        // ---- Panel (ST7796) ------------------------------------------------
        {
            auto cfg = _panel.config();
            cfg.pin_cs        = TDS3_PIN_TFT_CS;
            cfg.pin_rst       = TDS3_PIN_TFT_RST;
            cfg.pin_busy      = -1;

            // ST7796 GRAM is 320x480; the visible glass is 222 wide, so the
            // active area is offset by (320-222)/2 = 49 px in native rotation.
            cfg.memory_width  = 320;
            cfg.memory_height = 480;
            cfg.panel_width   = TDS3_TFT_WIDTH;   // 222
            cfg.panel_height  = TDS3_TFT_HEIGHT;  // 480
            cfg.offset_x      = 49;
            cfg.offset_y      = 0;
            cfg.offset_rotation = 0;

            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable      = false;  // we never read back from the panel
            cfg.invert        = true;   // IPS panel requires inversion
            cfg.rgb_order     = false;  // RGB
            cfg.dlen_16bit    = false;
            cfg.bus_shared    = true;   // SPI bus shared with the SD card
            _panel.config(cfg);
        }

        // ---- Backlight (PWM) ----------------------------------------------
        {
            auto cfg = _light.config();
            cfg.pin_bl      = TDS3_PIN_TFT_BL;
            cfg.invert      = false;
            cfg.freq        = TDS3_TFT_BL_FREQ;
            cfg.pwm_channel = TDS3_TFT_BL_CHANNEL;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        setPanel(&_panel);
    }
};
