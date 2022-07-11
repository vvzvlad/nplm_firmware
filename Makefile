main: build upload_fast miniterm git_commit

#fqbn:
#1) "--fqbn", not "-fqbn"
#2) format fqbn lib:cpu:board:param1,param2

#

build:
	arduino-cli compile --libraries ./libraries --fqbn=esp8266:esp8266:generic:xtal=80,baud=921600 npml1 --output-dir ./bin

upload:
	arduino-cli upload --port /dev/tty.usbserial* --fqbn=esp8266:esp8266:generic:baud=921600 npml1 --output-dir ./bin

upload_fast:
	/Users/vvzvlad/Library/Arduino15/packages/esp8266/tools/python3/3.7.2-post1/python3 -I /Users/vvzvlad/Library/Arduino15/packages/esp8266/hardware/esp8266/3.0.2/tools/upload.py --chip esp8266 --port /dev/tty.usbserial* --baud 3000000 --before default_reset --after hard_reset write_flash 0x0 ./bin/npml1.ino.bin

git_commit:
	echo "Press enter fot git commit & push"
	read -t 3 -n 1 || exit && git add . ; git commit -m "Auto(press enter): `date +'%Y-%m-%d %H:%M:%S'`" ; git remote | xargs -L1 git push --all > /dev/null 2>&1 &

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
