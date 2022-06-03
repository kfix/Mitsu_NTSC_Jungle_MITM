/*
 * LED codes from  https://github.com/kriswiner/ESP32/blob/master/PWM/ledcWrite_demo_ESP32.ino
 * Adapted for EZSBC ESP32 board
 */
#include <Arduino.h>
// https://github.com/espressif/arduino-esp32/blob/master/libraries/Wire/src/Wire.h
#include <Wire.h>


#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include "driver/adc.h"


// Set up the rgb led names
#define ledR  16
#define ledG  17
#define ledB  18
#define ledB2 19

uint8_t ledArray[3] = {1, 2, 3}; // three led channels

uint8_t color = 0;          // a value from 0 to 255 representing the hue
uint32_t R, G, B;           // the Red Green and Blue color components
uint8_t brightness = 255;  // 255 is maximum brightness, but can be changed

// Courtesy http://www.instructables.com/id/How-to-Use-an-RGB-LED/?ALLSTEPS
// function to convert a color to its Red, Green, and Blue components.

void hueToRGB(uint8_t hue, uint8_t brightness)
{
  uint16_t scaledHue = (hue * 6);
  uint8_t segment = scaledHue / 256; // segment 0 to 5 around the
  // color wheel
  uint16_t segmentOffset =
    scaledHue - (segment * 256); // position within the segment

  uint8_t complement = 0;
  uint16_t prev = (brightness * ( 255 -  segmentOffset)) / 256;
  uint16_t next = (brightness *  segmentOffset) / 256;

  switch (segment) {
    case 0:      // red
      R = brightness;
      G = next;
      B = complement;
      break;
    case 1:     // yellow
      R = prev;
      G = brightness;
      B = complement;
      break;
    case 2:     // green
      R = complement;
      G = brightness;
      B = next;
      break;
    case 3:    // cyan
      R = complement;
      G = prev;
      B = brightness;
      break;
    case 4:    // blue
      R = next;
      G = complement;
      B = brightness;
      break;
    case 5:      // magenta
    default:
      R = brightness;
      G = complement;
      B = prev;
      break;
  }
}

#define MITSU_I2C_ADDRESS 93U // 0x5d
#define REG_OSD_RGB_MODE 0x15u
// https://www.ezsbc.com/product/esp32-breakout-and-development-board/
#define SDA_1 21 // ezSBC 16(has built-in pullup) - master pin to jungle
#define SCL_1 22 // ezSBC 15
#define SDA_2 25  // ezSBC 10 slave pin to micom
#define SCL_2 26  // ezSBC 11
// GPIO 34, 35, 36 and 39 are INPUT ONY ports and can not be assigned as SCL or SDA
#define I2C_FREQ 100000U // 100khz lowspeed devices

TwoWire I2C_jungl = TwoWire(1); 
TwoWire I2C_micom = TwoWire(0);

uint8_t r[2] = {0, 0};
volatile byte mRCount = 0;

bool writeToJungle(const uint8_t reg, const uint8_t val, bool verbose) {
  I2C_jungl.beginTransmission((uint8_t) MITSU_I2C_ADDRESS);
  int b1 = I2C_jungl.write(reg);
  int b2 = I2C_jungl.write(val);
  int error;
  error = I2C_jungl.endTransmission();
  if((error != 0) || (b1 == 0) || (b2 == 0)) {
    char buff[50];
    sprintf(buff, "Failed writing register %#02x (%d) => %#02x (%d)\n", reg, reg, val, val);
    Serial.print(buff);
    return false;
  } else if (verbose) {
    char buff[50];
    sprintf(buff, "Writing register %#02x (%d) => %#02x (%d)\n", reg, reg, val, val);
    Serial.print(buff);
  }
  return true;
}

void writeToMicom()
{
  I2C_micom.write(205);
  return;
    //Serial.println("\nI/O to TV MICOM!");
    // sleep to increase likelihood of r[] having data?
    // I2C_micom.write(r, 2);
    // should we just writeback a pre-recorded OK instead?
    // https://github.com/coredump/27v66_i2c_intercept_promicro/blob/main/src/main.cpp#L88
/*
KILLERB 1 00H D7 Killer off for manual mode.
AFT0 1 00H D3 AFT output
AFT1 1 00H D2 AFT output
HCOINB 1 00H D1 Horizontal mute det output. 0: H coincident
FM STDETB 1 01H D4 Station det for FM Radio mode. 0: Station det.
VCOINB 1 01H D3 Vertical Sync det output. 0:V coincident
STDETB 1 01H D2 Station det for TV mode. 0: Station det.

D0 unassigned
HCOINB D1 Horizontal mute det output. 0: H coincident
AFT1 D2 AFT output
AFT0 D3 AFT output
STDETB D4 Station det for TV mode. 0: Station det.
VCOINB D5 Vertical Sync det output. 0:V coincident
FM STDETB D6 Station det for FM Radio mode. 0: Station det.
KILLERB D7 Killer off for manual mode.
*/
/*
 dumped seq of reads' datas:
 77 =>        '0b01001101'
 85 =>        '0b01010101'
 213 (x6) =>   0b11010101
 221 (x13) => '0b11011101'
 205 =>       '0b11001101'
*/
  if (mRCount == 1) {
    I2C_micom.write(77);
    mRCount++;
  } else if (mRCount == 2) {
    I2C_micom.write(85);
    mRCount++;
  } else if (mRCount < 9) {
    I2C_micom.write(213);
    mRCount++;
  } else if (mRCount < 21) {
    // could just jump straight to this?
    I2C_micom.write(221);
    mRCount++;
  } else {
    I2C_micom.write(205);
  }
    //Serial.println("\nheartbeat from MITMd NTSC to TV MICOM!");
    // this basically tells control chip that the H/V output is still
    // working with normal current draw.
    // this is a safety feature (auto CRT shutoff) we're subverting to
    // make our code simpler, so don't leave the TV on unattended, OK?
// note master will interrupt any write() of ours after it thinks
// its gotten enough data
}

void readFromMicom(int byteCount)
{
    //Serial.println("\nsettings from TV MICOM to MITMd NTSC!");
    if (byteCount != 2) {
      return;
    }
    uint8_t reg = I2C_micom.read();
    uint8_t val = I2C_micom.read();
    bool frobbled = false;
    bool verbose = false;
    switch (reg)
    {
      case REG_OSD_RGB_MODE: // is continously set on interval by Orion TV!
        // ensure "Analog OSD" is left pulled-up (the 5th bit from right)
        bitSet(val, 4); // Analog OSD 1=>ON 0=>OFF
        // bitSet(val, 3); // Force Monochrome 1=>ON 0=>OFF
        // bitSet(val, 2); // Force Color 1=>ON 0=>OFF
        frobbled = true;
      //case 0x08:
      //  bitSet(val, 7); // Force Blue BG 1=>ON 0=>OFF
      //   obscures Y-CVBS too
      // case 0x10:
      //  bitSet(val, 7); // Force White BG 1=>ON 0=>
      //case 0x13:
      //case 0x2:
      //  delay(50);
        break;
      case 0x06: // is continously set on interval by Orion TV!
        bitClear(val, 4); // we want C-video, not just Y-video (luminance - monochrome!);
        // bitSet(val, 2); // set EXT bit
        //      seems to not be needed to show External RGB if fast-blanking pin is correctly powered
        break;
      default:
        // passthru
        break;
    }
    if (!writeToJungle(reg, val, verbose)) {
      // lets do _one_ retry
      delay(50);
      writeToJungle(reg, val, verbose);
    }
    // could also put [reg, val] in a circular buffer that loop() flushes on interval,
    //   instead of proxying from the handler

    if (frobbled) {
      // Serial.println("OSD RGB analogging bit frobbled!");
    }
}

void readFromJungle(int byteCount)
{
  int error;
  I2C_jungl.requestFrom(MITSU_I2C_ADDRESS, byteCount, 1);
  // NAKs and STOPs after 2nd byte read from jungle/slave
  if (I2C_jungl.available() == byteCount)
  {
    I2C_jungl.readBytes(r, byteCount);
  }
}

// the setup routine runs once when you press reset:
void setup()
{
  // reduce power draw
  setCpuFrequencyMhz(80);
  adc_power_off();
  esp_wifi_stop();
  esp_bt_controller_disable();
  esp_bluedroid_disable();

  Serial.begin(115200);
  if (!I2C_micom.begin(MITSU_I2C_ADDRESS, SDA_2, SCL_2, I2C_FREQ)) {
    Serial.println("error connecting with micom");
  }
  I2C_micom.onReceive(readFromMicom); // master is writing to us
  I2C_micom.onRequest(writeToMicom); // master is reading from us
  Serial.println("\nMitsu NTSC MITM");
 
  //ledcAttachPin(ledR,  1); // assign RGB led pins to channels
  //ledcAttachPin(ledG,  2);
  //ledcAttachPin(ledB,  3);
  //ledcAttachPin(ledB2, 4);
  // Initialize channels
  // channels 0-15, resolution 1-16 bits, freq limits depend on resolution
  // ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution_bits);
  //ledcSetup(1, 12000, 8); // 12 kHz PWM, 8-bit resolution
  //ledcSetup(2, 12000, 8);
  //ledcSetup(3, 12000, 8);
  //ledcSetup(4, 12000, 8);

  if (!I2C_jungl.begin(SDA_1, SCL_1, I2C_FREQ)) {
    Serial.print("error connecting with jungle");
  }
  /*
  I2C_jungl.beginTransmission((uint8_t) MITSU_I2C_ADDRESS);
  if (0 == I2C_jungl.endTransmission()) {
    Serial.println("Mitsu NTSC jungle chip found!");
  } else {
    Serial.println("no jungle chip found!");
  }
  */
  //I2C_jungl.onReceive(readFromJungle);
}

// void loop runs over and over again
void loop()
{
  /*
  for (color = 0; color < 255; color++) { // Slew through the color spectrum
    hueToRGB(color, brightness);  // call function to convert hue to RGB

    // write the RGB values to the pins
    ledcWrite(1, R); // write red component to channel 1, etc.
    ledcWrite(2, G);
    ledcWrite(3, B);

    //ledcWrite(4, color); // makes other blue led dim on and off
    delay(10); // full cycle of rgb over 256 colors takes 26 seconds
  }
  */
 /*
  noInterrupts();
  readFromJungle(2);
  interrupts();
  delay(1000);
  */
}

