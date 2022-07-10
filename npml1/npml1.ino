

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <SPI.h>
#include <BH1750FVI.h>
#include <TickerScheduler.h>

#include <EncButton.h>

#include <ESP8266WiFi.h>

#define ST7735_TFT_CS         4 //D2  //белый
#define ST7735_TFT_RST        -1  //желтый
#define ST7735_TFT_DC         5 //D1  //синий
#define BUTTON_PIN         16

Adafruit_ST7735 tft = Adafruit_ST7735(ST7735_TFT_CS, ST7735_TFT_DC, ST7735_TFT_RST);
BH1750FVI LightSensor(BH1750FVI::k_DevModeContLowRes);
EncButton<EB_CALLBACK, BUTTON_PIN> btn(INPUT);

TickerScheduler ts(5);


#define ST7735_TFT_BLACK 0x0000
#define ST7735_TFT_BLUE 0x001F
#define ST7735_TFT_RED 0xF800
#define ST7735_TFT_GREEN 0x07E0
#define ST7735_TFT_CYAN 0x07FF
#define ST7735_TFT_MAGENTA 0xF81F
#define ST7735_TFT_YELLOW 0xFFE0
#define ST7735_TFT_WHITE 0xFFFF

#define graph_width 128
#define graph_height 50

#define graph_width_divider 4
#define graph_height_divider 20

#define CORRECTION_NUM_SAMPLES 512
#define MEASURE_NUM_SAMPLES 512
#define SYNC_NUM_SAMPLES 256

#define MAX_FREQ 300

uint16_t adc_values[MEASURE_NUM_SAMPLES] = {};

uint16_t adc_values_correction = 0;


void get_adc_correction_value() {
	uint32_t adc_values_sum = 0;

	for (uint16_t i=0; i<CORRECTION_NUM_SAMPLES; i++) {
		adc_values_sum = adc_values_sum + system_adc_read();
	}
	adc_values_correction = adc_values_sum/CORRECTION_NUM_SAMPLES;
	Serial.print("Correction:");
	Serial.print(adc_values_correction);
	Serial.print("\n\n");

}


void make_graph() {
	uint8_t graph_values[graph_width] = {};

	uint16_t adc_values_max = 0;
	uint8_t adc_values_multiplier = 0;
	for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
		if (adc_values[i] > adc_values_max) {adc_values_max = adc_values[i];}
	}
	adc_values_max = adc_values_max/100*10+adc_values_max;
	adc_values_multiplier = 1024/adc_values_max;


	for (uint16_t i=0; i < graph_width; i++) {
		graph_values[i] = (adc_values[i*graph_width_divider+0]*adc_values_multiplier+
												adc_values[i*graph_width_divider+1]*adc_values_multiplier+
												adc_values[i*graph_width_divider+2]*adc_values_multiplier+
												adc_values[i*graph_width_divider+3]*adc_values_multiplier)
												/graph_width_divider/graph_height_divider;
		if (graph_values[i] > 50) {graph_values[i] = 50;}
	}

	for (uint8_t col=0; col < 128; col++) {
		tft.drawLine(col, 110, col, 160, ST7735_TFT_BLACK);
		if (col == 0) {
			tft.drawPixel(col, 160-graph_values[col], ST7735_TFT_GREEN);
		}
		else {
			tft.drawLine(col-1, 160-graph_values[col-1], col, 160-graph_values[col], ST7735_TFT_GREEN);
		}
	}


	//Serial.print("\n");
	//for (uint8_t col=0; col < 128; col++) {
	//	Serial.print(graph_values[col]);
	//	Serial.print("NN");
	//}
	//Serial.print("\n");
}

void get_adc() {
	uint16_t adc_values_max = 0;
	uint16_t adc_values_min = 1024;
	uint16_t adc_values_avg = 0;
	uint16_t adc_values_min_max_mean = 0;
	uint32_t adc_values_sum = 0;
	float flicker_gost = 0;
	float flicker_simple = 0;

	//Serial.print("\n\n");
	uint32_t catch_start_time = 0;
	uint32_t catch_stop_time = 0;

  //Disable everything that can interrupt measurements (interruptions, WIFI)
	wifi_set_opmode(NULL_MODE);
	system_soft_wdt_stop();
	ESP.wdtDisable();
	ets_intr_lock( ); //close interrupt
	noInterrupts();

	// Synchronization of measurements with the waveform to prevent graph drift
	for (uint16_t i=0; i<SYNC_NUM_SAMPLES; i++) { adc_values_sum = adc_values_sum + system_adc_read(); }
	adc_values_avg = adc_values_sum/SYNC_NUM_SAMPLES;

	for (uint16_t i=0; i<SYNC_NUM_SAMPLES; i++) { if (system_adc_read() > adc_values_avg) break; }
	for (uint16_t i=0; i<SYNC_NUM_SAMPLES; i++) { if (system_adc_read() < adc_values_avg) break; }
	adc_values_sum = 0;
	//The next measurement will occur in the middle of the wave

	catch_start_time = micros();
	for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
		adc_values[i] = system_adc_read();
	}
	catch_stop_time = micros();

	//Allow interrupts back
	interrupts();
	ets_intr_unlock(); //open interrupt
	system_soft_wdt_restart();

	//Подсчет максимального, минимального, среднего, среднего между максимальным и минимальным значением
	for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
		if (adc_values[i] >= adc_values_correction) {adc_values[i] = adc_values[i] - adc_values_correction;}
		else {adc_values[i] = 0;}

		if (adc_values[i] > adc_values_max) {adc_values_max = adc_values[i];}
		if (adc_values[i] < adc_values_min) {adc_values_min = adc_values[i];}
		adc_values_sum = adc_values_sum + adc_values[i];
	}
	adc_values_avg = adc_values_sum/MEASURE_NUM_SAMPLES;
	adc_values_min_max_mean = (adc_values_max-adc_values_min)/2+adc_values_min;

	//Дебаг вывод буфера измерений
	Serial.print("adc_values:\n");
	for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
		Serial.print(adc_values[i]);
		Serial.print("NN");
	}
	Serial.print("\n");


	Serial.print("adc_values_min_max_mean:");
	Serial.print(adc_values_min_max_mean);
	Serial.print("\n");

	//Вычисление уровня мерцаний
	flicker_gost = ((float)adc_values_max-(float)adc_values_min)*100/(2*(float)adc_values_avg);
	flicker_simple = ((float)adc_values_max-(float)adc_values_min)*100/((float)adc_values_max+(float)adc_values_min);


	//Frequency counting
	uint16_t adc_mean_values[MEASURE_NUM_SAMPLES] = {};
	uint16_t adc_mean_values_i = 0;
	uint16_t adc_mean_values_last_value = adc_values[0];
	uint16_t adc_mean_values_last_number = MEASURE_NUM_SAMPLES;
	for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
		if (adc_mean_values_last_value <= adc_values_min_max_mean && adc_values[i] >= adc_values_min_max_mean)
		{
			int16_t upper_threshold = adc_mean_values_last_number + 15;
			if (upper_threshold > MEASURE_NUM_SAMPLES) upper_threshold = MEASURE_NUM_SAMPLES;
			int16_t low_threshold = adc_mean_values_last_number - 15;
			if (low_threshold < 0) low_threshold = 0;
			if (i < low_threshold && i > upper_threshold)
			{
				adc_mean_values[adc_mean_values_i] = i;
				adc_mean_values_last_number = i;
				adc_mean_values_i++;
			}
		}
		adc_mean_values_last_value = adc_values[i];
	}

	//Дебаг вывод для подсчета частоты
	Serial.print("adc_mean_values:\n");
	for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
		Serial.print(adc_mean_values[i]);
		Serial.print("NN");
	}
	Serial.print("\n");

	//Serial.print("Avg:");
	//Serial.print(adc_values_avg);
	//Serial.print(", Max:");
	//Serial.print(adc_values_max);
	//Serial.print(", min:");
	//Serial.print(adc_values_min);
	//Serial.print(", correction:");
	//Serial.print(adc_values_correction);
	//Serial.print(", flicker_gost:");
	//Serial.print(flicker_gost);
	//Serial.print(", flicker_simple:");
	//Serial.print(flicker_simple);

	//tft.fillRoundRect(0, 0, 128, 110, 0, ST7735_TFT_BLACK);
	tft.setCursor(0, 0);
	tft.setTextColor(ST7735_TFT_GREEN, ST7735_TFT_BLACK);
	tft.setTextSize(1);
	tft.println((String)"Flicker GOST:"+flicker_gost);
	tft.println((String)"Flicker simple:"+flicker_simple);
	tft.println((String)"Correction:"+adc_values_correction);
	tft.println((String)"Average:"+adc_values_avg);
	tft.println((String)"Max:"+adc_values_max);
	tft.println((String)"Min:"+adc_values_min);
	tft.println((String)"Tm:"+((float)(catch_stop_time-catch_start_time)/1000)+"ms");
	tft.println((String)"Freq:"+"0");
	tft.println((String)"Light:"+LightSensor.GetLightIntensity()+" lx");


	make_graph();
}


void measure_light() {
  uint16_t lux = LightSensor.GetLightIntensity();
  Serial.print("Light: ");
  Serial.print(lux);
  Serial.print(" lX\n");
}

void isr() {
  btn.tickISR();
}

void button_click_handler() {
  Serial.print("Click\n");
	get_adc();
}

void button_holded_handler() {
  Serial.print("Holded\n");
}

void button_hold_handler() {
  Serial.print("Hold\n");
}

void myClicks() {
  Serial.print("CLICKS_HANDLER: ");
  Serial.println(btn.clicks);
}


void setup(void) {
	WiFi.persistent(false); //Отключить запись настроек wifi во флеш
	WiFi.mode(WIFI_OFF); //отключить wifi
	WiFi.forceSleepBegin(); //отключить радиомодуль

	//attachInterrupt(BUTTON_PIN, isr, CHANGE); //прерывания кнопки
	btn.setButtonLevel(HIGH);
	btn.setHoldTimeout(1000);
	btn.attach(CLICK_HANDLER, button_click_handler);
	btn.attach(HOLDED_HANDLER, button_holded_handler);
	btn.attach(HOLD_HANDLER, button_hold_handler);
	//btn.attach(CLICKS_HANDLER, myClicks);
  //btn.attachClicks(5, fiveClicks);

  Serial.begin(115200);
  Serial.print("NPLM-1");

  LightSensor.begin();

  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(ST77XX_BLACK);

	//get_adc_correction_value();

	//ts.add(0, 2000, [&](void *) { measure_light(); }, nullptr, true);
	//ts.add(1, 100, [&](void *) { get_adc(); }, nullptr, true);


}

void loop() {
  ts.update();
	btn.tick();

	//if (btn.click()) Serial.println("click");
	//if (btn.held()) Serial.println("held");
	//if (btn.hasClicks(2)) Serial.println("action 2 clicks");
}
