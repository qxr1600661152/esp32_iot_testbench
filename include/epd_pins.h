#pragma once

#if defined(EPD_TARGET_S3) || defined(CONFIG_IDF_TARGET_ESP32S3)

#define EPD_BUSY  42
#define EPD_RST   41
#define EPD_DC    40
#define EPD_CS    39
#define EPD_SCK   38
#define EPD_MOSI  4

#define EPD_MISO  (-1)

#if (EPD_MOSI >= 35 && EPD_MOSI <= 37) || (EPD_SCK >= 35 && EPD_SCK <= 37) ||   \
    (EPD_MISO >= 35 && EPD_MISO <= 37)
#error "EPD SPI pin on GPIO35-37 conflicts with Octal PSRAM on S3 N16R8"
#endif

#define EPD_RST_NONE (-1)
#define EPD_BUSY_NONE (-1)

#else

#define EPD_BUSY  4
#define EPD_CS    1
#define EPD_DC    2
#define EPD_RST   3
#define EPD_MOSI  5
#define EPD_SCK   0
#define EPD_MISO  10
#define EPD_RST_NONE (-1)
#define EPD_BUSY_NONE (-1)

#endif
