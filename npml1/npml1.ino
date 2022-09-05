#include "nplm1.h"
#include <ESP8266WiFi.h>
#include <Esp.h>

#include <SPI.h>    								// Core SPI library for display
#include <Adafruit_GFX.h>    				// Core graphics library
#include <Adafruit_ST7735.h> 				// Hardware-specific library for ST7735
#include <Adafruit_GFX_Buffer.h>		// Framebuffer library to speed up rendering

#include <utf8rus.h>    						// Cyrillic support library
#include <verdana_bold_12.h>				// External font
#include <images.h>									// Assets

#include <BH1750FVI.h> 							// Brightness sensor library

#include <TickerScheduler.h>				// Scheduler library

#define EB_CLICK 400   							// EncButton clicks timeout
#include <EncButton.h> 							// Button library

#include <GyverFilters.h>						// Filters library
#include <GyverLBUF.h>							// Buffer library
#include <EEPROM.h>
#include <ErriezSerialTerminal.h>		// Console library

#include <ArduinoOTA.h>
#include <ESP8266httpUpdate.h>

//Debug: Serial.println(__LINE__);
// Serial.print((String)__LINE__+": "+target_app+"\n");

#define ST7735_TFT_CS         	4 		//D2  //белый https://cdn.compacttool.ru/images/docs/Wemos_D1_mini_pinout.jpg
#define ST7735_TFT_RST        	-1  				//желтый
#define ST7735_TFT_DC         	5 		//D1  //синий
#define ST7735_TFT_BACKLIGHT    12    //D6
#define BUTTON_GPIO         		15    //D8
#define ENABLE_GPIO         		16    //D0

//Screen resolution
#define ST7735_TFT_WIDTH 				128
#define ST7735_TFT_HEIGHT 			160

//Colors in RGB565 format: rrrrrggg:gggbbbbb
#define ST7735_TFT_BLACK 				0x0000
#define ST7735_TFT_BLUE 				0x001F
#define ST7735_TFT_RED 					0xF800
#define ST7735_TFT_LIGHT_RED 		0xfaeb
#define ST7735_TFT_GREEN 				0x07E0
#define ST7735_TFT_DARK_GREEN 	0x0220
#define ST7735_TFT_DARK_GREEN2 	0x0160
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
#define SCORE_NORMAL_POINT 			5
#define SCORE_BAD_POINT 				35
#define FLIKER_ACCURACY_SPAN 		2
#define NO_FLICKER_FREQ_VALUE 	300 //Flicker with a frequency greater than 300 Hz is considered safe, so it does not count in the rating
#define NO_FREQ_FLICKER_VALUE 	5   //At low flicker level the frequency count is wrong and strobes

#define EEPROM_SIZE 		  			100

#define AUTOPOWEROFF_TIME_S 		120

#define LUMINANCE_NORMAL 				200
#define LUMINANCE_GOOD 					300

typedef Adafruit_ST7735 display_t;
typedef Adafruit_GFX_Buffer<display_t> GFXBuffer_t;
GFXBuffer_t framebuffer = GFXBuffer_t(ST7735_TFT_WIDTH, ST7735_TFT_HEIGHT, display_t(ST7735_TFT_CS, ST7735_TFT_DC, ST7735_TFT_RST));
BH1750FVI LightSensor(BH1750FVI::k_DevModeContLowRes);
EncButton<EB_CALLBACK, BUTTON_GPIO> btn(INPUT);
TickerScheduler ts(TS_MAX);
SerialTerminal term('\n', ' '); //new line character(\n), delimiter character(space)

//Filters
GMedian<5, uint16_t> flicker_freq_filter;
GMedian<5, uint16_t> cal_filter;
GKalman luminance_lx_filter(3, 0.3);
GKalman luminance_arrow_diff_filter(3, 0.3);
GKalman flicker_simple_filter(3, 0.3); //TODO тоже бы в eeprom
GKalman flicker_gost_filter(3, 0.3);
GyverLBUF<uint8_t, 6> flicker_accuracy_buf;
GyverLBUF<uint8_t, 128> lum_graph_buf_log;

volatile uint16_t G_adc_correction = 0;

volatile uint8_t G_flicker_gost = 0;
volatile uint8_t G_flicker_simple = 0;
volatile uint16_t G_flicker_freq = 0;
volatile uint16_t G_adc_values_max = 1;
volatile uint16_t G_adc_values_min = MAX_ADC_VALUE;
volatile uint16_t G_adc_values[MEASURE_NUM_SAMPLES] = {};
volatile uint32_t G_min_free_mem = 1000*1000*1000;
volatile uint8_t G_boot_run_counter = 0;
volatile uint8_t G_cal_run_counter = 0;
volatile uint8_t G_shutdown_run_counter = 0;
volatile uint8_t G_power_run_counter = 0;
volatile uint32_t G_time_without_buttons_ms = 0;

volatile uint8_t G_cal_help_counter = 0;
volatile uint8_t G_cal_help_text_y = 0;

volatile uint16_t G_luminance = 0;
volatile uint16_t G_eeprom_luminance_normal_point = LUMINANCE_NORMAL;
volatile uint16_t G_eeprom_luminance_good_point = LUMINANCE_GOOD;

//EEPROM data mirror in global variables
volatile APPS G_eeprom_last_app = APP_UNKNOWN;
volatile FLAG G_eeprom_calibration = F_ACTIVE;

volatile FLAG G_flag_first_run = F_ACTIVE;
volatile APPS G_app_runned = APP_UNKNOWN;
volatile APPS G_app_previous = APP_UNKNOWN;
volatile APPS G_last_app_runned = APP_UNKNOWN;
volatile FLAG G_power_flag = F_ACTIVE;

volatile FLIKER_TYPE_CALC G_F_type = FT_SIMPLE;

//----------Measure and calc functions----------//

uint16_t adc_value_measure() {
  uint32_t adc_values_sum = 0;

  for (uint16_t i=0; i<CORRECTION_NUM_SAMPLES; i++) {
    adc_values_sum = adc_values_sum + system_adc_read();
  }
  uint16_t value = adc_values_sum/CORRECTION_NUM_SAMPLES;

  free_mem_calc();
  return value;
}



uint16_t frequency_calc(uint16_t adc_values_min_max_mean, uint32_t catch_time_us) {
  uint16_t adc_mean_values[MEASURE_NUM_SAMPLES] = {};
  uint16_t adc_mean_values_i = 0;

  uint16_t accumulator = 0;
  FLAG not_sync = F_INACTIVE;

  while(not_sync == F_INACTIVE) {
    not_sync = F_ACTIVE;
    for (uint16_t i=accumulator; i<MEASURE_NUM_SAMPLES; i++) {
      if (G_adc_values[i] > adc_values_min_max_mean)
      {
        //Serial.println((String)"i:"+i+", G_adc_values:"+G_adc_values[i]);
        //framebuffer.drawLine(i/GRAPH_WIDTH_DIVIDER, 110, i/GRAPH_WIDTH_DIVIDER, 160, ST7735_TFT_CYAN);
        //framebuffer.display();
        accumulator = i + 20;
        not_sync = F_INACTIVE;
        break;
      }
    }

    for (uint16_t i=accumulator; i<MEASURE_NUM_SAMPLES; i++) {
      if (not_sync == F_ACTIVE) break;
      else not_sync == F_ACTIVE;

      if (G_adc_values[i] < adc_values_min_max_mean)
      {
        //framebuffer.drawLine(i/GRAPH_WIDTH_DIVIDER, 110, i/GRAPH_WIDTH_DIVIDER, 160, ST7735_TFT_CYAN);
        //Serial.println((String)"i:"+i+", G_adc_values:"+G_adc_values[i]);
        //framebuffer.display();
        adc_mean_values[adc_mean_values_i] = i;
        adc_mean_values_i++;
        accumulator = i + 20;
        not_sync = F_INACTIVE;
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

  if (freq > 800) freq = 0;

  free_mem_calc();
  return (uint16_t)freq;
}

void flicker_measure() {
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
    G_adc_values[i] = system_adc_read();
  }
  catch_stop_time = micros();
  catch_time_us = catch_stop_time-catch_start_time;
  //End of measurement

  //Allow interrupts back
  interrupts();
  ets_intr_unlock(); //open interrupt
  system_soft_wdt_restart();

  //Debug output of the measurement buffer
  //Serial.print("G_adc_values:\n");
  //for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
  //	Serial.print(G_adc_values[i]);
  //	Serial.print("NN");
  //}
  //Serial.print("\n");

  //Applying Correction
  for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
    if 		(G_adc_values[i] <= G_adc_correction) { G_adc_values[i] = 0;  }
    else 																			{ G_adc_values[i] = G_adc_values[i] - G_adc_correction; }
  }

  //Calculating the maximum, minimum, average, average between the maximum and minimum values (for frequency measure)
  for (uint16_t i=0; i<MEASURE_NUM_SAMPLES; i++) {
    if (G_adc_values[i] > adc_values_max) {adc_values_max = G_adc_values[i];}
    if (G_adc_values[i] < adc_values_min) {adc_values_min = G_adc_values[i];}
    adc_values_sum = adc_values_sum + G_adc_values[i];
  }
  adc_values_avg = adc_values_sum/MEASURE_NUM_SAMPLES;
  adc_values_min_max_mean = (adc_values_max-adc_values_min)/2+adc_values_min;

  //Serial.print("adc_values_min_max_mean:");
  //Serial.print(adc_values_min_max_mean);
  //Serial.print("\n");

  //Flicker level calculation
  flicker_gost = ((float)adc_values_max-(float)adc_values_min)*100/(2*(float)adc_values_avg);
  flicker_simple = ((float)adc_values_max-(float)adc_values_min)*100/((float)adc_values_max+(float)adc_values_min);

  if (flicker_gost > 255) flicker_gost_uint = 255;
  if (flicker_gost < 0) flicker_gost_uint = 0;
  if (flicker_gost >= 0 && flicker_gost <= 255) flicker_gost_uint = (uint8_t)flicker_gost;

  if (flicker_simple > 255) flicker_simple_uint = 255;
  if (flicker_simple < 0) flicker_simple_uint = 0;
  if (flicker_simple >= 0 && flicker_simple <= 255) flicker_simple_uint = (uint8_t)flicker_simple;

  //Flicker frequency calculation
  uint16_t freq = 0;
  if (flicker_simple > NO_FREQ_FLICKER_VALUE) 	freq = frequency_calc(adc_values_min_max_mean, catch_time_us);
  else																					freq = 0; //At low flicker level the frequency count is wrong and strobes


  G_flicker_gost = flicker_gost_filter.filtered((int)flicker_gost_uint);
  G_flicker_simple = flicker_simple_filter.filtered((int)flicker_simple_uint);
  G_flicker_freq = flicker_freq_filter.filtered(freq);
  G_adc_values_max = adc_values_max;
  G_adc_values_min = adc_values_min;

  free_mem_calc();
}


void luminance_measure() {
  uint8_t graph_lum_value;
  uint16_t lum_value = LightSensor.GetLightIntensity();
  G_luminance = luminance_lx_filter.filtered(lum_value);

  if (lum_value < 2) 			graph_lum_value = 1;
  else 										graph_lum_value = (uint8_t)(log2(lum_value)/log2(1.24));
  lum_graph_buf_log.write(graph_lum_value);

  free_mem_calc();
}


//----------Screens render functions----------//


void luminance_render() {
  uint16_t luminance_lx = G_luminance;
  uint16_t score;
  uint16_t score_color;
  uint16_t normal_p = G_eeprom_luminance_normal_point;
  uint16_t good_p = G_eeprom_luminance_good_point;

  framebuffer.setTextColor(ST7735_TFT_WHITE);
  framebuffer.fillRoundRect(0, 0, ST7735_TFT_WIDTH, ST7735_TFT_HEIGHT-GRAPH_HEIGHT, 0, ST7735_TFT_BLACK); //Clearing only the image above the graph!

  //------ Calc score ------//


  if 			(luminance_lx >= 0 && luminance_lx <= normal_p)				score = SCORE_BAD;
  else if (luminance_lx > normal_p && luminance_lx <= good_p)		score = SCORE_NORMAL;
  else if (luminance_lx > good_p)																score = SCORE_GOOD;
  else 																													score = SCORE_GOOD;

  //Drawing the labels "good light", "dark lamp"
  if 			(score == SCORE_GOOD) 													draw_asset(&light_msg_good, 0, 0);
  else if (score == SCORE_BAD || score == SCORE_NORMAL) 	draw_asset(&light_msg_bad, 0, 0);

  draw_asset(&light_rainbow, 0, 39); //Rainbow

  uint8_t arrow_diff = 0;
  if (luminance_lx/8 > ST7735_TFT_WIDTH) arrow_diff = ST7735_TFT_WIDTH-7; //7 — ширина стрелки, для того, чтобы она не уходила за границы экрана
  else arrow_diff = luminance_arrow_diff_filter.filtered((int)luminance_lx/8);
  draw_asset(&arrow, arrow_diff, 56); //Arrow on the rainbow

  draw_asset(&light_text_light_level, 0, 61); //Text "уровень освещенности"


  //------ Draw lux text ------//
  framebuffer.setFont(&verdana_bold12pt7b); //LARGE text font
  framebuffer.setTextSize(0);
  String lx_screen_text;

  if 			(luminance_lx < 1000)			lx_screen_text = (String)(luminance_lx)+			 " lx";
  else if (luminance_lx >= 1000) 		lx_screen_text = (String)(luminance_lx/1000)+" klx";

  if 			(score == SCORE_GOOD)			score_color = ST7735_TFT_GREEN;
  else if (score == SCORE_NORMAL)		score_color = ST7735_TFT_YELLOW;
  else if (score == SCORE_BAD)			score_color = ST7735_TFT_LIGHT_RED;


  int8_t cursor_x = 0, cursor_y = 100;
  int16_t text_start_x, text_start_y; //not used
  uint16_t text_width, text_height;

  framebuffer.getTextBounds(lx_screen_text,  	 //Fucking magic to
                            cursor_x,					 //align text horizontally.
                            cursor_y,
                            &text_start_x,
                            &text_start_y,
                            &text_width,
                            &text_height);
  //framebuffer.drawRect(text_start_x, text_start_y, text_width, text_height, ST7735_TFT_BLUE); //debug rect

  cursor_x = (ST7735_TFT_WIDTH-text_width)/2;
  framebuffer.setTextColor(score_color);
  framebuffer.setCursor(cursor_x, cursor_y);
  framebuffer.print(lx_screen_text);
  framebuffer.setFont(); //Reset LARGE text font to the default


  //------ Graph render ------//

  //Normal cleaning of the graph part
  framebuffer.fillRoundRect(GRAPH_X, GRAPH_Y, GRAPH_WIDTH, GRAPH_Y+GRAPH_HEIGHT, 0, ST7735_TFT_BLACK);


  //Drawing vertical lines
  uint8_t vertical_lines[8] = { 5, 5+(16*1), 5+(16*2), 5+(16*3), 5+(16*4), 5+(16*5), 5+(16*6), 5+(16*7)};
  for (uint8_t i=0; i<8; i++) {
    framebuffer.drawLine(vertical_lines[i],
                        ST7735_TFT_HEIGHT-GRAPH_HEIGHT,
                        vertical_lines[i],
                        ST7735_TFT_HEIGHT,
                        ST7735_TFT_DARK_GREEN2);
  }

  //Drawing horizontal logarithmic lines
  framebuffer.drawLine(GRAPH_X, ST7735_TFT_HEIGHT, ST7735_TFT_WIDTH, ST7735_TFT_HEIGHT, ST7735_TFT_DARK_GREEN2);
  for (uint8_t x=2; x<GRAPH_HEIGHT; x = x*2) {
    framebuffer.drawLine(GRAPH_X,
                          ST7735_TFT_HEIGHT-(x),
                          ST7735_TFT_WIDTH,
                          ST7735_TFT_HEIGHT-(x),
                          ST7735_TFT_DARK_GREEN2);
  }
  framebuffer.drawLine(GRAPH_X, ST7735_TFT_HEIGHT-(GRAPH_HEIGHT), ST7735_TFT_WIDTH, ST7735_TFT_HEIGHT-(GRAPH_HEIGHT), ST7735_TFT_DARK_GREEN2);


  //Drawing the graph
  for (uint8_t current_colon = GRAPH_X; current_colon < GRAPH_WIDTH; current_colon++) {
    uint16_t graph_color;
    if 			(lum_graph_buf_log.read(current_colon) <= (uint8_t)(log2(normal_p)/log2(1.24)))		graph_color = ST7735_TFT_RED;
    else if (lum_graph_buf_log.read(current_colon) <= (uint8_t)(log2(good_p)/log2(1.24)))			graph_color = ST7735_TFT_YELLOW;
    else 																																											graph_color = ST7735_TFT_GREEN;

    if (current_colon == 0) {
      framebuffer.drawPixel(current_colon, //The first point is drawn as a pixel because it has no past point
                    ST7735_TFT_HEIGHT-lum_graph_buf_log.read(current_colon),
                    graph_color);
    }
    else {
      framebuffer.drawLine(current_colon-1,  //The following points are drawn as lines to prevent gaps between individual points on steep hillsides
                    ST7735_TFT_HEIGHT-lum_graph_buf_log.read(current_colon-1),
                    current_colon,
                    ST7735_TFT_HEIGHT-lum_graph_buf_log.read(current_colon),
                    graph_color);
    }
  }

  framebuffer.display();
  free_mem_calc();
}

void ota_progress_callback(int cur, int total) {
  uint8_t percent = (uint8_t)((float)cur/(float)total*100);

  Serial.printf("OTA progress: %d%%\n", percent);
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
}


void ota_update() {
  Serial.printf("Wi-Fi mode set to WIFI_STA %s\n", WiFi.mode(WIFI_STA) ? "" : "Failed!");

  WiFi.begin("IoT_Dobbi", "canned-ways-incense");
  while (WiFi.status() != WL_CONNECTED) //TODO нужен счетчик максимальных попыток
  {
    switch ( WiFi.status() ) {
      case 0: Serial.print(F("Status 0: WL_IDLE_STATUS (Wi-Fi is in process of changing between statuses)\n")); break;
      case 1: Serial.print(F("Status 1: WL_NO_SSID_AVAIL (no Wi-Fi network is available)\n")); break;
      case 2: Serial.print(F("Status 2: WL_SCAN_COMPLETED (scan is completed)\n")); break;
      case 3: Serial.print(F("Status 3: WL_CONNECTED (connection is established)\n")); break;
      case 4: Serial.print(F("Status 4: WL_CONNECT_FAILED (connection failed)\n")); break;
      case 5: Serial.print(F("Status 5: WL_CONNECTION_LOST (connection is lost)\n")); break;
      case 6: Serial.print(F("Status 6: WL_CONNECT_WRONG_PASSWORD (password is incorrect)")); break;
      case 7: Serial.print(F("Status 7: WL_DISCONNECTED (module is in progress connection, or not configured in station mode)\n")); break;
    }
    delay(1000);
  }
  Serial.print(F("Status 3: WL_CONNECTED (connection is established)"));

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());
  WiFi.printDiag(Serial);

  Serial.println("Starting OTA update...");



  WiFiClient client;
  ESPhttpUpdate.onProgress(ota_progress_callback);

  ESPhttpUpdate.update(client, "192.168.88.69", 8080, "/firmware_nplm.bin");
  free_mem_calc();
  }

void mem_test_2 (uint32_t count) {
  count = count + 100;
  volatile uint8_t test[100];
  for (uint8_t i=0; i<100; i++) {
    test[i] = 0xFF;
  }
  Serial.print("Free heap size: ");
  Serial.print(system_get_free_heap_size());
  Serial.print(" bytes, busy ");
  Serial.print(count);
  Serial.print(" bytes\n");
  delay(200);
  mem_test_2(count);
  mem_test_2(count);
}

void mem_test() {
  mem_test_2(0);
}

void flicker_render() {
  uint8_t flicker;
  uint16_t freq = G_flicker_freq;
  uint16_t adc_max = G_adc_values_max;
  uint16_t adc_min = G_adc_values_min;
  uint8_t ff_rating = 666;
  ACCURACY accuracy_flag = A_ACCURACY; //The flag of accuracy of the measured number, is set to 0 if the number changes quickly
  uint16_t score_color;
  SCORE score;


  if (G_F_type == FT_SIMPLE) 	flicker = G_flicker_simple;
  if (G_F_type == FT_GOST) 		flicker = G_flicker_gost;

  //------ Calc combined FF rating ------ //FF = Flicker + Freq, combined rating for total lamp score, abstract percent
  if 			(freq <= NO_FLICKER_FREQ_VALUE-50) 																		ff_rating = flicker;
  else if (freq > NO_FLICKER_FREQ_VALUE-50 && freq <= NO_FLICKER_FREQ_VALUE) 		ff_rating = flicker*(float)(((50-(freq-(NO_FLICKER_FREQ_VALUE-50))))/50);
  else if (freq > NO_FLICKER_FREQ_VALUE) 																				ff_rating = 0;
  else 																																					ff_rating = 666;
  //Flicker with a frequency greater than 300(NO_FLICKER_FREQ_VALUE) Hz is considered safe, so it does not count in the rating
  // new table: https://habrastorage.org/webt/8m/uq/yy/8muqyydrycyjfwniltnigoa1ig4.jpeg

  //------ Calc accuracy flag ------//
  flicker_accuracy_buf.write(flicker);
  uint8_t flicker_acc_max_level;
  uint8_t flicker_acc_min_level;
  if (flicker+FLIKER_ACCURACY_SPAN > 255) flicker_acc_max_level = 255;
  else flicker_acc_max_level = flicker+FLIKER_ACCURACY_SPAN;
  if (flicker-FLIKER_ACCURACY_SPAN < 0) flicker_acc_min_level = 0;
  else flicker_acc_min_level = flicker-FLIKER_ACCURACY_SPAN;
  for (int j = 0; j < 4; j++) {
    if (flicker_accuracy_buf.read(j) < flicker_acc_min_level ||
        flicker_accuracy_buf.read(j) > flicker_acc_max_level)
        accuracy_flag = A_INACCURACY;
  }

  //------ Calc text score ------//
  if 			(adc_min > TOO_LIGHT_ADC_VALUE) 																			score = SCORE_TOO_LIGHT;
  else if (adc_max < TOO_DARK_ADC_VALUE)																				score = SCORE_TOO_DARK;
  else if (accuracy_flag == A_INACCURACY)																				score = SCORE_INACC;
  else if (ff_rating <= SCORE_NORMAL_POINT)																			score = SCORE_GOOD;
  else if (ff_rating > SCORE_NORMAL_POINT && ff_rating <= SCORE_BAD_POINT)			score = SCORE_NORMAL;
  else if (ff_rating > SCORE_BAD_POINT)																					score = SCORE_BAD;
  else 																																					score = SCORE_INACC;

  if (score == SCORE_GOOD) 												score_color = ST7735_TFT_GREEN;
  else if (score == SCORE_NORMAL) 								score_color = ST7735_TFT_YELLOW;
  else if (score == SCORE_BAD) 										score_color = ST7735_TFT_RED;
  else if (score == SCORE_TOO_LIGHT) 							score_color = ST7735_TFT_GRAY;
  else if (score == SCORE_TOO_DARK) 							score_color = ST7735_TFT_GRAY;
  else if (score == SCORE_INACC) 									score_color = ST7735_TFT_GRAY;
  else 																						score_color = ST7735_TFT_GRAY;



  //------ Start image rendering (text and graphics) ------//
  framebuffer.setTextColor(ST7735_TFT_WHITE);
  framebuffer.fillRoundRect(0, 0, ST7735_TFT_WIDTH, ST7735_TFT_HEIGHT-GRAPH_HEIGHT, 0, ST7735_TFT_BLACK); //Clearing only the image above the graph!

  //Drawing the labels "good lamp", "bad lamp", "normal lamp", etc
  if (score == SCORE_GOOD) 				draw_asset(&flicker_msg_good_lamp, 0, 0);
  if (score == SCORE_NORMAL) 			draw_asset(&flicker_msg_normal_lamp, 0, 0);
  if (score == SCORE_BAD) 				draw_asset(&flicker_msg_bad_lamp, 0, 0);
  if (score == SCORE_TOO_LIGHT) 	draw_asset(&flicker_msg_too_big_lum, 0, 0);
  if (score == SCORE_TOO_DARK) 		draw_asset(&flicker_msg_too_low_lum, 0, 0);
  if (score == SCORE_INACC) 			draw_asset(&flicker_msg_process, 0, 0);

  draw_asset(&flicker_rainbow, 0, 39); //Rainbow

  if (score == SCORE_GOOD || score == SCORE_NORMAL || score == SCORE_BAD) {
    uint8_t arrow_diff = (uint8_t)(1.28*(float)ff_rating);
    draw_asset(&arrow, arrow_diff, 56); //Arrow on the rainbow
  }

  draw_asset(&flicker_text_flicker_level, 0, 61); //Text "уровень пульсаций"


  //------ Draw percent BIG text ------//
  framebuffer.setFont(&verdana_bold12pt7b); //LARGE text font
  framebuffer.setTextSize(0); //External font is one and already the right size, no need scaling
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

  if (score == SCORE_GOOD || score == SCORE_NORMAL || score == SCORE_BAD || score == SCORE_INACC) {
    framebuffer.setTextColor(score_color);
    framebuffer.print(flicker_percents);
  }
  if (score == SCORE_TOO_DARK || score == SCORE_TOO_LIGHT) {
    draw_asset(&flicker_text_no_data_big, 0, 80); //Text "Н/Д"
  }

  framebuffer.setFont(); //Reset LARGE text font to the default
  framebuffer.setTextColor(ST7735_TFT_WHITE); //Reset text color to the default



  //------ Graph calc ------//
  uint8_t adc_values_multiplier = 0;
  uint8_t graph_values[GRAPH_WIDTH] = {};
  int16_t adc_values_max = G_adc_values_max;

  adc_values_max = adc_values_max/100*10+adc_values_max;
  if (adc_values_max == 0) { adc_values_max = 1; };
  adc_values_multiplier = MAX_ADC_VALUE/adc_values_max;

  for (uint16_t i=0; i < GRAPH_WIDTH; i++) {
    graph_values[i] = ( G_adc_values[i*GRAPH_WIDTH_DIVIDER+0]*adc_values_multiplier+  //Take 4 values (=GRAPH_WIDTH_DIVIDER)
                          G_adc_values[i*GRAPH_WIDTH_DIVIDER+1]*adc_values_multiplier+ //from the adc values array at a time
                          G_adc_values[i*GRAPH_WIDTH_DIVIDER+2]*adc_values_multiplier+ //and collapse them into one
                          G_adc_values[i*GRAPH_WIDTH_DIVIDER+3]*adc_values_multiplier)
                            /GRAPH_WIDTH_DIVIDER/GRAPH_HEIGHT_DIVIDER;
    if (graph_values[i] > 50) { graph_values[i] = 50; }
  }



  //------ Graph render ------//

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

  //Drawing vertical lines
  uint8_t vertical_lines[8] = { 5, 5+(16*1), 5+(16*2), 5+(16*3), 5+(16*4), 5+(16*5), 5+(16*6), 5+(16*7)};
  for (uint8_t i=0; i<8; i++) {
    framebuffer.drawLine(vertical_lines[i],
                        ST7735_TFT_HEIGHT-GRAPH_HEIGHT,
                        vertical_lines[i],
                        ST7735_TFT_HEIGHT,
                        ST7735_TFT_DARK_GREEN2);
  }

  //Drawing horizontal lines

  for (uint8_t x=0; x<=GRAPH_HEIGHT; x = x + 10) {
    framebuffer.drawLine(GRAPH_X,
                          ST7735_TFT_HEIGHT-(x),
                          ST7735_TFT_WIDTH,
                          ST7735_TFT_HEIGHT-(x),
                          ST7735_TFT_DARK_GREEN2);
  }

  //Drawing the graph
  for (uint8_t current_colon = GRAPH_X; current_colon < GRAPH_WIDTH; current_colon++) {
    if (current_colon == 0) {
      framebuffer.drawPixel(current_colon, //The first point is drawn as a pixel because it has no past point
                    ST7735_TFT_HEIGHT-graph_values[current_colon],
                    score_color);
    }
    else {
      framebuffer.drawLine(current_colon-1,  //The following points are drawn as lines to prevent gaps between individual points on steep hillsides
                    ST7735_TFT_HEIGHT-graph_values[current_colon-1],
                    current_colon,
                    ST7735_TFT_HEIGHT-graph_values[current_colon],
                    score_color);
    }
  }


  //------ Text on top of the graphic ------//
  framebuffer.setTextColor(ST7735_TFT_WHITE);
  framebuffer.setTextSize(1);
  framebuffer.setCursor(90, 152);
  framebuffer.print((String)+G_flicker_freq+" Hz");

  if (G_F_type == FT_GOST){
    framebuffer.setCursor(0, 152);
    framebuffer.print((String)"GOST");
  }


  //------ Transfer image to display ------
  framebuffer.display();
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
  }

  free_mem_calc();
}

void calibration_help_render() {
  uint8_t counter = G_cal_help_counter++; //local counter value before increment

  if (counter == 0) G_cal_help_text_y = 80;

  uint8_t text_pixel_diff = 8;
  if (counter <= text_pixel_diff) G_cal_help_text_y++;
  if (counter >= text_pixel_diff) G_cal_help_text_y--;
  if (G_cal_help_counter > text_pixel_diff*2) G_cal_help_counter = 0;

  uint8_t easter = 0;
  if (easter == 1){
    draw_asset(&cal_help_screen_easter, 0, 0);
    draw_asset(&cal_help_screen_msg_easter, 0, G_cal_help_text_y);
  }
  else {
    draw_asset(&cal_help_screen, 0, 0);
    draw_asset(&cal_help_screen_msg, 0, G_cal_help_text_y);
  }

  framebuffer.display();
  free_mem_calc();
}

void calibration_measure_render() {
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
    G_adc_correction = cal_filter.filtered(adc_value_measure());
    framebuffer.setCursor(10, 120);
    framebuffer.print((String)"Value:"+G_adc_correction+" adc p.");
    framebuffer.display();
  }
  else {
    framebuffer.fillScreen(ST7735_TFT_BLACK);
    change_app(APP_FLICKER_SIMPLE);
    G_cal_run_counter = 0;
  }

  free_mem_calc();
}


void shutdown_screen_render() {
  uint8_t counter = G_shutdown_run_counter; //local counter value before increment

  if (counter == 0){
    for (uint8_t y=0; y<ST7735_TFT_HEIGHT; y++) {
      for (uint8_t x=0; x<ST7735_TFT_WIDTH; x++) {
        uint16_t current_pixel = framebuffer.getPixel(x, y);
        float divider = 0.4;
        uint8_t r = ((((current_pixel >> 11) & 0x1F) * 527) + 23) >> 6;
        uint8_t g = ((((current_pixel >> 5) & 0x3F) * 259) + 33) >> 6;
        uint8_t b = (((current_pixel & 0x1F) * 527) + 23) >> 6;
        r = r * divider;
        g = g * divider;
        b = b * divider;
        current_pixel = framebuffer.color565(r, g, b);
        framebuffer.drawPixel(x, y, current_pixel);
      }
    }
  }

  framebuffer.fillRoundRect(15, 32, 128-15-15, 35, 2, 0xa28a);

  framebuffer.setTextColor(ST7735_TFT_BLACK);
  framebuffer.setTextSize(1);

  framebuffer.setCursor(30, 35);
  framebuffer.println(utf8rus("Удерживайте"));
  framebuffer.setCursor(30, 44);
  framebuffer.println(utf8rus("кнопку для"));
  framebuffer.setCursor(30, 53);
  framebuffer.println(utf8rus("выключения"));

  framebuffer.fillRoundRect(15, 74, 128-15-15, 5, 2, 0xe5b6);
  framebuffer.fillRoundRect(15, 74, counter*3.5, 5, 2, 0x6904);
  if (counter*3.5 > 128-15-15){
    G_power_flag = F_INACTIVE;
  }

  if (btn.state() == 0) {
    G_shutdown_run_counter = 0;
    change_app(G_app_previous);
  }
  else {
    G_shutdown_run_counter++;
  }

  framebuffer.display();
  free_mem_calc();
}


//----------Switch app function----------//

void change_app(APPS target_app){
  if (G_app_runned == target_app) {return;}

  if (target_app == APP_FLICKER_SIMPLE || target_app == APP_FLICKER_GOST || target_app == APP_LIGHT) { //TODO сделать LAST_APP
    EEPROM.put(EEPROM_LAST_APP, target_app); EEPROM.commit();
    if (G_eeprom_last_app != APP_UNKNOWN && G_flag_first_run == F_ACTIVE) {
      target_app = G_eeprom_last_app;
      G_flag_first_run = F_INACTIVE;
    }
  }

  G_app_previous = G_app_runned;
  G_app_runned = target_app;
  ts.disableAll();
  ts.enable(TS_MEASURE_FLICKER); //To quickly update the values when switching
  ts.enable(TS_MEASURE_LIGHT);   //applications, the metering processes is always on
  ts.enable(TS_POWER);   				 //This process is always on

  if (target_app == APP_FLICKER_SIMPLE) {
    G_F_type = FT_SIMPLE;
    ts.enable(TS_RENDER_FLICKER);
    Serial.print(F("Run APP_FLICKER_SIMPLE app\n")); term_prompt();
    return;
  }

  if (target_app == APP_FLICKER_GOST) {
    G_F_type = FT_GOST;
    ts.enable(TS_RENDER_FLICKER);
    Serial.print(F("Run APP_FLICKER_GOST app\n")); term_prompt();
    return;
  }

  if (target_app == APP_LIGHT) {
    ts.enable(TS_RENDER_LIGHT);
    Serial.print(F("Run APP_LIGHT app\n")); term_prompt();
    return;
  }

  if (target_app == APP_BOOT) {
    ts.enable(TS_RENDER_BOOT);
    Serial.print(F("Run APP_BOOT app\n")); term_prompt();
    return;
  }

  if (target_app == APP_CAL_HELP) {
    ts.enable(TS_RENDER_CAL_HELP);
    Serial.print(F("Run APP_CAL_HELP app\n")); term_prompt();
    return;
  }

  if (target_app == APP_CAL_MEASURE) {
    ts.enable(TS_RENDER_CAL_PROCESS);
    Serial.print(F("Run APP_CAL_MEASURE app\n")); term_prompt();
    return;
  }

  if (target_app == APP_SHUTDOWN) {
    ts.enable(TS_RENDER_SHUTDOWN);
    Serial.print(F("Run APP_SHUTDOWN app\n")); term_prompt();
    return;
  }
}

//----------Button processing functions----------//

void button_two_clicks_handler() {
  Serial.print("2CLICKS_HANDLER: ");
  Serial.println(btn.clicks);
}

void button_click_handler() { //Switch applications cyclically at the touch of a button
  Serial.print(F("Click\n"));
  term_prompt();

  if 			(G_app_runned == APP_FLICKER_SIMPLE) 	change_app(APP_LIGHT); 						//clicking the button switching FLIKER_SIMPLE->LIGHT
  else if (G_app_runned == APP_LIGHT) 					change_app(APP_FLICKER_GOST);			//clicking the button switching LIGHT->FLIKER_GOST
  else if (G_app_runned == APP_FLICKER_GOST) 		change_app(APP_FLICKER_SIMPLE);		//clicking the button switching FLIKER_GOST->FLICKER_SIMPLE

  else if (G_app_runned == APP_BOOT) 						change_app(APP_FLICKER_SIMPLE);		//clicking the button will skip the screen and calibration
  else if (G_app_runned == APP_CAL_HELP) 				change_app(APP_CAL_MEASURE); 			//clicking the button starts the calibration(APP_CAL_MEASURE, calibration_measure_render)
  else 																					change_app(APP_SHUTDOWN); 				//strange behavior, fall down paws upwards

  G_time_without_buttons_ms = 0; //Autopower off timer reset
  free_mem_calc();
}

void button_holded_handler() { //A long press to turn device off
  Serial.print(F("Holded\n"));
  term_prompt();

  if 			(G_app_runned == APP_FLICKER_GOST) 		change_app(APP_SHUTDOWN); 				//power off
  else if (G_app_runned == APP_FLICKER_SIMPLE) 	change_app(APP_SHUTDOWN);					//power off
  else if (G_app_runned == APP_LIGHT) 					change_app(APP_SHUTDOWN);					//power off
  else if (G_app_runned == APP_BOOT)					 	change_app(APP_FLICKER_SIMPLE);		//holding the button skips the calibration
  else if (G_app_runned == APP_CAL_HELP) 				change_app(APP_FLICKER_SIMPLE); 	//holding the button skips the calibration
  else if (G_app_runned == APP_CAL_MEASURE) 		change_app(APP_FLICKER_SIMPLE); 	//holding the button skips the calibration
  else 																					change_app(APP_SHUTDOWN); 				//strange behavior, fall down paws upwards

  G_time_without_buttons_ms = 0; //Autopower off timer reset
  free_mem_calc();
}




//----------System functions----------//

void power_process() {
  G_time_without_buttons_ms = G_time_without_buttons_ms+100; //power_process runs every 100ms

  if (G_time_without_buttons_ms/1000 > AUTOPOWEROFF_TIME_S){
    Serial.print(F("Auto timer shutdown\n"));
    term_prompt();
    G_power_flag = F_INACTIVE;
  }

  if (G_power_flag == F_ACTIVE){
    if (G_power_run_counter > 1) {
      digitalWrite(ST7735_TFT_BACKLIGHT, HIGH); 	//Turn on the backlight with a delay so that the screen does not show transients and initialization
    }
    digitalWrite(ENABLE_GPIO, HIGH); 						//After booting up the system, maintain the high level on the ENABLE LDO by itself
                                                //(when turned on, the high level appears because the user pressed the button)
  }

  if (G_power_flag == F_INACTIVE){
    digitalWrite(ST7735_TFT_BACKLIGHT, LOW); 											//Turn off the backlight of the screen to make sure the user turns off the device
    G_power_run_counter++;
    if (G_power_run_counter > 5 and btn.state() == 0) digitalWrite(ENABLE_GPIO, LOW); //suicide a second later, so that the user is sure to release the button
    //if (G_power_run_counter > 15 and btn.state() == 0) ESP.restart(); //Тестовая схема для отладки

  }

  if (G_power_run_counter < 2) G_power_run_counter++;
}


void draw_asset(const asset_t *asset, uint8_t x, uint8_t y) {
  uint8_t h = pgm_read_byte(&asset->height);
  uint8_t w = pgm_read_byte(&asset->width);
  framebuffer.drawRGBBitmap(x, y, asset->image, w, h);
  free_mem_calc();
}


void free_mem_calc() {
  uint32_t current_free_mem = system_get_free_heap_size();
  if (G_min_free_mem > current_free_mem) G_min_free_mem = current_free_mem;
}


void ICACHE_RAM_ATTR isr() {
  btn.tickISR();
}

//----------Terminal functions----------//
void term_calibration_enable_disable() {
  char *arg = term.getNext();
  if (arg != NULL && strcmp(arg, "enable") == 0) {
    EEPROM.put(EEPROM_CALIBRATION_ACTIVE, F_ACTIVE); EEPROM.commit();
  }
  else if (arg != NULL && strcmp(arg, "disable") == 0) {
    EEPROM.put(EEPROM_CALIBRATION_ACTIVE, F_INACTIVE); EEPROM.commit();
  }
  else term_help();
  free_mem_calc();
}

void term_run_app() {
  char *arg = term.getNext();
  if (arg != NULL) change_app((APPS)atoi(arg));
  free_mem_calc();
}

void term_eeprom_write() {
  char *arg;
  uint8_t addr;
  uint8_t value;
  arg = term.getNext();
  if (arg != NULL) addr = atoi(arg);
  arg = term.getNext();
  if (arg != NULL) value = atoi(arg);
  EEPROM.put(addr, value); EEPROM.commit();
  free_mem_calc();
}

void term_lum_write() {
  char *arg;
  uint16_t normal = G_eeprom_luminance_normal_point;
  uint16_t good = G_eeprom_luminance_good_point;
  arg = term.getNext();
  if (arg != NULL) normal = atoi(arg);
  arg = term.getNext();
  if (arg != NULL) good = atoi(arg);
  G_eeprom_luminance_normal_point = normal;
  G_eeprom_luminance_good_point = good;
  EEPROM.put(EEPROM_LUMINANCE_NORMAL, normal);
  EEPROM.put(EEPROM_LUMINANCE_GOOD, good);
  EEPROM.commit();
  free_mem_calc();
}

void term_eeprom_read() {
  char *arg;
  uint8_t addr;
  uint8_t value;
  arg = term.getNext();
  if (arg != NULL) addr = atoi(arg);
  EEPROM.get(addr, value);
  Serial.println((String)F("EEPROM value: \t")+value+F("\n"));
  free_mem_calc();
}

void term_data_print() {

  Serial.print((String)F("Chip ID: \t\t\t0x"));
  Serial.print(system_get_chip_id(), HEX);
  Serial.print((String)F("\n"));

  Serial.println((String)F("SDK Version: \t\t\t")+system_get_sdk_version()+F(""));
  Serial.println((String)F("CPU frequency: \t\t\t")+system_get_cpu_freq()+F(" MHz"));

  Serial.print((String)F("Flash ID/Vendor ID: \t\t0x"));
  Serial.print(ESP.getFlashChipId(), HEX);
  Serial.print((String)F("/"));
  Serial.print(ESP.getFlashChipVendorId(), HEX);
  Serial.print((String)F("\n"));

  Serial.println((String)F("Flash real size: \t\t")+ESP.getFlashChipRealSize()/1024+F(" kbytes"));
  Serial.println((String)F("Flash size: \t\t\t")+ESP.getFlashChipSize()/1024+F(" kbytes"));
  Serial.println((String)F("Flash speed: \t\t\t")+ESP.getFlashChipSpeed()/1000/1000+F(" MHz"));

  Serial.println((String)F("App size (flash): \t\t")+ESP.getSketchSize()/1024+F(" kbytes"));
  Serial.println((String)F("Free FLASH: \t\t\t")+ESP.getFreeSketchSpace()/1024+F(" kbytes"));
  Serial.println((String)F("Free RAM: \t\t\t")+system_get_free_heap_size()+F(" bytes"));
  Serial.println((String)F("Free RAM (min): \t\t")+G_min_free_mem+F(" bytes"));

  Serial.println((String)F("Time without buttons press: \t")+G_time_without_buttons_ms/1000+F(" s"));

//getResetReason
//getResetInfo
//eraseConfig


  Serial.print("\n");
  Serial.println((String)F("Flicker freq: \t\t\t")+G_flicker_freq+F(" Hz"));
  Serial.println((String)F("Flicker (simple): \t\t")+G_flicker_simple+F(" %"));
  Serial.println((String)F("Flicker (gost): \t\t")+G_flicker_gost+F(" %"));
  Serial.println((String)F("ADC calibration: \t\t")+G_adc_correction+F(" adc points"));
  Serial.println((String)F("ADC values max: \t\t")+G_adc_values_max+F(" adc points"));
  Serial.println((String)F("ADC values min: \t\t")+G_adc_values_min+F(" adc points"));
  Serial.println((String)F("Luminance: \t\t\t")+G_luminance+F(" lx"));
  Serial.println((String)F("Luminance normal/good: \t\t")+G_eeprom_luminance_normal_point+"/"+G_eeprom_luminance_good_point+F(" lx"));

  Serial.print("\n");
  Serial.println((String)F("App running: \t\t\tenum APP #")+G_app_runned+F(""));
  Serial.println((String)F("Last app (eeprom): \t\tenum APP #")+EEPROM.read(EEPROM_LAST_APP)+F(""));
  Serial.println((String)F("First run flag: \t\t")+G_flag_first_run+F(""));
  Serial.println((String)F("Calibration flag (eeprom): \t")+EEPROM.read(EEPROM_CALIBRATION_ACTIVE)+F(""));
  free_mem_calc();
}

void term_init()
{
  term.setSerialEcho(true);
  term.addCommand("data", term_data_print);
  term.addCommand("help", term_help);
  term.addCommand("reboot", ESP.restart);
  term.addCommand("erase", eeprom_clear);
  term.addCommand("cal", term_calibration_enable_disable);
  term.addCommand("app", term_run_app);
  term.addCommand("ewrite", term_eeprom_write);
  term.addCommand("lum", term_lum_write);
  term.addCommand("eread", term_eeprom_read);
  term.addCommand("ota", ota_update);
  term.addCommand("mem", mem_test);
  term.addCommand("[A", term_data_print);
  term.setPostCommandHandler(term_prompt);
  term.setDefaultHandler(term_unknown_command);
}

void term_help()
{
  Serial.println(F("Console usage:"));
  Serial.println(F("  help/any				print this usage")); //todo: сделать настройку режимов и сохранение в eeprom
  Serial.println(F("  data					Show all data"));
  Serial.println(F("  reboot				Reboot"));
  Serial.println(F("  cal enable/disable			enable or disable calibration process after start"));
  Serial.println(F("  lum [normal value] [good value]	write lum values"));
  Serial.println(F("  app [num]				run app"));
  Serial.println(F("  ewrite [addr] [value]			write value to eeprom at addr"));
  Serial.println(F("  eread [addr]				read value from eeprom at addr"));
  Serial.println(F("  erase					Erase all eeprom settings"));
  Serial.println(F("  memtest					Filling memory before stack corruption and rebooting"));
  Serial.println(F("  ota					Update firmware by wifi connection"));
}

void term_unknown_command(const char *command)
{
  Serial.print(F("Unknown command: "));
  Serial.println(command);
  Serial.println("\n");
  term_help();
}

void term_prompt() {
  Serial.print(F("> "));
}

//----------EEPROM functions----------//

void eeprom_init() {
  uint32_t eeprom_flag = 0xDEADBEEF;
  uint32_t eeprom_value = 0;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(EEPROM_SIZE-4, eeprom_value);
  if (eeprom_value != eeprom_flag){
    eeprom_clear();
    EEPROM.put(EEPROM_SIZE-4, eeprom_flag);
    EEPROM.put(EEPROM_LAST_APP, G_eeprom_last_app);
    EEPROM.put(EEPROM_CALIBRATION_ACTIVE, G_eeprom_calibration);
    EEPROM.put(EEPROM_LUMINANCE_NORMAL, G_eeprom_luminance_normal_point);
    EEPROM.put(EEPROM_LUMINANCE_GOOD, G_eeprom_luminance_good_point);
    EEPROM.commit();
    Serial.print(F("First start, eeprom clear and initialized\n"));
  }
  else{
    EEPROM.get(EEPROM_LAST_APP, G_eeprom_last_app);
    EEPROM.get(EEPROM_CALIBRATION_ACTIVE, G_eeprom_calibration);
    EEPROM.get(EEPROM_LUMINANCE_NORMAL, G_eeprom_luminance_normal_point);
    EEPROM.get(EEPROM_LUMINANCE_GOOD, G_eeprom_luminance_good_point);
  }
}

void eeprom_clear() {
  for (int i = 0; i < EEPROM_SIZE; i++) { EEPROM.write(i, 0); }
  EEPROM.commit();
  Serial.print(F("EEPROM erase complete\n"));
  term_prompt();
}




//----------Entry point----------//

void setup(void) {
  Serial.begin(115200);
  delay(100); //Delay for the development console to open on the computer
  Serial.print(F("\n\nNPLM-1 Start\n"));

  WiFi.persistent(false); //Disable wifi settings recording in flash
  WiFi.mode(WIFI_OFF); //Deactivate wifi
  WiFi.forceSleepBegin(); //Disable radio module
  Serial.print(F("Wifi disabled\n"));

  pinMode(ENABLE_GPIO, OUTPUT);
  digitalWrite(ENABLE_GPIO, HIGH);

  attachInterrupt(BUTTON_GPIO, isr, CHANGE); //button interrupt
  btn.setButtonLevel(HIGH);
  btn.setHoldTimeout(400);
  btn.attach(CLICK_HANDLER, button_click_handler);
  btn.attach(HOLDED_HANDLER, button_holded_handler);
  //btn.attach(CLICKS_HANDLER, button_clicks_handler);
  btn.attachClicks(2, button_two_clicks_handler);
  Serial.print(F("Buttons triggers attach\n"));

  LightSensor.begin();
  Serial.print(F("Light sensor initialized\n"));

  eeprom_init();
  Serial.print(F("EEPROM initialized\n"));


  pinMode(ST7735_TFT_BACKLIGHT, OUTPUT);
  digitalWrite(ST7735_TFT_BACKLIGHT, LOW);
  framebuffer.initR(INITR_BLACKTAB);
  framebuffer.cp437(true); //Support for сyrillic in the standard font (works with the patched glcdfont.c)
  //framebuffer.fillScreen(ST7735_TFT_BLACK);
  //framebuffer.display();
  Serial.print(F("Display & framebuffer initialized\n"));


  term_init();
  Serial.print(F("Console initialized\n"));


  ts.add(TS_MEASURE_LIGHT, 			 200,  [&](void *) { luminance_measure(); 				  }, nullptr, false);
  ts.add(TS_MEASURE_FLICKER, 		 200,  [&](void *) { flicker_measure(); 						}, nullptr, false);
  ts.add(TS_RENDER_FLICKER, 		 200,  [&](void *) { flicker_render();  	  				}, nullptr, false);
  ts.add(TS_RENDER_LIGHT, 			 200,  [&](void *) { luminance_render(); 						}, nullptr, false);
  ts.add(TS_RENDER_BOOT, 				 200,  [&](void *) { boot_screen_render(); 			  	}, nullptr, false);
  ts.add(TS_RENDER_SHUTDOWN, 		  60,  [&](void *) { shutdown_screen_render(); 	  	}, nullptr, false);
  ts.add(TS_RENDER_CAL_HELP, 		  80,  [&](void *) { calibration_help_render(); 		}, nullptr, false);
  ts.add(TS_RENDER_CAL_PROCESS,  200,  [&](void *) { calibration_measure_render(); 	}, nullptr, false);
  ts.add(TS_POWER,  						 100,  [&](void *) { power_process(); 							}, nullptr, false);

  ts.disableAll();
  Serial.print(F("Sheduler initialized\n"));

  free_mem_calc();

  if (G_eeprom_calibration == F_INACTIVE)  change_app(APP_FLICKER_SIMPLE);
  if (G_eeprom_calibration == F_ACTIVE)  change_app(APP_BOOT);

  Serial.print(F("\n> "));
}

void loop() {
  ts.update();
  btn.tick();
  ts.update();
  term.readSerial();
}
