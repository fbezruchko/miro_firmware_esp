/*
Copyright <2017> <COPYRIGHT HOLDER>

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE

*/

#include "Arduino.h"
#include "utility/wifi_utils.h"
#include "config.h"

#ifndef H_COMM_ITF_H
#define H_COMM_ITF_H

// enum CHANNEL {
//   CH_SERIAL = 0,
//   CH_SPI
// };
//
// enum MCU {
//   AVR328P = 0,
//   NRF52,
//   STM32,
//   SAMD21
// };

class CommItf {

public:

	CommItf();
	bool begin();
	int read(tMsgPacket *_pck);
	void write(uint8_t *_pck, int transfer_size);
  void end();
  bool available();

private:

  int createPacket(tMsgPacket *_pck);

  /*SPI*/
  #if defined ESP_CH_SPI
  void SPISlaveInit();
  void SPISlaveWrite(uint8_t* _resPckt,int transfer_size);
  #endif

  /*Serial*/
  #if defined ESP_CH_UART
  String readStringUntil(char);
  int timedRead();
  #endif

};

extern CommItf CommunicationInterface;

#endif
