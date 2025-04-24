# Siebenuhr Controller Test Program

This program is designed to test the hardware components and basic functionality of the custom controller PCB for the Siebenuhr project. It allows interactive testing of LEDs, sensors, input controls, and configuration settings via the serial monitor and physical buttons/knob.

## Features Tested & Configurable Settings

* **Inputs:**
  * Rotary Encoder (Rotation and Push Button)
  * User Button (GPIO 33)
  * Boot Button (GPIO 0)
* **Outputs:**
  * FastLED RGB LED Strip (GPIO 21)
    * **Configurable:** Chipset Type (WS2812/SK6812), Pattern (Rainbow, RGB Check, Chase), LED Count (1-1000)
  * PWM LED 2 (GPIO 22)
  * PWM LED 3 (GPIO 19) - *Near Boot Button*
  * PWM LED 4 (GPIO 23) - *Near User Button*
* **Sensors (I2C Bus - SDA: GPIO 14, SCL: GPIO 13):**
  * BH1750 Ambient Light Sensor (Address 0x23)
  * INA219 Current/Voltage Sensor (Address 0x40)
* **Basic ESP32 Info**
* **Configuration Saving:** Chipset Type, LED Count saved to non-volatile memory (Preferences).
* **Idle Timeout:** Returns to splash screen after 1 minute of inactivity in the menu.

## Hardware

![Knob Controller PCB](media/knob-controller-pcb.jpeg)
![Miniclock Controller PCB](media/miniclock-controller-pcb.jpeg)

## Usage

This app uses PlatformIO, which can also be used inside of Cursor or other VSCode-compatible environments. 

1. **Build and Upload:** Compile and upload the firmware using PlatformIO.
2. **Open Serial Monitor:** Set the baud rate to 115200.
3. **Startup:** The program plays a brief static animation, shows a splash screen, and then displays a startup message with version and build date.
4. **Main Menu:** You will be presented with a numbered menu (order may change):
    * FastLED Test
    * LED2 Brightness
    * LED3 Brightness
    * LED4 Brightness
    * BH1750 Sensor
    * INA219 Sensor
    * I2C Scanner
    * LED Chipset
    * LED Pattern
    * LED Count
    * ESP Info

5. **Navigation:**
    * **Rotary Encoder:** Turn the knob to scroll through menu items or adjust values. Press the knob button to select the highlighted mode or save a setting.
    * **Serial Monitor:** Type the number corresponding to the desired mode (e.g., `1`) and press Enter, OR simply press Enter when the desired item is highlighted by the knob cursor.
6. **Action Modes:**
    * **LED/FastLED Modes (FastLED, LED2, LED3, LED4):**
        * Adjust brightness by turning the encoder knob or typing `+` or `-` in the serial monitor.
        * The LED state (on/off, brightness) persists even when you exit the mode.
    * **Sensor/Info Modes (BH1750, INA219, ESP Info, I2C Scanner):**
        * ESP Info & I2C Scan results are displayed once upon entry.
        * BH1750 and INA219 readings are displayed periodically (every 2 seconds).
    * **Configuration Modes (LED Chipset, LED Pattern, LED Count):**
        * Turn the encoder knob to cycle through available options.
        * Press the encoder button to select/set the pattern *or* to save the Chipset/LED Count.
        * **Saving Chipset/LED Count:** A "Saved! Reboot!" message appears for 2 seconds. The ESP32 must be rebooted (or power cycled) for these changes to take effect.
    * **Exiting a Mode:** Press the rotary encoder button (except when saving) *or* press Enter *or* type `b` in the serial monitor to return to the main menu.
7. **Direct Button Toggles:**
    * Pressing the **User Button (GPIO 33)** at any time toggles the on/off state of **LED 4** (nearby).
    * Pressing the **Boot Button (GPIO 0)** at any time toggles the on/off state of **LED 3** (nearby).
8. **Idle Behavior:** If left idle on the main menu for 1 minute, the display will return to the splash screen. Any interaction will bring back the menu.

![INA219 readings](media/current-sensor-readings.jpeg)

*example current sensor readings*

## Changelog

```txt
1.0   - Initial version (Apr 13 2025)
1.0.1 - Fix current measurement calculation for 1mOhm shunt resistor.
1.1.0 - Add Chase pattern, LED Chipset config (WS2812/SK6812), 
        LED Pattern selection, LED Count config (1-1000), idle splash screen.
        Improve display updates and rendering. Adjust Chase pattern visuals.
```

### Note

This app was created with the help of Cursor and Gemini 2.5-pro-exp.
