// ============================================================================
//  Claude Usage Monitor  -  LilyGo T-Display S3 Pro
//  Stage 1: bring up the ST7796 display via LovyanGFX and draw a test screen.
// ============================================================================
#include <Arduino.h>

#include "display/DisplayHAL.h"

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[Claude Usage Monitor] boot");

    if (!display::begin()) {
        Serial.println("[display] init FAILED");
        return;
    }
    Serial.println("[display] init OK");

    display::drawTestScreen();
    Serial.println("[display] test screen drawn");
}

void loop() {
    delay(1000);
}
