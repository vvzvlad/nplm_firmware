#include <framebuffer.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <SPI.h>

class  aFrameBuffer : public Adafruit_GFX {
  public:
    uint16_t *buffer;
		int16_t fb_width;
		int16_t fb_height;
		uint8_t tft_spi_dc;
		uint8_t tft_spi_cs;
		Adafruit_ST7735 *display_tft;

    aFrameBuffer(int16_t w, int16_t h, Adafruit_ST7735 *tft, uint8_t spi_dc, uint8_t spi_cs): Adafruit_GFX(w, h)
    {
			int16_t fb_width = w;
			int16_t fb_height = h;
			uint8_t tft_spi_dc = spi_dc;
			uint8_t tft_spi_cs = spi_cs;
			display_tft = tft;
      buffer = (uint16_t*)malloc(2 * h * fb_width);
      for (int i = 0; i < fb_height * fb_width; i++)
        buffer[i] = 0;
    }
		void init()
    {
      display_tft->initR(INITR_BLACKTAB);
      display_tft->fillScreen(ST7735_TFT_BLACK);
    }
    void drawPixel( int16_t x, int16_t y, uint16_t color)
    {
      if (x > fb_width-1)
        return;
      if (x < 0)
        return;
      if (y > fb_height-1)
        return;
      if (y < 0)
        return;
      buffer[x + y * _width] = color;
    }

    void display()
    {
      display_tft->setAddrWindow(0, 0, fb_height, fb_height);
      digitalWrite(tft_spi_dc, HIGH);
      digitalWrite(tft_spi_cs, LOW);
      SPI.beginTransaction(SPISettings(80000000, MSBFIRST, SPI_MODE0));
      for (uint16_t i = 0; i < fb_height * fb_height; i++)
      {
        SPI.write16(buffer[i]);
      }
      SPI.endTransaction();
      digitalWrite(tft_spi_cs, HIGH);
    }
};
