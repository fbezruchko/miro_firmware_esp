/*
 * C api for arduino esp8266 web server
 * To be built under ARDUINO IDE only (as a part of libdfu for arduino)
 */
#include <ESP8266WebServer.h>
#include <dfu.h>
#include "arduino-server.h"

static ESP8266WebServer *server;
static int first_chunk, error, chunk_ready, last_chunk;
static uint8_t *current_chunk;
static int current_chunk_len;
static int finalized;

// variables for upload for Uno WiFi with default IDE network upload tool arduinoOTA
enum struct OtaState {
  READY,
  START,
  WAIT_HEX,
  READ_HEX,
  FINALIZE
};

#define HEX_LINE_LENGTH 45
#define HEX_BUFFER_SIZE (20 * HEX_LINE_LENGTH)
#define OTA_TIMEOUT 5000

static WiFiServer ota_server(80); // started only for hex upload, ESP8266WebServer can't do that
static WiFiClient ota_client;
static OtaState ota_state = OtaState::READY;
static size_t hex_size = 0;
static size_t hex_pos = 0;
static byte buffer[HEX_BUFFER_SIZE];
static unsigned long ota_start = 0; // for timeouts
// end of variables for upload for Uno WiFi

// handles upload with new mcu_ota tool
static void handle_ota_post(void)
{
	HTTPUpload& upload = server->upload();
	int n, tot;

	tot = server->arg("totchunk").toInt();
	n = server->arg("numchunk").toInt();
	first_chunk = n == 1;
	last_chunk = n == tot;
	chunk_ready = 1;
	current_chunk = upload.buf;
	current_chunk_len = upload.currentSize;
}


extern "C" int arduino_server_send(int code, const char *msg)
{
  if (ota_state == OtaState::READY) { // WebServer is active
	  server->send(code, "text/plain", msg);
  }

  // for arduinOTA tool, send response only after upload is complete
  if (ota_state == OtaState::FINALIZE) {
    dfu_log("%s %d %s\n", __func__, code, msg);
    ota_client.printf("HTTP/1.1 %d %s\r\n\r\n", code, msg);
    delay(100);
    ota_client.stop();
    ota_server.stop();
    server->begin();
    dfu_log("ota server stop/web server start\n");
    ota_state = OtaState::READY;
  }
	return 0;
}

extern "C" int arduino_server_poll(void)
{
  if (ota_state == OtaState::START) {
    server->stop();
    ota_server.begin();
    ota_state = OtaState::WAIT_HEX;
    ota_start = millis();
    dfu_log("web server stop/ota server start\n");
  }
  if (ota_state == OtaState::WAIT_HEX) {
    if (millis() - ota_start > OTA_TIMEOUT) {
      error = 1;
      ota_state = OtaState::FINALIZE;
      return 1;
    }
    ota_client = ota_server.available();
    if (ota_client) {
      if (ota_client.connected()) {
        char buff[100];
        buff[ota_client.readBytesUntil('\r', buff, 100)] = 0;
        if (!strstr(buff, "/pgm/upload")) {
          ota_client.stop();
          return 0;
        }
        ota_client.find("Content-Length:");
        hex_size = ota_client.parseInt();
        ota_client.find("\r\n\r\n");
        ota_state = OtaState::READ_HEX;
        chunk_ready = 0;
        dfu_log("Content-Length: %d\n", hex_size);
      }
    }
  }
  if (ota_state == OtaState::READ_HEX && !chunk_ready) {
    dfu_log("%s\n", __func__);
    first_chunk = (hex_pos == 0);
    int l = 0;
    ota_start = millis();
    while (1) {
      int b = ota_client.peek();
      if (b == -1) {
        if (millis() - ota_start > OTA_TIMEOUT) {
          dfu_log("ota timeout");
          ota_client.stop();
          error = 1;
          ota_state = OtaState::FINALIZE;
          return 1;
        }
        continue;
      }
      if (b == ':' && l > 0) {
        buffer[l++] = '\r';
        buffer[l++] = '\n';
        if (l > HEX_BUFFER_SIZE - HEX_LINE_LENGTH)
          break;
      }
      ota_client.read();
      hex_pos++;
      buffer[l++] = b;
      if (hex_pos == hex_size) {
        buffer[l++] = '\r';
        buffer[l++] = '\n';
        break;
      }
    }
    dfu_log("hex_pos %d  block length %d\n", hex_pos, l);
    last_chunk = (hex_pos == hex_size);
    chunk_ready = 1;
    current_chunk = buffer;
    current_chunk_len = l;
    if (last_chunk) {
      ota_state = OtaState::FINALIZE;
    }
  }
  //server->handleClient(); // it is in the main loop
	return chunk_ready;
}

extern "C" void arduino_server_ack(void)
{
  dfu_log("%s\n", __func__);
	chunk_ready = 0;
}

extern "C" void arduino_server_get_data(struct arduino_server_data *sd)
{
	sd->chunk_ready = chunk_ready;
	sd->first_chunk = first_chunk;
	sd->last_chunk = last_chunk;
	sd->error = error;
	sd->chunk_ptr = current_chunk;
	sd->chunk_len = current_chunk_len;
}

// Necessary for arduinoOTA tool. returns what is expected
static void handle_ota_sync_start(void) {
  arduino_server_send(204, "");
}

// Necessary for arduinoOTA tool. returns what is expected and starts 'the ota_states'
static void handle_ota_start(void) {
  arduino_server_send(200, "SYNC");
  ota_state = OtaState::START;
}

// Necessary for arduinoOTA tool. returns what is expected
static void handle_ota_reset(void) {
  arduino_server_send(200, "");
}

extern "C" int arduino_server_init(void *_srv)
{
	server = (ESP8266WebServer *)_srv;
	if (!finalized) {
		server->on("/otafile", HTTP_POST, handle_ota_post); // only this is for new mcu_ota tool
    server->on("/pgm/sync", HTTP_POST, handle_ota_sync_start);
    server->on("/pgm/sync", HTTP_GET, handle_ota_start);
    server->on("/log/reset", HTTP_POST, handle_ota_reset);
//		server->begin();  // to be started outside
	}
	finalized = 0;
}

extern "C" void arduino_server_fini(void)
{
	/*
	 * ESP8266WebServer has no end() method.
	 * We just take note that the server has been finalized and avoid
	 * trying to re-initialize it next time
	 */
	finalized = 1;
}
