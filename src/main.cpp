/**
 * BME688 + GC9A01 Round Display with ESP32-C3 SuperMini
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
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

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
Adafruit_BME680 bme(BME_CS, BME_MOSI, BME_MISO, BME_SCK);
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

bool sensorFound = false;
bool displayFound = true;
bool firstDraw = true;  // Track first display update

// Baseline tracking for air quality estimation
float gasBaseline = 50000.0;
float humidityBaseline = 40.0;
bool baselineCalibrated = false;
unsigned long startTime = 0;
unsigned long sampleCount = 0;
float gasSum = 0;

// Trend tracking (keep last 5 readings for trend detection)
#define TREND_SAMPLES 5
float tempHistory[TREND_SAMPLES] = {0};
float humHistory[TREND_SAMPLES] = {0};
int iaqHistory[TREND_SAMPLES] = {0};
int historyIndex = 0;
bool historyFull = false;

// Previous arc end position for partial redraw
int lastIAQ = -1;

// Calculate IAQ-like score (0-500, lower is better)
int calculateAirQualityIndex(float gasRes, float humidity) {
    float humidityFactor = 1.0;
    if (humidity > 80) {
        humidityFactor = 0.8;
    } else if (humidity < 20) {
        humidityFactor = 0.9;
    }
    
    float ratio = gasBaseline / gasRes;
    
    int iaq;
    if (ratio <= 0.5) {
        iaq = 0;
    } else if (ratio <= 1.0) {
        iaq = (int)((ratio - 0.5) * 100);
    } else if (ratio <= 2.0) {
        iaq = 50 + (int)((ratio - 1.0) * 100);
    } else if (ratio <= 4.0) {
        iaq = 150 + (int)((ratio - 2.0) * 75);
    } else {
        iaq = 300 + (int)((ratio - 4.0) * 50);
    }
    
    iaq = (int)(iaq * humidityFactor);
    if (iaq < 0) iaq = 0;
    if (iaq > 500) iaq = 500;
    
    return iaq;
}

// Air quality rating based on IAQ score
const char* getAirQualityFromIAQ(int iaq) {
    if (iaq <= 50) return "Excellent";
    if (iaq <= 100) return "Good";
    if (iaq <= 150) return "Light";
    if (iaq <= 200) return "Moderate";
    if (iaq <= 250) return "Heavy";
    if (iaq <= 350) return "Severe";
    return "Extreme";
}

// Get color based on IAQ
uint16_t getIAQColor(int iaq) {
    if (iaq <= 50) return COLOR_IAQ_GOOD;
    if (iaq <= 100) return 0x9FE0;  // Light green
    if (iaq <= 150) return COLOR_IAQ_MOD;
    if (iaq <= 200) return COLOR_IAQ_BAD;
    return COLOR_IAQ_POOR;
}

// Estimate CO2 equivalent
int estimateCO2(float gasRes, float humidity) {
    float ratio = gasBaseline / gasRes;
    int co2 = 400 + (int)((ratio - 0.5) * 800);
    if (co2 < 400) co2 = 400;
    if (co2 > 5000) co2 = 5000;
    return co2;
}

// Calculate trend from history (-1 = falling, 0 = stable, 1 = rising)
int getTrend(float* history, int currentIndex, bool isFull) {
    if (!isFull && currentIndex < 2) return 0;
    
    int samples = isFull ? TREND_SAMPLES : currentIndex + 1;
    if (samples < 2) return 0;
    
    // Compare average of first half vs second half
    float firstHalf = 0, secondHalf = 0;
    int halfPoint = samples / 2;
    
    for (int i = 0; i < halfPoint; i++) {
        int idx = (currentIndex - samples + 1 + i + TREND_SAMPLES) % TREND_SAMPLES;
        firstHalf += history[idx];
    }
    for (int i = halfPoint; i < samples; i++) {
        int idx = (currentIndex - samples + 1 + i + TREND_SAMPLES) % TREND_SAMPLES;
        secondHalf += history[idx];
    }
    
    firstHalf /= halfPoint;
    secondHalf /= (samples - halfPoint);
    
    float diff = secondHalf - firstHalf;
    float threshold = (firstHalf + secondHalf) / 2 * 0.02;  // 2% threshold
    
    if (diff > threshold) return 1;   // Rising
    if (diff < -threshold) return -1; // Falling
    return 0;  // Stable
}

int getIAQTrend(int* history, int currentIndex, bool isFull) {
    float floatHistory[TREND_SAMPLES];
    for (int i = 0; i < TREND_SAMPLES; i++) {
        floatHistory[i] = (float)history[i];
    }
    return getTrend(floatHistory, currentIndex, isFull);
}

// Draw trend arrow
void drawTrendArrow(int16_t x, int16_t y, int trend, uint16_t color) {
    tft.setTextColor(color, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(x, y);
    if (trend > 0) {
        // Rising arrow (using characters)
        tft.fillTriangle(x, y+6, x+4, y, x+8, y+6, color);
    } else if (trend < 0) {
        // Falling arrow
        tft.fillTriangle(x, y, x+4, y+6, x+8, y, color);
    } else {
        // Stable (dash)
        tft.fillRect(x, y+2, 8, 3, color);
    }
}

// Optimized arc drawing using filled triangles for thickness
void drawThickArc(int16_t cx, int16_t cy, int16_t outerR, int16_t innerR,
                  float startAngle, float endAngle, uint16_t color) {
    if (endAngle <= startAngle) return;
    
    float step = 0.08;  // Angle step in radians
    float prevAngle = startAngle;
    
    for (float angle = startAngle + step; angle <= endAngle + 0.001; angle += step) {
        float a1 = prevAngle;
        float a2 = (angle > endAngle) ? endAngle : angle;
        
        // Calculate 4 corners of the arc segment
        int16_t x1o = cx + outerR * cos(a1);
        int16_t y1o = cy + outerR * sin(a1);
        int16_t x2o = cx + outerR * cos(a2);
        int16_t y2o = cy + outerR * sin(a2);
        int16_t x1i = cx + innerR * cos(a1);
        int16_t y1i = cy + innerR * sin(a1);
        int16_t x2i = cx + innerR * cos(a2);
        int16_t y2i = cy + innerR * sin(a2);
        
        // Draw two triangles to fill the segment
        tft.fillTriangle(x1o, y1o, x2o, y2o, x1i, y1i, color);
        tft.fillTriangle(x2o, y2o, x2i, y2i, x1i, y1i, color);
        
        prevAngle = a2;
    }
    yield();
}

// Draw the IAQ gauge arc with color gradient
void drawIAQGauge(int iaq) {
    const float START_ANGLE = 2.356;   // 135 degrees (bottom-left)
    const float END_ANGLE = 7.069;     // 405 degrees (bottom-right, going clockwise)
    const int16_t OUTER_R = 118;
    const int16_t INNER_R = 105;
    
    // Only redraw if IAQ changed significantly or first time
    if (lastIAQ >= 0 && abs(iaq - lastIAQ) < 5) return;
    
    // Clear previous arc by drawing background
    if (lastIAQ >= 0) {
        drawThickArc(CENTER_X, CENTER_Y, OUTER_R, INNER_R, START_ANGLE, END_ANGLE, COLOR_RING);
    }
    
    // Calculate the end angle based on IAQ (0-500 maps to full arc)
    float iaqRatio = constrain(iaq, 0, 500) / 500.0;
    float iaqEndAngle = START_ANGLE + iaqRatio * (END_ANGLE - START_ANGLE);
    
    // Draw colored segments based on IAQ zones
    // Excellent (0-50): Green
    float zone1End = START_ANGLE + 0.1 * (END_ANGLE - START_ANGLE);  // 0-50
    float zone2End = START_ANGLE + 0.2 * (END_ANGLE - START_ANGLE);  // 50-100
    float zone3End = START_ANGLE + 0.3 * (END_ANGLE - START_ANGLE);  // 100-150
    float zone4End = START_ANGLE + 0.4 * (END_ANGLE - START_ANGLE);  // 150-200
    float zone5End = START_ANGLE + 0.5 * (END_ANGLE - START_ANGLE);  // 200-250
    
    // Draw background arc (gray)
    drawThickArc(CENTER_X, CENTER_Y, OUTER_R, INNER_R, START_ANGLE, END_ANGLE, COLOR_RING);
    
    // Draw colored portion up to current IAQ
    if (iaqEndAngle > START_ANGLE) {
        // Zone 1: Excellent (Green)
        if (iaqEndAngle > START_ANGLE) {
            float end = min(iaqEndAngle, zone1End);
            drawThickArc(CENTER_X, CENTER_Y, OUTER_R, INNER_R, START_ANGLE, end, COLOR_IAQ_GOOD);
        }
        // Zone 2: Good (Light Green)
        if (iaqEndAngle > zone1End) {
            float end = min(iaqEndAngle, zone2End);
            drawThickArc(CENTER_X, CENTER_Y, OUTER_R, INNER_R, zone1End, end, 0x9FE0);
        }
        // Zone 3: Light (Yellow)
        if (iaqEndAngle > zone2End) {
            float end = min(iaqEndAngle, zone3End);
            drawThickArc(CENTER_X, CENTER_Y, OUTER_R, INNER_R, zone2End, end, COLOR_IAQ_MOD);
        }
        // Zone 4: Moderate (Orange)
        if (iaqEndAngle > zone3End) {
            float end = min(iaqEndAngle, zone4End);
            drawThickArc(CENTER_X, CENTER_Y, OUTER_R, INNER_R, zone3End, end, COLOR_IAQ_BAD);
        }
        // Zone 5+: Poor to Severe (Red)
        if (iaqEndAngle > zone4End) {
            drawThickArc(CENTER_X, CENTER_Y, OUTER_R, INNER_R, zone4End, iaqEndAngle, COLOR_IAQ_POOR);
        }
    }
    
    // Draw tick marks at zone boundaries
    for (int i = 0; i <= 5; i++) {
        float tickAngle = START_ANGLE + (i * 0.1) * (END_ANGLE - START_ANGLE);
        int16_t x1 = CENTER_X + (OUTER_R + 2) * cos(tickAngle);
        int16_t y1 = CENTER_Y + (OUTER_R + 2) * sin(tickAngle);
        int16_t x2 = CENTER_X + (OUTER_R + 6) * cos(tickAngle);
        int16_t y2 = CENTER_Y + (OUTER_R + 6) * sin(tickAngle);
        tft.drawLine(x1, y1, x2, y2, COLOR_TEXT);
    }
    
    lastIAQ = iaq;
}

void displayReadings(float temp, float humidity, float pressure, float altitude, float gasRes) {
    // Update baseline
    sampleCount++;
    gasSum += gasRes;
    unsigned long elapsed = millis() - startTime;
    
    if (elapsed < 300000) {
        gasBaseline = gasSum / sampleCount;
    } else if (!baselineCalibrated) {
        baselineCalibrated = true;
        Serial.println(">>> Baseline calibrated! <<<");
    }
    
    int iaq = calculateAirQualityIndex(gasRes, humidity);
    int co2 = estimateCO2(gasRes, humidity);
    
    // Update history for trend tracking
    tempHistory[historyIndex] = temp;
    humHistory[historyIndex] = humidity;
    iaqHistory[historyIndex] = iaq;
    historyIndex = (historyIndex + 1) % TREND_SAMPLES;
    if (historyIndex == 0) historyFull = true;
    
    // Calculate trends
    int tempTrend = getTrend(tempHistory, (historyIndex - 1 + TREND_SAMPLES) % TREND_SAMPLES, historyFull);
    int humTrend = getTrend(humHistory, (historyIndex - 1 + TREND_SAMPLES) % TREND_SAMPLES, historyFull);
    int iaqTrend = getIAQTrend(iaqHistory, (historyIndex - 1 + TREND_SAMPLES) % TREND_SAMPLES, historyFull);
    
    // On first draw only, set up the screen
    if (firstDraw) {
        tft.fillScreen(COLOR_BG);
        
        // Static label at top
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        tft.setTextSize(1);
        tft.setCursor(CENTER_X - 6, 8);
        tft.print("IAQ");
        
        // Divider line
        tft.drawFastHLine(35, 135, 170, COLOR_RING);
        
        // Static labels for bottom section - reorganized layout
        tft.setTextColor(0x8410, COLOR_BG);  // Gray labels
        tft.setTextSize(1);
        tft.setCursor(40, 142);
        tft.print("CO2");
        tft.setCursor(105, 142);
        tft.print("GAS");
        tft.setCursor(165, 142);
        tft.print("ALT");
        
        tft.setCursor(40, 185);
        tft.print("hPa");
        
        firstDraw = false;
    }
    
    // Draw the IAQ arc gauge (only redraws if IAQ changed significantly)
    drawIAQGauge(iaq);
    
    // IAQ Score - big centered number
    tft.setTextColor(getIAQColor(iaq), COLOR_BG);
    tft.setTextSize(5);
    tft.setCursor(70, 28);
    char iaqBuf[6];
    sprintf(iaqBuf, "%3d", iaq);
    tft.print(iaqBuf);
    
    // IAQ trend arrow next to score
    drawTrendArrow(165, 35, iaqTrend, getIAQColor(iaq));
    
    // Air quality text - centered under the number
    const char* qualityText = getAirQualityFromIAQ(iaq);
    int textWidth = strlen(qualityText) * 12;  // 12 pixels per char at size 2
    int textX = CENTER_X - (textWidth / 2);
    tft.setTextSize(2);
    tft.setCursor(textX, 68);
    // Clear area first, then print centered text
    char qualBuf[12];
    sprintf(qualBuf, "%-9s", qualityText);
    tft.setCursor(60, 68);  // Fixed position for clearing
    tft.print("         ");  // Clear previous text
    tft.setCursor(textX, 68);
    tft.print(qualityText);
    
    // Temperature with trend arrow
    tft.setTextColor(COLOR_TEMP, COLOR_BG);
    tft.setTextSize(3);
    tft.setCursor(28, 95);
    char tempBuf[8];
    sprintf(tempBuf, "%5.1f", temp);
    tft.print(tempBuf);
    tft.setTextSize(2);
    tft.print("C");
    // Temperature trend arrow
    drawTrendArrow(28, 120, tempTrend, COLOR_TEMP);
    
    // Humidity with trend arrow
    tft.setTextColor(COLOR_HUMIDITY, COLOR_BG);
    tft.setTextSize(3);
    tft.setCursor(138, 95);
    char humBuf[6];
    sprintf(humBuf, "%3d", (int)humidity);
    tft.print(humBuf);
    tft.setTextSize(2);
    tft.print("%");
    // Humidity trend arrow
    drawTrendArrow(200, 120, humTrend, COLOR_HUMIDITY);
    
    // CO2 value
    tft.setTextColor(COLOR_GAS, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(30, 152);
    char co2Buf[8];
    sprintf(co2Buf, "%4d", co2);
    tft.print(co2Buf);
    
    // Gas resistance
    tft.setTextColor(COLOR_PRESSURE, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(95, 152);
    char gasBuf[8];
    sprintf(gasBuf, "%5.1f", gasRes / 1000.0);
    tft.print(gasBuf);
    
    // Altitude
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(155, 152);
    char altBuf[6];
    sprintf(altBuf, "%4dm", (int)altitude);
    tft.print(altBuf);
    
    // Pressure (new row)
    tft.setTextColor(COLOR_HUMIDITY, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(65, 175);
    char presBuf[10];
    sprintf(presBuf, "%7.1f", pressure / 100.0);
    tft.print(presBuf);
    
    // Calibration status at very bottom
    tft.setTextSize(1);
    tft.setCursor(CENTER_X - 45, 200);
    if (!baselineCalibrated) {
        unsigned long remaining = (300000 - elapsed) / 1000;
        char calBuf[20];
        sprintf(calBuf, "Calibrating %3lus", remaining);
        tft.setTextColor(COLOR_IAQ_MOD, COLOR_BG);
        tft.print(calBuf);
    } else {
        tft.setTextColor(COLOR_IAQ_GOOD, COLOR_BG);
        tft.print("  Calibrated    ");
    }
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
    
    // Initialize BME688
    if (!bme.begin()) {
        Serial.println("BME688 not found! Check wiring.");
        Serial.println("  SCK  -> GPIO4");
        Serial.println("  SDI  -> GPIO6");
        Serial.println("  SDO  -> GPIO5");
        Serial.println("  CS   -> GPIO7");
        displayError("BME688 not found!\nCheck SPI wiring.");
        sensorFound = false;
    } else {
        Serial.println("BME688 found!");
        sensorFound = true;
        
        // Configure sensor
        bme.setTemperatureOversampling(BME680_OS_8X);
        bme.setHumidityOversampling(BME680_OS_2X);
        bme.setPressureOversampling(BME680_OS_4X);
        bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
        bme.setGasHeater(320, 150);
    }
    
    if (sensorFound) {
        tft.fillScreen(COLOR_BG);
        tft.setTextColor(COLOR_IAQ_GOOD);
        tft.setTextSize(3);
        tft.setCursor(CENTER_X - 45, CENTER_Y - 10);
        tft.println("Ready!");
        delay(1000);
    }
    
    // Initialize start time for baseline calibration
    startTime = millis();
    
    Serial.println();
}

void loop() {
    static int failCount = 0;  // Track consecutive failures
    static bool firstReading = true;  // First reading needs extra time
    
    if (!sensorFound) {
        if (bme.begin()) {
            sensorFound = true;
            bme.setTemperatureOversampling(BME680_OS_8X);
            bme.setHumidityOversampling(BME680_OS_2X);
            bme.setPressureOversampling(BME680_OS_4X);
            bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
            bme.setGasHeater(320, 150);
            firstReading = true;  // Reset first reading flag
        } else {
            delay(5000);
            return;
        }
    }
    
    // Perform reading with retry logic
    unsigned long endTime = bme.beginReading();
    if (endTime == 0) {
        Serial.println("Failed to begin reading!");
        failCount++;
        if (failCount > 5) {
            Serial.println("Too many failures, reinitializing sensor...");
            sensorFound = false;
            failCount = 0;
        }
        delay(1000);
        return;
    }
    
    // Wait for the reading to complete
    // First reading needs extra time for gas heater warmup
    if (firstReading) {
        Serial.println("First reading - waiting for sensor warmup...");
        delay(500);  // Extra warmup time for first reading
    } else {
        delay(150);  // Normal wait time (heater duration is 150ms)
    }
    
    if (!bme.endReading()) {
        Serial.println("Failed to complete reading!");
        failCount++;
        if (failCount > 5) {
            Serial.println("Too many failures, reinitializing sensor...");
            sensorFound = false;
            failCount = 0;
        }
        delay(1000);
        return;
    }
    
    // Get readings
    float temp = bme.temperature;
    float humidity = bme.humidity;
    float pressure = bme.pressure;
    float gasRes = bme.gas_resistance;
    float altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);
    
    // Check for valid readings - but be lenient on gas at startup
    if (isnan(temp) || isnan(humidity)) {
        Serial.println("Invalid sensor data (temp/humidity)!");
        failCount++;
        delay(1000);
        return;
    }
    
    // Gas resistance can be 0 on first few readings - skip display but don't fail
    if (gasRes == 0) {
        Serial.println("Gas sensor warming up...");
        delay(1000);
        return;
    }
    
    // Success! Reset fail counter
    failCount = 0;
    firstReading = false;
    
    // Update display
    displayReadings(temp, humidity, pressure, altitude, gasRes);
    
    // Calculate IAQ and CO2 for serial output
    int iaq = calculateAirQualityIndex(gasRes, humidity);
    int co2 = estimateCO2(gasRes, humidity);
    
    // Print to Serial
    Serial.println("--- Sensor Reading ---");
    Serial.print("Temperature: "); Serial.print(temp); Serial.println(" C");
    Serial.print("Humidity: "); Serial.print(humidity); Serial.println(" %");
    Serial.print("Pressure: "); Serial.print(pressure / 100.0); Serial.println(" hPa");
    Serial.print("Altitude: "); Serial.print(altitude); Serial.println(" m");
    Serial.print("Gas Resistance: "); Serial.print(gasRes / 1000.0); Serial.println(" kOhm");
    Serial.print("IAQ Score: "); Serial.print(iaq); Serial.print(" ("); Serial.print(getAirQualityFromIAQ(iaq)); Serial.println(")");
    Serial.print("Est. CO2: "); Serial.print(co2); Serial.println(" ppm");
    Serial.print("Baseline Calibrated: "); Serial.println(baselineCalibrated ? "Yes" : "No");
    Serial.println();
    
    delay(2000);
}
