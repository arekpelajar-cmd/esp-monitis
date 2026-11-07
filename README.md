# Smart IV Drip Monitor (Dual-Channel) - ESP32 Firmware

Sistem pemantauan dan kontrol otomatis laju tetesan infus dual-channel menggunakan ESP32, FreeRTOS, MQTT, dan OLED 0.96".

## Fitur Arsitektur
- **Dual-Channel Indepeden:** Mengontrol 2 infus sekaligus (Channel A & B).
- **FreeRTOS Real-time Multitasking:** Core 1 untuk PID & pembacaan sensor IR interrupt, Core 0 untuk UI Display OLED & Komunikasi MQTT.
- **Thread Safety (Mutex):** Memproteksi data bersama menggunakan FreeRTOS Mutex.
- **13 Layar UI Flow:** Menggunakan 1 unit Rotary Encoder untuk navigasi menu, pengaturan DPM, diagnostik, hingga riwayat alarm.

## Skema Pinout
- **Channel A:** Sensor IR (GPIO 27), Servo PWM (GPIO 18)
- **Channel B:** Sensor IR (GPIO 14), Servo PWM (GPIO 19)
- **Rotary Encoder:** CLK (GPIO 25), DT (GPIO 26), SW (GPIO 32)
- **OLED 0.96 I2C:** SDA (GPIO 21), SCL (GPIO 22)

## Catatan Catu Daya (PENTING)
Gunakan adaptor 5V eksternal (minimal 2A) khusus untuk motor servo. Hubungkan Common GND antara ESP32 dan adaptor 5V eksternal.
