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

#include "GyverFilters.h"
#include <ErriezSerialTerminal.h>


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
#define ST7735_TFT_DARK_GREEN 	0x0220
#define ST7735_TFT_GRAY 				0x8c71
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

#define CORRECTION_NUM_SAMPLES 	512
#define MEASURE_NUM_SAMPLES 		512
#define SYNC_NUM_SAMPLES 				256

#define MAX_ADC_VALUE 					1024
#define TOO_LIGHT_ADC_VALUE 		1000
#define TOO_DARK_ADC_VALUE 		  20

#define NO_FLICKER_FREQ_VALUE 	300 //Flicker with a frequency greater than 300 Hz is considered safe, so it does not count in the rating
#define NO_FREQ_FLICKER_VALUE 	5   //At low flicker level the frequency count is wrong and strobes

enum APPS {
	APP_BOOT,
	APP_FLICKER,
	APP_LIGHT,
	APP_HELP,
	APP_SHUTDOWN,
	APP_CAL_HELP,
	APP_CAL_MEASURE,
	APP_UNKNOWN
};

enum TASKS {
	TS_MEASURE_LIGHT,
	TS_MEASURE_FLICKER,
	TS_RENDER_FLICKER,
	TS_RENDER_LIGHT,
	TS_RENDER_BOOT,
	TS_RENDER_SHUTDOWN,
	TS_RENDER_CAL_HELP,
	TS_RENDER_CAL_PROCESS,
	TS_DEBUG,
	TS_MAX,
};

typedef Adafruit_ST7735 display_t;
typedef Adafruit_GFX_Buffer<display_t> GFXBuffer_t;
GFXBuffer_t framebuffer = GFXBuffer_t(ST7735_TFT_WIDTH, ST7735_TFT_HEIGHT, display_t(ST7735_TFT_CS, ST7735_TFT_DC, ST7735_TFT_RST));
BH1750FVI LightSensor(BH1750FVI::k_DevModeContLowRes);
EncButton<EB_CALLBACK, BUTTON_PIN> btn(INPUT);
TickerScheduler ts(TS_MAX);
SerialTerminal term('\n', ' '); //new line character(\n), delimiter character(space)

//Filters
GMedian<5, uint16_t> flicker_freq_filter;
GMedian<2, uint16_t> lum_lum_filter;
GMedian<5, uint16_t> cal_filter;
GKalman flicker_simple_filter(3, 0.3);
GKalman flicker_gost_filter(3, 0.3);

volatile uint16_t G_adc_correction = 0;

volatile uint8_t G_flicker_gost = 0;
volatile uint8_t G_flicker_simple = 0;
volatile uint16_t G_flicker_freq = 0;
volatile uint16_t G_adc_values_max = 1;
volatile uint16_t G_adc_values_min = MAX_ADC_VALUE;
volatile uint8_t G_graph_values[GRAPH_WIDTH] = {};
volatile uint32_t G_min_free_mem = 1000*1000*1000;
volatile uint8_t G_boot_run_counter = 0;
volatile uint8_t G_cal_run_counter = 0;
volatile uint8_t G_cal_help_counter = 0;
volatile uint8_t G_cal_help_text_y = 100;

volatile uint16_t G_lum = 0;

volatile uint16_t G_app_runned = APP_UNKNOWN;

void draw_asset(const asset_t *asset, uint8_t x, uint8_t y) {
  uint8_t h = pgm_read_byte(&asset->height);
	uint8_t w = pgm_read_byte(&asset->width);
	framebuffer.drawRGBBitmap(x, y, asset->image, w, h);
	free_mem_calc();
}



uint16_t get_adc_correction_value() {
	uint32_t adc_values_sum = 0;

	for (uint16_t i=0; i<CORRECTION_NUM_SAMPLES; i++) {
		adc_values_sum = adc_values_sum + system_adc_read();
	}
	uint16_t correction_value = adc_values_sum/CORRECTION_NUM_SAMPLES;
	//Serial.print("Correction:");
	//Serial.print(correction_value);
	//Serial.print("\n\n");

	free_mem_calc();
	return correction_value;
}


void render_light_screen() {
	uint16_t lum = G_lum;

	framebuffer.setTextColor(ST7735_TFT_WHITE);
	framebuffer.fillRoundRect(0, 0, ST7735_TFT_WIDTH, ST7735_TFT_HEIGHT-GRAPH_HEIGHT, 0, ST7735_TFT_BLACK); //Clearing only the image above the graph!


	//Drawing the labels "good lamp", "bad lamp", "normal lamp"
	if (lum >= 0 && lum <= 200) 	draw_asset(&flicker_msg_too_low_lum, 0, 0); //TODO временно!
	else if (lum > 200) 					draw_asset(&light_msg_good, 0, 0);

	draw_asset(&light_rainbow, 0, 39); //Rainbow

	uint8_t arrow_diff = 0;
	if (lum/8 > ST7735_TFT_WIDTH) arrow_diff = ST7735_TFT_WIDTH-7; //8 — ширина стрелки, для того, чтобы она не уходила за границы экрана
	else arrow_diff = lum/8;
	draw_asset(&arrow, arrow_diff, 56); //Arrow on the rainbow

	draw_asset(&light_text_light_level, 0, 61); //Text "уровень освещенности"


	//------ Draw lux text ------//
	framebuffer.setFont(&verdana_bold12pt7b); //LARGE text font
	framebuffer.setTextSize(0);
	String lux_value;
	if (lum < 1000)					lux_value = (String)lum+" lux";
	else if (lum >= 1000) 	lux_value = (String)(lum/1000)+"k lux";


	int8_t cursor_x = 0, cursor_y = 100;
	int16_t text_start_x, text_start_y; //not used
	uint16_t text_width, text_height;

	framebuffer.getTextBounds(lux_value,  			 //Fucking magic to
														cursor_x,					 //align text horizontally.
														cursor_y,
														&text_start_x,
														&text_start_y,
														&text_width,
														&text_height);
	//framebuffer.drawRect(text_start_x, text_start_y, text_width, text_height, ST7735_TFT_BLUE); //debug rect

	cursor_x = (ST7735_TFT_WIDTH-text_width)/2;
	framebuffer.setCursor(cursor_x, cursor_y);
	framebuffer.print(lux_value);
	framebuffer.setFont(); //Reset LARGE text font to the default

	framebuffer.display();
	free_mem_calc();
}



void render_flicker_screen() {
	uint8_t flicker = G_flicker_simple; //or G_flicker_gost
	uint16_t freq = G_flicker_freq;
	uint16_t adc_max = G_adc_values_max;
	uint16_t adc_min = G_adc_values_min;

	framebuffer.setTextColor(ST7735_TFT_WHITE);
	framebuffer.fillRoundRect(0, 0, ST7735_TFT_WIDTH, ST7735_TFT_HEIGHT-GRAPH_HEIGHT, 0, ST7735_TFT_BLACK); //Clearing only the image above the graph!

	//------ Calc combined score ------//

	uint8_t ff_combined = 666; //FF = Flicker + Freq, combined for total lamp score, abstract percent



	//Flicker with a frequency greater than 300(NO_FLICKER_FREQ_VALUE) Hz is considered safe, so it does not count in the rating
	if (freq >= 0 && freq <= NO_FLICKER_FREQ_VALUE-50) 													ff_combined = flicker;
	else if (freq > NO_FLICKER_FREQ_VALUE-50 && freq <= NO_FLICKER_FREQ_VALUE) 	ff_combined = flicker*(float)(((50-(freq-(NO_FLICKER_FREQ_VALUE-50))))/50);
	else if (freq > NO_FLICKER_FREQ_VALUE) 																			ff_combined = 0;
	else 																																				ff_combined = 666;

	//Drawing the labels "good lamp", "bad lamp", "normal lamp"
	if (ff_combined >= 0 && ff_combined <= 5) 			draw_asset(&flicker_msg_good_lamp, 0, 0); //TODO в дефайны!
	else if (ff_combined > 5 && ff_combined <= 35) 	draw_asset(&flicker_msg_normal_lamp, 0, 0);
	else if (ff_combined > 35) 											draw_asset(&flicker_msg_bad_lamp, 0, 0);
	else  																					Serial.println((String)"FF score:"+ff_combined);

	if (adc_min > TOO_LIGHT_ADC_VALUE)							draw_asset(&flicker_msg_too_big_lum, 0, 0);
	if (adc_max < TOO_DARK_ADC_VALUE)								draw_asset(&flicker_msg_too_low_lum, 0, 0);
	//TODO:как может быть уровень пульсаций y/д, но "лампа хорошая"??


	draw_asset(&flicker_rainbow, 0, 39); //Rainbow

	if (adc_min < TOO_LIGHT_ADC_VALUE && adc_max > TOO_DARK_ADC_VALUE) {
		uint8_t arrow_diff = (uint8_t)(1.28*(float)ff_combined);
		draw_asset(&arrow, arrow_diff, 56); //Arrow on the rainbow
	}

	draw_asset(&flicker_text_flicker_level, 0, 61); //Text "уровень пульсаций"



	//------ Draw percent text ------//
	framebuffer.setFont(&verdana_bold12pt7b); //LARGE text font
	framebuffer.setTextSize(0);
	String flicker_percents = (String)flicker+"%";

	int8_t cursor_x = 0, cursor_y = 100;
	int16_t text_start_x, text_start_y; //not used
	uint16_t text_width, text_height;

	framebuffer.getTextBounds(flicker_percents,  //Fucking magic to
														cursor_x,					 //align text horizontally.
														cursor_y,
														&text_start_x,
														&text_start_y,
														&text_width,
														&text_height);
	//framebuffer.drawRect(text_start_x, text_start_y, text_width, text_height, ST7735_TFT_BLUE); //debug rect

	cursor_x = (ST7735_TFT_WIDTH-text_width)/2;
	framebuffer.setCursor(cursor_x, cursor_y);

	if (adc_min < TOO_LIGHT_ADC_VALUE && adc_max > TOO_DARK_ADC_VALUE) {
		framebuffer.setTextColor(ST7735_TFT_GRAY);
		framebuffer.print(flicker_percents);
		framebuffer.setTextColor(ST7735_TFT_WHITE);
	}
	else {
		draw_asset(&flicker_text_no_data_big, 0, 80); //Text "Н/Д"
	}

	framebuffer.setFont(); //Reset LARGE text font to the default







	//------ Graph render ------//
	uint16_t graph_color = 0;

	if (adc_min < TOO_LIGHT_ADC_VALUE && adc_max > TOO_DARK_ADC_VALUE) {
		graph_color = ST7735_TFT_GREEN;
	}
	else {
		graph_color = ST7735_TFT_DARK_GREEN;
	}

	//Normal cleaning of the graph part
	framebuffer.fillRoundRect(GRAPH_X, GRAPH_Y, GRAPH_WIDTH, GRAPH_Y+GRAPH_HEIGHT, 0, ST7735_TFT_BLACK);

	//Phosphor emulation - old lines gradually fade and go out finally after a several frames
	//for (uint8_t y=GRAPH_Y; y<GRAPH_Y+GRAPH_HEIGHT; y++) {
  //  for (uint8_t x=GRAPH_X; x<GRAPH_WIDTH; x++) {
	//		uint16_t current_pixel = framebuffer.getPixel(x, y);
	//		float divider = 0.6;
	//		uint8_t r = ((((current_pixel >> 11) & 0x1F) * 527) + 23) >> 6;
	//		uint8_t g = ((((current_pixel >> 5) & 0x3F) * 259) + 33) >> 6;
	//		uint8_t b = (((current_pixel & 0x1F) * 527) + 23) >> 6;
	//		r = r * divider;
	//		g = g * divider;
	//		b = b * divider;
	//		current_pixel = framebuffer.color565(r, g, b);
  //    framebuffer.drawPixel(x, y, current_pixel);
  //  }
  //} //YYght, bad idea, but it's a pity to delete the written code

	for (uint8_t current_colon = GRAPH_X; current_colon < GRAPH_WIDTH; current_colon++) {
		if (current_colon == 0) {
			framebuffer.drawPixel(current_colon, //The first point is drawn as a pixel because it has no past point
										ST7735_TFT_HEIGHT-G_graph_values[current_colon],
										graph_color);
		}
		else {
			framebuffer.drawLine(current_colon-1,  //The following points are drawn as lines to prevent gaps between individual points on steep hillsides
										ST7735_TFT_HEIGHT-G_graph_values[current_colon-1],
										current_colon,
										ST7735_TFT_HEIGHT-G_graph_values[current_colon],
										graph_color);
		}
	}


	//------ Text on top of the graphic ------//
	framebuffer.setTextColor(ST7735_TFT_WHITE);
	framebuffer.setTextSize(1);
	framebuffer.setCursor(90, 152);
	//framebuffer.println((String)"Flicker simple:"+flicker_simple_uint);
	//framebuffer.println((String)"Correction:"+G_adc_correction);
	//framebuffer.println((String)"Average:"+adc_values_avg);
	//framebuffer.println((String)"Max:"+adc_values_max);
	//framebuffer.println((String)"Min:"+adc_values_min);
	//framebuffer.println((String)"Tm:	"+((float)catch_time_us/1000)+"ms");
	framebuffer.print((String)+G_flicker_freq+" Hz");
	//framebuffer.println((String)"Light:"+LightSensor.GetLightIntensity()+" lx");



	//------Transfer image------
	framebuffer.display();
	free_mem_calc();
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

	free_mem_calc();
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
		if 		(adc_values[i] <= G_adc_correction) { adc_values[i] = 0;  }
		else 																			{ adc_values[i] = adc_values[i] - G_adc_correction; }
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
	uint16_t freq = 0;
	if (flicker_simple > NO_FREQ_FLICKER_VALUE) 	freq = calc_frequency(adc_values, adc_values_min_max_mean, catch_time_us);
	else																					freq = 0; //At low flicker level the frequency count is wrong and strobes

	G_flicker_gost = flicker_gost_filter.filtered((int)flicker_gost_uint);
	G_flicker_simple = flicker_simple_filter.filtered((int)flicker_simple_uint);

	G_flicker_freq = flicker_freq_filter.filtered(freq);
	G_adc_values_max = adc_values_max;
	G_adc_values_min = adc_values_min;


	//Graph data
	uint8_t adc_values_multiplier = 0;

	adc_values_max = adc_values_max/100*10+adc_values_max;
	if (adc_values_max == 0) { adc_values_max = 1; };
	adc_values_multiplier = MAX_ADC_VALUE/adc_values_max;

	for (uint16_t i=0; i < GRAPH_WIDTH; i++) {
		G_graph_values[i] = (adc_values[i*GRAPH_WIDTH_DIVIDER+0]*adc_values_multiplier+  //Take 4 values (=GRAPH_WIDTH_DIVIDER)
																	adc_values[i*GRAPH_WIDTH_DIVIDER+1]*adc_values_multiplier+ //from the adc values array at a time
																	adc_values[i*GRAPH_WIDTH_DIVIDER+2]*adc_values_multiplier+ //and collapse them into one
																	adc_values[i*GRAPH_WIDTH_DIVIDER+3]*adc_values_multiplier)
																	/GRAPH_WIDTH_DIVIDER/GRAPH_HEIGHT_DIVIDER;
		if (G_graph_values[i] > 50) { G_graph_values[i] = 50; }
	}


	//debug prints
	//Serial.print("Avg:");
	//Serial.print(adc_values_avg);
	//Serial.print(", Max:");
	//Serial.print(adc_values_max);
	//Serial.print(", min:");
	//Serial.print(adc_values_min);
	//Serial.print(", correction:");
	//Serial.print(G_adc_correction);
	//Serial.print(", flicker_gost:");
	//Serial.print(flicker_gost);
	//Serial.print(", flicker_simple:");
	//Serial.print(flicker_simple);
	free_mem_calc();
}


void measure_light() {
  uint16_t lum = LightSensor.GetLightIntensity();
	G_lum = lum_lum_filter.filtered(lum);
	free_mem_calc();
}


void change_app(APPS target_app){
	if (G_app_runned == target_app) {return;}
	G_app_runned = target_app;
	ts.disableAll();
	ts.enable(TS_DEBUG);

	if (target_app == APP_FLICKER) {
		ts.enable(TS_MEASURE_FLICKER);
		ts.enable(TS_MEASURE_LIGHT); //To quickly update the brightness value when switching applications, the brightness metering process is always on
		ts.enable(TS_RENDER_FLICKER);
		framebuffer.fillScreen(ST7735_TFT_BLACK);
		return;
	}

	if (target_app == APP_LIGHT) {
		ts.enable(TS_MEASURE_LIGHT);
		ts.enable(TS_RENDER_LIGHT);
		framebuffer.fillScreen(ST7735_TFT_BLACK);
		return;
	}

	if (target_app == APP_BOOT) {
		ts.enable(TS_RENDER_BOOT);
		framebuffer.fillScreen(ST7735_TFT_BLACK);
		return;
	}

	if (target_app == APP_CAL_HELP) {
		ts.enable(TS_RENDER_CAL_HELP);
		framebuffer.fillScreen(ST7735_TFT_BLACK);
		return;
	}

	if (target_app == APP_CAL_MEASURE) {
		ts.enable(TS_RENDER_CAL_PROCESS);
		framebuffer.fillScreen(ST7735_TFT_BLACK);
		return;
	}

	if (target_app == APP_SHUTDOWN) {
		ts.enable(TS_RENDER_SHUTDOWN);
		//No clear screen — this application draws the interface on top
		//of the old app canvas (like is a popup window)
		return;
	}
}

void button_click_handler() { //Switch applications cyclically at the touch of a button
	Serial.print(F("Click\n"));
	term_post_command();

	if 			(G_app_runned == APP_FLICKER) 		change_app(APP_LIGHT); 				//clicking the button switching FLIKER->LIGHT
	else if (G_app_runned == APP_LIGHT) 			change_app(APP_FLICKER);			//clicking the button switching LIGHT->FLIKER

	else if (G_app_runned == APP_BOOT) 				change_app(APP_FLICKER);			//clicking the button will skip the screen and calibration
	else if (G_app_runned == APP_CAL_HELP) 		change_app(APP_CAL_MEASURE); 	//clicking the button starts the calibration(APP_CAL_MEASURE, cal_measure_screen_render)
	else if (G_app_runned == APP_SHUTDOWN) 		change_app(APP_BOOT); 				//simulate rebooting
	else 																			change_app(APP_SHUTDOWN); 		//strange behavior, fall down paws upwards
	free_mem_calc();
}

void button_holded_handler() {
  Serial.print(F("Holded\n"));
	term_post_command();

	if 			(G_app_runned == APP_FLICKER) 		change_app(APP_SHUTDOWN); 		//power off
	else if (G_app_runned == APP_LIGHT) 			change_app(APP_SHUTDOWN);			//power off
	else if (G_app_runned == APP_CAL_HELP) 		change_app(APP_FLICKER); 			//holding the button skips the calibration
	else if (G_app_runned == APP_CAL_MEASURE) change_app(APP_FLICKER); 			//holding the button skips the calibration
	else 																			change_app(APP_SHUTDOWN); 		//strange behavior, fall down paws upwards

	free_mem_calc();
}

void button_hold_handler() { //A long press to turn device off
  //Serial.print(F("Hold\n"));
	//term_post_command();
	free_mem_calc();
}

void boot_screen_render() {
	uint8_t counter = G_boot_run_counter++; //local counter value before increment

	if (counter < 10){ // 200ms*10cycles=2000ms
		framebuffer.fillScreen(ST7735_TFT_BLACK);
		draw_asset(&boot_screen, 0, 0);
		framebuffer.setCursor(34, 97);
		framebuffer.setTextColor(ST7735_TFT_WHITE);
		framebuffer.setTextSize(1);
		framebuffer.print(utf8rus("Загрузка"));
		if 			(counter > 8) 	framebuffer.print(F("..."));
		else if (counter > 5) 	framebuffer.print(F(".."));
		else if (counter > 2) 	framebuffer.print(F("."));
		framebuffer.display();
	}
	else {
		framebuffer.fillScreen(ST7735_TFT_BLACK);
		change_app(APP_CAL_HELP);
		G_boot_run_counter = 0;
	}

	free_mem_calc();
}

void cal_help_screen_render() {
	uint8_t counter = G_cal_help_counter++; //local counter value before increment

	framebuffer.fillScreen(ST7735_TFT_BLACK);
	framebuffer.setCursor(0, 10);
	framebuffer.setTextColor(ST7735_TFT_WHITE);
	framebuffer.setTextSize(1);
	//framebuffer.println(utf8rus("Во время калибровки"));
	//framebuffer.println(utf8rus("не направляйте"));
	//framebuffer.println(utf8rus("устройство на"));
	//framebuffer.println(utf8rus("измеряемую лампу"));

	framebuffer.println(utf8rus("ЭТОТ КОРАБЛЬ ЕСТЬ "));
	framebuffer.println(utf8rus("ОПТИМИЗМ ЦЕНТРА "));
	framebuffer.println(utf8rus("ПЕРСОНЫ КОРАБЛЯ"));
  framebuffer.println(utf8rus("ВЫ НЕ УДАРИЛИ НАС"));
  framebuffer.println(utf8rus("СЛЕДОВАТЕЛЬНО ВЫ "));
	framebuffer.println(utf8rus("ЕДИТЕ МЛАДЕНЦЕВ"));

	uint8_t text_pixel_diff = 8;
	if (counter <= text_pixel_diff) G_cal_help_text_y++;
	if (counter >= text_pixel_diff) G_cal_help_text_y--;
	if (G_cal_help_counter > text_pixel_diff*2) G_cal_help_counter = 0;


	framebuffer.setCursor(0, G_cal_help_text_y);
	framebuffer.println(utf8rus("ЧТО НАШЕ ТО ВАШЕ,"));
  framebuffer.println(utf8rus("ЧТО ВАШЕ ТО НАШЕ"));
	//framebuffer.println(utf8rus("Нажмите кнопку один"));
	//framebuffer.println(utf8rus("раз для калибровки"));
	//framebuffer.println(utf8rus("Или удерживайте для "));
	//framebuffer.println(utf8rus("пропуска  --->"));


	framebuffer.display();
	free_mem_calc();
}

void cal_measure_screen_render() {
	uint8_t counter = G_cal_run_counter++; //local counter value before increment

	if (counter < 10){ // 200ms*10cycles=2000ms
		framebuffer.fillScreen(ST7735_TFT_BLACK);
		framebuffer.setCursor(34, 97);
		framebuffer.setTextColor(ST7735_TFT_WHITE);
		framebuffer.setTextSize(1);
		framebuffer.print(utf8rus("Калибровка"));
		if 			(counter > 8) 	framebuffer.print(F("..."));
		else if (counter > 5) 	framebuffer.print(F(".."));
		else if (counter > 2) 	framebuffer.print(F("."));
		G_adc_correction = cal_filter.filtered(get_adc_correction_value());
		framebuffer.setCursor(10, 120);
		framebuffer.print((String)"Value:"+G_adc_correction+" adc p.");
		framebuffer.display();
	}
	else {
		framebuffer.fillScreen(ST7735_TFT_BLACK);
		change_app(APP_FLICKER);
		G_cal_run_counter = 0;
	}

	free_mem_calc();
}

void shutdown_screen_render() {
	framebuffer.fillScreen(ST7735_TFT_BLACK); //Temporarily!
	framebuffer.setCursor(34, 97);
	framebuffer.setTextColor(ST7735_TFT_WHITE);
	framebuffer.setTextSize(1);
	framebuffer.println(utf8rus("Выключение..."));
	framebuffer.display();
	ts.disableAll(); //Freeze until reboot
	free_mem_calc();
}

void free_mem_calc() {
	uint32_t current_free_mem = system_get_free_heap_size();
	if (G_min_free_mem > current_free_mem) G_min_free_mem = current_free_mem;
}


void debug() {
	//memory_print();
}

void data_print() {
	Serial.print(F("\nFlicker freq: "));
  Serial.print(G_flicker_freq);
	Serial.print(F(" Hz, flicker (simple): "));
	Serial.print(G_flicker_simple);
	Serial.print(F(" %, flicker (gost): "));
	Serial.print(G_flicker_gost);
	//Serial.print(F(" %, flicker (gost): "));
	Serial.print("\n");
}

void memory_print() {
	Serial.print(F("\nFree memory: "));
  Serial.print(system_get_free_heap_size());
	Serial.print(F(", min free memory: "));
	Serial.print(G_min_free_mem);
	Serial.print("\n");
}

void term_help()
{
    Serial.println(F("Console usage:"));
    Serial.println(F("  help          		 Print this usage")); //todo: сделать настройку режимов и сохранение в eeprom
    Serial.println(F("  mem                Show memory free"));
		Serial.println(F("  data               Show measures"));
}

void term_unknown_command(const char *command)
{
    Serial.print(F("Unknown command: "));
    Serial.println(command);
}

void term_post_command() {
  Serial.print(F("> "));
}

void isr() {
  btn.tickISR();
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
	framebuffer.cp437(true); //Support for сyrillic in the standard font (works with the patched glcdfont.c)
	framebuffer.fillScreen(ST7735_TFT_BLACK);
	framebuffer.display();


	Serial.begin(115200);
  Serial.print(F("\n\nNPLM-1 Start"));
	term.setSerialEcho(true);
	term.addCommand("mem", memory_print);
	term.addCommand("data", data_print);
	term.addCommand("help", term_help);
	term.setPostCommandHandler(term_post_command);
	term.setDefaultHandler(term_unknown_command);


	ts.add(TS_MEASURE_LIGHT, 			 200,  [&](void *) { measure_light(); 				  	}, nullptr, false);
	ts.add(TS_MEASURE_FLICKER, 		 200,  [&](void *) { measure_flicker(); 					}, nullptr, false);
	ts.add(TS_RENDER_FLICKER, 		 200,  [&](void *) { render_flicker_screen();  	  }, nullptr, false);
	ts.add(TS_RENDER_LIGHT, 			 200,  [&](void *) { render_light_screen(); 			}, nullptr, false);
	ts.add(TS_DEBUG, 							 5000, [&](void *) { debug(); 										}, nullptr, false);
	ts.add(TS_RENDER_BOOT, 				 200,  [&](void *) { boot_screen_render(); 			  }, nullptr, false);
	ts.add(TS_RENDER_SHUTDOWN, 		 200,  [&](void *) { shutdown_screen_render(); 	  }, nullptr, false);
	ts.add(TS_RENDER_CAL_HELP, 		 100,  [&](void *) { cal_help_screen_render(); 		}, nullptr, false);
	ts.add(TS_RENDER_CAL_PROCESS,  200,  [&](void *) { cal_measure_screen_render(); }, nullptr, false);
	ts.disableAll();

	ts.enable(TS_DEBUG);

	free_mem_calc();
	change_app(APP_BOOT);
	Serial.print(F("\n> "));
}

void loop() {
  ts.update();
	btn.tick();
	term.readSerial();

	//if (btn.click()) Serial.println("click");
	//if (btn.held()) Serial.println("held");
	//if (btn.hasClicks(2)) Serial.println("action 2 clicks");
}
