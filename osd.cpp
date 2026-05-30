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
#include <Arduino_H7_Video.h>
extern "C" {
#include <video_modes.h>
}
#include "dsi.h"
#include "SDRAM.h"
#include <stdlib.h>

extern "C" {
#include "osd.h"
#include "nofrendo.h"
#include "nofconfig.h"
#include "gui.h"
#include "nes.h"
#include "nes_rom.h"
}

#define sleep mbed_sleep_impl
#include <Arduino_GigaDisplay.h>
#undef sleep

#include "ea_malloc.h"
#include <mbed.h>
#include "hw_config.h"

// -----------------------------------------------------------------------------
// AUDIO NES VIA INTERNAL DAC (Arduino_AdvancedAnalog)
// -----------------------------------------------------------------------------
#include <Arduino_AdvancedAnalog.h>

static AdvancedDAC nesDac(A12);

/* * WARNING: These values are optimized for the STM32H7. Changing 
 * NES_SAMPLE_RATE or NES_DAC_SAMPLES may cause audio desync, 
 * increased latency, or emulation stuttering.
 */
static const int NES_SAMPLE_RATE      = 44100;  
static const size_t NES_DAC_SAMPLES   = 512;

static void (*nes_audio_callback)(void *buffer, int size) = nullptr;

static int16_t audio_tmp_buf[NES_DAC_SAMPLES]
    __attribute__((aligned(32)));

extern "C" {

void audio_pump()
{
    if (!nes_audio_callback) return;

    while (nesDac.available()) {
        SampleBuffer dacbuf = nesDac.dequeue();
        size_t n = dacbuf.size();

        if (n > NES_DAC_SAMPLES) n = NES_DAC_SAMPLES;

        nes_audio_callback((void*)audio_tmp_buf, (int)n);

        for (size_t i = 0; i < n; i++) { 
            int32_t sample = audio_tmp_buf[i];
            dacbuf[i] = (uint16_t)((sample + 32768) >> 4);
        }

        for (size_t i = n; i < dacbuf.size(); i++) {
            dacbuf[i] = 2048;
        }

        nesDac.write(dacbuf);
    }
}

void osd_setsound(void (*playfunc)(void *buffer, int size))
{
    nes_audio_callback = playfunc;
}

void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = NES_SAMPLE_RATE;
    info->bps         = 16;
}

void osd_stopsound() {}

int osd_init_sound()
{
    /* * WARNING: The last parameter (8) sets the DMA buffer queue depth. 
     * Reducing this value will likely cause audio underruns (crackling), 
     * while increasing it will add unnecessary latency. 
     * This is the "sweet spot" for the STM32H7.
     */	
    bool ok = nesDac.begin(AN_RESOLUTION_12,
                           NES_SAMPLE_RATE,
                           NES_DAC_SAMPLES,
                           8); 
    if (!ok) {
        DEBUG_PRINTLN("nesDac.begin() failed, audio disabled");
        return -1;
    }
    return 0;
}

} // extern "C"

// -----------------------------------------------------------------------------
// NES CONTROLLER
// -----------------------------------------------------------------------------
static uint8 pad1_state = 0;
static uint8 pad2_state = 0;
static int pad1_index = 0;
static int pad2_index = 0;

extern "C" {
    void osd_poll_inputs(void) {
        uint8 p1 = 0;
        uint8 p2 = 0;

        // Latch pulse: Pin 2 is PA3
        GPIOA->BSRR = (1UL << 3); // SET
        delayMicroseconds(4);
        GPIOA->BSRR = (1UL << (3 + 16)); // RESET
        delayMicroseconds(4);

        for (int i = 0; i < 8; i++) {
            // Read bits (Inverted logic as per original code)
            // Pin 4 is PJ8 (Data 1)
            if (!(GPIOJ->IDR & (1UL << 8))) p1 |= (1 << (7 - i));
            // Pin 5 is PA7 (Data 2)
            if (!(GPIOA->IDR & (1UL << 7))) p2 |= (1 << (7 - i));

            // Clock pulse: Pin 3 is PA2
            GPIOA->BSRR = (1UL << 2); // SET
            delayMicroseconds(4);
            GPIOA->BSRR = (1UL << (2 + 16)); // RESET
            delayMicroseconds(4);
        }
        
        pad1_state = p1;
        pad2_state = p2;
    }

    void osd_nes_init() {
        pinMode(NES_LATCH_PIN, OUTPUT);
        pinMode(NES_CLOCK_PIN, OUTPUT);
        pinMode(NES_DATA_PIN, INPUT_PULLUP);
        pinMode(NES_DATA2_PIN, INPUT_PULLUP);
        
        digitalWrite(NES_LATCH_PIN, LOW);
        digitalWrite(NES_CLOCK_PIN, LOW);
    }

    void osd_nes_latch(uint8 value) {
        if (value & 1) {
            pad1_index = 0;
            pad2_index = 0;
        }
    }

    uint8 osd_nes_clock_read(uint32 address) {
        uint8 value = 0;
        if (address == 0x4016) {
            if (pad1_index < 8) {
                value = (pad1_state & (1 << (7 - pad1_index))) ? 0x01 : 0x00;
                pad1_index++;
            } else {
                value = 0x01; // Standard NES behavior for bits > 8
            }
        }
        else if (address == 0x4017) {
            if (pad2_index < 8) {
                value = (pad2_state & (1 << (7 - pad2_index))) ? 0x01 : 0x00;
                pad2_index++;
            } else {
                value = 0x01;
            }
        }
        return value;
    }
}

// -----------------------------------------------------------------------------
// ULTRA-FAST MEMORY (DTCM) 
// -----------------------------------------------------------------------------
uint8_t emu_buf[NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT]
    __attribute__((section(".dtcmram_bss"))) __attribute__((aligned(32)));

uint32_t hw_palette[256]
    __attribute__((section(".dtcmram_bss"))) __attribute__((aligned(32)));

// Intermediate buffer for 8 NES lines (16 display columns) in DTCM
static uint64_t blit_strip[NES_SCREEN_HEIGHT][2] 
    __attribute__((section(".dtcmram_bss"))) __attribute__((aligned(32)));

Arduino_H7_Video Display(800, 480, GigaDisplayShield);
GigaDisplayBacklight backlight;

mbed::Ticker nesTicker;
static void (*nes_timer_callback)(void) = NULL;

static void ticker_handler() {
    if (nes_timer_callback) nes_timer_callback();
}

static int logprint(const char *string) { return strlen(string); }

// -----------------------------------------------------------------------------
// VIDEO DRIVER
// -----------------------------------------------------------------------------
static int giga_vid_init(int width, int height) { return 0; }
static void giga_vid_shutdown(void) {}
static int giga_vid_set_mode(int width, int height) { return 0; }

static void giga_vid_set_palette(rgb_t *pal) {
    for (int i = 0; i < 256; i++) {
        uint8_t r = pal[i].r, g = pal[i].g, b = pal[i].b;
        uint16_t c = ((r & 0xF8) << 8) |
                     ((g & 0xFC) << 3) |
                     (b >> 3);
        hw_palette[i] = (c << 16) | c;
    }
}

static void giga_vid_clear(uint8 color) {
    uint16_t* ptr = (uint16_t*)dsi_getCurrentFrameBuffer();
    if (!ptr) return;
    int w = dsi_getDisplayXSize();
    int h = dsi_getDisplayYSize();
    uint32_t* p32 = (uint32_t*)ptr;
    int total = (w * h) / 2;
    for (int i = 0; i < total; i++) p32[i] = 0;
}

static bitmap_t *giga_vid_lock_write(void) {
    static bitmap_t *bmp = NULL;
    if (!bmp)
        bmp = bmp_createhw(emu_buf,
                           NES_SCREEN_WIDTH,
                           NES_SCREEN_HEIGHT,
                           NES_SCREEN_WIDTH);
    return bmp;
}

static void giga_vid_free_write(int num_dirties, rect_t *dirty_rects) {}

// -----------------------------------------------------------------------------
// BLIT SYSTEM & REPORT ONLY FPS + BLIT 
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// BLIT BY 8-LINE BLOCKS (16 ROTATED COLUMNS) 
// -----------------------------------------------------------------------------
extern "C" void osd_blit_line_block(uint8_t *src_buf, int start_line, int num_lines)
{
    uint16_t* dsi_ptr = (uint16_t*)dsi_getCurrentFrameBuffer();
    if (!dsi_ptr) return;

    int screen_w = dsi_getDisplayXSize(); // 480 (Portrait axis)
    int screen_h = dsi_getDisplayYSize(); // 800 (Portrait axis)

    // Perfect 2x scale: 240 NES height -> 480 Display | 256 NES width -> 512 Display 
    int offset_x = 0;   // Centered on the 480 axis
    int offset_y = 144; // Centered on the 800 axis (800 - 512) / 2 = 144

    int stride_32 = screen_w / 2;
    
    for (int l = 0; l < num_lines; l++) {
        int nes_y = start_line + l;
        int display_col = (NES_SCREEN_HEIGHT - 1 - nes_y) * 2;
        
        for (int nes_x = 0; nes_x < NES_SCREEN_WIDTH; nes_x++) {
            uint8_t p_idx = src_buf[nes_y * NES_SCREEN_WIDTH + nes_x];
            uint32_t color = hw_palette[p_idx];

	    // In rotated mode for Giga: nes_x is the memory's Y-axis (800)
            // nes_y is the memory's X-axis (480)
            int display_row = nes_x * 2;
            uint32_t* dst_ptr = (uint32_t*)(dsi_ptr + ((offset_y + display_row) * screen_w) + offset_x + display_col);
            
            *dst_ptr = color;
            *(dst_ptr + stride_32) = color;
        }
    }
}

static void giga_vid_custom_blit(bitmap_t *bmp, int num_dirties, rect_t *dirty_rects)
{
    uint16_t* dsi_ptr = (uint16_t*)dsi_getCurrentFrameBuffer();
    if (!dsi_ptr) return;

    int screen_w = dsi_getDisplayXSize(); // 480
    int screen_h = dsi_getDisplayYSize(); // 800

    // Perfect 2x scale: 512x480 centered on 800x480
    // offset_y in memory corresponds to the horizontal screen axis (800px)
    // offset_x in memory corresponds to the vertical screen axis (480px)
    int offset_x = 0;
    int offset_y = 144; 

    int stride_32 = screen_w / 2;
    uint32_t* dst_ptr_base = (uint32_t*)(dsi_ptr + (offset_y * screen_w) + offset_x);

    uint8_t * __restrict__ src_buf = bmp->line[0];
    uint32_t blit_start = micros();

    // Outer loop: iterates through NES columns (256 columns -> 512 display lines)
    for (int nes_x = 0; nes_x < NES_SCREEN_WIDTH; nes_x++) {
        uint64_t* __restrict__ l1_64 = (uint64_t*)dst_ptr_base;
        uint64_t* __restrict__ l2_64 = (uint64_t*)(dst_ptr_base + stride_32);

	// Pointer to the end of the current column (scan from bottom to top for rotation)
        const uint8_t* p_src = src_buf + (NES_SCREEN_HEIGHT - 1) * NES_SCREEN_WIDTH + nes_x;

	// Unroll 8x: 240 pixels high NES / 8 = 30 passes
        for (int i = 0; i < 30; i++) {
            uint32_t c1 = hw_palette[p_src[0]];
            uint32_t c2 = hw_palette[p_src[-NES_SCREEN_WIDTH]];
            uint64_t out1 = ((uint64_t)c2 << 32) | c1;

            uint32_t c3 = hw_palette[p_src[-2*NES_SCREEN_WIDTH]];
            uint32_t c4 = hw_palette[p_src[-3*NES_SCREEN_WIDTH]];
            uint64_t out2 = ((uint64_t)c4 << 32) | c3;

            uint32_t c5 = hw_palette[p_src[-4*NES_SCREEN_WIDTH]];
            uint32_t c6 = hw_palette[p_src[-5*NES_SCREEN_WIDTH]];
            uint64_t out3 = ((uint64_t)c6 << 32) | c5;

            uint32_t c7 = hw_palette[p_src[-6*NES_SCREEN_WIDTH]];
            uint32_t c8 = hw_palette[p_src[-7*NES_SCREEN_WIDTH]];
            uint64_t out4 = ((uint64_t)c8 << 32) | c7;

            p_src -= (NES_SCREEN_WIDTH * 8);

            l1_64[0] = out1; l1_64[1] = out2; l1_64[2] = out3; l1_64[3] = out4;
            l2_64[0] = out1; l2_64[1] = out2; l2_64[2] = out3; l2_64[3] = out4;
            l1_64 += 4;
            l2_64 += 4;
        }
        dst_ptr_base += (stride_32 * 2);
    }
    uint32_t blit_time = micros() - blit_start;

    dsi_drawCurrentFrameBuffer(true);

    static uint32_t frame_count = 0;
    static uint32_t last_time = 0;
    static uint32_t total_blit_time = 0;
    
    frame_count++;
    total_blit_time += blit_time;
    
    if (frame_count >= 60) {
        uint32_t now = millis();
        float fps = 60000.0f / (now - last_time);
        float avg_blit = (float)total_blit_time / 60.0f;
        
        DEBUG_PRINT("FPS: ");
        DEBUG_PRINT(fps);
        DEBUG_PRINT(" | Avg Blit: ");
        DEBUG_PRINT(avg_blit);
        DEBUG_PRINTLN(" us");
        
        last_time = now;
        frame_count = 0;
        total_blit_time = 0;
    }
}

viddriver_t gigaDriver = {
    "Giga CPU Master",
    giga_vid_init,
    giga_vid_shutdown,
    giga_vid_set_mode,
    giga_vid_set_palette,
    giga_vid_clear,
    giga_vid_lock_write,
    giga_vid_free_write,
    giga_vid_custom_blit,
    false
};

// -----------------------------------------------------------------------------
// OSD & MAIN CORE
// -----------------------------------------------------------------------------
void osd_getvideoinfo(vidinfo_t *info) {
    info->default_width = NES_SCREEN_WIDTH;
    info->default_height = NES_SCREEN_HEIGHT;
    info->driver = &gigaDriver;
}

void osd_getmouse(int *x, int *y, int *button) {}

void osd_getinput(void) {
    osd_poll_inputs();
    audio_pump();
    yield();

    static uint32_t sram_first_dirty_tick = 0;

    // Auto-save logic: 
    if (sram_dirty) {
        if (sram_first_dirty_tick == 0) {
            sram_first_dirty_tick = nofrendo_ticks;
        }

	/*
	 * Trigger asynchronous saving if:
         * 1. 60 ticks (1 second) have passed since the last game write.
         * 2. Or if 900 ticks (15 seconds) have passed since the first change was detected (forced save).
         */
        if (((uint32_t)(nofrendo_ticks - sram_last_write) > 60) || 
            ((uint32_t)(nofrendo_ticks - sram_first_dirty_tick) > 900)) {
            
            nes_t *machine = nes_getcontextptr();
            if (machine && machine->rominfo) {
                rom_savesram_async(machine->rominfo);
		/* rom_savesram_async sets sram_dirty to 0 */
            }
            sram_first_dirty_tick = 0;
        }
    } else {
        sram_first_dirty_tick = 0;
    }
}

void* mem_alloc(int size, bool prefer_fast_memory) {
    void* ptr = malloc(size);
    if (!ptr) ptr = SDRAM.malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
        SCB_CleanDCache_by_Addr((uint32_t*)ptr, size);
    }
    return ptr;
}

int osd_installtimer(float frequency, void *func, int funcsize,
                     void *counter, int countersize) {
    nes_timer_callback = (void (*)(void))func;
    nesTicker.attach(ticker_handler,
                     std::chrono::microseconds((int)(1000000.0f / frequency)));
    return 0;
}

extern "C" {

static float current_refresh_hz = 0.0f;

void osd_setup_video(float hz)
{ 
    (void)hz;
    return;
}

int osd_init(float hz) {
    nofrendo_log_chain_logfunc(logprint);

    /* 1. Turn on SDRAM */
    SDRAM.begin();

    /* 2. Configure the hardware according to the detected region BEFORE starting */
    if (hz > 0.0f) {
        current_refresh_hz = hz;
    } else {
        current_refresh_hz = 59.94f; // NTSC by default
    }
    
    /* * WARNING: CRITICAL HARDWARE PARAMETERS.
     * These values override the Arduino Core defaults to enable correct 50Hz/60Hz
     * synchronization for PAL/NTSC. Incorrect modification of pixel_clock or 
     * timing porches (hactive, vactive, etc.) may lead to display instability
     * or potential hardware damage. DO NOT MODIFY unless necessary.
     */

    // 3. Inject optimized values based on region (NTSC < 55Hz < PAL)
    // 60Hz (59.94): 28400 | 50Hz (50.00): 23700 
    if (current_refresh_hz < 55.0f) {
        envie_known_modes[EDID_MODE_480x800_60Hz].pixel_clock = 23700; // PAL 50Hz
    } else {
        envie_known_modes[EDID_MODE_480x800_60Hz].pixel_clock = 28400; // NTSC 60Hz
    }
    envie_known_modes[EDID_MODE_480x800_60Hz].hactive     = 480;
    envie_known_modes[EDID_MODE_480x800_60Hz].hback_porch = 40;
    envie_known_modes[EDID_MODE_480x800_60Hz].hfront_porch= 40;
    envie_known_modes[EDID_MODE_480x800_60Hz].hsync_len   = 8;
    envie_known_modes[EDID_MODE_480x800_60Hz].vactive     = 800;
    envie_known_modes[EDID_MODE_480x800_60Hz].vback_porch = 20;
    envie_known_modes[EDID_MODE_480x800_60Hz].vfront_porch= 10;
    envie_known_modes[EDID_MODE_480x800_60Hz].vsync_len   = 4;

    Display.begin();
    backlight.begin();
    backlight.set(100);

    osd_init_sound();
    osd_nes_init();
    return 0;
}

void osd_shutdown() {}

char configfilename[] = "nofrendo.cfg";

int osd_main(int argc, char *argv[]) {
    config.filename = configfilename;
    return main_loop(argv[0], system_autodetect);
}

void osd_fullname(char *fullname, const char *shortname) {
    strncpy(fullname, shortname, PATH_MAX);
}

char *osd_newextension(char *string, char *ext) {
    size_t l = strlen(string);
    if (l > 3) {
        string[l-3] = ext[1];
        string[l-2] = ext[2];
        string[l-1] = ext[3];
    }
    return string;
}

int osd_makesnapname(char *filename, int len) { return -1; }

} // extern "C"

