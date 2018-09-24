// Chromaduino
// A driver for a Funduino/Colorduino (slave) board: 
// * an 8x8 RGB LED matrix common anode
// * an ATMEGA328P connected to
//   * a DM163 which drives 24 channels of PWM data connected to 8x Red, Green and Blue LED columns.  
//       ATMEGA328P pins PD6 & 7 are SCL & SDA to DM163 pins DCK & SIN. DM163 pins IOUT0-23 are connected to LED RGB row pins
//   * an M54564FP which drives the LED rows.
//       ATMEGA328P pins PB0-4 & PD3,4 connect to M54564FP pins IN1-8. M54564FP pins OUT1-8 connect to LED pins VCC0-7
//   * an EXTERNAL master Arduino device driving the LED colours via I2C/Wire (address 0x70)
// This code is based on the "Colorduino" library but simplified/clarified and with the addition of a simple I2C master/slave protocol.
//
// Commands are a 1-byte transmission from the Master to the Chromaduino (Slave, address 0x70).  Data is a 3-byte transmission. 
// The Chromaduino has two RGB channel buffers of 3x8x8 bytes.  One buffer (READ) is being read from to drive the LED display. 
// The other buffer (WRITE) is being written to by the Master.
// There is a third buffer, FAST, which is 12 bytes long (see command 0x11)
//
// Commands:
// 0x00 sets the write pointer to the start of the WRITE buffer.
// 0x01 swaps the WRITE and READ buffers
// 0x02 takes the low 6 bits of the first 3 bytes in the WRITE buffer as GLOBAL scalars of the R, G and B values ("colour balance").  
//       The default is 35, 55, 63.  This downplays the intensity of Red/Green to achieve a reasonable "White"
//       If the first byte is 0x80 the command is ignored
// 0x03 takes the 1st byte in the WRITE buffer as the TCNT2 value, provided the 2nd and 3rd bytes are 16 (clock freq in MHz) and 128 (clock divisor)
//       TCNT2 drives the timer which updates the LED rows. The default is 99 for a frequency of 800Hz.  
//       For a desired FREQ in Hz, TCNT2 = 255 - CLOCK/FREQ where CLOCK=16000000/128. 
// 
// 0x10 clears the WRITE buffer to all 0's
// 0x11 sets the write pointer to the start of FAST
// Data:
//   Triplets of bytes are written sequentially to the WRITE buffer as R, G & B (bytes beyond the buffer are ignored)
//   EXCEPT that after an 0x11 command, triplets of bytes are written sequentially to the FAST buffer. When the 4th triplet is received the buffer is interpreted as
//     R,G,B,  flags,row0,row1,  row2,row3,row4,  row5,row6,row7
//     And the WRITE buffer is updated with the RGB value where a bit is set in the row data.  
//     If bit0 of flags is set, the WRITE and READ buffers are then swapped at the end.
//     If bit1 of flags is set, BLACK is written to the WRITE buffer where a bit is unset in the row data (otherwise it is untouched)
//   Other transmissions are ignored
// Request:
//   Requesting a single byte returns the number of bytes written to the WRITE buffer
// Demo:
//   If #define'd, a demo will run after 5s if no wire data is received from the Master
//
// Programming:
// Program the board by (for example) popping the ATmega chip off a Duemilanove and connecting 
//    GND-GND, 5V-VCC, RX(D0)-RXD, TX(D1)-TXD and RESET-DTR
// and programming the Duemilanove as normal.  This will program the ATmega on the Funduino/Colorduino board.

// v6 Mark Wilson 2016: original
// v7 Mark Wilson 2018: changed I2C address (was 0x05, non-standard?); added 0x10 & 0x11 commands; ignore white balance if R=0x80
// v8 David Chao 18-09-11: 
//    Use lincomatic's Colorduino Library https://github.com/dchao99/Colorduino
//    Add a nice plasma morph routine into demo library https://lodev.org/cgtutor/plasma.html

#include <Wire.h>
#include <Colorduino.h>

#define DEMO                      // enable demo
//#define DEBUG                   // enable debug output to console

#ifdef DEMO
#define LIB8STATIC __attribute__ ((unused)) static inline
#include "trig8.h"                // Copied from FastLED library 
#include "gamma8.h"
#endif

#define WIRE_DEVICE_ADDRESS 0x70  // I2C address

#define MatrixChannels     3
#define MatrixLeds         ColorduinoScreenHeight*ColorduinoScreenWidth
#define MatrixRowChannels  ColorduinoScreenWidth*MatrixChannels

byte defaultCorrection[3] = {35, 55, 63};

bool processBalance = false;
bool balanceRecieved = false;  // true when we've rx'd a white balance command

byte* wire_DataPtr = NULL;  // the write pointer
int   wire_DataCount = 0;   // bytes received

bool processClear = false;

byte  fastCommandBuffer[12];
byte* fastCommandPtr = NULL;
bool  processFast = false;

#ifdef DEMO
unsigned long demoTimeout = 1000UL; //1s should be long enough to wait the WhiteBal command
                                    //Note: make sure WhiteBal command is not received in the middle of demo
unsigned long demoTimestamp;
// rolling palette:
unsigned long demoR = 0xFFFF00FFUL;
unsigned long demoG = 0xFF00FFFFUL;
unsigned long demoB = 0xFFFFFF00UL;
unsigned long demoStep = 0;
// plasma morph:
unsigned long timeShift = 128000;   // initial seed
unsigned char brightness = 192;     // brightness
// To show off our hardware PWM LED driver (capable of displaying 16M colors) 
// increase the scaling of the plasma to see more details of the color transition.
// When using fast sin16(uint_16), need to convert from radians to uint_16 
// (1 rad = 57.2958 degree).  Default sin() uses radians
const unsigned int PlasmaScaling = 1043;  // = 57.2958 * 65536 / 360 / (10) <- scaler
#endif

void wire_Request(void)
{
  // ISR to process request for bytes over I2C from external master
  byte response = wire_DataCount;
  // write MUST be a byte buffer
  Wire.write(&response, 1);
}

void wire_Receive(int numBytes) 
{
  // ISR to process receipt of bytes over I2C from external master
  // Get out quickly!
  
  if (numBytes == 1) // a command
  {
    byte command = Wire.read();
    if (command == 0x00) // start frame
    {
      wire_DataPtr = (byte *)Colorduino.GetPixel(0,0);
      wire_DataCount = 0;
    }
    else if (command == 0x01) // show frame
    {
      Colorduino.FlipPage();
    }
    else if (command == 0x02) // set colour balance
    {
      processBalance = true;
    }
    else if (command == 0x03) // set TCNT2
    {
      byte *ptr = (byte *)Colorduino.GetPixel(0,0);
      if (ptr[1] == 16 && ptr[2] == 128)
         Colorduino.SetTimerCounter(ptr[0]);
    }
    else if (command == 0x10) // Clear to black
    {
      processClear = true;
    }
    else if (command == 0x11) // fast fill from bitmasks
    {
      fastCommandPtr = fastCommandBuffer;
      wire_DataPtr = NULL;
      wire_DataCount = 0;
    }
    return;
  }
  else if (wire_DataPtr && ((numBytes % MatrixChannels) == 0))  // read channel (RGB) data
  {
    while (Wire.available() && wire_DataCount < ColorduinoScreenHeight*MatrixRowChannels)
    {
      *(wire_DataPtr++) = Wire.read();
      wire_DataCount++;
    }
  }
  else if (fastCommandPtr && ((numBytes % MatrixChannels) == 0))  // read triplets for fast cmd
  {
    while (Wire.available() && wire_DataCount < sizeof(fastCommandBuffer))
    {
      *(fastCommandPtr++) = Wire.read();
      wire_DataCount++;
    }
  
    if (wire_DataCount == sizeof(fastCommandBuffer))
    {
      // process the cmd (outside the Wire handler)
      processFast = true;
    }
  }
  
  // discard extras
  while (Wire.available())
    Wire.read();
}

void doFastCommand()
{
  // r,g,b, flags,row0,row1, row2,row3,row4, row5,row6,row7
  int Idx = 0;
  fastCommandPtr = NULL;
  
  byte R = fastCommandBuffer[Idx++];
  byte G = fastCommandBuffer[Idx++];
  byte B = fastCommandBuffer[Idx++];
  
  byte F = fastCommandBuffer[Idx++];
  // F=flags, B000000xy, if x is set, use black as background (otherwise leave as-is), if y is set, show buffer at end
  bool flagSetBackground = F & B00000010;
  PixelRGB *ptr = Colorduino.GetPixel(0,0);
  for (int row = 0; row < ColorduinoScreenHeight; row++)
  {
    byte mask = fastCommandBuffer[Idx++];
    for (int col = 0; col < ColorduinoScreenWidth; col++)
    {
      if (mask & 0x01)  // set the colour
      {
         ptr->r = R;
         ptr->g = G;
         ptr->b = B;
      }
      else if (flagSetBackground) // write black
      {
         ptr->r = 0;
         ptr->g = 0;
         ptr->b = 0;
      }
      ptr++;
      mask >>= 1;
    }
  }
  
  if (F & B00000001)  // start displaying
  {
    Colorduino.FlipPage();
  }
}

void setup()
{
#ifdef DEBUG
  Serial.begin(115200);
#endif

  // initialize the led matrix controller
  Colorduino.Init();
  Colorduino.SetWhiteBal(defaultCorrection);
  
  Wire.begin(WIRE_DEVICE_ADDRESS);
  Wire.onRequest(wire_Request);
  Wire.onReceive(wire_Receive);
  
#ifdef DEMO
  demoTimestamp = millis();
#endif  
}

void loop()
{
  if (processBalance)
  {
    byte *newCorrection;
    newCorrection = (byte *)Colorduino.GetPixel(0,0);
    if (newCorrection[0] != 0x80)  // 0x80: skip, use default correction
      Colorduino.SetWhiteBal(newCorrection);
    processBalance = false;
    balanceRecieved = true;
  }
  else if (processFast)
  {
    doFastCommand();
    processFast = false;
  }
  else if (processClear)
  {
    Colorduino.ColorFill(0,0,0);
    processClear = false;
  }
  
#ifdef DEMO
  else if (!balanceRecieved)
  {
    unsigned long now = millis();
    if (now - demoTimestamp >= demoTimeout)
    {
      // kick demo animation
      demoTimestamp = now;
      PixelRGB* pChannel = Colorduino.GetPixel(0,0);
      
      if (demoStep < 20)  // first frames are solid/dimmed
      {
        byte demoCorrection[3];
        int dim = 4-(demoStep%5);
        for (int chan = MatrixChannels - 1; chan >= 0; chan--)
          demoCorrection[chan] = defaultCorrection[chan] >> dim;
        Colorduino.SetWhiteBal(demoCorrection);
      }
        
      for (unsigned int row = 0; row < ColorduinoScreenHeight; row++) 
      {
        for (unsigned int col = ColorduinoScreenWidth*(WIRE_DEVICE_ADDRESS&0x0f); 
             col < ColorduinoScreenWidth*((WIRE_DEVICE_ADDRESS&0x0F)+1); col++)
        {
          if (demoStep < 20)
          {
            int index = 8*(demoStep/5);
            pChannel->r = demoR >> index;
            pChannel->g = demoG >> index;
            pChannel->b = demoB >> index;
          }
          else
          {
            long value = (long)sin16((col+timeShift) * PlasmaScaling) 
                       + (long)sin16((unsigned int)(dist(col, row, 64, 64) * PlasmaScaling)) 
                       + (long)sin16((row*PlasmaScaling + timeShift*PlasmaScaling/7) )
                       + (long)sin16((unsigned int)(dist(col, row, 192, 100) * PlasmaScaling));
            //map to -3072 to +3072 then onto 4 palette loops (0 to 1536) x 4
            int hue = (int)(( value*3 ) >> 7);
#ifdef DEBUG
            Serial.print(String(hue)+",");
#endif
            HSVtoRGB( pChannel, hue, 255, brightness);
          }
          pChannel++;
        }
#ifdef DEBUG
        Serial.println();
#endif
      }
      Colorduino.FlipPage();

      if (demoStep < 20) {
        demoStep++;
        demoTimeout = 500UL;  // solid color pattern frame delay
      } else {
#ifdef DEBUG
        Serial.println("-----");
#endif      
        timeShift++;
        demoTimeout = 100UL;  // plasm morphing frame delay
      }

    }
  }
#endif
}

#ifdef DEMO
// Converts an HSV color to RGB color
// hue between 0 to +1536
void HSVtoRGB(void *pChannel, int hue, uint8_t sat, uint8_t val) 
{
  uint8_t  r, g, b;
  uint16_t s1, v1;
  unsigned char *pRGB = (unsigned char *)pChannel;

  hue %= 1536;              //between -1535 to +1535
  if (hue<0) hue += 1536;   //between 0 to +1535
    
  uint8_t lo = hue & 255;  // Low byte  = primary/secondary color mix
  switch(hue >> 8) {       // High byte = sextant of colorwheel
    case 0 : r = 255     ; g =  lo     ; b =   0     ; break; // R to Y
    case 1 : r = 255 - lo; g = 255     ; b =   0     ; break; // Y to G
    case 2 : r =   0     ; g = 255     ; b =  lo     ; break; // G to C
    case 3 : r =   0     ; g = 255 - lo; b = 255     ; break; // C to B
    case 4 : r =  lo     ; g =   0     ; b = 255     ; break; // B to M
    case 5 : r = 255     ; g =   0     ; b = 255 - lo; break; // M to R
    default: r =   0     ; g =   0     ; b =   0     ; break; // black
  }

  // Saturation: add 1 so range is 1 to 256, allowig a quick shift operation
  // on the result rather than a costly divide, while the type upgrade to int
  // avoids repeated type conversions in both directions.
  s1 = sat + 1;
  r  = 255 - (((255 - r) * s1) >> 8);
  g  = 255 - (((255 - g) * s1) >> 8);
  b  = 255 - (((255 - b) * s1) >> 8);

  // Value (brightness) & 16-bit color reduction: similar to above, add 1
  // to allow shifts, and upgrade to int makes other conversions implicit.
  v1 = val + 1;
  *(pRGB++) = pgm_read_byte(&gamma8[(r*v1)>>8]);
  *(pRGB++) = pgm_read_byte(&gamma8[(g*v1)>>8]);
  *(pRGB++) = pgm_read_byte(&gamma8[(b*v1)>>8]);
}

float dist(int a, int b, int c, int d) 
{
  return sqrt((c-a)*(c-a)+(d-b)*(d-b));
}
#endif
