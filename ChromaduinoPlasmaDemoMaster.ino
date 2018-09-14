// Example of using I2C to drive a Chromaduino/Funduino LED matrix slave
// Plasma Morphing Routine
// v8 David Chao (18-09-12)

#include <Wire.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif

//#define DEBUG

//********** CONFIGURATION BEGINS

// Arduino: Use default SCL and SDA, nothing to do here
// SCL_PIN = A5, SDA_PIN = A4

#ifdef ESP8266
#define SCL_PIN D4
#define SDA_PIN D3
#define RST_PIN D2             // connected to DTR on the Colorduino
#else // Arduino
#define RST_PIN A0             // connected to DTR on the Colorduino
#endif

#define LED_MATRIX_COUNT (1)  // number of LED matrices connected
//#define DISPLAY_ROTATED       // if defined, rotates the display 180deg

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

unsigned long paletteShift=128000;  // initial seed
unsigned char plasmaValue=127;      // brightness
unsigned long frameTimeout = 160UL; //ms between each frame
unsigned long frameTimestamp;

//********** CONFIGURATION ENDS


//********** MATRIX MANIPULATION CODE BEGINS

// Arrays representing the LEDs we are displaying.
// Simple on/off here, but could be palette indices
byte Display[LED_MATRIX_COUNT][8][8];

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
  
  for (int row = 0; row < 8; row++)
    for (int col = 0; col < 8; col++)
    {
      int hue;
      unsigned char pRGB[3];
      
#ifdef DISPLAY_ROTATED    
      hue = Display[matrix][7-row][7-col];
#else      
      hue = Display[matrix][row][col];
#endif
      HSVtoRGB( pRGB, hue );
      WriteData( matrix, pRGB[0], pRGB[1], pRGB[2] );
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

//Converts an HSV color to RGB color
void HSVtoRGB(void *pChannel, unsigned char hue) 
{
  float r, g, b, h, s, v; //this function works with floats between 0 and 1
  float f, p, q, t;
  int i;
  unsigned char *pRGB = (unsigned char *)pChannel;

  h = (float)(hue / 256.0);
  s = (float)(255 / 256.0);
  v = (float)(plasmaValue / 256.0);

  //if saturation is 0, the color is a shade of grey
  if(s == 0.0) {
    b = v;
    g = b;
    r = g;
  }
  //if saturation > 0, more complex calculations are needed
  else
  {
    h *= 6.0; //to bring hue to a number between 0 and 6, better for the calculations
    i = (int)(floor(h)); //e.g. 2.7 becomes 2 and 3.01 becomes 3 or 4.9999 becomes 4
    f = h - i;//the fractional part of h

    p = (float)(v * (1.0 - s));
    q = (float)(v * (1.0 - (s * f)));
    t = (float)(v * (1.0 - (s * (1.0 - f))));

    switch(i)
    {
      case 0: r=v; g=t; b=p; break;
      case 1: r=q; g=v; b=p; break;
      case 2: r=p; g=v; b=t; break;
      case 3: r=p; g=q; b=v; break;
      case 4: r=t; g=p; b=v; break;
      case 5: r=v; g=p; b=q; break;
      default: r = g = b = 0; break;
    }
  }
  *(pRGB++) = (int)(r * 255.0);
  *(pRGB++) = (int)(g * 255.0);
  *(pRGB++) = (int)(b * 255.0);
}

float dist(float a, float b, float c, float d) 
{
  return sqrt((c-a)*(c-a)+(d-b)*(d-b));
}

//********** ARDUINO MAIN CODE BEGINS

void setup()
{
#ifdef DEBUG
  Serial.begin(115200);
#endif

#ifdef ESP8266
  WiFi.mode( WIFI_OFF );      // Turn off WiFi, we don't need it yet
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

void loop()
{
  unsigned long now = millis();
  if (now - frameTimestamp >= frameTimeout)
  {
    // kick plasma morphing animation
    frameTimestamp = now;

    for (int row = 0; row < 8; row++) 
      for (int col = 0; col < (8*LED_MATRIX_COUNT); col++)
      {
        float value = sin(dist(col + paletteShift, row, 128.0, 128.0) / 8.0) 
                    + sin(dist(col, row, 64.0, 64.0) / 8.0) 
                    + sin(dist(col, row + paletteShift / 7, 192.0, 64) / 7.0)
                    + sin(dist(col, row, 192.0, 100.0) / 8.0);
        //HSVtoRGB( pChannel, (unsigned char)(value * 128)&0xff );
        Display[col/8][row][col%8] = (byte)(value * 128) & 0xff;
      }
    paletteShift++;

    // update the LEDs
    for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
    {
      SendDisplay(matrix);
    }

    // flip to displaying the new pattern
    for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
    {
      ShowBuffer(matrix);
    }
  }
}

