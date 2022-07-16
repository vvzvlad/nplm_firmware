#include <Adafruit_GFX.h>    // Core graphics library

class  aFrameBuffer : public Adafruit_GFX {
  public:
    aFrameBuffer(int16_t w, int16_t h, Adafruit_ST7735 *tft, uint8_t spi_dc, uint8_t spi_cs): Adafruit_GFX(w, h);

    void init();
    void drawPixel( int16_t x, int16_t y, uint16_t color);
    void display();
};
