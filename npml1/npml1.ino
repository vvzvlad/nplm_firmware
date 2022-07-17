#include <ESP8266WiFi.h>

#include <Adafruit_GFX.h>    				// Core graphics library
#include <Adafruit_ST7735.h> 				// Hardware-specific library for ST7735
#include <Adafruit_GFX_Buffer.h>		// Framebuffer library to speed up rendering
#include <utf8rus.h>
#include <verdana_bold_12.h>

#include <SPI.h>

#include <BH1750FVI.h>

#include <TickerScheduler.h>
#include <EncButton.h>
#include <images.h>


//Debug: Serial.println(__LINE__);

#define ST7735_TFT_CS         	4 		//D2  //белый https://cdn.compacttool.ru/images/docs/Wemos_D1_mini_pinout.jpg
#define ST7735_TFT_RST        	-1  	//желтый
#define ST7735_TFT_DC         	5 		//D1  //синий
#define BUTTON_PIN         			16

//Screen resolution
#define ST7735_TFT_WIDTH 				128
#define ST7735_TFT_HEIGHT 			160

//Colors in RGB565 format: rrrrrggg:gggbbbbb
#define ST7735_TFT_BLACK 				0x0000
#define ST7735_TFT_BLUE 				0x001F
#define ST7735_TFT_RED 					0xF800
#define ST7735_TFT_GREEN 				0x07E0
#define ST7735_TFT_CYAN 				0x07FF
#define ST7735_TFT_MAGENTA 			0xF81F
#define ST7735_TFT_YELLOW 			0xFFE0
#define ST7735_TFT_WHITE 				0xFFFF


//Graph defines
#define GRAPH_WIDTH 						ST7735_TFT_WIDTH
#define GRAPH_HEIGHT 						50
#define GRAPH_WIDTH_DIVIDER 		4   // 512(samples number, MEASURE_NUM_SAMPLES) -> 128 (chart width, GRAPH_WIDTH)
#define GRAPH_HEIGHT_DIVIDER 		20  // 1024(single-count resolution, MAX_ADC_VALUE) ->  50(chart height, GRAPH_HEIGHT)
#define GRAPH_X 								0
#define GRAPH_Y 								ST7735_TFT_HEIGHT-GRAPH_HEIGHT
#define LUMINOFOR_EM_MODE_DEEP 	3

#define CORRECTION_NUM_SAMPLES 	512
#define MEASURE_NUM_SAMPLES 		512
#define SYNC_NUM_SAMPLES 				256

#define MAX_ADC_VALUE 					1024

#define MAX_FREQ 								300

typedef Adafruit_ST7735 display_t;
typedef Adafruit_GFX_Buffer<display_t> GFXBuffer_t;
GFXBuffer_t framebuffer = GFXBuffer_t(128, 160, display_t(ST7735_TFT_CS, ST7735_TFT_DC, ST7735_TFT_RST));
BH1750FVI LightSensor(BH1750FVI::k_DevModeContLowRes);
EncButton<EB_CALLBACK, BUTTON_PIN> btn(INPUT);
TickerScheduler ts(5);

volatile uint16_t GLOBAL_adc_correction = 0;

volatile uint8_t GLOBAL_flicker_gost_uint = 0;
volatile uint8_t GLOBAL_flicker_simple_uint = 0;
volatile uint16_t GLOBAL_freq = 0;
volatile uint16_t GLOBAL_adc_values_max = 1;
volatile uint8_t GLOBAL_graph_values[LUMINOFOR_EM_MODE_DEEP][GRAPH_WIDTH] = {};

volatile uint16_t GLOBAL_lum = 0;

void draw_asset(const asset_t *asset, uint8_t x, uint8_t y) {
  uint8_t h = pgm_read_byte(&asset->height);
	uint8_t w = pgm_read_byte(&asset->width);
	framebuffer.drawRGBBitmap(x, y, asset->image, w, h);
}



uint16_t get_adc_correction_value(uint16_t correction_catch_time_ms) {
	uint32_t adc_values_sum = 0;
	uint8_t one_step_time_ms = 10;
	uint16_t steps = correction_catch_time_ms/one_step_time_ms;

	for (uint16_t i=0; i<steps; i++) {
		adc_values_sum = adc_values_sum + system_adc_read();
		delay(one_step_time_ms);
	}
	uint16_t correction_value = adc_values_sum/steps;
	//Serial.print("Correction:");
	//Serial.print(correction_value);
	//Serial.print("\n\n");

	return correction_value;
}



void render_flicker_screen() {
	//uint16_t GLOBAL_freq = freq;
	//uint16_t GLOBAL_adc_values_max = adc_values_max;

	framebuffer.fillRoundRect(0, 0, 128, 160-GRAPH_HEIGHT, 0, ST7735_TFT_BLACK); //Очистка только избражения над графиком!
	uint8_t flicker = GLOBAL_flicker_simple_uint; //or GLOBAL_flicker_gost_uint

	if (flicker >= 0 && flicker <= 5) {
		draw_asset(&flicker_msg_good_lamp, 0, 0);
	}
	else if (flicker > 5 && flicker <= 30) {
		draw_asset(&flicker_msg_normal_lamp, 0, 0);
	}
	else if (flicker > 30) {
		draw_asset(&flicker_msg_bad_lamp, 0, 0);
	}
	else {
		Serial.println((String)"Flicker simple:"+flicker);
	}
	draw_asset(&flicker_rainbow, 0, 39);
	draw_asset(&arrow, 10, 56);
	draw_asset(&flicker_text_flicker_level, 0, 61);

	framebuffer.setFont(&verdana_bold12pt7b);
	framebuffer.setCursor(30, 100);
	framebuffer.setTextSize(1);
	framebuffer.println((String)flicker+"%");
	framebuffer.setFont();


	framebuffer.setCursor(0, 150);
	framebuffer.setTextColor(ST7735_TFT_WHITE);
	framebuffer.setTextSize(1);
	//framebuffer.println((String)"Flicker simple:"+flicker_simple_uint);
	//framebuffer.println((String)"Correction:"+GLOBAL_adc_correction);
	//framebuffer.println((String)"Average:"+adc_values_avg);
	//framebuffer.println((String)"Max:"+adc_values_max);
	//framebuffer.println((String)"Min:"+adc_values_min);
	//framebuffer.println((String)"Tm:	"+((float)catch_time_us/1000)+"ms");
	framebuffer.println((String)"Freq:"+GLOBAL_freq+" Hz");
	//framebuffer.println((String)"Light:"+LightSensor.GetLightIntensity()+" lx");




	//Graph render

	for (uint8_t row=GRAPH_Y; row<GRAPH_Y+GRAPH_HEIGHT; row++) {
    for (uint8_t col=GRAPH_X; col<GRAPH_WIDTH; col++) {
      framebuffer.drawPixel(col, row, ST7735_TFT_GREEN);
    }
  }
	//uint16_t current_pixel = framebuffer.getPixel(100, 100);
//
	//for (uint8_t current_colon = GRAPH_X; current_colon < GRAPH_WIDTH; current_colon++) {
	//	if (current_colon == 0) {
	//		framebuffer.drawPixel(current_colon, //The first point is drawn as a pixel because it has no past point
	//									ST7735_TFT_HEIGHT-GLOBAL_graph_values[0][current_colon],
	//									ST7735_TFT_GREEN);
	//	}
	//	else {
	//		framebuffer.drawLine(current_colon-1,  //The following points are drawn as lines to prevent gaps between individual points on steep hillsides
	//									ST7735_TFT_HEIGHT-GLOBAL_graph_values[0][current_colon-1],
	//									current_colon,
	//									ST7735_TFT_HEIGHT-GLOBAL_graph_values[0][current_colon],
	//									ST7735_TFT_GREEN);
	//	}
	//}

	framebuffer.display();
}




uint16_t calc_frequency(uint16_t *adc_values_array, uint16_t adc_values_min_max_mean, uint32_t catch_time_us) {
	uint16_t adc_mean_values[MEASURE_NUM_SAMPLES] = {};
	uint16_t adc_mean_values_i = 0;

	uint16_t acc=0;
	uint8_t not_found_flag = 0;

	while(not_found_flag == 0) {
		not_found_flag=1;
		for (uint16_t i=acc; i<MEASURE_NUM_SAMPLES; i++) {
			if (adc_values_array[i] > adc_values_min_max_mean)
			{
				//Serial.println((String)"i:"+i+", adc_values_array:"+adc_values_array[i]);
				//framebuffer.drawLine(i/GRAPH_WIDTH_DIVIDER, 110, i/GRAPH_WIDTH_DIVIDER, 160, ST7735_TFT_CYAN);
				//framebuffer.display();
				acc = i+20;
				not_found_flag = 0;
				break;
			}
		}

		for (uint16_t i=acc; i<MEASURE_NUM_SAMPLES; i++) {
			if (not_found_flag == 1) break;
			else not_found_flag == 1;

			if (adc_values_array[i] < adc_values_min_max_mean)
			{
				//framebuffer.drawLine(i/GRAPH_WIDTH_DIVIDER, 110, i/GRAPH_WIDTH_DIVIDER, 160, ST7735_TFT_CYAN);
				//Serial.println((String)"i:"+i+", adc_values_array:"+adc_values_array[i]);
				//framebuffer.display();
				adc_mean_values[adc_mean_values_i] = i;
				adc_mean_values_i++;
				acc = i+20;
				not_found_flag = 0;
				break;
			}
		}
	}

	uint16_t adc_mean_values_distances_sum = 0;
	float adc_mean_values_distances_mean = 0;
	uint8_t adc_mean_values_distances_i = 0;

	for (uint16_t i=1; i<MEASURE_NUM_SAMPLES; i++) {
		if (adc_mean_values[i] != 0) {
			adc_mean_values_distances_sum = adc_mean_values_distances_sum + adc_mean_values[i]-adc_mean_values[i-1];
			adc_mean_values_distances_i++;
		}
		else {
			if (adc_mean_values_distances_i != 0) {
				adc_mean_values_distances_mean = (float)adc_mean_values_distances_sum/(float)adc_mean_values_distances_i;
			}
			else {
				adc_mean_values_distances_mean = 0;
			}
		};
	}

	uint16_t sample_time_us = catch_time_us/MEASURE_NUM_SAMPLES;
	uint16_t period_time_us = sample_time_us*adc_mean_values_distances_mean;
	float freq = (float)1000000/period_time_us;

	if (freq > 2000) freq = 0;

	//Serial.print("period_time_us:");
	//Serial.print(period_time_us);

	//Debug output for frequency counting
	//Serial.print("adc_mean_values:\n");
	//for (uint16_t i=0; i<50; i++) {
	//	Serial.print(adc_mean_values[i]);
	//	Serial.print("NN");
	//}
	//Serial.print("\n");


	return (uint16_t)freq;
}

void measure_flicker() {
	uint16_t adc_values[MEASURE_NUM_SAMPLES] = {};
	uint16_t adc_values_max = 1; //inital value 1 to prevent division by zero if we get zeros in the measurement (or after applying the correction).
	uint16_t adc_values_min = MAX_ADC_VALUE;
	uint16_t adc_values_avg = 0;
	uint16_t adc_values_min_max_mean = 0;
	uint32_t adc_values_sum = 0;
	float flicker_gost = 0;
	float flicker_simple = 0;
	uint8_t flicker_gost_uint = 0;
	uint8_t flicker_simple_uint = 0;

	uint32_t catch_start_time = 0;
	uint32_t catch_stop_time = 0;
	uint32_t catch_time_us = 0;

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

	//The measurement itself
	catch_start_time = micros();
	for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
		adc_values[i] = system_adc_read();
	}
	catch_stop_time = micros();
	catch_time_us = catch_stop_time-catch_start_time;
	//End of measurement

	//Allow interrupts back
	interrupts();
	ets_intr_unlock(); //open interrupt
	system_soft_wdt_restart();

	//Debug output of the measurement buffer
	//Serial.print("adc_values:\n");
	//for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
	//	Serial.print(adc_values[i]);
	//	Serial.print("NN");
	//}
	//Serial.print("\n");

	//Applying Correction
	for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
		if (adc_values[i] <= GLOBAL_adc_correction) { adc_values[i] = 0;  }
		else { adc_values[i] = adc_values[i] - GLOBAL_adc_correction; }
	}

	//Calculating the maximum, minimum, average, average between the maximum and minimum values (for frequency measure)
	for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
		if (adc_values[i] > adc_values_max) {adc_values_max = adc_values[i];}
		if (adc_values[i] < adc_values_min) {adc_values_min = adc_values[i];}
		adc_values_sum = adc_values_sum + adc_values[i];
	}
	adc_values_avg = adc_values_sum/MEASURE_NUM_SAMPLES;
	adc_values_min_max_mean = (adc_values_max-adc_values_min)/2+adc_values_min;

	//Serial.print("adc_values_min_max_mean:");
	//Serial.print(adc_values_min_max_mean);
	//Serial.print("\n");

	//Flicker level calculation
	flicker_gost = ((float)adc_values_max-(float)adc_values_min)*100/(2*(float)adc_values_avg);
	flicker_simple = ((float)adc_values_max-(float)adc_values_min)*100/((float)adc_values_max+(float)adc_values_min);
	flicker_gost_uint = (uint8_t)flicker_gost;
	flicker_simple_uint = (uint8_t)flicker_simple;

	//Flicker frequency calculation
	uint16_t freq = calc_frequency(adc_values, adc_values_min_max_mean, catch_time_us);

	GLOBAL_flicker_gost_uint = flicker_gost_uint;
	GLOBAL_flicker_simple_uint = flicker_simple_uint;
	GLOBAL_freq = freq;
	GLOBAL_adc_values_max = adc_values_max;


	//Graph data
	uint8_t adc_values_multiplier = 0;

	adc_values_max = adc_values_max/100*10+adc_values_max;
	if (adc_values_max == 0) { adc_values_max = 1; };
	adc_values_multiplier = MAX_ADC_VALUE/adc_values_max;

	for (uint16_t i=0; i < GRAPH_WIDTH; i++) {
		GLOBAL_graph_values[0][i] = (adc_values[i*GRAPH_WIDTH_DIVIDER+0]*adc_values_multiplier+  //Take 4 values (=GRAPH_WIDTH_DIVIDER)
																	adc_values[i*GRAPH_WIDTH_DIVIDER+1]*adc_values_multiplier+ //from the adc values array at a time
																	adc_values[i*GRAPH_WIDTH_DIVIDER+2]*adc_values_multiplier+ //and collapse them into one
																	adc_values[i*GRAPH_WIDTH_DIVIDER+3]*adc_values_multiplier)
																	/GRAPH_WIDTH_DIVIDER/GRAPH_HEIGHT_DIVIDER;
		if (GLOBAL_graph_values[0][i] > 50) { GLOBAL_graph_values[0][i] = 50; }
	}


	//debug prints
	//Serial.print("Avg:");
	//Serial.print(adc_values_avg);
	//Serial.print(", Max:");
	//Serial.print(adc_values_max);
	//Serial.print(", min:");
	//Serial.print(adc_values_min);
	//Serial.print(", correction:");
	//Serial.print(GLOBAL_adc_correction);
	//Serial.print(", flicker_gost:");
	//Serial.print(flicker_gost);
	//Serial.print(", flicker_simple:");
	//Serial.print(flicker_simple);
}


void measure_light() {
  uint16_t lum = LightSensor.GetLightIntensity();
	GLOBAL_lum = lum;
	//framebuffer.fillScreen(ST7735_TFT_BLACK);
  //framebuffer.setCursor(0, 0);
	//framebuffer.setTextColor(ST7735_TFT_WHITE);
	//framebuffer.setTextSize(0);
	//framebuffer.println((String)"Light:"+lum+" lx");
	//framebuffer.display();
}

void isr() {
  btn.tickISR();
}

void button_click_handler() {
  Serial.print("Click\n");
}

void free_mem_print() {
	Serial.print("\nFree memory: ");
  Serial.print(system_get_free_heap_size());
	Serial.print("\n");
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

void show_startup_screen_and_get_correction() {
	draw_asset(&boot_screen, 0, 0);
	framebuffer.setCursor(34, 97);
	framebuffer.setTextColor(ST7735_TFT_WHITE);
	framebuffer.setTextSize(1);
	framebuffer.println(utf8rus("Загрузка..."));
	framebuffer.display();
	//framebuffer.println((String)"Calibration... ");
	//Serial.print("\n\nNPLM-1 Calibration..");
	//framebuffer.display();
	//GLOBAL_adc_correction = get_adc_correction_value(2000);
	//framebuffer.println((String)"Correction: "+GLOBAL_adc_correction);
	//framebuffer.display();
	//Serial.print(GLOBAL_adc_correction);
	//Serial.print(", OK.\n");
	delay(2000);
	framebuffer.fillScreen(ST7735_TFT_BLACK);
}




void setup(void) {
	WiFi.persistent(false); //Disable wifi settings recording in flash
	WiFi.mode(WIFI_OFF); //Deactivate wifi
	WiFi.forceSleepBegin(); //Disable radio module

	//attachInterrupt(BUTTON_PIN, isr, CHANGE); //button interrupt
	btn.setButtonLevel(HIGH);
	btn.setHoldTimeout(1000);
	btn.attach(CLICK_HANDLER, button_click_handler);
	btn.attach(HOLDED_HANDLER, button_holded_handler);
	btn.attach(HOLD_HANDLER, button_hold_handler);
	//btn.attach(CLICKS_HANDLER, myClicks);
  //btn.attachClicks(5, fiveClicks);

  LightSensor.begin();

  framebuffer.initR(INITR_BLACKTAB);
	framebuffer.cp437(true);
	framebuffer.fillScreen(ST7735_TFT_BLACK);
	framebuffer.display();
	//

	Serial.begin(115200);
  Serial.print("\n\nNPLM-1 Start..");


	show_startup_screen_and_get_correction();

	ts.add(0, 200, [&](void *) { measure_light(); }, nullptr, true);

	ts.add(1, 100, [&](void *) { measure_flicker(); }, nullptr, true);
	ts.add(2, 100, [&](void *) { render_flicker_screen(); }, nullptr, true);


	ts.add(3, 5000, [&](void *) { free_mem_print(); }, nullptr, true);


}

void loop() {
  ts.update();
	btn.tick();

	//if (btn.click()) Serial.println("click");
	//if (btn.held()) Serial.println("held");
	//if (btn.hasClicks(2)) Serial.println("action 2 clicks");
}
