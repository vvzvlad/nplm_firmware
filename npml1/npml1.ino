#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <SPI.h>
#include <BH1750FVI.h>
#include <TickerScheduler.h>

#include <EncButton.h>

#include <ESP8266WiFi.h>

#define ST7735_TFT_CS         4 		//D2  //белый
#define ST7735_TFT_RST        -1  	//желтый
#define ST7735_TFT_DC         5 		//D1  //синий
#define BUTTON_PIN         		16

Adafruit_ST7735 tft = Adafruit_ST7735(ST7735_TFT_CS, ST7735_TFT_DC, ST7735_TFT_RST);
BH1750FVI LightSensor(BH1750FVI::k_DevModeContLowRes);
EncButton<EB_CALLBACK, BUTTON_PIN> btn(INPUT);

TickerScheduler ts(5);

//Screen resolution
#define ST7735_TFT_WIDTH 128
#define ST7735_TFT_HEIGHT 160

//Colors in RGB565 format: rrrrrggg:gggbbbbb
#define ST7735_TFT_BLACK 0x0000
#define ST7735_TFT_BLUE 0x001F
#define ST7735_TFT_RED 0xF800
#define ST7735_TFT_GREEN 0x07E0
#define ST7735_TFT_CYAN 0x07FF
#define ST7735_TFT_MAGENTA 0xF81F
#define ST7735_TFT_YELLOW 0xFFE0
#define ST7735_TFT_WHITE 0xFFFF


//Graph defines
#define GRAPH_WIDTH ST7735_TFT_WIDTH
#define GRAPH_HEIGHT 50
#define GRAPH_WIDTH_DIVIDER 4   // 512(samples number, MEASURE_NUM_SAMPLES) -> 128 (chart width, GRAPH_WIDTH)
#define GRAPH_HEIGHT_DIVIDER 20 // 1024(single-count resolution) ->  50(chart height, GRAPH_HEIGHT)
#define GRAPH_X 0
#define GRAPH_Y ST7735_TFT_HEIGHT-GRAPH_HEIGHT

#define CORRECTION_NUM_SAMPLES 512
#define MEASURE_NUM_SAMPLES 512
#define SYNC_NUM_SAMPLES 256

#define MAX_FREQ 300

uint16_t GLOBAL_adc_correction = 0;



uint16_t get_adc_correction_value(uint16_t samples) {
	uint32_t adc_values_sum = 0;

	for (uint16_t i=0; i<samples; i++) {
		adc_values_sum = adc_values_sum + system_adc_read();
	}
	uint16_t correction_value = adc_values_sum/samples;
	//Serial.print("Correction:");
	//Serial.print(correction_value);
	//Serial.print("\n\n");

	return correction_value;
}

float calc_frequency(uint16_t *adc_values_array, uint16_t adc_values_min_max_mean, uint32_t catch_time_us) {
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
				//tft.drawLine(i/GRAPH_WIDTH_DIVIDER, 110, i/GRAPH_WIDTH_DIVIDER, 160, ST7735_TFT_CYAN);
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
				//tft.drawLine(i/GRAPH_WIDTH_DIVIDER, 110, i/GRAPH_WIDTH_DIVIDER, 160, ST7735_TFT_CYAN);
				//Serial.println((String)"i:"+i+", adc_values_array:"+adc_values_array[i]);
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
	//Serial.print("period_time_us:");
	//Serial.print(period_time_us);

	//Дебаг вывод для подсчета частоты
	//Serial.print("adc_mean_values:\n");
	//for (uint16_t i=0; i<50; i++) {
	//	Serial.print(adc_mean_values[i]);
	//	Serial.print("NN");
	//}
	//Serial.print("\n");

	return freq;
}


void make_graph(uint16_t *adc_values_array, uint16_t adc_values_max) {
	uint8_t graph_values[GRAPH_WIDTH] = {};
	//uint16_t adc_values_max = 0;
	uint8_t adc_values_multiplier = 0;

	//for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
	//	if (adc_values[i] > adc_values_max) {adc_values_max = adc_values[i];}
	//}

	adc_values_max = adc_values_max/100*10+adc_values_max;
	adc_values_multiplier = 1024/adc_values_max;


	for (uint16_t i=0; i < GRAPH_WIDTH; i++) {
		graph_values[i] = (adc_values_array[i*GRAPH_WIDTH_DIVIDER+0]*adc_values_multiplier+  //Take 4 values (=GRAPH_WIDTH_DIVIDER)
												adc_values_array[i*GRAPH_WIDTH_DIVIDER+1]*adc_values_multiplier+ //from the adc values array at a time
												adc_values_array[i*GRAPH_WIDTH_DIVIDER+2]*adc_values_multiplier+ //and collapse them into one
												adc_values_array[i*GRAPH_WIDTH_DIVIDER+3]*adc_values_multiplier)
												/GRAPH_WIDTH_DIVIDER/GRAPH_HEIGHT_DIVIDER;
		if (graph_values[i] > 50) {graph_values[i] = 50;}
	}

	for (uint8_t current_colon = GRAPH_X; current_colon < GRAPH_WIDTH; current_colon++) {
		tft.drawLine(current_colon, GRAPH_Y, current_colon, ST7735_TFT_HEIGHT, ST7735_TFT_BLACK); //old graph colon-by-colon clearing
		if (current_colon == 0) {
			tft.drawPixel(current_colon, //The first point is drawn as a pixel because it has no past point
										ST7735_TFT_HEIGHT-graph_values[current_colon],
										ST7735_TFT_GREEN);
		}
		else {
			tft.drawLine(current_colon-1,  //The following points are drawn as lines to prevent gaps between individual points on steep hillsides
										ST7735_TFT_HEIGHT-graph_values[current_colon-1],
										current_colon,
										ST7735_TFT_HEIGHT-graph_values[current_colon],
										ST7735_TFT_GREEN);
		}
	}


	//Serial.print("\n");
	//for (uint8_t current_colon=0; current_colon < GRAPH_WIDTH; current_colon++) {
	//	Serial.print(graph_values[current_colon]);
	//	Serial.print("NN");
	//}
	//Serial.print("\n");
}

void measure_flicker() {
	uint16_t adc_values[MEASURE_NUM_SAMPLES] = {};
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

	//Calculating the maximum, minimum, average, average between the maximum and minimum values (for frequency measure)
	for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
		//if (adc_values[i] >= GLOBAL_adc_correction) {adc_values[i] = adc_values[i] - GLOBAL_adc_correction;}
		//else {adc_values[i] = 0;}

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

	//Flicker frequency calculation
	float freq = calc_frequency(adc_values, adc_values_min_max_mean, catch_time_us);

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

	//tft.fillRoundRect(0, 0, 128, 110, 0, ST7735_TFT_BLACK);
	tft.setCursor(0, 0);
	tft.setTextColor(ST7735_TFT_GREEN, ST7735_TFT_BLACK);
	tft.setTextSize(1);
	tft.println((String)"Flicker GOST:"+flicker_gost);
	tft.println((String)"Flicker simple:"+flicker_simple);
	tft.println((String)"Correction:"+GLOBAL_adc_correction);
	tft.println((String)"Average:"+adc_values_avg);
	tft.println((String)"Max:"+adc_values_max);
	tft.println((String)"Min:"+adc_values_min);
	//tft.println((String)"Tm:"+((float)catch_time_us/1000)+"ms");
	tft.println((String)"Freq:"+freq+" Hz");
	tft.println((String)"Light:"+LightSensor.GetLightIntensity()+" lx");


	make_graph(adc_values, adc_values_max);
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
	measure_flicker();
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
  tft.fillRoundRect(0, 0, ST7735_TFT_WIDTH, ST7735_TFT_HEIGHT, 0, ST7735_TFT_BLACK);
	tft.setCursor(0, 0);
	tft.setTextColor(ST7735_TFT_GREEN, ST7735_TFT_BLACK);
	tft.setTextSize(1);
	tft.println((String)"NPLM v0.0.1");
	GLOBAL_adc_correction = get_adc_correction_value(CORRECTION_NUM_SAMPLES);
	//tft.println((String)"Correction: "+GLOBAL_adc_correction);
	delay(1000);
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

  Serial.begin(115200);
  Serial.print("NPLM-1");

  LightSensor.begin();

  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(ST77XX_BLACK);


	show_startup_screen_and_get_correction();

	//ts.add(0, 2000, [&](void *) { measure_light(); }, nullptr, true);
	ts.add(1, 100, [&](void *) { measure_flicker(); }, nullptr, true);


}

void loop() {
  ts.update();
	btn.tick();

	//if (btn.click()) Serial.println("click");
	//if (btn.held()) Serial.println("held");
	//if (btn.hasClicks(2)) Serial.println("action 2 clicks");
}
