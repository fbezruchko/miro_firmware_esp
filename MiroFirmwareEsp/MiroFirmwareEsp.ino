#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include "Configuration.h"
#include <algorithm>

#include "utility/wifi_utils.h"
#include "config.h"

#include <FS.h>
#include <ArduinoJson.h>
#include <Hash.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

int ledState = LOW;             // used to set the LED state
long previousMillis = 0;        // will store last time LED was updated
long ap_interval = 50;         //blink interval in ap mode
IPAddress default_IP(192, 168, 240, 1); //defaul IP Address
String HOSTNAME = DEF_HOSTNAME;
String staticIP_param;
String netmask_param;
String gateway_param;
String dhcp = "on";

#define MAX_TELNET_CLIENTS 1
char telnet_login[] = "miro";
char telnet_pass[] = "robot";
bool telnet_active[MAX_TELNET_CLIENTS];

WiFiServer telnet(23);
WiFiClient telnetClient[MAX_TELNET_CLIENTS];
ESP8266WebServer server(80);    //server UI
ESP8266HTTPUpdateServer httpUpdater;

void read_EEPROM_PWD(char *pass)
{
  unsigned int addr = EEPROM_PWD_ADDRESS;
  unsigned char i, str_len = 0;
  str_len = EEPROM.read(addr);
  addr++;
  for (i = 0; i < str_len; i++) *(pass + i) = EEPROM.read(addr+i);
  *(pass + i) = '\0';
}

void write_EEPROM_PWD(char *pass)
{
  unsigned int addr = EEPROM_PWD_ADDRESS;
  unsigned char i, str_len;
  str_len = (unsigned char)strlen(pass);
  EEPROM.write(addr, str_len);
  addr++;
  for (i = 0; i < str_len; i++) EEPROM.write(addr+i, *(pass + i));
}

void setup() {

  _setup_dfu();
  pinMode(WIFI_LED, OUTPUT);      //initialize wifi LED
  digitalWrite(WIFI_LED, LOW);

  ArduinoOTA.begin();
  //OTA ESP
  initMDNS();

  pinMode(3, INPUT_PULLUP);
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  //pinMode(3, INPUT);
  Serial.begin(BAUDRATE_COMMUNICATION);
  Serial.setRxBufferSize(RXBUFFERSIZE);
  while (!Serial);

  SPIFFS.begin();
  initHostname();
  setWiFiConfig();

  httpUpdater.setup(&server);
  initWebServer();                 //UI begin

  //start telnet server
  telnet.begin();
  telnet.setNoDelay(true);
}

void loop() {
  ArduinoOTA.handle();
  handleWebServer();
  wifiLed();
  _handle_Mcu_OTA();

  telnetCheckClients();
  handleTelnetServer();
}

void initMDNS() {

  MDNS.begin(HOSTNAME.c_str());
  MDNS.setInstanceName(HOSTNAME);
  MDNS.addServiceTxt("miro", "tcp", "fw_name", FW_NAME);
  MDNS.addServiceTxt("miro", "tcp", "fw_version", FW_VERSION);

}

void initHostname() {
  //retrieve user defined hostname
  String tmpHostname = Config.getParam("hostname");
  if ( tmpHostname != "" )
    HOSTNAME = tmpHostname;
  WiFi.hostname(HOSTNAME);

}

void wifiLed() {
  unsigned long currentMillis = millis();
  int wifi_status = WiFi.status();
  if ((WiFi.getMode() == 1 || WiFi.getMode() == 3) && wifi_status == WL_CONNECTED) {    //wifi LED in STA MODE
    if (currentMillis - previousMillis > ap_interval) {
      previousMillis = currentMillis;
      if (ledState == LOW) {
        ledState = HIGH;
        ap_interval = 200;    //time wifi led ON
      }
      else {
        ledState = LOW;
        ap_interval = 2800;   //time wifi led OFF
      }
      digitalWrite(WIFI_LED, ledState);
    }
  }
  else { //if (WiFi.softAPgetStationNum() > 0 ) {   //wifi LED on in AP mode
    if (currentMillis - previousMillis > ap_interval) {
      previousMillis = currentMillis;
      if (ledState == LOW) {
        ledState = HIGH;
        ap_interval = 950;
      }
      else {
        ledState = LOW;
        ap_interval = 50;
      }
      digitalWrite(WIFI_LED, ledState);
    }
  }
}

void setWiFiConfig() {
  //WiFi mode is remembered by the esp sdk
  if (WiFi.getMode() != WIFI_STA)
  {
    //set default AP
    String mac = WiFi.macAddress();
    String apSSID = String(SSIDNAME) + "-" + String(mac[9]) + String(mac[10]) + String(mac[12]) + String(mac[13]) + String(mac[15]) + String(mac[16]);
    //char softApssid[18];
    //apSSID.toCharArray(softApssid, apSSID.length() + 1);
    ////delay(1000);
    //WiFi.softAP(softApssid);
    WiFi.softAP(apSSID.c_str());
    WiFi.softAPConfig(default_IP, default_IP, IPAddress(255, 255, 255, 0));   //set default ip for AP mode
  }
  //set STA mode
  else
  { // first static config if configured
    String staticIP = Config.getParam("staticIP").c_str();
    if (staticIP != "" && staticIP != "0.0.0.0") {
      dhcp = "off";
      staticIP_param = staticIP;
      netmask_param = Config.getParam("netMask").c_str();
      gateway_param = Config.getParam("gatewayIP").c_str();
      WiFi.config(stringToIP(staticIP_param), stringToIP(gateway_param), stringToIP(netmask_param));
    }
  }

  WiFi.begin(); // connect to AP with credentials remembered by esp sdk
  if (WiFi.waitForConnectResult() != WL_CONNECTED && WiFi.getMode() == WIFI_STA) {
    // if STA didn't connect, start AP
    WiFi.mode(WIFI_AP_STA); // STA must be active for library connects
    setWiFiConfig(); // setup AP
  }
}

void telnetCheckClients()
{
  //check if there are any new clients
  static int CountClients = 0;
  if (telnet.hasClient()) {
    int i;
    for (i = 0; i < MAX_TELNET_CLIENTS; i++)
      if (!telnetClient[i])
      {
        telnetClient[i] = telnet.available();
        char loginString[32];
        char passString[32];
        char inChar = 0;
        unsigned char login_buffer_pos = 0;
        unsigned char pass_buffer_pos = 0;
        telnetClient[i].print("login: ");
        telnetClient[i].flush();
        while (telnetClient[i].available()) telnetClient[i].read();
        while ( (telnetClient[i].connected()) && (inChar != '\n') && (inChar != '\r') && (login_buffer_pos < 32) )
        {
          if (telnetClient[i].available())
          {
            inChar = telnetClient[i].read();
            loginString[login_buffer_pos] = inChar;
            login_buffer_pos++;
          }
        }
        loginString[login_buffer_pos-1] = '\0';
        //telnetClient[i].print(loginString);
        //telnetClient[i].print('\n');
        
        telnetClient[i].print("password: ");
        telnetClient[i].flush();
        while (telnetClient[i].available()) telnetClient[i].read();
        inChar = 0;
        while ( (telnetClient[i].connected()) && (inChar != '\n') && (inChar != '\r') && (pass_buffer_pos < 32) )
        {
          if (telnetClient[i].available())
          {
            inChar = telnetClient[i].read();
            passString[pass_buffer_pos] = inChar;
            pass_buffer_pos++;
          }
        }
        passString[pass_buffer_pos-1] = '\0';
        //telnetClient[i].print(passString);
        //telnetClient[i].print('\n');

        if ( strcmp(loginString, telnet_login) || strcmp(passString, telnet_pass) )
        {
          telnetClient[i].println("Login or password incorrect");
          telnetClient[i].println("Connection terminating...");
          delay(1000);
          telnetClient[i].flush();
          telnetClient[i].stop();
          return;
        }
        else
        {
          telnet_active[i] = true;
          //telnetClient[i].print('\r');
          //telnetClient[i].print('\r');
          //telnetClient[i].print('\r');
        }

        telnetClient[i].println("Enter 'help' for a list of built-in commands.");
        telnetClient[i].println();
        telnetClient[i].println(" .-.      .-..-..------. .-----.");
        telnetClient[i].println(" |  \\    /  || ||  __  | |  _  |");
        telnetClient[i].println(" |   \\  /   || || |  | | | | | |");
        telnetClient[i].println(" | |\\ \\/ /| || || |__| . | | | |");
        telnetClient[i].println(" | | \\__/ | || || |  \\ \\ | |_| |");
        telnetClient[i].println(" | |      |_||_||_|   \\_\\|_____|");
        telnetClient[i].println(" |_| OPEN SOURCE ROBOT PLATFORM");
        telnetClient[i].println(" -----------------------------------------------------");
        telnetClient[i].println(" KHABAROVSK (RUSSIA): 48.476234, 135.089290");
        telnetClient[i].println(" -----------------------------------------------------");
        telnetClient[i].println(" * Amur river");
        telnetClient[i].println(" * Himalayan bear");
        telnetClient[i].println(" * Khabarovsk (Alekseevsky) bridge");
        telnetClient[i].println(" * Chum (salmon fish)");
        telnetClient[i].println(" * Pacific National University");
        telnetClient[i].println(" -----------------------------------------------------");
        telnetClient[i].println();
        telnetClient[i].print("Local IP: ");
        telnetClient[i].println(WiFi.localIP());
        telnetClient[i].println();
        Serial.println();
        break;
      }
    //no free/disconnected spot so reject
    if (i == MAX_TELNET_CLIENTS) {
      telnet.available().println("Server is busy!");
      // hints: server.available() is a WiFiClient with short-term scope
      // when out of scope, a WiFiClient will
      // - flush() - all data will be sent
      // - stop() - automatically too
    }
  }
}

void handleTelnetServer()
{
#if 1

  // Incredibly, this code is faster than the bufferred one below - #4620 is needed
  // loopback/3000000baud average 348KB/s
  for (int i = 0; i < MAX_TELNET_CLIENTS; i++)
  {
    if (telnet_active[i])
    {
      while (telnetClient[i].available() && Serial.availableForWrite() > 0) {
        // working char by char is not very efficient
        Serial.write(telnetClient[i].read());
      }
    }
  }

#else

  // loopback/3000000baud average: 312KB/s
  for (int i = 0; i < MAX_TELNET_CLIENTS; i++)
  {
    while (telnetClient[i].available() && Serial.availableForWrite() > 0) {
      size_t maxToSerial = std::min(telnetClient.available(), Serial.availableForWrite());
      maxToSerial = std::min(maxToSerial, (size_t)STACK_PROTECTOR);
      uint8_t buf[maxToSerial];
      size_t tcp_got = telnetClient[i].read(buf, maxToSerial);
      size_t serial_sent = Serial.write(buf, tcp_got);
    }
  }

#endif

  // determine maximum output size "fair TCP use"
  // client.availableForWrite() returns 0 when !client.connected()
  size_t maxToTcp = 0;
  for (int i = 0; i < MAX_TELNET_CLIENTS; i++)
  {
    if (telnet_active[i])
    {
      if (telnetClient[i]) {
        size_t afw = telnetClient[i].availableForWrite();
        if (afw) {
          if (!maxToTcp) {
            maxToTcp = afw;
          } else {
            maxToTcp = std::min(maxToTcp, afw);
          }
        }
      }
    }
  }

  //check UART for data
  size_t len = std::min((size_t)Serial.available(), maxToTcp);
  //size_t len = (size_t)Serial.available();
  len = std::min(len, (size_t)STACK_PROTECTOR);
  if (len)
  {
    uint8_t sbuf[len];
    size_t serial_got = Serial.readBytes(sbuf, len);
    // push UART data to connected telnet client
    for (int i = 0; i < MAX_TELNET_CLIENTS; i++)
    {
      if (telnet_active[i])
      {
        // if client.availableForWrite() was 0 (congested)
        // and increased since then,
        // ensure write space is sufficient:
        if (telnetClient[i].availableForWrite() >= serial_got) {
          size_t tcp_sent = telnetClient[i].write(sbuf, serial_got);
        }
      }
    }
  }
}
