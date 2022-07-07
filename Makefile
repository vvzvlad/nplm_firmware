main: build upload miniterm

#fqbn:
#1) "--fqbn", not "-fqbn"
#2) format fqbn lib:cpu:board:param1,param2

#

build:
	arduino-cli compile --libraries ./libraries --fqbn=esp8266:esp8266:generic:xtal=80,baud=921600 npml1

upload:
	arduino-cli upload --port /dev/tty.usbserial* --fqbn=esp8266:esp8266:generic:baud=921600 npml1

miniterm:
	miniterm.py /dev/tty.usbserial* 115200

clean:
	arduino-cli cache clean

install_env:
	arduino-cli core update-index --additional-urls http://arduino.esp8266.com/stable/package_esp8266com_index.json
	arduino-cli core install esp8266:esp8266 --additional-urls http://arduino.esp8266.com/stable/package_esp8266com_index.json
#	arduino-cli lib install "WiFiManager"
#	arduino-cli lib install "ArduinoJson"
#	arduino-cli lib install "EspMQTTClient"
#	arduino-cli lib install "OneWire"
#	arduino-cli lib install "DallasTemperature"
#	arduino-cli lib install "ESP8266_ISR_Servo"
#	arduino-cli lib install "Wire"
#	arduino-cli lib install "Adafruit_PWM_Servo_Driver_Library"
