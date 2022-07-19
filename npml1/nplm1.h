typedef enum: uint8_t {
  APP_BOOT,
	APP_FLICKER_SIMPLE,
  APP_FLICKER_GOST,
	APP_LIGHT,
	APP_HELP,
	APP_SHUTDOWN,
	APP_CAL_HELP,
	APP_CAL_MEASURE,
	APP_UNKNOWN
} APPS;

typedef enum: uint8_t {
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
} TASKS;

typedef enum: uint8_t {
	A_ACCURACY,
	A_INACCURACY,
} ACCURACY;

typedef enum: uint8_t {
	SCORE_TOO_LIGHT,
	SCORE_TOO_DARK,
	SCORE_GOOD,
	SCORE_NORMAL,
	SCORE_BAD,
	SCORE_INACC,
} SCORE;

typedef enum: uint8_t {
	FT_SIMPLE,
	FT_GOST,
} FLIKER_TYPE_CALC;

typedef enum: uint8_t {
  FLAG_INACTIVE,
	FLAG_ACTIVE,
} FLAG;


typedef enum: uint8_t {
	EEPROM_LAST_APP,
  EEPROM_CALIBRATION_ACTIVE,
} EEPROM_ADDR;
