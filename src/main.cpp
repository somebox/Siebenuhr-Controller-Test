#include <Arduino.h>
#include <FastLED.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_INA219.h>

#include "fastled.h"

// --- Program Info ---
#define PROGRAM_VERSION "1.0"
// --- End Program Info ---

#define SDA_PIN 14
#define SCL_PIN 13
#define USER_BUTTON_PIN 33
#define LED1_PIN 21
#define LED2_PIN 22
#define LED3_PIN 19
#define LED4_PIN 23
#define ROT_ENC_A_PIN 26
#define ROT_ENC_B_PIN 27
#define ROT_ENC_BUTTON_PIN 18
#define BOOT_BUTTON_PIN 0

#define NUM_LEDS 150
CRGB leds[NUM_LEDS];
BH1750 lightMeter; // Default address 0x23
Adafruit_INA219 ina219; // Default address 0x40

// --- State Management Structs ---
struct InputState {
    // Rotary Encoder
    volatile long encoderValue = 0;
    int lastEncoded = 0;
    long lastEncoderValueReported = 0;
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
    int brightness = 0; // 0-255
};

struct FastLedState {
    bool isOn = true; // Default FastLED strip to on
    int brightness = 128; // 0-255
    uint8_t hue = 0; // For animation state
};

InputState inputState;
LedPwmState led2State, led3State, led4State;
FastLedState fastLedState;
// --- End State Management Structs ---

// --- UI State Machine ---
enum UIState {
  MENU,
  ACTION
};

enum AppMode {
  ESP_INFO,
  FASTLED_TEST,
  LED2_MODE,
  LED3_MODE,
  LED4_MODE,
  LIGHT_SENSOR,
  INA219_SENSOR
};

const char* modeNames[] = {
  "ESP Info",
  "FastLED Test",
  "LED2 Brightness",
  "LED3 Brightness",
  "LED4 Brightness",
  "BH1750 Light Sensor",
  "INA219 Current Sensor"
};
const int numModes = sizeof(modeNames) / sizeof(modeNames[0]);

UIState currentState = MENU;
AppMode currentMode = ESP_INFO; // Default mode
int menuSelection = 0; // Index of the currently selected mode in the menu
int currentBrightness = 128; // Shared brightness value (0-255)

const int pulsesPerMenuStep = 2; // <<< How many encoder counts per menu selection change
// --- End UI State Machine ---

// PWM Configuration
const int pwmFreq = 5000; // 5 kHz frequency
const int pwmResolution = 8; // 8-bit resolution (0-255)
const int ledcChannel2 = 0;
const int ledcChannel3 = 1;
const int ledcChannel4 = 2;

// Rotary Encoder State
volatile long encoderValue = 0;
int lastEncoded = 0;
long lastencoderValue = 0;

// Button State
int buttonState = HIGH;             // The debounced state
int lastButtonState = HIGH;         // The previous raw reading
unsigned long lastDebounceTime = 0; // for button debouncing
unsigned long debounceDelay = 50;    // debounce time; increase if bouncing seen

// Light Sensor Timer
unsigned long lastSensorReadTime = 0; // Renamed from lastLightReadTime
const unsigned long sensorReadInterval = 2000; // 2 seconds

// --- Helper Functions ---
void printMenu() {
  Serial.println("--- MENU ---");
  for (int i = 0; i < numModes; i++) {
    Serial.print( (i == menuSelection) ? ">" : " "); // Indicate selection cursor (optional now)
    Serial.print(i); 
    Serial.print(": ");
    Serial.println(modeNames[i]);
  }
  Serial.println("------------");
  Serial.print("Enter selection (0-"); Serial.print(numModes - 1); Serial.println(") or press ENTER to select.");
}

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
        float lux = lightMeter.readLightLevel();
        if (lux < 0) { Serial.println(F("Error reading BH1750")); }
        else { Serial.print("Light: "); Serial.print(lux); Serial.println(" lx"); }
        lastSensorReadTime = millis(); 
    }
}

void readAndPrintIna219() {
    if (millis() - lastSensorReadTime >= sensorReadInterval) {
        float busvoltage = ina219.getBusVoltage_V();
        float shuntvoltage = ina219.getShuntVoltage_mV();
        float current_mA = ina219.getCurrent_mA();
        float loadvoltage = busvoltage + (shuntvoltage / 1000);
        Serial.print("Bus V: "); Serial.print(busvoltage); Serial.print(" | Load V: "); Serial.print(loadvoltage);
        Serial.print(" | Current: "); Serial.print(current_mA); Serial.println(" mA");
        lastSensorReadTime = millis();
    }
}
// --- End Helper Functions ---

void setup() {
  Serial.begin(115200);
  delay(1000); // Wait for serial monitor
  Serial.println("\n\n--- Solid Difference Clock Controller Test Program ---");
  Serial.print("Version: "); Serial.println(PROGRAM_VERSION);
  Serial.print("Build Date: "); Serial.print(__DATE__); Serial.print(" "); Serial.println(__TIME__);
  Serial.println("-------------------------------------------------");
  Serial.println("Setup starting...");

  // --- Initialize I2C and Sensors ---
  Wire.begin(SDA_PIN, SCL_PIN);
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
  FastLED.addLeds<WS2812, LED1_PIN, GRB>(leds, NUM_LEDS);
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

  // --- Initialize Input Pins ---
  pinMode(ROT_ENC_A_PIN, INPUT);
  pinMode(ROT_ENC_B_PIN, INPUT);
  pinMode(ROT_ENC_BUTTON_PIN, INPUT);
  pinMode(USER_BUTTON_PIN, INPUT_PULLUP); // User button needs pullup
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP); // Boot button often needs pullup
  // --- End Input Pin Init ---

  Serial.println("Setup complete. Entering MENU state.");
  printMenu(); // Show initial menu
}

void loop() {
    // --- 1. Read Inputs (Physical & Serial) ---
    // Read Physical Encoder
    int MSB = digitalRead(ROT_ENC_A_PIN);
    int LSB = digitalRead(ROT_ENC_B_PIN);
    int encoded = (MSB << 1) | LSB;
    int sum = (inputState.lastEncoded << 2) | encoded;
    if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) inputState.encoderValue++;
    if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) inputState.encoderValue--;
    inputState.lastEncoded = encoded;
    long physicalEncoderChange = inputState.encoderValue - inputState.lastEncoderValueReported; // Store physical change

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
            }
        }
    }
    inputState.rotLastButtonState = rotButtonReading;

    // --- Initialize loop variables based on physical inputs ---
    long encoderChange = physicalEncoderChange; // Start with physical change
    bool rotButtonPressedEvent = physicalRotButtonPressedEvent; // Start with physical press

    // Keep User/Boot button reading
    bool userButtonPressedEvent = inputState.userButtonPressedEvent; 
    bool bootButtonPressedEvent = inputState.bootButtonPressedEvent; 
    inputState.userButtonPressedEvent = false;
    inputState.bootButtonPressedEvent = false;

    // Check for User/Boot physical buttons (debounce)
    int userButtonReading = digitalRead(USER_BUTTON_PIN);
    if (userButtonReading != inputState.userLastButtonState) { inputState.userLastDebounceTime = millis(); }
    if ((millis() - inputState.userLastDebounceTime) > debounceDelay) {
        if (userButtonReading != inputState.userButtonState) {
            inputState.userButtonState = userButtonReading;
            if (inputState.userButtonState == LOW) { inputState.userButtonPressedEvent = true; }
        }
    }
    inputState.userLastButtonState = userButtonReading;

    int bootButtonReading = digitalRead(BOOT_BUTTON_PIN);
    if (bootButtonReading != inputState.bootLastButtonState) { inputState.bootLastDebounceTime = millis(); }
    if ((millis() - inputState.bootLastDebounceTime) > debounceDelay) {
        if (bootButtonReading != inputState.bootButtonState) {
            inputState.bootButtonState = bootButtonReading;
            if (inputState.bootButtonState == LOW) { inputState.bootButtonPressedEvent = true; }
        }
    }
    inputState.bootLastButtonState = bootButtonReading;

    // Process Serial Input (can override or add to physical input)
    if (Serial.available() > 0) {
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
                    rotButtonPressedEvent = true; // Simulate press to enter mode
                    Serial.print("Selected menu item: "); Serial.println(menuSelection);
                    printMenu(); // Refresh menu with new cursor
                    delay(50); // Allow serial buffer to clear slightly
                } else {
                    Serial.println("Invalid menu number.");
                }
            } else if (incomingChar == '\n') { // Enter key in MENU
                 Serial.println("Simulating Enter Press (Select)");
                 rotButtonPressedEvent = true; // Simulate press to enter selected mode
            } else {
                // Optional: Handle other chars in menu mode if needed
            }
        } else if (currentState == ACTION) {
            if (incomingChar == '+') {
                encoderChange = 10; // Simulate encoder turn up (adjust step size as needed)
                 Serial.println("Simulating Encoder +");
            } else if (incomingChar == '-') {
                encoderChange = -10; // Simulate encoder turn down
                 Serial.println("Simulating Encoder -");
            } else if (incomingChar == 'b' || incomingChar == 'B') {
                rotButtonPressedEvent = true; // Simulate press to go back
                 Serial.println("Simulating Back button");
            } else if (incomingChar == '\n') { // Enter key in ACTION
                 Serial.println("Simulating Enter Press (Back)");
                 rotButtonPressedEvent = true; // Simulate press to go back
            } else {
                // Optional: Handle other chars in action mode if needed
            }
        }
        // Consume any remaining characters in the buffer for this loop cycle
        while(Serial.available() > 0) { Serial.read(); }
    }

    // --- 2. Process Inputs & Handle UI State Transitions ---
    if (currentState == MENU) {
        // Use combined encoderChange for Menu Navigation
        if (abs(encoderChange) >= pulsesPerMenuStep) {
            menuSelection = constrain(menuSelection + encoderChange / pulsesPerMenuStep, 0, numModes - 1);
            rotButtonPressedEvent = true; // Simulate press to enter mode
            Serial.print("Selected menu item: "); Serial.println(menuSelection);
            printMenu(); // Refresh menu with new cursor
            delay(50); // Allow serial buffer to clear slightly
        }

        // Button Press (Simulated or Physical) to Enter Mode
        if (rotButtonPressedEvent) { 
            currentMode = (AppMode)menuSelection;
            currentState = ACTION;
            Serial.print("\nEntering mode: ");
            Serial.println(modeNames[currentMode]);
            // Mode-specific entry actions
            switch (currentMode) {
                case ESP_INFO: 
                    printEspInfo(); 
                    Serial.println("Press ENTER or 'b' to return to menu.");
                    break;
                case FASTLED_TEST:
                case LED2_MODE:
                case LED3_MODE:
                case LED4_MODE:
                    // Ensure LEDs start at current brightness
                    // ... (led init code) ...
                    Serial.println("Turn encoder knob, type +/- or ENTER/b for menu.");
                    break;
                case LIGHT_SENSOR:
                case INA219_SENSOR:
                    Serial.println("Reading sensor every 2 seconds...");
                    Serial.println("Press ENTER or 'b' to return to menu.");
                    lastSensorReadTime = millis(); // Reset sensor timer
                    break;
            }
        }

    } else if (currentState == ACTION) {
        // Use combined encoderChange for Mode Action (Brightness)
        if (encoderChange != 0) {
            bool brightnessChanged = false;
            switch (currentMode) {
                case FASTLED_TEST:
                    fastLedState.brightness = constrain(fastLedState.brightness + encoderChange * 5, 0, 255);
                    fastLedState.isOn = true; // Turn on if adjusting
                    brightnessChanged = true;
                    break;
                case LED2_MODE:
                    led2State.brightness = constrain(led2State.brightness + encoderChange * 5, 0, 255);
                    led2State.isOn = true; // Turn on if adjusting
                    brightnessChanged = true;
                    break;
                case LED3_MODE:
                    led3State.brightness = constrain(led3State.brightness + encoderChange * 5, 0, 255);
                    led3State.isOn = true;
                    brightnessChanged = true;
                    break;
                case LED4_MODE:
                    led4State.brightness = constrain(led4State.brightness + encoderChange * 5, 0, 255);
                    led4State.isOn = true;
                    brightnessChanged = true;
                    break;
                default: // Ignore encoder in other modes
                     Serial.println("(Encoder ignored in this mode)");
                     break;
            }
            if (brightnessChanged) {
                 Serial.print("Brightness set to: ");
                 // Print the specific brightness that was just changed
                 if(currentMode == FASTLED_TEST) Serial.println(fastLedState.brightness); 
                 else if(currentMode == LED2_MODE) Serial.println(led2State.brightness);
                 else if(currentMode == LED3_MODE) Serial.println(led3State.brightness);
                 else if(currentMode == LED4_MODE) Serial.println(led4State.brightness);
            }
             // Use physical encoder value for reporting reference
             inputState.lastEncoderValueReported = inputState.encoderValue; 
        }

        // Use combined rotButtonPressedEvent to Exit Mode
        if (rotButtonPressedEvent) { 
            currentState = MENU;
            Serial.println("Exiting mode.");
            printMenu();
            // No specific exit actions needed here anymore, LEDs stay as they are
        }
    }

    // --- 2b. Process Direct Button Actions (Physical Buttons) ---
    if (inputState.userButtonPressedEvent) {
        Serial.println("User Button Pressed - Toggling LED 4");
        led4State.isOn = !led4State.isOn;
        // Optional: Set brightness to a default if turning on from off state?
        // if (led4State.isOn && led4State.brightness == 0) led4State.brightness = 128;
    }
    if (inputState.bootButtonPressedEvent) {
        Serial.println("Boot Button Pressed - Toggling LED 3");
        led3State.isOn = !led3State.isOn;
        // Optional: Set brightness to a default if turning on from off state?
        // if (led3State.isOn && led3State.brightness == 0) led3State.brightness = 128;
    }

    // --- 3. Update LED/Output States (Every Cycle) ---
    // FastLED Update
    if (fastLedState.isOn) {
        FastLED.setBrightness(fastLedState.brightness);
        // Simple animation for example, adjust as needed
        fastLedState.hue++; 
        fill_rainbow(leds, NUM_LEDS, fastLedState.hue, 7); 
        FastLED.show();
        delay(10);
    } else {
        Serial.println("FastLED off");
        // Optional: Ensure FastLED is explicitly off if state demands it
        // FastLED.clear(); 
        // FastLED.show(); // Might cause flicker if called unnecessarily
    }

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
            default: // No continuous actions for other modes
                break;
        }
    }
}
