#include <Arduino.h>
#include <FastLED.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_INA219.h>
#include <U8g2lib.h>
#include <ESP32Encoder.h>
#include <Preferences.h>

#include "fastled.h"

// --- Logo Bitmap ---
// 'favicon-32x32, 32x32px
const unsigned char logo_bitmap_32x32[] PROGMEM = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xdf, 0xff, 0xff, 0xff, 0x6f, 0xff, 0xff, 
	0xff, 0xf7, 0xfe, 0xff, 0xff, 0xfb, 0xfd, 0xff, 0xff, 0xfb, 0xf8, 0xff, 0xff, 0xfb, 0xf7, 0xff, 
	0xff, 0x7b, 0xf5, 0xff, 0xff, 0xfb, 0xfa, 0xff, 0xff, 0xfb, 0xfe, 0xff, 0xcf, 0xf3, 0x7d, 0xfe, 
	0xaf, 0xeb, 0xbb, 0xfd, 0x77, 0xf7, 0x77, 0xf9, 0xf7, 0xd6, 0x77, 0xf6, 0xf7, 0xac, 0x6f, 0xcd, 
	0xef, 0xae, 0x9f, 0xdb, 0xef, 0x5c, 0x5f, 0xd7, 0xdf, 0x7e, 0xbf, 0xee, 0x3f, 0xbe, 0xbe, 0xf9, 
	0xff, 0x7e, 0xff, 0xff, 0xff, 0x7e, 0xbe, 0xff, 0xff, 0xff, 0xbe, 0xff, 0xff, 0x6f, 0xbf, 0xff, 
	0xff, 0xa7, 0xbf, 0xff, 0xff, 0xdf, 0xdf, 0xff, 0xff, 0xbf, 0xef, 0xff, 0xff, 0x7f, 0xf7, 0xff, 
	0xff, 0xff, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
// --- End Logo Bitmap ---

// --- Program Info ---
#define PROGRAM_VERSION "1.0.1"
// --- End Program Info ---

#define SDA_PIN 14
#define SCL_PIN 13
#define USER_BUTTON_PIN 33
#ifndef LED1_PIN // Add default if not defined by build flag (for linter)
  #define LED1_PIN 21
#endif
#define LED2_PIN 22
#define LED3_PIN 19
#define LED4_PIN 23
#define ROT_ENC_A_PIN 26
#define ROT_ENC_B_PIN 27
#define ROT_ENC_BUTTON_PIN 18
#define BOOT_BUTTON_PIN 0

#ifndef MAX_LEDS // Max buffer size, defined by build flag or default (linter)
  #define MAX_LEDS 1000 // Increased buffer size
#endif
CRGB leds[MAX_LEDS];
BH1750 lightMeter; // Default address 0x23
Adafruit_INA219 ina219; // Default address 0x40
int numLedsConfigured = MAX_LEDS; // Active LED count, default to max
int numLedsProposed = MAX_LEDS;   // Temp variable for selection screen

// U8g2 Display Setup (using Hardware I2C)
// Choose constructor based on display: https://github.com/olikraus/u8g2/wiki/u8g2setupcpp
// SSD1306 128x32, HW I2C, No Reset pin
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
bool displayAvailable = false; // Flag to track if display is detected

// --- State Management Structs ---
struct InputState {
    // Rotary Encoder - REMOVED, using ESP32Encoder library
    // volatile long encoderValue = 0;
    // int lastEncoded = 0;
    // long lastEncoderValueReported = 0;
    // Rotary Encoder Button
    int rotButtonState = HIGH;
    int rotLastButtonState = HIGH;
    unsigned long rotLastDebounceTime = 0;
    bool rotButtonPressedEvent = false; 
    // User Button (GPIO 33)
    int userButtonState = HIGH;
    int userLastButtonState = HIGH;
    unsigned long userLastDebounceTime = 0;
    bool userButtonPressedEvent = false;
    // Boot Button (GPIO 0)
    int bootButtonState = HIGH;
    int bootLastButtonState = HIGH;
    unsigned long bootLastDebounceTime = 0;
    bool bootButtonPressedEvent = false;
};

struct LedPwmState {
    bool isOn = false;
    int brightness = 50; // 0-255
};

struct FastLedState {
    bool isOn = true; // Default FastLED strip to on
    int brightness = 30; // Default brightness set to 30
    uint8_t hue = 0; // For animation state
};

InputState inputState;
LedPwmState led2State, led3State, led4State;
FastLedState fastLedState;

// long encoderAccumulator = 0; // REMOVED, using ESP32Encoder library
ESP32Encoder encoder; // Encoder library object
long lastEncoderCount = 0; // Track last count from encoder library
Preferences preferences; // Preferences object for NVM storage

// Define Chipset Types
const int CHIPSET_TYPE_WS2812 = 0;
const int CHIPSET_TYPE_SK6812 = 1;
const int NUM_CHIPSET_TYPES = 2; // Total number of options
const char* chipsetNames[] = {"WS2812", "SK6812"};
int savedChipsetType = CHIPSET_TYPE_WS2812; // Variable to hold loaded/saved type, default WS2812
int chipsetSelectionProposed = 0; // Temp variable for selection screen

// Define FastLED Patterns
enum FastLedPattern {
  RAINBOW,
  RGB_CHECKER,
  CHASE // Added Chase pattern
};
const int NUM_FASTLED_PATTERNS = 3; // Increased count
const char* patternNames[] = {"Rainbow", "RGB Check", "Chase"}; // Added Chase
FastLedPattern currentFastLedPattern = RAINBOW; // Default pattern
int patternSelectionProposed = 0; // Temp variable for pattern selection screen

// State for RGB Checker Pattern
int rgbCheckerStage = 0; // 0=Red, 1=Green, 2=Blue
int rgbCheckerPulseCount = 0;
unsigned long lastRgbCheckerActionTime = 0;
const int pulseOnTime = 500; // ms
const int pulseOffTime = 250; // ms
const int interColorDelay = 600; // ms
bool rgbCheckerLedState = false; // false = off, true = on

// State for Chase Pattern
// int chasePixelIndex = 0; // REMOVED - Not used in wave logic
// int chaseBrightness = 0; // REMOVED - Not used in wave logic
// bool chaseFadingUp = true; // REMOVED - Not used in wave logic
unsigned long lastChaseUpdateTime = 0;
const int chaseStepDelay = 2; // ms between position updates (was 10)
float chasePosition = 0.0;    // Floating point position for smooth movement
const int chaseFadeRate = 100; // Brightness decrease per pixel distance (Higher = narrower wave)

int lastI2cDeviceCount = -1; // Store result of last I2C scan (-1 if not scanned)

unsigned long lastInteractionTime = 0; // Track time of last user interaction
const unsigned long idleTimeoutDuration = 60000; // 1 minute in milliseconds

// --- Restore Deleted Declarations --- 
// --- End State Management Structs ---

// --- UI State Machine ---
enum UIState {
  MENU,
  ACTION,
  SPLASH, // State for idle splash screen
  STARTUP_SPLASH // State for initial splash screen wait
};

enum AppMode {
  FASTLED_TEST, // Moved to first
  LED2_MODE,
  LED3_MODE,
  LED4_MODE,
  LIGHT_SENSOR,
  INA219_SENSOR,
  I2C_SCANNER,
  LED_CHIPSET_SELECT, // Added mode for selecting LED chipset
  FASTLED_PATTERN, // Added mode for selecting FastLED pattern
  LED_COUNT_SELECT, // Added mode for selecting LED count
  ESP_INFO // Moved to last
};

const char* modeNames[] = {
  "FastLED Test", // Updated order
  "LED2 Brightness",
  "LED3 Brightness",
  "LED4 Brightness",
  "BH1750 Sensor", 
  "INA219 Sensor", 
  "I2C Scanner",
  "LED Chipset", // Name for the new mode
  "LED Pattern", // Name for the new mode
  "LED Count",   // Name for the LED count mode
  "ESP Info" // Updated order
};
const int numModes = sizeof(modeNames) / sizeof(modeNames[0]);

UIState currentState = MENU;
AppMode currentMode = FASTLED_TEST; // Default mode changed to FastLED Test
int menuSelection = 0; // Index of the currently selected mode in the menu (default 0 is now FastLED)

// const int pulsesPerMenuStep = 4; // REMOVED, using ESP32Encoder library
// --- End UI State Machine ---

// PWM Configuration
const int pwmFreq = 5000; // 5 kHz frequency
const int pwmResolution = 8; // 8-bit resolution (0-255)
const int ledcChannel2 = 0;
const int ledcChannel3 = 1;
const int ledcChannel4 = 2;

// Rotary Encoder State
// volatile long encoderValue = 0; // Removed
// int lastEncoded = 0; // Removed
// long lastencoderValue = 0; // Removed

// Button State
// int buttonState = HIGH;             // Removed
// int lastButtonState = HIGH;         // Removed
// unsigned long lastDebounceTime = 0; // Removed
unsigned long debounceDelay = 50;    // debounce time; increase if bouncing seen

// Light Sensor Timer
unsigned long lastSensorReadTime = 0; // Renamed from lastLightReadTime
const unsigned long sensorReadInterval = 2000; // 2 seconds
float lastLuxValue = -1.0; // Store last sensor value for display
float lastBusVoltage = 0.0, lastCurrentMa = 0.0; // Store last INA values for display
// --- End Restore Deleted Declarations ---

// --- Helper Functions ---
void printEspInfo() {
    Serial.println("--- ESP Chip Info ---");
    Serial.printf("Chip ID: %04X", (uint16_t)(ESP.getEfuseMac()>>32));
    Serial.printf("%08X\n", (uint32_t)ESP.getEfuseMac());
    Serial.printf("Chip Revision: %d\n", ESP.getChipRevision());
    Serial.printf("CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.println("---------------------");
}

void readAndPrintLightSensor() {
    if (millis() - lastSensorReadTime >= sensorReadInterval) {
        lastLuxValue = lightMeter.readLightLevel(); // Store value
        if (lastLuxValue < 0) { Serial.println(F("Error reading BH1750")); }
        else { Serial.print("Light: "); Serial.print(lastLuxValue); Serial.println(" lx"); }
        lastSensorReadTime = millis();
    }
}

void readAndPrintIna219() {
    if (millis() - lastSensorReadTime >= sensorReadInterval) {
        lastBusVoltage = ina219.getBusVoltage_V(); // Store for display
        float shuntvoltage = ina219.getShuntVoltage_mV();  // in mV
        lastCurrentMa = shuntvoltage * 100.0; // Store for display (crude calculation for example)
        float loadvoltage = lastBusVoltage + (shuntvoltage / 1000.0); // in V
        float power_mW = lastBusVoltage * lastCurrentMa;          // V * mA = mW

        // Format Bus Voltage (already in V)
        Serial.print("Bus: ");
        Serial.print(lastBusVoltage, 2);
        Serial.print("V");

        // Format Shunt Voltage (always in mV for INA219)
        Serial.print(" | Shunt: ");
        Serial.print(shuntvoltage, 2);
        Serial.print("mV");

        // Format Load Voltage (already in V)
        Serial.print(" | Load: ");
        Serial.print(loadvoltage, 2);
        Serial.print("V");

        // Format Current with adaptive units (mA or A)
        Serial.print(" | Current: ");
        if (abs(lastCurrentMa) >= 500.0) {
            Serial.print(lastCurrentMa / 1000.0, 2); // Convert to A with 2 decimal places
            Serial.print("A");
        } else {
            Serial.print(lastCurrentMa, 1); // Keep as mA with 1 decimal place
            Serial.print("mA");
        }

        // Format Power with adaptive units (mW or W)
        Serial.print(" | Power: ");
        if (abs(power_mW) >= 500.0) {
            Serial.print(power_mW / 1000.0, 2); // Convert to W with 2 decimal places
            Serial.print("W");
        } else {
            Serial.print(power_mW, 1); // Keep as mW with 1 decimal place
            Serial.print("mW");
        }

        Serial.println();
        lastSensorReadTime = millis();
    }
}

int scanI2CBus() {
  byte error, address;
  int nDevices;

  Serial.println("Scanning I2C bus...");

  nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.println(" !");
      nDevices++;
    } else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }    
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("Scan complete.\n");

  return nDevices;
}

// Function to play startup static animation
void playStaticAnimation() {
    if (!displayAvailable) return;

    Serial.println("Playing static animation...");
    unsigned long startTime = millis();
    while (millis() - startTime < 2000) { // Animate for 2 seconds
        u8g2.clearBuffer();
        for (int i = 0; i < 100; i++) { // Draw 100 random pixels per frame
            u8g2.drawPixel(random(u8g2.getDisplayWidth()), random(u8g2.getDisplayHeight()));
        }
        u8g2.sendBuffer();
        delay(30); // Frame delay
    }
    Serial.println("Static animation complete.");
}

// Function to display the splash screen
void displaySplashScreen() {
    if (!displayAvailable) return;

    u8g2.clearBuffer();
    // Draw logo
    u8g2.drawXBM(0, 0, 32, 32, logo_bitmap_32x32);
    
    // Set font for text next to logo
    u8g2.setFont(u8g2_font_helvB12_tr); // Use Helvetica Bold 12px sans-serif font
    int textX = 32 + 4; // X position for text (logo width + padding)
    // Font Height approx 12px for helvB12
    int textY1 = 14;     // Lowered Y for first line (start at pixel 12)
    int textY2 = textY1 + 12 + 2; // Y for second line (font height + padding)
    u8g2.drawStr(textX, textY1, "Solid");
    u8g2.drawStr(textX, textY2, "Difference");
    
    u8g2.sendBuffer(); // Send splash screen to display
}

// --- End Helper Functions ---

// --- Forward Declarations for Pattern Functions ---
void runRainbowPattern();
void runRgbCheckerPattern();
void runChasePattern();
// --- End Forward Declarations ---

void setup() {
  Serial.begin(115200);
  delay(1000); // Wait for serial monitor
  Serial.println("\n\n--- Solid Difference Clock Controller Test Program ---");
  Serial.print("Version: "); Serial.println(PROGRAM_VERSION);
  Serial.print("Build Date: "); Serial.print(__DATE__); Serial.print(" "); Serial.println(__TIME__);
  Serial.println("-------------------------------------------------");
  Serial.println("Setup starting...");

  // --- Read Saved Settings Early ---
  preferences.begin("led-config", false); // Open NVM namespace, read-write
  // Read saved chipset type, default to WS2812 (0) if not found
  savedChipsetType = preferences.getInt("chipset", CHIPSET_TYPE_WS2812);
  preferences.end(); // Close preferences for now
  Serial.print("Saved Chipset Type loaded: "); Serial.println(savedChipsetType == CHIPSET_TYPE_SK6812 ? "SK6812" : "WS2812");

  // Read saved LED count, default to MAX_LEDS if not found or invalid
  preferences.begin("led-config", true); // Open read-only first to check
  numLedsConfigured = preferences.getInt("ledCount", MAX_LEDS);
  preferences.end();
  // Validate loaded value
  if (numLedsConfigured <= 0 || numLedsConfigured > MAX_LEDS) {
    Serial.print("Invalid saved LED count ("); Serial.print(numLedsConfigured); Serial.println("), defaulting to MAX_LEDS.");
    numLedsConfigured = MAX_LEDS;
    // Optionally: Save the corrected default value back?
    // preferences.begin("led-config", false); 
    // preferences.putInt("ledCount", numLedsConfigured);
    // preferences.end();
  }
  Serial.print("Saved LED Count loaded: "); Serial.println(numLedsConfigured);

  // --- Initialize I2C First ---
  Wire.begin(SDA_PIN, SCL_PIN);

  // --- Initialize Display Early for Splash ---
  displayAvailable = u8g2.begin(); 
  if (displayAvailable) {
      Serial.println("SSD1306 Display Initialized (Found at 0x3C)");
  } else {
      Serial.println("SSD1306 Display not found at 0x3C.");
  }

  // --- Initialize Input Pins ---
  pinMode(ROT_ENC_A_PIN, INPUT);
  pinMode(ROT_ENC_B_PIN, INPUT);
  pinMode(ROT_ENC_BUTTON_PIN, INPUT_PULLUP); // Encoder button often needs pullup
  pinMode(USER_BUTTON_PIN, INPUT_PULLUP); 
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP); 

  // --- Initialize Encoder Library ---
  // Pullups enabled by default
  encoder.attachHalfQuad(ROT_ENC_A_PIN, ROT_ENC_B_PIN);
  encoder.setCount(0); 
  lastEncoderCount = 0;
  Serial.println("ESP32Encoder Initialized.");

  // --- Display Initial Animation & Splash Screen ---
  if (displayAvailable) {
      playStaticAnimation(); // Play static first
      displaySplashScreen(); // Then show the logo/text splash screen
      Serial.println("Showing startup splash screen...");
  }
  currentState = STARTUP_SPLASH; // Set initial state

  // --- Initialize Sensors ---
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println(F("BH1750 Initialized"));
  } else {
    Serial.println(F("Error initialising BH1750"));
  }
  if (ina219.begin()) {
    Serial.println(F("INA219 Initialized"));
  } else {
    Serial.println(F("Error initialising INA219"));
  }
  // --- End Sensor Init ---

  // --- Initialize FastLED ---
  Serial.print("Configuring FastLED for type: ");
  if (savedChipsetType == CHIPSET_TYPE_WS2812) {
    Serial.println("WS2812 (GRB)");
    FastLED.addLeds<WS2812, LED1_PIN, GRB>(leds, MAX_LEDS);
  } else if (savedChipsetType == CHIPSET_TYPE_SK6812) {
    Serial.println("SK6812 (RGB)");
    // Note: Add RGBW logic here if needed based on another preference
    FastLED.addLeds<SK6812, LED1_PIN, RGB>(leds, MAX_LEDS); 
  } else {
    // Fallback / Error case
    Serial.println("Unknown type! Defaulting to WS2812 (GRB)");
    FastLED.addLeds<WS2812, LED1_PIN, GRB>(leds, MAX_LEDS);
  }

  FastLED.setBrightness(fastLedState.brightness); // Use initial brightness from struct
  if (fastLedState.isOn) {
     // Optional: Set an initial pattern if needed, otherwise it starts black
     // fill_solid(leds, NUM_LEDS, CRGB::Black); 
     FastLED.show();
  } else {
     FastLED.clear();
     FastLED.show();
  }
  // --- End FastLED Init ---

  // --- Initialize PWM LEDs ---
  ledcSetup(ledcChannel2, pwmFreq, pwmResolution);
  ledcSetup(ledcChannel3, pwmFreq, pwmResolution);
  ledcSetup(ledcChannel4, pwmFreq, pwmResolution);
  ledcAttachPin(LED2_PIN, ledcChannel2);
  ledcAttachPin(LED3_PIN, ledcChannel3);
  ledcAttachPin(LED4_PIN, ledcChannel4);
  ledcWrite(ledcChannel2, led2State.isOn ? led2State.brightness : 0);
  ledcWrite(ledcChannel3, led3State.isOn ? led3State.brightness : 0);
  ledcWrite(ledcChannel4, led4State.isOn ? led4State.brightness : 0);
  // --- End PWM Init ---

  // Set initial state AFTER splash wait - REMOVED, set earlier
  // currentState = MENU; 
  // lastInteractionTime = millis(); // Start interaction timer

  Serial.println("Setup complete. Entering main loop.");

  // Initial display update for the menu - REMOVED, handled by state machine
  /*
  if (displayAvailable) {
      u8g2.clearBuffer(); 
      u8g2.setFont(u8g2_font_profont22_tf); 
      updateDisplay(); // Show initial menu state
  }
  */
}

// --- Display Update Function ---
void updateDisplay() {
    if (!displayAvailable || currentState == SPLASH || currentState == STARTUP_SPLASH) return; // Skip if splash

    u8g2.clearBuffer(); // Clear previous frame

    // Determine positioning
    u8g2.setFont(u8g2_font_profont22_tf); // Ensure font is set to Profont22 for main UI
    const int lineHeight = 16; // 14px font height + 2px padding
    const int line1Y = 14;     // Adjusted Y for 1st line (starts at pixel 14)
    const int line2Y = line1Y + lineHeight; // Adjusted Y for 2nd line

    char buffer[32]; // Buffer for formatting strings

    if (currentState == MENU) {
        // --- New Menu Formatting --- 
        const char* fullName = modeNames[menuSelection];
        char part1[20]; // Buffer for first part of name
        char part2[20]; // Buffer for second part of name
        part1[0] = '\0'; // Initialize empty
        part2[0] = '\0';

        const char* lastSpace = strrchr(fullName, ' ');
        int part1Len = strlen(fullName);

        // Try splitting at the last space if it exists and isn't too close to the start
        if (lastSpace != nullptr && (lastSpace - fullName > 2)) { 
            part1Len = lastSpace - fullName;
            strncpy(part1, fullName, part1Len);
            part1[part1Len] = '\0'; // Null-terminate
            strncpy(part2, lastSpace + 1, sizeof(part2) - 1);
            part2[sizeof(part2) - 1] = '\0'; // Ensure null-termination
        } else {
            // No suitable space found, put whole name on line 1
            strncpy(part1, fullName, sizeof(part1) - 1);
            part1[sizeof(part1) - 1] = '\0'; // Ensure null-termination
            // part2 remains empty
        }

        // Line 1: Format Index and Part 1
        snprintf(buffer, sizeof(buffer), "%d: %s", menuSelection, part1);
        u8g2.drawStr(0, line1Y, buffer);

        // Line 2: Format indented Part 2 (if it exists)
        if (part2[0] != '\0') {
            snprintf(buffer, sizeof(buffer), "   %s", part2);
            u8g2.drawStr(0, line2Y, buffer);
        }
        // --- End New Menu Formatting ---

        /* // Old Menu Formatting Removed
        // Line 1: Show selected mode with '>'
        snprintf(buffer, sizeof(buffer), "> %s", modeNames[menuSelection]);
        u8g2.drawStr(0, line1Y, buffer);

        // Line 2: Show index
        snprintf(buffer, sizeof(buffer), "(%d/%d)", menuSelection + 1, numModes);
        u8g2.drawStr(0, line2Y, buffer);
        */

    } else if (currentState == ACTION) {
        // --- Restore Original Action State Font & Layout ---
        u8g2.setFont(u8g2_font_profont22_tf); // Restore Profont22
        // Use the original line Y positions defined earlier (line 480-482)
        // const int lineHeight = 16;
        // const int line1Y = 14;
        // const int line2Y = line1Y + lineHeight;

        // Line 1: Show current mode name (using original font/pos)
        u8g2.drawStr(0, line1Y, modeNames[currentMode]);

        // Line 2: Show mode-specific info (using original font/pos)
        switch (currentMode) {
            case ESP_INFO:
                {
                    // Format compact ESP info
                    int cpuFreq = ESP.getCpuFreqMHz();
                    int flashSize = ESP.getFlashChipSize() / (1024 * 1024);
                    snprintf(buffer, sizeof(buffer), "%dMHz/%dMB", cpuFreq, flashSize);
                }
                u8g2.drawStr(0, line2Y, buffer);
                break;
            case I2C_SCANNER:
                // Show scan result
                if (lastI2cDeviceCount >= 0) {
                    snprintf(buffer, sizeof(buffer), "Devices: %d", lastI2cDeviceCount);
                } else {
                    snprintf(buffer, sizeof(buffer), "Devices: --"); // Should not happen if scanned on entry
                }
                u8g2.drawStr(0, line2Y, buffer);
                break;
            case FASTLED_TEST:
                snprintf(buffer, sizeof(buffer), "Bright: %d", fastLedState.brightness);
                u8g2.drawStr(0, line2Y, buffer);
                break;
            case LED2_MODE:
                snprintf(buffer, sizeof(buffer), "Bright: %d", led2State.brightness);
                u8g2.drawStr(0, line2Y, buffer);
                break;
            case LED3_MODE:
                snprintf(buffer, sizeof(buffer), "Bright: %d", led3State.brightness);
                u8g2.drawStr(0, line2Y, buffer);
                break;
            case LED4_MODE:
                snprintf(buffer, sizeof(buffer), "Bright: %d", led4State.brightness);
                u8g2.drawStr(0, line2Y, buffer);
                break;
            case LIGHT_SENSOR:
                if (lastLuxValue >= 0) {
                     snprintf(buffer, sizeof(buffer), "Lux: %.0f", lastLuxValue);
                } else {
                     snprintf(buffer, sizeof(buffer), "Reading...");
                }
                u8g2.drawStr(0, line2Y, buffer);
                break;
            case INA219_SENSOR:
                 {
                     // Check for zero/invalid values
                     bool voltageValid = !(abs(lastBusVoltage) < 0.01);
                     bool currentValid = !(abs(lastCurrentMa) < 0.1); // Use a small threshold for float comparison

                     char voltageStr[8];
                     char currentStr[10];

                     // Format Voltage or "--"
                     if (voltageValid) {
                         snprintf(voltageStr, sizeof(voltageStr), "%.1fV", lastBusVoltage);
                     } else {
                         snprintf(voltageStr, sizeof(voltageStr), "-- V");
                     }

                     // Format Current (mA or A) or "--"
                     if (currentValid) {
                         if (abs(lastCurrentMa) >= 1000.0) {
                             snprintf(currentStr, sizeof(currentStr), "%.1fA", lastCurrentMa / 1000.0);
                         } else {
                             snprintf(currentStr, sizeof(currentStr), "%.0fmA", lastCurrentMa);
                         }
                     } else {
                         snprintf(currentStr, sizeof(currentStr), "-- mA");
                     }

                     // Combine strings
                     snprintf(buffer, sizeof(buffer), "%s %s", voltageStr, currentStr);
                 }
                 u8g2.drawStr(0, line2Y, buffer);
                 break;
            case LED_CHIPSET_SELECT:
                u8g2.clearBuffer(); 
                // Show the proposed chipset name with indicator '>'
                snprintf(buffer, sizeof(buffer), "> %s", chipsetNames[chipsetSelectionProposed]);
                u8g2.drawStr(0, line1Y, buffer); // Use Line 1 for the proposed item
                u8g2.drawStr(0, line2Y, "Press btn->Save"); // Use Line 2 for instruction
                break;
            case FASTLED_PATTERN:
                u8g2.clearBuffer(); 
                // Show the proposed pattern name with indicator '>'
                snprintf(buffer, sizeof(buffer), "> %s", patternNames[patternSelectionProposed]);
                u8g2.drawStr(0, line1Y, buffer); // Use Line 1 for the proposed item
                u8g2.drawStr(0, line2Y, "Press btn->Set"); // Use Line 2 for instruction
                break;
            case LED_COUNT_SELECT:
                u8g2.clearBuffer(); // Add buffer clear
                // Show the proposed LED count
                snprintf(buffer, sizeof(buffer), "Count: %d", numLedsProposed);
                u8g2.drawStr(0, line1Y, buffer); // Use Line 1 for count
                u8g2.drawStr(0, line2Y, "Press btn->Save"); // Use Line 2 for instruction
                break;
        }
    }

    u8g2.sendBuffer(); // Send buffer to display
}
// --- End Display Update Function ---

void loop() {
    bool stateChanged = false; // Declare at the start of the loop scope
    bool interactionDetected = false; // Flag to track if interaction occurred this loop

    // --- 1. Read Inputs (Physical & Serial) ---

    // --- Read Encoder using Library ---
    long currentCount = encoder.getCount();
    long encoderChange = currentCount - lastEncoderCount;
    lastEncoderCount = currentCount;
    // Optional: Reset count periodically if it gets too large?
    // if (abs(currentCount) > 10000) { encoder.clearCount(); lastEncoderCount = 0; }

    if (encoderChange != 0) { interactionDetected = true; } // Mark interaction on encoder change

    // --- Debug: Print encoderChange ---
    // if (encoderChange != 0) { Serial.printf("Encoder Change: %ld\n", encoderChange); }
    // --- End Debug ---

    // --- Read Physical Buttons (Keep Debounce Logic) ---
    // Debounce Physical Rotary Button
    bool physicalRotButtonPressedEvent = false;
    int rotButtonReading = digitalRead(ROT_ENC_BUTTON_PIN);
    if (rotButtonReading != inputState.rotLastButtonState) {
        inputState.rotLastDebounceTime = millis();
    }
    if ((millis() - inputState.rotLastDebounceTime) > debounceDelay) {
        if (rotButtonReading != inputState.rotButtonState) {
            inputState.rotButtonState = rotButtonReading;
            if (inputState.rotButtonState == LOW) {
                physicalRotButtonPressedEvent = true;
                interactionDetected = true; // Mark interaction
                Serial.println("Physical Rot Button Pressed"); // Debug
            }
        }
    }
    inputState.rotLastButtonState = rotButtonReading;

    // Read User Button
    int userButtonReading = digitalRead(USER_BUTTON_PIN);
    if (userButtonReading != inputState.userLastButtonState) { inputState.userLastDebounceTime = millis(); }
    if ((millis() - inputState.userLastDebounceTime) > debounceDelay) {
        if (userButtonReading != inputState.userButtonState) {
            inputState.userButtonState = userButtonReading;
            if (inputState.userButtonState == LOW) { 
                inputState.userButtonPressedEvent = true; // Set flag
                interactionDetected = true; // Mark interaction
            }
        }
    }
    inputState.userLastButtonState = userButtonReading;

    // Read Boot Button
    int bootButtonReading = digitalRead(BOOT_BUTTON_PIN);
    if (bootButtonReading != inputState.bootLastButtonState) { inputState.bootLastDebounceTime = millis(); }
    if ((millis() - inputState.bootLastDebounceTime) > debounceDelay) {
        if (bootButtonReading != inputState.bootButtonState) {
            inputState.bootButtonState = bootButtonReading;
            if (inputState.bootButtonState == LOW) { 
                inputState.bootButtonPressedEvent = true; // Set flag
                interactionDetected = true; // Mark interaction
            }
        }
    }
    inputState.bootLastButtonState = bootButtonReading;
    // --- End Button Reading ---


    // --- Initialize loop variables based on physical inputs ---
    // Use encoderChange directly from library, BUT REVERSED
    long encoderChangeSteps = -encoderChange; // Reverse direction

    bool rotButtonPressedEvent = physicalRotButtonPressedEvent; // Start with physical press

    // Consume physical button events flags
    bool userButtonPressedEvent = inputState.userButtonPressedEvent;
    bool bootButtonPressedEvent = inputState.bootButtonPressedEvent;
    inputState.userButtonPressedEvent = false; 
    inputState.bootButtonPressedEvent = false;


    // Process Serial Input (can override or add to physical input)
    if (Serial.available() > 0) {
        interactionDetected = true; // Mark interaction on any serial input
        char incomingChar = Serial.read();
        // Consume potential preceding carriage return if Enter sends \r\n
        if (incomingChar == '\r' && Serial.peek() == '\n') {
            Serial.read(); // Consume the \n
            incomingChar = '\n'; // Treat \r\n sequence as just \n (Enter)
        }

        if (currentState == MENU) {
            if (isdigit(incomingChar)) {
                int selected = incomingChar - '0'; // Convert char to int
                if (selected >= 0 && selected < numModes) {
                    menuSelection = selected; // Update selection visually (optional)
                    // Don't simulate press here, just update selection
                    Serial.print("Selected menu item via Serial: "); Serial.println(menuSelection);
                    stateChanged = true; // Force display update
                    delay(50); // Allow serial buffer to clear slightly
                } else {
                    Serial.println("Invalid menu number.");
                }
            } else if (incomingChar == '\n') { // Enter key in MENU
                 Serial.println("Simulating Enter Press (Select) via Serial");
                 rotButtonPressedEvent = true; // Simulate press to enter selected mode
            } else {
                // Optional: Handle other chars in menu mode if needed
            }
        } else if (currentState == ACTION) {
            if (incomingChar == '+') {
                 // Directly simulate a positive change step for serial (reversed)
                 encoderChangeSteps = -1; // Simulate one step down (because main logic is reversed)
                 Serial.println("Simulating Encoder + Step via Serial (Reversed to Down)");
            } else if (incomingChar == '-') {
                 // Directly simulate a negative change step for serial (reversed)
                 encoderChangeSteps = 1; // Simulate one step up (because main logic is reversed)
                 Serial.println("Simulating Encoder - Step via Serial (Reversed to Up)");
            } else if (incomingChar == 'b' || incomingChar == 'B') {
                rotButtonPressedEvent = true; // Simulate press to go back
                 Serial.println("Simulating Back button via Serial");
            } else if (incomingChar == '\n') { // Enter key in ACTION
                 Serial.println("Simulating Enter Press (Back) via Serial");
                 rotButtonPressedEvent = true; // Simulate press to go back
            } else {
                // Optional: Handle other chars in action mode if needed
            }
        }
        // Consume any remaining characters in the buffer for this loop cycle
        while(Serial.available() > 0) { Serial.read(); }
    }


    // --- 2. Process Inputs & Handle UI State Transitions ---

    bool performMenuExit = false; // Flag to signal exiting ACTION mode

    // --- State Transitions First ---
    // Check for Idle Timeout
    if (currentState == MENU && (millis() - lastInteractionTime > idleTimeoutDuration)) {
        Serial.println("Idle timeout, returning to splash screen.");
        currentState = SPLASH;
        displaySplashScreen(); // Show splash immediately
        return; // Skip further processing this loop iteration
    }

    // Handle transition from IDLE SPLASH state on interaction
    if (currentState == SPLASH && interactionDetected) {
        Serial.println("Interaction detected, returning to menu.");
        currentState = MENU;
        stateChanged = true; 
    }

    // Handle transition from STARTUP SPLASH state on interaction
    if (currentState == STARTUP_SPLASH && interactionDetected) {
        Serial.println("Interaction detected, entering menu.");
        currentState = MENU;
        stateChanged = true; 
        encoderChangeSteps = 0; // Consume the encoder change that triggered the transition
    }

    // Handle transition from MENU to ACTION on button press
    if (currentState == MENU && rotButtonPressedEvent) {
        interactionDetected = true; // Button press is interaction
        currentMode = (AppMode)menuSelection;
        currentState = ACTION;
        stateChanged = true;
        Serial.print("\nEntering mode: ");
        Serial.println(modeNames[currentMode]);
        // Mode-specific entry actions
        switch (currentMode) {
            case ESP_INFO:
                printEspInfo();
                break;
            case FASTLED_TEST:
            case LED2_MODE:
            case LED3_MODE:
            case LED4_MODE:
                break; // No specific entry action needed
            case LIGHT_SENSOR:
            case INA219_SENSOR:
                lastSensorReadTime = millis() - sensorReadInterval; // Force immediate read
                readAndPrintLightSensor(); // Read both, only INA needed for display update here
                readAndPrintIna219();
                if (currentMode == INA219_SENSOR && displayAvailable) {
                    updateDisplay(); // Force display update *after* reading
                }
                break;
            case I2C_SCANNER:
                lastI2cDeviceCount = scanI2CBus(); // Scan and store result on entry
                break;
            case LED_CHIPSET_SELECT:
                // Initialize proposed selection to current saved value on entry
                chipsetSelectionProposed = savedChipsetType;
                break;
            case FASTLED_PATTERN:
                // Initialize proposed selection to current pattern
                patternSelectionProposed = currentFastLedPattern;
                break;
            case LED_COUNT_SELECT:
                // Initialize proposed selection to current LED count
                numLedsProposed = numLedsConfigured;
                break;
        }
        // Consume the button press event after using it for state transition
        rotButtonPressedEvent = false; 
    }

    // Update last interaction time if interaction was detected THIS loop cycle
    if (interactionDetected) {
        lastInteractionTime = millis();
    }

    // --- Process Actions within Current State ---
    if (currentState == MENU) {
        // Handle encoder for menu navigation
        if (encoderChangeSteps != 0) {
            menuSelection = constrain(menuSelection + encoderChangeSteps, 0, numModes - 1);
            Serial.print("Menu Selection Changed: "); Serial.println(menuSelection);
            stateChanged = true; 
        }
    } else if (currentState == ACTION) {
        // Handle encoder changes within ACTION mode
        if (encoderChangeSteps != 0) {
            interactionDetected = true; // Encoder counts as interaction
            lastInteractionTime = millis(); // Update timer immediately
            bool brightnessChanged = false;
            int brightnessStep = encoderChangeSteps * 3; 
            switch (currentMode) {
                case FASTLED_TEST:
                    fastLedState.brightness = constrain(fastLedState.brightness + brightnessStep, 0, 255);
                    fastLedState.isOn = true;
                    brightnessChanged = true;
                    break;
                case LED2_MODE:
                    led2State.brightness = constrain(led2State.brightness + brightnessStep, 0, 255);
                    led2State.isOn = true;
                    brightnessChanged = true;
                    break;
                case LED3_MODE:
                    led3State.brightness = constrain(led3State.brightness + brightnessStep, 0, 255);
                    led3State.isOn = true;
                    brightnessChanged = true;
                    break;
                case LED4_MODE:
                    led4State.brightness = constrain(led4State.brightness + brightnessStep, 0, 255);
                    led4State.isOn = true;
                    brightnessChanged = true;
                    break;
                case LIGHT_SENSOR:
                case INA219_SENSOR:
                    fastLedState.brightness = constrain(fastLedState.brightness + brightnessStep, 0, 255);
                    fastLedState.isOn = true;
                    brightnessChanged = true;
                    break;
                case LED_CHIPSET_SELECT:
                    chipsetSelectionProposed = (chipsetSelectionProposed + encoderChangeSteps) % NUM_CHIPSET_TYPES;
                    if (chipsetSelectionProposed < 0) { chipsetSelectionProposed += NUM_CHIPSET_TYPES; }
                    Serial.print("Proposed chipset: "); Serial.println(chipsetNames[chipsetSelectionProposed]);
                    break;
                case FASTLED_PATTERN:
                    // Encoder changes the proposed pattern selection
                    patternSelectionProposed = (patternSelectionProposed + encoderChangeSteps) % NUM_FASTLED_PATTERNS;
                    if (patternSelectionProposed < 0) { patternSelectionProposed += NUM_FASTLED_PATTERNS; }
                    Serial.print("Proposed pattern: "); Serial.println(patternNames[patternSelectionProposed]);
                    break;
                case LED_COUNT_SELECT:
                    // Encoder changes the proposed LED count
                    numLedsProposed = constrain(numLedsProposed + encoderChangeSteps, 1, MAX_LEDS);
                    Serial.print("Proposed LED count: "); Serial.println(numLedsProposed);
                    break;
            }
            // Log brightness changes
            if (brightnessChanged) {
                 Serial.print("Brightness set to: ");
                 if(currentMode == FASTLED_TEST || currentMode == LIGHT_SENSOR || currentMode == INA219_SENSOR) Serial.println(fastLedState.brightness);
                 else if(currentMode == LED2_MODE) Serial.println(led2State.brightness);
                 else if(currentMode == LED3_MODE) Serial.println(led3State.brightness);
                 else if(currentMode == LED4_MODE) Serial.println(led4State.brightness);
            }
            stateChanged = true; // Update display after encoder action
        }

        // Handle button press within ACTION mode
        if (rotButtonPressedEvent) {
            interactionDetected = true; // Button press is interaction
            lastInteractionTime = millis(); // Update timer immediately
            Serial.println("Button pressed in ACTION state."); // Debug
            if (currentMode == LED_CHIPSET_SELECT) {
                // --- SAVE Logic --- 
                savedChipsetType = chipsetSelectionProposed;
                Serial.print("Saving Chipset Type: "); Serial.println(chipsetNames[savedChipsetType]);
                preferences.begin("led-config", false);
                preferences.putInt("chipset", savedChipsetType);
                preferences.end();
                Serial.print("** Chipset selection saved: "); Serial.print(chipsetNames[savedChipsetType]); // Added **
                Serial.println(". Reboot required for change to take effect. **");
                
                // --- Display Save Message ---
                if (displayAvailable) {
                    u8g2.clearBuffer();
                    u8g2.setFont(u8g2_font_profont22_tf); // Use standard font
                    int line1Y = 14; // Centered vertically-ish
                    int line2Y = line1Y + 16;
                    u8g2.drawStr(u8g2.getDisplayWidth()/2 - u8g2.getStrWidth("Saved!")/2, line1Y, "Saved!");
                    u8g2.drawStr(u8g2.getDisplayWidth()/2 - u8g2.getStrWidth("Reboot!")/2, line2Y, "Reboot!");
                    u8g2.sendBuffer();
                    delay(2000); // Show message for 2 seconds
                }
                // --- End Display Save Message ---
                
                // We still exit to menu after saving
                performMenuExit = true;
            } else if (currentMode == FASTLED_PATTERN) {
                // --- Set Pattern Logic --- 
                currentFastLedPattern = (FastLedPattern)patternSelectionProposed;
                Serial.print("** Pattern set to: "); Serial.println(patternNames[currentFastLedPattern]);
                // Clear FastLED buffer when changing patterns
                FastLED.clear(true); // Clear and show black
                // Reset pattern state if needed (e.g., for RGB checker)
                rgbCheckerStage = 0;
                rgbCheckerPulseCount = 0;
                lastRgbCheckerActionTime = millis();
                rgbCheckerLedState = false;
                performMenuExit = true; // Exit to menu after setting
            } else if (currentMode == LED_COUNT_SELECT) {
                // --- SAVE Logic --- 
                numLedsConfigured = numLedsProposed;
                Serial.print("Saving LED Count: "); Serial.println(numLedsConfigured);
                preferences.begin("led-config", false);
                preferences.putInt("ledCount", numLedsConfigured);
                preferences.end();
                Serial.print("** LED count saved: "); Serial.print(numLedsConfigured); // Added **
                Serial.println(". Reboot required for change to take effect. **");
                
                // --- Display Save Message ---
                if (displayAvailable) {
                    u8g2.clearBuffer();
                    u8g2.setFont(u8g2_font_profont22_tf); // Use standard font
                    int line1Y = 14; // Centered vertically-ish
                    int line2Y = line1Y + 16;
                    u8g2.drawStr(u8g2.getDisplayWidth()/2 - u8g2.getStrWidth("Saved!")/2, line1Y, "Saved!");
                    u8g2.drawStr(u8g2.getDisplayWidth()/2 - u8g2.getStrWidth("Reboot!")/2, line2Y, "Reboot!");
                    u8g2.sendBuffer();
                    delay(2000); // Show message for 2 seconds
                }
                // --- End Display Save Message ---
                
                // We still exit to menu after saving
                performMenuExit = true;
            } else {
                // Default action for other modes is just to exit
                performMenuExit = true;
            }
        }
        
        // Perform continuous actions for specific modes
        if (currentMode == LIGHT_SENSOR) {
            readAndPrintLightSensor();
        } else if (currentMode == INA219_SENSOR) {
            readAndPrintIna219();
        }

    } // End ACTION state processing

    // Perform exit from ACTION to MENU if flagged
    if (performMenuExit) {
        currentState = MENU;
        stateChanged = true;
        Serial.println("Exiting mode to Menu.");
    }


    // --- Process Direct Button Actions (Physical Buttons) ---
    if (inputState.userButtonPressedEvent) {
        Serial.println("User Button Pressed - Toggling LED 4");
        led4State.isOn = !led4State.isOn;
        stateChanged = true; // May need display update if in LED4 mode
    }
    if (inputState.bootButtonPressedEvent) {
        Serial.println("Boot Button Pressed - Toggling LED 3");
        led3State.isOn = !led3State.isOn;
         stateChanged = true; // May need display update if in LED3 mode
    }

    // --- 3. Update LED/Output States (Every Cycle) ---
    // Call the appropriate pattern function based on state

    // --- Restore Original Pattern Switch --- 
    switch (currentFastLedPattern) {
        case RAINBOW:
            runRainbowPattern();
            break;
        case RGB_CHECKER:
            runRgbCheckerPattern();
            break;
        case CHASE:
            runChasePattern();
            break;
    }
    // --- End Restore ---

    /* // DEBUG code removed
    // --- DEBUG: Force Rainbow Pattern ---
    runRainbowPattern();
    // --- END DEBUG ---
    */
    /* // Original switch temporarily disabled - REMOVED
    switch (currentFastLedPattern) {
        case RAINBOW:
            runRainbowPattern();
            break;
        case RGB_CHECKER:
            runRgbCheckerPattern();
            break;
    }
    */

    // PWM LED Update
    ledcWrite(ledcChannel2, led2State.isOn ? led2State.brightness : 0);
    ledcWrite(ledcChannel3, led3State.isOn ? led3State.brightness : 0);
    ledcWrite(ledcChannel4, led4State.isOn ? led4State.brightness : 0);

    // --- 4. Perform Continuous Mode Actions (Sensors/Info) ---
    if (currentState == ACTION) {
        switch (currentMode) {
            // ESP_INFO printed once on entry
            case LIGHT_SENSOR:
                readAndPrintLightSensor();
                break;
            case INA219_SENSOR:
                readAndPrintIna219();
                break;
            // I2C_SCANNER done on entry
            default: // No continuous actions for other modes
                break;
        }
    }

    // --- 5. Update Display ---
    // Update display if state changed, or sensor was read, or in relevant action modes
    if (stateChanged || (currentState == ACTION && 
       (currentMode == FASTLED_TEST || currentMode == LED_CHIPSET_SELECT || currentMode == FASTLED_PATTERN || 
        currentMode == INA219_SENSOR || currentMode == LIGHT_SENSOR || currentMode == LED_COUNT_SELECT)) ) 
    {
       updateDisplay();
    } else if (currentState == MENU && encoderChangeSteps != 0) {
        // Update display in menu mode immediately if encoder moved
        updateDisplay();
    }

    // Small delay to prevent loop spinning too fast if no other delays are hit
    // delay(1); // Add small delay if needed, but FastLED delay might be sufficient
}

// --- Pattern Functions ---
void runRainbowPattern() {
    if (!fastLedState.isOn) {
        FastLED.clearData();
        // Don't call show if LEDs are meant to be off
        return; 
    }
    FastLED.setBrightness(fastLedState.brightness);
    fastLedState.hue++; 
    fill_rainbow(leds, numLedsConfigured, fastLedState.hue, 7); 
    FastLED.show();
    delay(10); // Keep delay associated with rainbow animation rate
}

void runRgbCheckerPattern() {
    if (!fastLedState.isOn) {
        FastLED.clearData();
        // Don't call show if LEDs are meant to be off
        return;
    }

    FastLED.setBrightness(fastLedState.brightness);
    unsigned long currentTime = millis();
    unsigned long timeSinceLastAction = currentTime - lastRgbCheckerActionTime;
    int pulsesNeeded = 0;
    CRGB currentColor;
    const char* colorName = "";

    // Determine current stage details
    switch (rgbCheckerStage) {
        case 0: pulsesNeeded = 1; currentColor = CRGB::Red; colorName = "Red"; break;
        case 1: pulsesNeeded = 2; currentColor = CRGB::Green; colorName = "Green"; break; // Swapped G and B for common order
        case 2: pulsesNeeded = 3; currentColor = CRGB::Blue; colorName = "Blue"; break; 
    }

    bool updateLeds = false;

    // State machine for pulsing
    if (rgbCheckerLedState) { // Currently ON
        if (timeSinceLastAction >= pulseOnTime) {
            fill_solid(leds, numLedsConfigured, CRGB::Black);
            rgbCheckerLedState = false;
            lastRgbCheckerActionTime = currentTime;
            rgbCheckerPulseCount++;
            updateLeds = true;
        }
    } else { // Currently OFF
        if (rgbCheckerPulseCount >= pulsesNeeded) { // Done with pulses for this color?
            if (timeSinceLastAction >= interColorDelay) { // Wait before next color
                rgbCheckerStage = (rgbCheckerStage + 1) % 3;
                rgbCheckerPulseCount = 0;
                // Re-determine details for the NEW stage immediately
                switch (rgbCheckerStage) {
                    case 0: pulsesNeeded = 1; currentColor = CRGB::Red; colorName = "Red"; break;
                    case 1: pulsesNeeded = 2; currentColor = CRGB::Green; colorName = "Green"; break; 
                    case 2: pulsesNeeded = 3; currentColor = CRGB::Blue; colorName = "Blue"; break; 
                }
                // Start next color's pulse
                fill_solid(leds, numLedsConfigured, currentColor); // Use the new currentColor
                rgbCheckerLedState = true;
                lastRgbCheckerActionTime = currentTime;
                Serial.print("RGB Check: "); Serial.println(colorName); // Log color change
                updateLeds = true;
            }
        } else { // Still need more pulses for this color
            if (timeSinceLastAction >= pulseOffTime) { // Time for next pulse?
                 fill_solid(leds, numLedsConfigured, currentColor);
                 rgbCheckerLedState = true;
                 lastRgbCheckerActionTime = currentTime;
                 Serial.print("RGB Check: "); Serial.println(colorName); // Log color change
                 updateLeds = true;
            }
        }
    }

    if (updateLeds) {
        FastLED.show();
    }
    // No delay here, timing is handled by millis()
}

void runChasePattern() {
    if (!fastLedState.isOn) {
        // Ensure strip is off if state is off
        if (FastLED.getBrightness() > 0) { // Check if it was previously on
            FastLED.clear(true); // Clear and force show
        }
        FastLED.setBrightness(0); // Explicitly set brightness to 0
        return;
    }

    unsigned long currentTime = millis();
    if (currentTime - lastChaseUpdateTime >= chaseStepDelay) {
        lastChaseUpdateTime = currentTime;

        FastLED.setBrightness(fastLedState.brightness); // Apply global brightness

        // --- Wave Logic --- 
        // Update the wave position
        chasePosition += 0.1; // Adjust this value to change speed independent of update rate
        if (chasePosition >= numLedsConfigured) {
            chasePosition -= numLedsConfigured; // Wrap based on configured count
        }

        // Update all LEDs based on distance from the wave position
        for (int i = 0; i < numLedsConfigured; i++) {
            // Calculate distance, handling wrap-around
            float distance = abs(i - chasePosition);
            float distanceWrap = min(distance, (float)numLedsConfigured - distance); // Wrap based on configured count

            // Calculate brightness based on distance (linear falloff)
            int brightness = 255 - (int)(distanceWrap * chaseFadeRate);
            brightness = max(0, brightness); // Clamp at 0

            // Set LED color (White)
            leds[i] = CRGB(brightness, brightness, brightness);
        }
        // --- End Wave Logic ---

        FastLED.show();
    }
    // No delay needed, timing handled by chaseStepDelay
}
// --- End Pattern Functions ---
