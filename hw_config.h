/*******************************************************************************
 GigaNES (c) 2026 Kaled Souky <https://github.com/KaledSouky> 


 GigaNES: High-Performance NES Emulator for Arduino Giga

 GigaNES is a specialized port of the Nofrendo NES emulator core, 
 custom-engineered for the Arduino Giga R1 WiFi (STM32H747XI) and the Arduino
 Giga Display Shield. This project leverages the high-speed Cortex-M7 core and 
 low-level STM32 register access to deliver a full-speed, low-latency gaming 
 experience.


 This program is free software: you can redistribute it and/or modify it under 
 the terms of the GNU General Public License as published by the Free Software 
 Foundation, either version 3 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful, but WITHOUT 
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
 FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along with 
 this program. If not, see <https://www.gnu.org/licenses/>.


 hw_config.h

 Hardware configuration for GigaNES:
 1. CONTROLLERS: Pin mapping for dual-controller support using direct GPIO 
    access.
 2. DEBUG SYSTEM: Toggleable macro-based logger. When ENABLE_GIGA_DEBUG is 0, 
    serial overhead is removed from the binary, ensuring the CPU remains 
    dedicated to the emulation cycle without diagnostic interrupts.   
*******************************************************************************/
 
#ifndef HW_CONFIG_H
#define HW_CONFIG_H

// -- NES Controller Pins --
// Pin configuration for Arduino Giga R1 WiFi

// Common pins for both controllers
#define NES_LATCH_PIN 2
#define NES_CLOCK_PIN 3

// Individual data pins for each controller
#define NES_DATA_PIN  4
#define NES_DATA2_PIN 5

// --- Debug Configuration ---
#define ENABLE_GIGA_DEBUG 0  // 1 = Enabled, 0 = Disabled (Default)
// Removes serial overhead to ensure the M7 core focuses on emulation.

#if ENABLE_GIGA_DEBUG
  #define DEBUG_BEGIN(baud)    Serial.begin(baud)
  #define DEBUG_PRINT(x)       Serial.print(x)
  #define DEBUG_PRINTLN(x)     Serial.println(x)
  #define DEBUG_PRINT_F(...)   Serial.print(__VA_ARGS__)
#else
  #define DEBUG_BEGIN(baud)
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINT_F(...)
#endif

/*******************************************************************************
 * WIRING GUIDE:
 * - LATCH (Pin 2) -> Connect to LATCH on BOTH controllers
 * - CLOCK (Pin 3) -> Connect to CLOCK on BOTH controllers
 * - DATA1 (Pin 4) -> Connect to DATA on Controller 1 ONLY
 * - DATA2 (Pin 5) -> Connect to DATA on Controller 2 ONLY 
 *
 * WARNING:
 * - Connect Controller VCC to Arduino 3.3V pin (NOT 5V)
 * - Connect Controller GND to Arduino GND pin
 * This ensures 3.3V logic levels and protects the STM32H747XI pins.
 *******************************************************************************/

#endif
