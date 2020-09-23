
/*
 * Firmware version and build date
 */

#define BUILD_DATE  __DATE__ " " __TIME__
#define FW_VERSION  "1.1.0"
#define FW_NAME     "miro_firmware_esp"

/*
 * Define board model name
 */

#define GENERIC_ESP8266

#define MCU_OTA

//#define SECURE_TELNET
#define LOGIN "miro"
#define PASSWORD "robot"

/*
 * Define board hostname
 */

#define DEF_HOSTNAME "MIRO"

/*
 * Defines the communication channel between microcontroller
 * and esp82266, with concerning parameters
 */

//#define HOSTNAME "arduino" //used by mdns protocol

#define BOARDMODEL "MIRO_ESP8266"
#define ARDUINO_BOARD "miro"   //mdns
#define ESP_CH_UART
#define BAUDRATE_COMMUNICATION 115200 // to start with SoftwareSerial, set 115200 for good connection
#define RXBUFFERSIZE 1024
#define STACK_PROTECTOR  512 // bytes
#define WIFI_LED 14
#define SSIDNAME "MIRO"

#define EEPROM_PWD_ADDRESS 0
