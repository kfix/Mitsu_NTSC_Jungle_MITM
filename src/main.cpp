#include <Arduino.h>
#include <Wire.h>

#define MITSU_I2C_ADDRESS 93U // 0x5d
#define REG_OSD_RGB_MODE 0x15u
// https://www.ezsbc.com/product/esp32-breakout-and-development-board/
#define SDA_1 22 // ezSBC 15(has built-in pullup) - master pin to jungle
// I had trouble using an external 3.3V PSU for the ESP && external pullups --
//   the built-in ones seem to be good-enough....
#define SCL_1 21 // ezSBC 16
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

void writeToMicomStatic()
{
  I2C_micom.write(205);
  return;
  // this basically tells control chip that the H/V output is still
  // working with normal current draw.
  // this is a safety feature (auto CRT shutoff) we're subverting,
  // so don't leave the TV on unattended, OK?
/*
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
}

void writeToMicom()
{
    I2C_micom.write(r, 2);
  // NOTE: master will interrupt any write() of ours after it thinks
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
      //  bitClear(val, 7); // don't allow video to be muted
      //  delay(50);
        break;
      case 0x06: // is continously set on interval by Orion TV!
        bitClear(val, 4); // we want C-video, not just Y-video (luminance - monochrome!);
        // bitSet(val, 2); // set EXT bit
        /*
            seems to make chip ignore Video AND Sync on the Y-in pin,
            guessing that EXT/C pin becomes the active input...
            TODO:
              * install a sync-stripper off comp-vid input and route it to EXT/C
              * tie a GPIO into my blanking-switch (RGB OFF/ON)
              * if switch-pin is HIGH, then set this bit to use the stripped-sync as input
        */
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
  Serial.begin(115200);
  I2C_jungl.onReceive(readFromJungle);
  while (!I2C_jungl.begin(SDA_1, SCL_1, I2C_FREQ)) {
    Serial.print("error initializing I2C for jungle, will retry");
    delay(300);
  }
  I2C_micom.onReceive(readFromMicom); // master is writing to us
  I2C_micom.onRequest(writeToMicom); // master is reading from us
  while (!I2C_micom.begin(MITSU_I2C_ADDRESS, SDA_2, SCL_2, I2C_FREQ)) {
    Serial.println("error initializing I2C for micom, will retry");
    delay(300);
  }
}

void loop()
{
}