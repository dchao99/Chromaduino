/* 
 * Example of using I2C to drive a Chromaduino/Funduino LED matrix slave.
 * Plasma Morphing Demo
 * Lode's Computer Graphics Tutorial: https://lodev.org/cgtutor/plasma.html
 * v8 David Chao (18-09-12)
 */
 
#include <Wire.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#define LIB8STATIC __attribute__ ((unused)) static inline
#include "trig8.h"                // Copied from FastLED library 
#include "gamma8.h"

//#define DEBUG

//********** CONFIGURATION BEGINS

// Arduino: Use default SCL and SDA, nothing to do here
// SCL_PIN = A5, SDA_PIN = A4

#if defined(ESP8266)
#define SCL_PIN D4
#define SDA_PIN D3
#define RST_PIN D2             // connected to DTR on the Colorduino
#else // Arduino UNO
#define RST_PIN A0             // connected to DTR on the Colorduino
#endif

#define LED_MATRIX_COUNT (2)   // number of LED matrices connected
//#define DISPLAY_ROTATED      // if defined, rotates the display 180deg

// I2C address of the LED matrices.  
// We can use one of the LED matrix's as a master in the chain now  
// 0x00 = I'm a master-matrix  (position of the master is not important)
int matrixAddress[] = {0x70, 0x71, 0x72, 0x73, 0x74};

const int matrixBalances[][3] PROGMEM = 
            {{0x80, 0, 0},   // using 0x80 keeps the default data
             {0x80, 0, 0},
             {0x80, 0, 0},
             {0x80, 0, 0},
             {0x80, 0, 0}};

unsigned long timeShift=128000;     //initial seed
unsigned char brightness=224;       //brightness
unsigned long frameTimeout=100UL;   //ms between each frame
unsigned long frameTimestamp;

// To show off our hardware PWM LED driver (capable of displaying 16M colors) 
// increase the scaling of the plasma to see more details of the color transition.
// When using fast sin16(uint_16), need to convert from radians to uint_16 
// (1 rad = 57.2958 degree).  Default sin() uses radians
const unsigned int PlasmaScaling = 1043;  // = 57.2958 * 65536 / 360 / (10) <- scaler

//********** CONFIGURATION ENDS


//********** MATRIX MANIPULATION CODE BEGINS

// Arrays representing the LEDs we are displaying.
// Simple on/off here, but could be palette indices
int Display[LED_MATRIX_COUNT][8][8];

// The Arduino wire library uses a 32 byte receive buffer, we must not send more 
// than 32 bytes of data in one packet.  
byte dataBuffer[32];

int GetMatrixIndex(int matrix)
{
  // returns the index if the given logical LED matrix
#ifdef DISPLAY_ROTATED    
  return matrix;
#else      
  return LED_MATRIX_COUNT - matrix - 1;
#endif      
}

#define GetMatrixAddress(_m) matrixAddress[GetMatrixIndex(_m)]

void StartBuffer(int matrix)
{
  // start writing to WRITE
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x00);
  Wire.endTransmission();
  delay(1);
}

void StartFastCmd(int matrix)
{
  // start writing to FAST
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x11);
  Wire.endTransmission();
  delay(1);
}

void WriteData(int matrix, byte R, byte G, byte B)
{
  // write a triple
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write(R);
  Wire.write(G);
  Wire.write(B);
  Wire.endTransmission();
  delay(1);
}

void WriteBuffer(int matrix, byte *pBuf, int count)
{
  // must be a multiple of triples
  count = (count / 3) * 3;
  if(count >= 3)
  {
    if (count > 30) count = 30;
    Wire.beginTransmission(GetMatrixAddress(matrix));
    for (int i=0; i<count; i++) {
      Wire.write( *pBuf++ );
    }
    Wire.endTransmission();
    delay(1);
  }
}

bool SetBalance(int matrix)
{
  // true if there are 3 bytes in the slave's buffer
  Wire.requestFrom(GetMatrixAddress(matrix), 1);
  byte count = 0;
  if (Wire.available())
    count = Wire.read();
    
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x02); // set the 3 bytes to be balance
  Wire.endTransmission();
  delay(1);
  return count == 3;  // the slave got 3 bytes
}

void ShowBuffer(int matrix)
{
  // flip the buffers
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x01);
  Wire.endTransmission();
  delay(1);
}

void SendDisplay(int matrix)
{
#ifdef DEBUG
    Serial.println("SendDisplay ("+String(matrix)+")");
#endif
  // sends the Display data to the given slave LED matrix
  StartBuffer(matrix);
  
  byte *pRGB = dataBuffer;
  for (int row = 0; row < 8; row++)
  {
    for (int col = 0; col < 8; col++)
    {      
#ifdef DISPLAY_ROTATED    
      int hue = Display[matrix][7-row][7-col];
#else      
      int hue = Display[matrix][row][col];
#endif
      HSVtoRGB( pRGB, hue, 255, brightness);
      pRGB += 3;
    }
    pRGB = dataBuffer;
    WriteBuffer( matrix, pRGB, 24 );
  }
}

bool Configure(int matrix)
{
  int idx = GetMatrixIndex(matrix);
  byte r = pgm_read_byte(matrixBalances[idx]);
  byte g = pgm_read_byte(matrixBalances[idx]+1);
  byte b = pgm_read_byte(matrixBalances[idx]+2);

#ifdef DEBUG
    Serial.println("Config ("+String(matrix)+")");
#endif
  // write the balances to slave, true if got expected response from matrix
  StartBuffer(matrix);
  WriteData(matrix, r, g, b);
  return SetBalance(matrix);
}

//********** COLOR TOOLS CODE BEGINS

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
    default: r =   0     ; g =   0     ; b =   0     ; break; // black / dead space
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

//********** ARDUINO MAIN CODE BEGINS

void setup()
{
#ifdef DEBUG
  Serial.begin(115200);
#endif

#if defined(ESP8266)
  WiFi.mode( WIFI_OFF );      //Turn off WiFi, we don't need it yet
  WiFi.forceSleepBegin();
  Wire.begin(SDA_PIN, SCL_PIN);
#else // Arduino
  Wire.begin();
#endif
  
  // reset the board(s)
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(1);
  digitalWrite(RST_PIN, HIGH);

  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
  {
    // keep trying to set the balance until it's awake
    do {
      delay(100);
    } while (!Configure(matrix));
  }
  frameTimestamp = millis();
}

/* 
 * Original equation: https://lodev.org/cgtutor/plasma.html
 * const float PlasmaScaling = 10.0; 
 * float value = sin((col+timeShift) / PlasmaScaling) 
 *             + sin(dist(col, row, 64.0, 64.0) / PlasmaScaling) 
 *             + sin((row+timeShift/7.0) / PlasmaScaling)
 *             + sin(dist(col, row, 192.0, 100.0) / PlasmaScaling);
 * //map to -3072 to +3072 then onto 4 palette loops (0 to 1536) x 4
 * int hue = (int)(value * 128 * 6); 
 */
void loop()
{
  unsigned long now = millis();
  if (now - frameTimestamp >= frameTimeout)
  {
#ifdef DEBUG
    Serial.println("Loop Entry = "+String(now));
#endif
    // kick plasma morphing animation
    frameTimestamp = now;

    for (int row = 0; row < 8; row++) 
    {
      for (int col = 0; col < (8*LED_MATRIX_COUNT); col++)
      {
        long value = (long)sin16((col+timeShift) * PlasmaScaling) 
                   + (long)sin16((unsigned int)(dist(col, row, 64, 64) * PlasmaScaling)) 
                   + (long)sin16((row*PlasmaScaling + timeShift*PlasmaScaling/7) )
                   + (long)sin16((unsigned int)(dist(col, row, 192, 100) * PlasmaScaling));
        //map to -3072 to +3072 then onto 4 palette loops (0 to 1536) x 4
        int hue = (int)(( value*3 ) >> 7);
#ifdef DEBUG
//      Serial.print(String(hue)+",");
#endif
        Display[col/8][row][col%8] =  hue;
      }
#ifdef DEBUG
//    Serial.println();
#endif
    }
    timeShift++;

#ifdef DEBUG
    Serial.println("Before Send = "+String(millis()));
#endif
    // update the LEDs
    for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
    {
      SendDisplay(matrix);
    }

#ifdef DEBUG
    Serial.println("After Send = "+String(millis()));
#endif
    // flip to displaying the new pattern
    for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
    {
      ShowBuffer(matrix);
    }
  }
}
