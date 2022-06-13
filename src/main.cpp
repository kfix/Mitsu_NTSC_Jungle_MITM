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
#define RGB_SWITCH 33 // ezSBC 7
// GPIO 34, 35, 36 and 39 are INPUT ONY ports and can not be assigned as SCL or SDA
#define I2C_FREQ 100000U // 100khz lowspeed devices
TwoWire I2C_jungl = TwoWire(1); 
TwoWire I2C_micom = TwoWire(0);

uint8_t r[2] = {0, 0};
volatile byte mRCount = 0;
volatile bool rgb_switched = false;

// int verbosed[16];

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
HCOINB D1 Horizontal mute det output. 0 == no-Hsync-detected
AFT1 D2 AFT output
AFT0 D3 AFT output
STDETB D4 Station det for TV mode. 0: Station det.
VCOINB D5 Vertical Sync det output. 0 == no-Vsync-detected
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
    if (byteCount != 2) {
      return;
    }
    uint8_t reg = I2C_micom.read();
    uint8_t val = I2C_micom.read();
    bool frobbled = false;
    bool verbose = false;
    // NOTE these register maps are model specific!
    // TODO: make this a map struct/class so we can work with a diff model
    switch (reg)
    {
      case REG_OSD_RGB_MODE: // is continously set on interval by Orion TV!
        // ensure "Analog OSD" is left pulled-up (the 5th bit from right)
        bitSet(val, 4); // Analog OSD 1=>ON 0=>OFF
        // bitSet(val, 3); // Force Monochrome 1=>ON 0=>OFF
        // bitSet(val, 2); // Force Color 1=>ON 0=>OFF
        // bit7 // AFC2 H Phase (MSB bit)
        // bit5 // Fsc free | Free-running mode of crystal oscillator | 0: OFF | 1: Free-running
        // bit0 // Color Killer Sensitivity Threshold Switch | 0: 43dB | 1: 45dB
        frobbled = true;
      case 0x2: // is continously set on interval by Orion TV to 0x80
        //  bit7 // Luminance signal Mute ON/OFF switch | 0: Out | 1: Mute
        //  bit6 // AF Direct out/External Audio input signal switch | 0: AF amp out | 1: External
        //  bit5 // Forced Spot Killer under Power on condition | 1: OFF | 0: Forced S.Killer
        //  bit4 // Chroma Trap ON/OFF switch | 0:Chroma Trap ON | 1: Chroma Trap Off
        //  bit3 // Video Tone Gain (Hi/Normal) switch | 0: normal | 1: high(sharp)
        //  bit2 // ABCL | 0 => OFF (ACL) | 1 => ON
        //  bit0 // Chroma BPF/Take Off Switch | 0 :BPF | 1: Take Off
        if (rgb_switched) {
          // if sync line is Comp-video, we'd get "double-exposed" over saturation unless we mute
          //bitSet(val, 7);
        } else {
          //bitClear(val, 7);
        }
        //verbose = true;
        break;
      case 0x06: // is continously set on interval by Orion TV! to 0xa0
        // bit2 // AV Switch Selector |  V-Latch | 0: TV mode | 1: EXT mode
        // bit3 // Black Stretch function ON/OFF switch | 0: ON | 1: OFF
        // bit4 // AV Switch Selector |  V-Latch | 0: Composite video input | 1: Y/C input mode
        if (rgb_switched) {
          //bitSet(val, 4);
          //bitSet(val, 2);
        } else {
          bitClear(val, 4); // 1 would show luminance as a monochrome picturel!;
        }
        /*
            seems to make chip ignore Video AND Sync on the Y-in pin,
            guessing that EXT/C pin becomes the active input...
            TODO:
              * install a sync-stripper off comp-vid input and route it to EXT/C
              * tie a GPIO into my blanking-switch (RGB OFF/ON)
              * if rgb-switch is HIGH, then set this bit to use the stripped-sync as input
        */
        verbose = true;
        break;
      // -- we can check the rest in sorted order, not as frequently sent or important at startup --
      case 0x3: // continous 0x0
      // bit7  // AF Direct out ON/OFF(Mute) switch | 0: Sound ON (Non Mute) | 1: Mute
        break;
      case 0x4: // set continuously to 0xa8 => 0b10101000
        // bit2 // ABCL Gain | 0 => Lo | 1 => Hi
        // bit6 // AFT OUT ON/OFF(Defeat) switch | 0: AFT ON (Non Defeat) | 1: Defeat
        break;
      //case 0x7:
        // bit1 // VIF AGC Gain Normal/Minimum switch |  0: AGC Function |1: Defeat(Minimum Gain)
      //case 0x08:
        // bitSet(val, 7); // Blue Back mode switch | 1=>ON | 0=>OFF
        //  obscures Y-CVBS too
      case 0x9: // continuous 0x59 => 0b01011001
        // bit1 // Horizontal AFC2 Gain switch | 0: High | 1: Low
        break;
      //case 0x0b:
      // bit7 // Vertical Forced free-running mode switch | 0: OFF | 1: Forced Free-running
      case 0x10: // continous 0x14
        //  bitSet(val, 7); // White Raster Mode Switch | 1=>ON 0=>OFF
        //  bit6 // Sync Det Slice Level (50%/40%) | 0: 50% | 1: 40%
        //  bit3-5 // H VCO free-running frequency Adjustment | 0%-100%
        break;
      //case 0x12:
        //  bit4-7 // Intelligent Monitor mode selector
      case 0x13: //continuous 0x12
        // bit5 // Horizontal AFC Gain switch | 0: Low | 1: High
        // bit6 // Vertical Sync. Det mode (1 Window/2 Window) | 0: 2 Window/Vsyncdet=9μs | 1: 1Window/Vsyncdet=11μs
        // bit7 // Horizontal Forced free-running mode switch | 0: OFF |1: Forced Free-running
        break;
      case 0x14: // continuouts 0x74
        // bit7 // Pin6 FBP slice level switch | 0:Vth=2V(narrow) | 1:Vth=1V(wide)
        // bit3 // Sync Det Slice Level (50%/30%)|  0: 50% | 1: 30%
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

// the setup routine runs once when you press reset,
// also when TV is plugged into AC power
void setup()
{
  Serial.begin(115200);
  I2C_jungl.onReceive(readFromJungle);
  I2C_micom.onReceive(readFromMicom); // master is writing to us
  I2C_micom.onRequest(writeToMicom); // master is reading from us

  noInterrupts();
  // Serial.print("  \xb7 init control-chip i2c ");
  while (!I2C_jungl.begin(SDA_1, SCL_1, I2C_FREQ)) {
    delay(50);
  }
  Serial.println("\n [ OK ] ");

  while (!I2C_micom.begin(MITSU_I2C_ADDRESS, SDA_2, SCL_2, I2C_FREQ)) {
    delay(50);
  }
  pinMode(RGB_SWITCH, INPUT);
  Serial.println("\n [ OK ]\n setup() complete");
  interrupts();
}

void loop()
{
  int blanking_level = analogRead(RGB_SWITCH);
  if (!rgb_switched && blanking_level > 2000) {
    rgb_switched = true;
    Serial.print("\t\tRGB switched ON -- level ");
    Serial.println(blanking_level);
  } else if (rgb_switched && blanking_level < 2000) {
    rgb_switched = false;
    Serial.print("\t\tRGB switched OFF -- level ");
    Serial.println(blanking_level);
  }
  delay(50);
}