main: build upload_fast miniterm git_commit_wait

#fqbn:
#1) "--fqbn", not "-fqbn"
#2) format fqbn lib:cpu:board:param1,param2

#

build:
	@echo "Compile..."
	@arduino-cli compile --libraries ./libraries --fqbn=esp8266:esp8266:generic:xtal=80,baud=921600 npml1 --output-dir ./bin

upload:
	@echo "Upload ./bin/npml1.ino.bin..."
	@arduino-cli upload --port /dev/tty.usbserial* --fqbn=esp8266:esp8266:generic:baud=921600 npml1 --output-dir ./bin

upload_fast:
	@echo "Upload ./bin/npml1.ino.bin..."
	@~/Library/Arduino15/packages/esp8266/tools/python3/3.7.2-post1/python3 -I /Users/vvzvlad/Library/Arduino15/packages/esp8266/hardware/esp8266/3.0.2/tools/upload.py --chip esp8266 --port /dev/tty.usbserial* --baud 3000000 --before default_reset --after hard_reset write_flash 0x0 ./bin/npml1.ino.bin

git_commit:
	git add . ; git commit -m "Auto(press enter): `date +'%Y-%m-%d %H:%M:%S'`" ; git remote | xargs -L1 git push --all

git_commit_wait:
	@echo "-----> Press enter for NOT git commit & push within 4 seconds <-----"
	@read -t 4 -n 1 && exit 0 ||

miniterm:
	@miniterm.py /dev/tty.usbserial* 115200

exeption_decode:
	@echo "Paste stacktrace and press 'Ctrl-D' twice "
	@cat > ./bin/trace.tmp && echo "\n\n" && java -jar ./common/EspStackTraceDecoder.jar ~/Library/Arduino15/packages/esp8266/tools/xtensa-lx106-elf-gcc/3.0.4-gcc10.3-1757bed/bin/xtensa-lx106-elf-addr2line ./bin/npml1.ino.elf ./bin/trace.tmp ; rm ./bin/trace.tmp

assets_convert:
	@python3 ./common/rgb565_converter.py --input ./assets --output ./libraries/assets/images.h

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
