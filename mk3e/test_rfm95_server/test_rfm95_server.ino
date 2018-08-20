// Library needed to run this sketch: https://www.airspayce.com/mikem/arduino/RadioHead/
// Example sketch showing how to create a simple addressed, reliable messaging server
// with the RHReliableDatagram class, using the RH_RF95 driver to control a RF95 radio.
// It is designed to work with the other example rf95_reliable_datagram_client
// Tested on a Canique MK3e

#include <RHReliableDatagram.h>
#include <RH_RF95.h>
#include <SPI.h>

#define CLIENT_ADDRESS 1
#define SERVER_ADDRESS 2


#define FREQUENCY   433000 //for RFM96
//#define FREQUENCY   868000 //for RFM95

// Singleton instance of the radio driver
RH_RF95 driver(10, 2); // Canique MK3e - slave select pin D10, interrupt pin D2

// Class to manage message delivery and receipt, using the driver declared above
RHReliableDatagram manager(driver, SERVER_ADDRESS);


void setup() 
{
  Serial.begin(115200);
  while (!Serial) ; // Wait for serial port to be available
  
  if (!manager.init())
  {
    Serial.println("init failed");
    return;
  }
  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

  #if FREQUENCY == 868000
  driver.setFrequency(868.0);
  #endif

  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then 
  // you can set transmitter powers from 5 to 23 dBm:
  driver.setTxPower(5, false);
  
  // You can optionally require this module to wait until Channel Activity
  // Detection shows no activity on the channel before transmitting by setting
  // the CAD timeout to non-zero:
//  driver.setCADTimeout(10000);

  Serial.println("Waiting for messages");
  Serial.flush();
}

uint8_t data[] = "And hello back to you";
// Dont put this on the stack:
uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];

void loop()
{
  if (manager.available())
  {
    // Wait for a message addressed to us from the client
    uint8_t len = sizeof(buf);
    uint8_t from;
    if (manager.recvfromAck(buf, &len, &from))
    {
      Serial.print("got request from : 0x");
      Serial.print(from, HEX);
      Serial.print(": ");
      Serial.println((char*)buf);

      Serial.print("SNR: ");
      Serial.println(driver.lastSNR());
      Serial.flush();

      // uncomment to send a reply back to the originator client
      /*
      if (!manager.sendtoWait(data, sizeof(data), from))
        Serial.println("sendtoWait failed");
      */
    }
  }
}


