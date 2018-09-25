/* 
 * Example of using I2C to drive a Chromaduino/Funduino LED matrix slave.
 * Plasma Morphing Demo
 * Lode's Computer Graphics Tutorial: https://lodev.org/cgtutor/plasma.html
 * v8 David Chao (18-09-12)
 */
 
#include <Wire.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#endif //ESP8266

#define LED_MATRIX_COUNT   (2)  // number of LED matrices connected
#define DISPLAY_ROTATED         // rotates display 180deg from PCB silkscreen orientation
#include "comm.h"

#define LIB8STATIC __attribute__ ((unused)) static inline
#include "trig8.h"                // Copied from FastLED library 
#include "hsv2rgb.h"

#define DEBUG

//********** CONFIGURATION BEGINS

// Arduino: Use default SCL and SDA, nothing to do here
// SCL_PIN = A5, SDA_PIN = A4

#if defined(ESP8266)
#define SCL_PIN D4
#define SDA_PIN D3
#define RST_PIN D2             // connected to DTR on the Colorduino
#else  //Arduino UNO
#define RST_PIN A0             // connected to DTR on the Colorduino
#endif //ESP8266

#define ColorduinoScreenWidth  (8)
#define ColorduinoScreenHeight (8)

const byte matrixBalances[][3] PROGMEM = 
           { {35, 55, 63},   // using 0x80 keeps the default data
             {35, 55, 63},
             {0x80, 0, 0} };

unsigned long timeShift=128000;     //initial seed
unsigned char brightness=192;       //brightness
unsigned long frameTimeout=100UL;   //ms between each frame
unsigned long frameTimestamp;

// To show off our hardware PWM LED driver (capable of displaying 16M colors) 
// increase the scaling of the plasma to see more details of the color transition.
// When using fast sin16(uint_16), need to convert from radians to uint_16 
// (1 rad = 57.2958 degree).  Default sin() uses radians
const unsigned int PlasmaScaling = 1043;  // = 57.2958 * 65536 / 360 / (10) <- scaler

// Arrays representing the LEDs we are displaying.
// this palette is -3072 to +3072, an integer type
int Display[LED_MATRIX_COUNT][ColorduinoScreenHeight][ColorduinoScreenWidth];

//********** CONFIGURATION ENDS

//********** MATRIX CONFIG AND DISPLAY CODE BEGINS

bool Configure(int matrix)
{
  int idx = GetMatrixIndex(matrix);
  byte balanceRGB[4];
  balanceRGB[0] = pgm_read_byte(matrixBalances[idx]);
  balanceRGB[1] = pgm_read_byte(matrixBalances[idx]+1);
  balanceRGB[2] = pgm_read_byte(matrixBalances[idx]+2);
  balanceRGB[3] = 0;
  #ifdef DEBUG
  char ch[8];
  sprintf(ch, "%06x", *(unsigned long *)balanceRGB);
  Serial.println("Config ("+String(matrix)+") RGB="+ch);
  #endif
  // write the balances to slave, true if got expected response from matrix
  StartBuffer(matrix);
  WriteData(matrix, balanceRGB);
  return SetBalance(matrix);
}

void SendDisplay(int matrix)
{
  #ifdef DEBUG
  Serial.println("SendDisplay ("+String(matrix)+")");
  #endif
  // sends the Display data to the given slave LED matrix
  StartBuffer(matrix);

  // The Arduino wire library uses a 32 byte receive buffer, to prevent overflow,
  // we must not send more than 32 bytes of data in one packet.  
  byte dataBuffer[32];
  byte *pRGB = dataBuffer;
  for (int row = 0; row < ColorduinoScreenHeight; row++)
  {
    for (int col = 0; col < ColorduinoScreenWidth; col++)
    {      
      #ifdef DISPLAY_ROTATED    
      int hue = Display[matrix][row][col];
      #else      
      int hue = Display[matrix][ColorduinoScreenHeight-1-row][ColorduinoScreenWidth-1-col];
      #endif
      HSVtoRGB( pRGB, hue, 255, brightness);
      pRGB += 3;
    }
    pRGB = dataBuffer;
    WriteBlock( matrix, pRGB, 24 );
  }
}

//********** ARDUINO MAIN CODE BEGINS

// Original equation from: https://lodev.org/cgtutor/plasma.html
// const float PlasmaScaling = 10.0; 
// float value = sin((col+time) / scaling) + sin(dist(col, row, 64.0, 64.0) / scaling) 
//             + sin((row+time/7.0) / scaling) + sin(dist(col, row, 192.0, 100.0) / scaling);
// int hue = (int)(value * 128 * 6); 

void plasma_morph(unsigned long time)
{
  for (int row = 0; row < 8; row++) 
  {
    for (int col = 0; col < 8*LED_MATRIX_COUNT; col++)
    {
      long value = (long)sin16((col+time) * PlasmaScaling) 
                 + (long)sin16((unsigned int)(dist(col, row, 64, 64) * PlasmaScaling)) 
                 + (long)sin16((row*PlasmaScaling + time*PlasmaScaling/7) )
                 + (long)sin16((unsigned int)(dist(col, row, 192, 100) * PlasmaScaling));
      //map to -3072 to +3072 then onto 4 palette loops (0 to 1536) x 4
      int hue = (int)(( value*3 ) >> 7);
      #ifdef DEBUG
      //Serial.print(String(hue)+",");
      #endif
      Display[col/8][row][col%8] =  hue;
    }
    #ifdef DEBUG
    //Serial.println();
    #endif
  } 
}

float dist(int a, int b, int c, int d) 
{
  return sqrt((c-a)*(c-a)+(d-b)*(d-b));
}

void setup()
{
  #ifdef DEBUG
  Serial.begin(115200);
  #endif

  #if defined(ESP8266)
  WiFi.mode( WIFI_OFF );      //Turn off WiFi, we don't need it yet
  WiFi.forceSleepBegin();
  Wire.begin(SDA_PIN, SCL_PIN);
  #endif //ESP8266

  Wire.begin();
  
  // reset the board(s)
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(1);
  digitalWrite(RST_PIN, HIGH);

  // keep trying to set the WhiteBal until all slaves are awake
  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)   
    do {
      delay(100);
    } while (!Configure(matrix));
    
  frameTimestamp = millis();
}

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

    plasma_morph(timeShift++);

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
