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
    bool verbose = false;
    // NOTE these register maps are model specific!
    // TODO: make this a map struct/class so we can work with a diff model
    switch (reg)
    {
      case REG_OSD_RGB_MODE: // is continously set on interval by Orion TV to 0xa0 => 0b10100000
        // bit7 // AFC2 H Phase (MSB bit)
        // bit6 // Not Assigned
        // bit5 // Fsc free | Free-running mode of crystal oscillator | 0: OFF | 1: Free-running
        // bit4 // Analog OSD 1=>ON 0=>OFF
        // bit3 // Force Monochrome 1=>ON 0=>OFF
        // bit2 // Force Color 1=>ON 0=>OFF
        // bit0 // Color Killer Sensitivity Threshold Switch | 0: 43dB | 1: 45dB

        bitSet(val, 4); // ensure "Analog OSD"
        // I put diodes inline with the OSD RGBS to protect the chip so Analog-mode needs to stay on
        // the diodes' added voltage-drop prevents Digital High from being read at all
        // so its not even theroretically usable when RGB switch is OFF
        break;
/*
      case 0x2: // is continously set on interval by Orion TV to 0x8 => '0b00001000'
        //  bit7 // Luminance signal Mute ON/OFF switch | 0: Out | 1: Mute
        //  bit6 // AF Direct out/External Audio input signal switch | 0: AF amp out | 1: External
        //  bit5 // Forced Spot Killer under Power on condition | 0: Forced S.Killer | 1: Off
        //  bit4 // Y-signal Chroma Trap ON/OFF switch | 0: Chroma Trap ON | 1: Chroma Trap Off
        //  bit3 // Video Tone Gain (Hi/Normal) switch | 0: normal | 1: high(sharp) [orion default]
        //  bit2 // ABCL | 0 => OFF (ACL) | 1 => ON
        //  bit1 // Luminance Signal Delay time Fine pitch Adjustment
        //  bit0 // Chroma BPF/Take-Off Function Switch | 0: BPF | 1: Take-Off
        if (rgb_switched) {
          // if sync input is CVBS(Comp-video), I see "double-exposed" color
          // (excess luminance)
          // FIXME: mute that color-video processing!
          //  i don't think its possible with the jungle settings..
          //   might just have to install a sync cleaner and switch it on,
          //   then switch to EXT input which is wired to the cleaner's output
          //  or maybe i just need to increase my blanking voltage?
          bitSet(val, 7); // maybe ignore luminance of Y-in?
          bitSet(val, 4); // maybe don't process colors of Y-in?
            //picture still works, but still over-luminated
          bitClear(val, 3); // no diff...
          bitSet(val, 2); // no diff....
        }
        verbose = true;
        break;
*/
      case 0x06: // is continously set on interval by Orion TV to either
      // 0xa0 => '0b10100000'
      // 0xb0 => '0b10110000' (after v-latchings?)
        // bit5:7 // VIF Video Out Gain
        // bit4 // AV Switch Selector |  V-Latch | 0: Composite video input | 1: Y/C input mode
        // bit3 // Black Stretch function ON/OFF switch | 0: ON | 1: OFF
        // bit2 // AV Switch Selector |  V-Latch | 0: TV mode | 1: EXT mode
        // bit0:1 // Luminance Signal Delay time Adjustment
        if (rgb_switched) {
          // bitSet(val, 4); // input be CSync, ignore CVBS
             // no change for RGB+CVBSync, still overluminated
             // RGBS doesn't seem to need it changed
        } else {
          bitClear(val, 4); // be CVBS, else its luminance is shown as a monochrome picture
        }
        break;
      //case 0x0:
        // bit7 // Inter Carrier/Split Carrier Switch 0: Inter Carrier, 1: Split Carrier
        // bit0:6 //RF AGC Delay Point Adjustment by 7bit DAC
      //case 0x1:
        // bit7 N/A
        // bit6 VIF Frequency Selector 0: 45.75MHz, 1: 58.75MHz
        // bit0:5 // VIF VCO Free-running Frequency Adjustment by 5bit DAC
      //case 0x3: // continous 0x0
        // bit7  // AF Direct out ON/OFF(Mute) switch | 0: Sound ON (Non Mute) | 1: Mute
        // bit0:5 // Audio Out Level Attenuation by 7bit DAC MAX gain=0dB
      //case 0x4: // set continuously to 0xa8 => 0b10101000
        // bit7 // ABCL Gain | 0 => Lo | 1 => Hi
        // bit6 // AFT OUT ON/OFF(Defeat) switch | 0: AFT ON (Non Defeat) | 1: Defeat
        // bit0:5 // Video Tone - Delay line type Aperture Control
      // case 0x5:
        // bit7 // Contrast Control Clip Switch when OSD mode | 0: Clip ON | 1: Clip OFF
        // bit0:6 //Contrast Control by 7bit DAC
      //case 0x7:
        // bit7 // VIF AGC Gain Normal/Minimum switch |  0: AGC Function | 1: Defeat(Minimum Gain)
        // bit0:6 // Tint Control by 7bit DAC
      //case 0x08:
        // bit7 // Blue Back mode switch | 1=>ON | 0=>OFF
        // bit0:6 // Color Saturation Control by 7bit DAC
      //case 0x9: // continuous 0x59 => 0b01011001
        // bit4:7 // AFC2 H Phase
        // bit1:3 // Not Assigned
        // bit0 // Horizontal AFC2 Gain switch | 0: High | 1: Low
      //case 0x0a:
        // bit0:7 // Brightness Control by 8bit DAC
      //case 0x0b:
        // bit7 // Vertical Forced free-running mode switch | 0: OFF | 1: Forced Free-running
        // bit0:6 // R OUT Amplitude Adjustment by 7bit DAC
      //case 0x10: // continously written - HVCO varies with calibration -'0b00XXX100'
        // bit7 // White Raster Mode Switch | 1=>ON 0=>OFF
        // bit6 // Sync Det Slice Level (50%/40%) | 0: 50% | 1: 40%
        // bit3-5 // H VCO free-running frequency Adjustment | 0%-100% | (Orion Service Menu #04)
        // bit0:2 // Not Assigned
      //case 0x11:
        // bit0-5 // V-Size V RAMP Amplitude Adjustment by 6bit DAC.
      //case 0x12: // continously 0xb0 => 0b10110000
        // bit4-7 // Intelligent Monitor mode selector
        // bit2-3 // Luminance Gamma Threshold Control | 0:Gamma OFF
        // bit0-2 // TRAP Fine Adj
      case 0x13: // continuously set 0x12 => '0b00010010' (at boot, Orion makes 1 request to 0x11 => '0b00010001')
        // bit7 // Horizontal Forced free-running mode switch | 0: OFF | 1: Forced Free-running
        // bit6 // Vertical Sync. Det mode (1 Window/2 Window) | 0: 2 Window/Vsyncdet=9μs | 1: 1Window/Vsyncdet=11μs
        // bit5 // Horizontal AFC Gain switch | 0: Low | 1: High << is twitching in svc menu
        // bit4 // Horizontal output switch | 0: H-deflection Off | 1: H-deflection On
        // bit3 // Service Switch | 0: V-Out ON, Contrast-Control Normal | 1: V-Out off, Contrast Control Minimum
                // triggered by Orion Service Menu #01 to create a single-horizontal line raster for yoke calibration
        // bit0:2 // V-Shift V RAMP Start timing Adjustment 2Line/Step

        // Orion Service menu flips AFC-Gain => ON randomly, shifting the picture horizontally
        // (OEM bug? or because I've taken over RF input for CVBS/sync input but tuner isn't really tuned into anything)
        // AFC-Gain doesn't appear to be needed for RGBS input to work...& maybe it adds lag?
        // but CVBS really seems to need it (picture smears at the top without)
        // so lets just force it on always to keep the geometry consistent across both modes
        bitClear(val, 5);

        if (rgb_switched) {
          // lets keep V-deflection always on too...
          //  have seen this get flicked randomly during non-svc-menu use (dirty bus line??)
          bitClear(val, 3);
        }
        break;
      //case 0x14: // continuous 0x74 => 0b01110100
        // bit7 // Pin6 FBP slice level switch | 0:Vth=2V(narrow) | 1:Vth=1V(wide)
        // bit6 // Y SW OUT frequency switch | 0: FLAT | 1: LPF(fc=700KHz)
        // bit4:5 // Charge Time Constant Adjustment for Black Stretch
        // bit3 // Sync Det Slice Level (50%/30%)|  0: 50% | 1: 30%
        // bit0:2 // FM Station Level
      default:
        // passthru
        break;
    }

    if (!writeToJungle(reg, val, verbose)) {
      // lets do _one_ retry
      delay(50);
      writeToJungle(reg, val, verbose);
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

void poll_rgb_switch() {
  // FIXME: need to debounce this
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
}

// the setup routine runs once when you press reset,
// also when TV is plugged into AC power
void setup()
{
  Serial.begin(115200);
  pinMode(RGB_SWITCH, INPUT);
  poll_rgb_switch();
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
  Serial.println("\n [ OK ]\n setup() complete");
  interrupts();
}

void loop()
{
  poll_rgb_switch();
  delay(50);
}