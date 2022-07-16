#include <Adafruit_GFX.h>    // Core graphics library

class  aFrameBuffer : public Adafruit_GFX {
  public:
    uint16_t *buffer;
    int16_t fb_width;
    int16_t fb_height;
    uint8_t tft_spi_dc;
    uint8_t tft_spi_cs;
    Adafruit_ST7735 *display_tft;

    aFrameBuffer(int16_t w, int16_t h, Adafruit_ST7735 *tft, uint8_t spi_dc, uint8_t spi_cs);

    void init();
    void drawPixel( int16_t x, int16_t y, uint16_t color);
    void display();
};
