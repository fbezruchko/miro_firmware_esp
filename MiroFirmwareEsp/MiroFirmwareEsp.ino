#include "CommLgc.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "Configuration.h"
#include <algorithm>

#include <FS.h>
#include <ArduinoJson.h>
#include <Hash.h>
#include <ESP8266WebServer.h>

int ledState = LOW;             // used to set the LED state
long previousMillis = 0;        // will store last time LED was updated
long ap_interval = 50;         //blink interval in ap mode
IPAddress default_IP(192, 168, 240, 1); //defaul IP Address
String HOSTNAME = DEF_HOSTNAME;
String staticIP_param ;
String netmask_param;
String gateway_param;
String dhcp = "on";

WiFiServer telnet(23);
WiFiClient telnetClient;
ESP8266WebServer server(80);    //server UI

void setup() {

#if defined(MCU_OTA)
  _setup_dfu();
#endif

  pinMode(WIFI_LED, OUTPUT);      //initialize wifi LED
  digitalWrite(WIFI_LED, LOW);

  ArduinoOTA.begin();
  //OTA ESP
  initMDNS();
  CommunicationLogic.begin();
  SPIFFS.begin();
  initHostname();
  setWiFiConfig();
  initWebServer();                 //UI begin

  //start telnet server
  telnet.begin();
  telnet.setNoDelay(true);
}

void loop() {

  ArduinoOTA.handle();
  //CommunicationLogic.handle();
  handleWebServer();
  wifiLed();

#if defined(MCU_OTA)
  _handle_Mcu_OTA();
#endif

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
  if (WiFi.getMode() != WIFI_STA) {

    //set default AP
    String mac = WiFi.macAddress();
    String apSSID = String(SSIDNAME) + "-" + String(mac[9]) + String(mac[10]) + String(mac[12]) + String(mac[13]) + String(mac[15]) + String(mac[16]);
    char softApssid[18];
    apSSID.toCharArray(softApssid, apSSID.length() + 1);
    //delay(1000);
    WiFi.softAP(softApssid);
    WiFi.softAPConfig(default_IP, default_IP, IPAddress(255, 255, 255, 0));   //set default ip for AP mode
  }
  //set STA mode
#if defined(ESP_CH_SPI)
  ETS_SPI_INTR_DISABLE();
#endif
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
#if defined(ESP_CH_SPI)
  ETS_SPI_INTR_ENABLE();
#endif
}

void telnetCheckClients()
{
  //check if there are any new clients
  if (telnet.hasClient()) {
    //find free/disconnected spot
    if (!telnetClient) { // equivalent to !serverClients[i].connected()
      telnetClient = telnet.available();
    }
  }
}

void handleTelnetServer()
{
#if 1
  // Incredibly, this code is faster than the bufferred one below - #4620 is needed
  // loopback/3000000baud average 348KB/s
  while (telnetClient.available() && Serial.availableForWrite() > 0) {
    // working char by char is not very efficient
    Serial.write(telnetClient.read());
  }
#else
  // loopback/3000000baud average: 312KB/s
  while (telnetClient.available() && Serial.availableForWrite() > 0) {
    size_t maxToSerial = std::min(telnetClient.available(), Serial.availableForWrite());
    maxToSerial = std::min(maxToSerial, (size_t)STACK_PROTECTOR);
    uint8_t buf[maxToSerial];
    size_t tcp_got = telnetClient.read(buf, maxToSerial);
    size_t serial_sent = Serial.write(buf, tcp_got);
  }
#endif


  // determine maximum output size "fair TCP use"
  // client.availableForWrite() returns 0 when !client.connected()
  size_t maxToTcp = 0;
  if (telnetClient) {
    size_t afw = telnetClient.availableForWrite();
    if (afw) {
      if (!maxToTcp) {
        maxToTcp = afw;
      } else {
        maxToTcp = std::min(maxToTcp, afw);
      }
    }
  }

  //check UART for data
  size_t len = std::min((size_t)Serial.available(), maxToTcp);
  //size_t len = (size_t)Serial.available();
  len = std::min(len, (size_t)STACK_PROTECTOR);
  if (len) {
    uint8_t sbuf[len];
    size_t serial_got = Serial.readBytes(sbuf, len);
    // push UART data to connected telnet client
    // if client.availableForWrite() was 0 (congested)
    // and increased since then,
    // ensure write space is sufficient:
    //telnetClient.print("Recieved from AVR: ");
    //telnetClient.print(serial_got);
    //telnetClient.println();
    //size_t tcp_sent = telnetClient.write(sbuf, serial_got);
    if (telnetClient.availableForWrite() >= serial_got) {
      size_t tcp_sent = telnetClient.write(sbuf, serial_got);
    }
  }
  //delay(1);
}
