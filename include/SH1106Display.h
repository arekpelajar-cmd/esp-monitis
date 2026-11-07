#ifndef SH1106_DISPLAY_H
#define SH1106_DISPLAY_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>

#define SH110X_BLACK 0
#define SH110X_WHITE 1
#define SH110X_INVERSE 2

#ifndef SSD1306_BLACK
#define SSD1306_BLACK 0
#endif
#ifndef SSD1306_WHITE
#define SSD1306_WHITE 1
#endif
#ifndef SSD1306_INVERSE
#define SSD1306_INVERSE 2
#endif

// Driver Universal & Mandiri untuk OLED 1.3" (SH1106) dan 0.96" (SSD1306)
// Mendukung inisialisasi ganda charge-pump agar pasti menyala di kedua jenis layar
class Adafruit_SH1106G : public Adafruit_GFX {
private:
    uint8_t buffer[1024];
    uint8_t i2c_addr;
    TwoWire* wire;
    uint8_t colOffset; // 2 untuk SH1106 1.3", 0 untuk SSD1306 0.96"

    void sendCommands(const uint8_t* cmds, size_t len) {
        wire->beginTransmission(i2c_addr);
        wire->write(0x00); // Co = 0, D/C# = 0 (Command stream)
        for (size_t i = 0; i < len; i++) {
            wire->write(cmds[i]);
        }
        wire->endTransmission();
    }

public:
    Adafruit_SH1106G(int16_t w, int16_t h, TwoWire* tw = &Wire, int8_t rst = -1) 
        : Adafruit_GFX(w, h), i2c_addr(0x3C), wire(tw), colOffset(2) {
        memset(buffer, 0, sizeof(buffer));
    }

    void setColumnOffset(uint8_t offset) {
        colOffset = offset;
    }

    bool begin(uint8_t addr = 0x3C, bool reset = true) {
        // 1. Deteksi Alamat I2C Otomatis (Cek 0x3C dulu, lalu 0x3D)
        wire->beginTransmission(addr);
        if (wire->endTransmission() == 0) {
            i2c_addr = addr;
        } else {
            wire->beginTransmission(0x3D);
            if (wire->endTransmission() == 0) {
                i2c_addr = 0x3D;
            } else {
                wire->beginTransmission(0x3C);
                i2c_addr = 0x3C; // Default fallback
                Serial.println("[OLED] Peringatan: I2C tidak merespon di 0x3C / 0x3D. Periksa kabel SDA/SCL!");
            }
        }
        Serial.printf("[OLED] Inisialisasi display pada I2C Address: 0x%02X\n", i2c_addr);

        // 2. Kirim Urutan Inisialisasi Lengkap (Kompatibel SH1106 & SSD1306)
        // Diperlukan pengiriman multi-byte dalam 1 stream agar internal DC-DC charge pump aktif
        const uint8_t initSeq1[] = {
            0xAE,             // Display OFF
            0x00,             // Set Lower Column Address
            0x10,             // Set Higher Column Address
            0x40,             // Set Display Start Line (0)
            0xB0,             // Set Page Address (0)
            0x81, 0xCF,       // Set Contrast Control (207)
            0xA1,             // Set Segment Re-map (A1 = kolom 127 dimetakan ke SEG0)
            0xA6,             // Set Normal Display (bukan Inverse)
            0xA8, 0x3F,       // Set Multiplex Ratio (64MUX)
            0xC8,             // Set COM Output Scan Direction (re-mapped)
            0xD3, 0x00,       // Set Display Offset (0)
            0xD5, 0x80,       // Set Display Clock Divide / Oscillator Frequency
            0xD9, 0xF1,       // Set Pre-charge Period
            0xDA, 0x12,       // Set COM Pins Hardware Configuration
            0xDB, 0x40,       // Set VCOM Deselect Level
            0x20, 0x02        // Page Addressing Mode
        };
        sendCommands(initSeq1, sizeof(initSeq1));

        // 3. AKTIFKAN CHARGE PUMP (PENTING AGAR LAYAR MENYALA!)
        // Untuk SSD1306 (0.96"): 0x8D, 0x14
        const uint8_t pumpSSD1306[] = { 0x8D, 0x14 };
        sendCommands(pumpSSD1306, sizeof(pumpSSD1306));

        // Untuk SH1106 (1.3"): 0xAD, 0x8B (Aktifkan internal DC-DC) dan 0x33 (Voltase 9.0V)
        const uint8_t pumpSH1106[] = { 0xAD, 0x8B, 0x33 };
        sendCommands(pumpSH1106, sizeof(pumpSH1106));

        // 4. Nyalakan Layar (Display ON)
        const uint8_t initSeq2[] = {
            0xA4,             // Entire Display ON (output follows RAM)
            0xAF              // Display ON!
        };
        sendCommands(initSeq2, sizeof(initSeq2));

        clearDisplay();
        display();
        return true;
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if ((x < 0) || (x >= width()) || (y < 0) || (y >= height())) return;
        switch (color) {
            case 1: // WHITE
                buffer[x + (y / 8) * 128] |= (1 << (y & 7));
                break;
            case 0: // BLACK
                buffer[x + (y / 8) * 128] &= ~(1 << (y & 7));
                break;
            case 2: // INVERSE
                buffer[x + (y / 8) * 128] ^= (1 << (y & 7));
                break;
        }
    }

    void clearDisplay() {
        memset(buffer, 0, sizeof(buffer));
    }

    void display() {
        for (uint8_t page = 0; page < 8; page++) {
            // Set alamat page dan offset kolom (0x02 untuk SH1106, 0x00 untuk SSD1306)
            wire->beginTransmission(i2c_addr);
            wire->write(0x00); // Command stream
            wire->write(0xB0 + page);
            wire->write(colOffset & 0x0F);       // Lower column address
            wire->write(0x10 | (colOffset >> 4)); // Higher column address
            wire->endTransmission();

            // Kirim 128 byte data piksel dalam 4 chunk (masing-masing 32 byte batas buffer I2C Wire ESP32)
            for (uint8_t col = 0; col < 128; col += 32) {
                wire->beginTransmission(i2c_addr);
                wire->write(0x40); // Data stream
                wire->write(&buffer[page * 128 + col], 32);
                wire->endTransmission();
            }
        }
    }
};

#endif
