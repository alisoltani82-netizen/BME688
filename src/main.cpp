/**
 * BME688 + BSEC2 Gas Classification + GC9A01 Round Display
 * ESP32-C3 SuperMini
 *
 * Model: FieldAir_HandSanitizer (Bosch pre-trained 2-class classifier)
 *   GAS_ESTIMATE_1 = Field Air probability [0..1]
 *   GAS_ESTIMATE_2 = Hand Sanitizer probability [0..1]
 *
 * Wiring:
 *   BME688 (Software SPI):
 *     VCC  -> 3.3V
 *     GND  -> GND
 *     SCK  -> GPIO4
 *     SDI  -> GPIO6  (MOSI)
 *     SDO  -> GPIO5  (MISO)
 *     CS   -> GPIO7
 * 
 *   GC9A01 Round Display 240x240 (Hardware SPI):
 *     VCC  -> 3.3V
 *     GND  -> GND
 *     SCL  -> GPIO2  (SPI CLK)
 *     SDA  -> GPIO3  (SPI MOSI)
 *     DC   -> GPIO8
 *     CS   -> GPIO9
 *     RST  -> GPIO10
 */

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <bsec2.h>
#include "bsec_model_config.h"
#include "model_labels.h"

// BME688 Software SPI pins
#define BME_SCK   4
#define BME_MOSI  6
#define BME_MISO  5
#define BME_CS    7

// GC9A01 Display SPI pins
#define TFT_CS    9
#define TFT_DC    8
#define TFT_RST   10
#define TFT_MOSI  3
#define TFT_SCLK  2

#define SEALEVELPRESSURE_HPA (1013.25)

// Screen dimensions
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240
#define CENTER_X      120
#define CENTER_Y      120

// Colors (RGB565)
#define COLOR_BG        0x0000  // Black
#define COLOR_TEXT      0xFFFF  // White
#define COLOR_TEMP      0xFD20  // Orange
#define COLOR_HUMIDITY  0x07FF  // Cyan
#define COLOR_PRESSURE  0xF81F  // Magenta
#define COLOR_GAS       0x07E0  // Green
#define COLOR_IAQ_GOOD  0x07E0  // Green
#define COLOR_IAQ_MOD   0xFFE0  // Yellow
#define COLOR_IAQ_BAD   0xFD20  // Orange
#define COLOR_IAQ_POOR  0xF800  // Red
#define COLOR_RING      0x4208  // Dark gray

// Create instances
SPIClass bmeSpi(FSPI);
Bsec2 bme;
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

bool sensorFound = false;
bool displayFound = true;
bool firstDraw = true;  // Track first display update

// BSEC2 classification output values
float bsecTemp = NAN;
float bsecHumidity = NAN;
float bsecPressure = NAN;
float bsecGasRes = NAN;
float bsecGasEstimate[4] = {NAN, NAN, NAN, NAN};
uint8_t bsecAccuracy = 0;
bool bsecFrameReady = false;
unsigned long callbackCount = 0;

// ── BSEC state persistence (NVS flash) ─────────────────────
#include <Preferences.h>
Preferences prefs;
#define STATE_SAVE_PERIOD_MS  (360UL * 60UL * 1000UL)  // every 6 hours
static uint32_t stateUpdateCounter = 0;

bool loadBsecState() {
    prefs.begin("bsec", true);  // read-only
    size_t len = prefs.getBytesLength("state");
    if (len == BSEC_MAX_STATE_BLOB_SIZE) {
        uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
        prefs.getBytes("state", state, len);
        prefs.end();
        if (bme.setState(state)) {
            Serial.println("BSEC state loaded from NVS");
            return true;
        }
        Serial.println("BSEC setState failed");
        return false;
    }
    prefs.end();
    Serial.println("No saved BSEC state found");
    return true;  // not an error — first boot
}

bool saveBsecState() {
    uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
    if (!bme.getState(state))
        return false;
    prefs.begin("bsec", false);  // read-write
    prefs.putBytes("state", state, BSEC_MAX_STATE_BLOB_SIZE);
    prefs.end();
    Serial.println("BSEC state saved to NVS");
    return true;
}

void updateBsecState() {
    if (!stateUpdateCounter || (stateUpdateCounter * STATE_SAVE_PERIOD_MS) < millis()) {
        saveBsecState();
        stateUpdateCounter++;
    }
}

void checkBsecStatus(const char* stage) {
    if (bme.status < BSEC_OK) {
        Serial.print("BSEC fatal @ ");
        Serial.print(stage);
        Serial.print(": ");
        Serial.println((int)bme.status);
    } else if (bme.status > BSEC_OK) {
        Serial.print("BSEC warning @ ");
        Serial.print(stage);
        Serial.print(": ");
        Serial.println((int)bme.status);
    }

    if (bme.sensor.status < BME68X_OK) {
        Serial.print("BME68x error @ ");
        Serial.print(stage);
        Serial.print(": ");
        Serial.println(bme.sensor.status);
    } else if (bme.sensor.status > BME68X_OK) {
        Serial.print("BME68x warning @ ");
        Serial.print(stage);
        Serial.print(": ");
        Serial.println(bme.sensor.status);
    }
}

void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec) {
    (void)bsec;

    if (!outputs.nOutputs) return;

    callbackCount++;

    for (uint8_t i = 0; i < outputs.nOutputs; i++) {
        const bsecData output = outputs.output[i];
        uint8_t sid = output.sensor_id;

        if (sid == 6 || sid == 14) {          // RAW_TEMPERATURE / HEAT_COMP_TEMPERATURE
            bsecTemp = output.signal;
        } else if (sid == 8 || sid == 12) {   // RAW_HUMIDITY / HEAT_COMP_HUMIDITY
            bsecHumidity = output.signal;
        } else if (sid == 7) {                // RAW_PRESSURE
            bsecPressure = output.signal;
        } else if (sid == 9) {                // RAW_GAS
            bsecGasRes = output.signal;
        } else if (sid == 22) {               // GAS_ESTIMATE_1
            // Only update classification values if accuracy > 0
            // (acc=0 means BSEC missed gas data due to FIFO timing)
            if (output.accuracy > 0) {
                bsecGasEstimate[0] = output.signal;
                bsecAccuracy = output.accuracy;
            }
        } else if (sid == 23) {               // GAS_ESTIMATE_2
            if (output.accuracy > 0)
                bsecGasEstimate[1] = output.signal;
        } else if (sid == 24) {               // GAS_ESTIMATE_3
            if (output.accuracy > 0)
                bsecGasEstimate[2] = output.signal;
        } else if (sid == 25) {               // GAS_ESTIMATE_4
            if (output.accuracy > 0)
                bsecGasEstimate[3] = output.signal;
        }
    }

    bsecFrameReady = true;
    updateBsecState();
}

void displayReadings(float temp, float humidity, float pressure, float altitude, float gasRes) {
    // Find top class from BSEC2 classification
    int topIdx = 0;
    float topProb = 0.0f;
    bool hasData = false;
    for (int i = 0; i < 4; i++) {
        if (!isnan(bsecGasEstimate[i])) {
            hasData = true;
            if (bsecGasEstimate[i] > topProb) {
                topProb = bsecGasEstimate[i];
                topIdx = i;
            }
        }
    }

    // Static screen setup on first draw only
    if (firstDraw) {
        tft.fillScreen(COLOR_BG);

        // Header
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        tft.setTextSize(2);
        tft.setCursor(22, 8);
        tft.print("Gas  Detect");
        tft.drawFastHLine(20, 30, 200, COLOR_RING);

        // Middle divider
        tft.drawFastHLine(20, 142, 200, COLOR_RING);

        // Static labels bottom section
        tft.setTextColor(0x8410, COLOR_BG);
        tft.setTextSize(1);
        tft.setCursor(16, 148); tft.print(kModelLabels[0]);
        tft.setCursor(16, 162); tft.print(kModelLabels[1]);
        tft.setCursor(16, 176); tft.print(kModelLabels[2]);
        tft.setCursor(16, 190); tft.print(kModelLabels[3]);

        firstDraw = false;
    }

    // ── TOP HALF: class name + confidence ──────────────────
    uint16_t classColor = hasData && topProb >= 0.7f ? COLOR_IAQ_GOOD
                        : hasData && topProb >= 0.5f ? COLOR_IAQ_MOD
                        : COLOR_IAQ_BAD;

    // Class name (size 3, ~18px/char, up to ~13 chars)
    tft.setTextColor(classColor, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(22, 38);
    char nameBuf[24];
    snprintf(nameBuf, sizeof(nameBuf), "%-18s", hasData ? kModelLabels[topIdx] : "Warming up...");
    tft.print(nameBuf);

    // Confidence % (size 5 = very large)
    tft.setTextColor(classColor, COLOR_BG);
    tft.setTextSize(5);
    tft.setCursor(52, 62);
    char confBuf[8];
    snprintf(confBuf, sizeof(confBuf), "%3d%%", hasData ? (int)(topProb * 100.0f) : 0);
    tft.print(confBuf);

    // Small env values top-right corner
    tft.setTextColor(COLOR_TEMP, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(140, 112);
    char tbuf[20]; snprintf(tbuf, sizeof(tbuf), "T %5.1fC ", temp);
    tft.print(tbuf);

    tft.setTextColor(COLOR_HUMIDITY, COLOR_BG);
    tft.setCursor(140, 124);
    char hbuf[20]; snprintf(hbuf, sizeof(hbuf), "RH%4.0f%%  ", humidity);
    tft.print(hbuf);

    tft.setTextColor(COLOR_PRESSURE, COLOR_BG);
    tft.setCursor(140, 136);
    char gbuf[20]; snprintf(gbuf, sizeof(gbuf), "G %5.1fk", isnan(gasRes) ? 0.0f : gasRes / 1000.0f);
    tft.print(gbuf);

    // ── BOTTOM: probability bars ────────────────────────────
    const uint16_t BAR_COLORS[4] = {COLOR_IAQ_GOOD, 0x07FF, COLOR_IAQ_MOD, COLOR_IAQ_POOR};
    for (int i = 0; i < 4; i++) {
        int pct = hasData && !isnan(bsecGasEstimate[i]) ? (int)(bsecGasEstimate[i] * 100.0f) : 0;
        uint16_t col = (i == topIdx && hasData) ? BAR_COLORS[i] : COLOR_RING;
        int y = 148 + i * 14;

        // Probability text
        tft.setTextColor(col, COLOR_BG);
        tft.setTextSize(1);
        tft.setCursor(122, y);
        char pbuf[8]; snprintf(pbuf, sizeof(pbuf), "%3d%%", pct);
        tft.print(pbuf);

        // Bar (max 88px)
        int barW = (int)(pct * 0.88f);
        tft.fillRect(152, y, 88, 7, COLOR_BG);
        if (barW > 0) tft.fillRect(152, y, barW, 7, col);
    }

    // ── FOOTER: accuracy + scan count ────────────────────────
    const char* accLabel[] = {"Stabilizing", "Low acc", "Medium acc", "Calibrated!"};
    uint16_t accCol[] = {COLOR_IAQ_MOD, COLOR_IAQ_BAD, COLOR_IAQ_MOD, COLOR_IAQ_GOOD};
    uint8_t acc = bsecAccuracy < 4 ? bsecAccuracy : 0;
    tft.setTextColor(accCol[acc], COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(16, 210);
    char accBuf[30]; snprintf(accBuf, sizeof(accBuf), "BSEC: %-16s", accLabel[acc]);
    tft.print(accBuf);

    // Show scan count + uptime
    tft.setTextColor(COLOR_RING, COLOR_BG);
    tft.setCursor(16, 222);
    char scanBuf[30]; snprintf(scanBuf, sizeof(scanBuf), "Scans:%-5lu  %lus  ", callbackCount, millis()/1000);
    tft.print(scanBuf);
}

void displayError(const char* msg) {
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_IAQ_POOR);
    tft.setTextSize(2);
    tft.setCursor(CENTER_X - 30, CENTER_Y - 30);
    tft.println("ERROR");
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(1);
    tft.setCursor(20, CENTER_Y);
    tft.println(msg);
}

void displayStartup() {
    tft.fillScreen(COLOR_BG);
    
    // Draw decorative circle
    tft.drawCircle(CENTER_X, CENTER_Y, 100, COLOR_RING);
    tft.drawCircle(CENTER_X, CENTER_Y, 95, COLOR_RING);
    
    // Title
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(2);
    tft.setCursor(CENTER_X - 42, CENTER_Y - 30);
    tft.println("BME688");
    
    tft.setTextSize(1);
    tft.setCursor(CENTER_X - 45, CENTER_Y);
    tft.println("Air Quality");
    tft.setCursor(CENTER_X - 35, CENTER_Y + 15);
    tft.println("Monitor");
    
    // Loading indicator
    tft.setTextColor(COLOR_IAQ_MOD);
    tft.setCursor(CENTER_X - 40, CENTER_Y + 40);
    tft.println("Initializing...");
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println();
    Serial.println("=================================");
    Serial.println("BME688 + GC9A01 Round Display");
    Serial.println("ESP32-C3 SuperMini");
    Serial.println("=================================");
    
    // Initialize display
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(COLOR_BG);
    Serial.println("GC9A01 display initialized!");
    
    displayStartup();
    delay(1000);

    // Initialize BME688 with BSEC2 over SPI
    bmeSpi.begin(BME_SCK, BME_MISO, BME_MOSI, BME_CS);
    if (!bme.begin(BME_CS, bmeSpi)) {
        Serial.println("BME688/BSEC init failed! Check wiring.");
        checkBsecStatus("begin");
        displayError("BME688 init failed");
        sensorFound = false;
    } else {
        sensorFound = true;
        Serial.println("BME688 + BSEC initialized");

        {
            // Load the classification model config
            // (auto-copied from BSEC2 library by copy_bsec_config.py pre-build script)
            if (!bme.setConfig(bsec_config)) {
                checkBsecStatus("setConfig");
                char cfgErr[40];
                snprintf(cfgErr, sizeof(cfgErr), "setConfig err %d", (int)bme.status);
                displayError(cfgErr);
                sensorFound = false;
            } else {

            // Restore saved BSEC learning state from NVS (if any)
            loadBsecState();

            // Temperature offset to compensate for sensor self-heating
            bme.setTemperatureOffset(1.0f);

            bsecSensor sensorList[] = {
                BSEC_OUTPUT_RAW_TEMPERATURE,
                BSEC_OUTPUT_RAW_PRESSURE,
                BSEC_OUTPUT_RAW_HUMIDITY,
                BSEC_OUTPUT_RAW_GAS,
                BSEC_OUTPUT_RAW_GAS_INDEX,
                BSEC_OUTPUT_GAS_ESTIMATE_1,
                BSEC_OUTPUT_GAS_ESTIMATE_2,
                BSEC_OUTPUT_GAS_ESTIMATE_3,
                BSEC_OUTPUT_GAS_ESTIMATE_4
            };

            if (!bme.updateSubscription(sensorList, ARRAY_LEN(sensorList), BSEC_SAMPLE_RATE_SCAN)) {
                checkBsecStatus("updateSubscription");
                char subErr[40];
                snprintf(subErr, sizeof(subErr), "Subscribe err %d", (int)bme.status);
                displayError(subErr);
                sensorFound = false;
            } else {
                bme.attachCallback(newDataCallback);
                Serial.print("BSEC version ");
                Serial.print(bme.version.major);
                Serial.print('.');
                Serial.print(bme.version.minor);
                Serial.print('.');
                Serial.print(bme.version.major_bugfix);
                Serial.print('.');
                Serial.println(bme.version.minor_bugfix);
            }
            } // end setConfig else
        }
    }
    
    if (sensorFound) {
        tft.fillScreen(COLOR_BG);
        tft.setTextColor(COLOR_IAQ_GOOD);
        tft.setTextSize(3);
        tft.setCursor(CENTER_X - 45, CENTER_Y - 10);
        tft.println("Ready!");
        delay(1000);
    }
    
    Serial.println();
}

void loop() {
    if (!sensorFound) {
        delay(2000);
        return;
    }

    // Critical: call bme.run() as fast as possible to avoid BME688 FIFO overflow
    // In parallel mode, the 3-field FIFO fills every ~420ms (3 steps × 140ms).
    // If we don't poll fast enough, heater step data is lost.
    if (!bme.run()) {
        checkBsecStatus("run");
    }

    // Update display and serial output at reduced rate (not on every callback)
    static unsigned long lastDisplayUpdate = 0;
    unsigned long now = millis();
    if (bsecFrameReady && (now - lastDisplayUpdate >= 5000)) {
        bsecFrameReady = false;
        lastDisplayUpdate = now;

        float temp = bsecTemp;
        float humidity = bsecHumidity;
        float pressure = bsecPressure;
        float gasRes = bsecGasRes;
        float altitude = 44330.0f * (1.0f - powf((pressure / 100.0f) / SEALEVELPRESSURE_HPA, 0.1903f));

        displayReadings(temp, humidity, pressure, altitude, gasRes);

        // Immediately catch up on FIFO data that accumulated during display draw
        bme.run();

        Serial.print("T:"); Serial.print(temp, 1);
        Serial.print(" H:"); Serial.print(humidity, 0);
        Serial.print(" G:"); Serial.print(gasRes / 1000.0, 1);
        Serial.print("k acc:"); Serial.print(bsecAccuracy);
        for (int i = 0; i < 4; i++) {
            Serial.print(" P"); Serial.print(i+1); Serial.print("=");
            Serial.print(isnan(bsecGasEstimate[i]) ? 0.0f : bsecGasEstimate[i] * 100.0f, 1);
        }
        Serial.println();
    } else if (bsecFrameReady) {
        // Clear flag even if we don't display, to avoid stale state
        bsecFrameReady = false;
    }
}
