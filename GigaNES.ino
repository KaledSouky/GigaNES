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

 1. Technical Specification: Architecture (v1.0)
 -------------------------------------------------------------------------------
 * Video System (M7 Brute Force): Due to hardware DMA2D limitations for 
   simultaneous rotation/scaling, the Cortex-M7 performs a 90° software 
   rotation + 2x Scaling via code. The NES output (256×240) is transformed to 
   fit the Giga Display (480×800). By rotating 90°, the NES height (240×2=480) 
   perfectly matches the display's short axis, while the NES width (256×2=512) 
   is centered on the long axis (800). This enables a true Landscape 
   orientation on a native Portrait panel. It utilizes 64-bit writes and 8x 
   loop unrolling to achieve Blit times of ~6.0ms (36% CPU budget).

 * Audio System (High Fidelity): Sampling rate set to 44100 Hz to ensure 
   maximum sound accuracy. Utilizes the advanced 12-bit DAC (pin A12/DAC0) 
   with a circular buffer for crisp, glitch-free audio performance.

 * Memory Management (Hybrid Hierarchy): 
   - DTCMRAM (128 KB): Zero-latency memory for emulation buffer and palette.
   - External SDRAM (8 MB): Managed via FMC for Double Framebuffer and ROM 
     storage.

 * Input Management (Low-Latency): Bypasses Arduino abstraction (digitalWrite) 
   for direct BSRR/IDR register access, providing single-clock cycle response 
   for original NES controllers.

 * Storage & SD (Asynchronous): Uses SdFat in DEDICATED_SPI mode (50 MHz). An 
   independent RTOS SaveThread handles auto-saves to .sav files in the 
   background, preserving "natural" game progress without emulation stuttering. 

 * Region Detection: Automatic iNES header parsing for NTSC (60Hz) and PAL 
   (50Hz), with dynamic Pixel Clock synchronization.

 2. SD Card Requirements & Compatibility
 -------------------------------------------------------------------------------
 * Capacity & Speed: A microSD card of up to 32 GB is recommended. High-speed 
   ratings (Class 10/UHS) are not required as ROMs are loaded directly into 
   SDRAM. 
 * Format: The SD card MUST be formatted in FAT32.
 * Naming Convention (8.3 Rule): For maximum stability, use the 8.3 filename 
   rule. Keep filenames to 8 characters or less, followed by the .nes 
   extension (e.g., supermar.nes, zelda1.nes).
 * Location: Place .nes files in the root directory of the SD card.
 
 3. Software Requirements (Install via Arduino Library Manager)
 -------------------------------------------------------------------------------
 1. Arduino_H7_Video
 2. Arduino_GigaDisplay
 3. Arduino_AdvancedAnalog
 4. SdFat (by Bill Greiman)
 5. SDRAM (part of the Arduino Mbed OS Giga Board Package)
  
 4. Usage Instructions
 -------------------------------------------------------------------------------
 1. Download or clone the GigaNES folder.
 2. Open GigaNES.ino in the Arduino IDE.
 3. Ensure you have the Arduino Giga R1 board package installed (Tools > Board > 
    Arduino Mbed OS Giga Boards), and select "Arduino Giga R1". 
 4. Connect the Arduino Giga R1 WiFi to the Giga Display Shield and follow the 
    provided connection diagram (Connection_Diagram.png) for the rest of the 
    hardware peripherals.
    WARNING: Use 3.3V for all peripherals. Do not use 5V.
 5. Upload and enjoy!
********************************************************************************/

#include <Arduino.h>
#include <SdFat.h>
#include "hw_config.h"

extern "C" {
  int osd_main(int argc, char *argv[]);
}

const int chipSelect = 10;
SdFs sd;

void setup() {
  DEBUG_BEGIN(115200);
  delay(2000); 
  
  DEBUG_PRINTLN("==================================");
  DEBUG_PRINTLN("   Arduino Giga Nofrendo (Video)  ");
  DEBUG_PRINTLN("==================================");

  DEBUG_PRINT("Initializing SD card... ");
  // Using SdSpiConfig with DEDICATED_SPI for better performance.
  // Giga R1 uses the Mbed core, ensuring correct SPI pins are used for pin 10.
  bool sdAvailable = true;
  if (!sd.begin(SdSpiConfig(chipSelect, DEDICATED_SPI, SD_SCK_MHZ(50)))) {
    DEBUG_PRINTLN("FAILED! (SD not present or error)");
    sdAvailable = false;
  } else {
    DEBUG_PRINTLN("OK.");
  }

  String romFilename = "";

  if (sdAvailable) {
    File root = sd.open("/");
    if (root) {
      while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
          char nameBuf[256];
          entry.getName(nameBuf, sizeof(nameBuf));
          String name = nameBuf;
          if (name.endsWith(".nes") || name.endsWith(".NES")) {
            romFilename = name;
            entry.close();
            break;
          }
        }
        entry.close();
      }
      root.close();
    }
  }

  if (romFilename == "") {
    DEBUG_PRINTLN("No .nes files found! Will play the Nofrendo intro.");
    romFilename = "intro.nes"; 
  }

  DEBUG_PRINT("Loading ROM: ");
  DEBUG_PRINTLN(romFilename);
  
  String fullPath = "/" + romFilename;
  char* argv[1];
  char pathBuf[128];
  fullPath.toCharArray(pathBuf, 128);
  argv[0] = pathBuf;

  DEBUG_PRINTLN("Starting emulation...");
  osd_main(1, argv);
  
  DEBUG_PRINTLN("Emulation ended.");
}

void loop() {}
