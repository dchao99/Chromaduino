// Example of using I2C to drive a Chromoduino/Funduino LED matrix slave: scrolling text, 
// using faster command (0x11)
//
// Mark Wilson Jul '18
// v8 David Chao 18-09-11: 
//    Use lincomatic's Colorduino Library https://github.com/dchao99/Colorduino
//    Use Adafruit's classic 5x8font
//    Allow one of the matrix's in the chain to become a master 

//#define COLORDUINO       // running this code on a Colordurino with a LED matrix?

#include <Wire.h>
#include "Font5x8.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#endif
#ifdef COLORDUINO
#include <Colorduino.h>
#endif //COLORDUINO

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

#define LED_MATRIX_COUNT (1)  // number of LED matrices connected
//#define DISPLAY_ROTATED       // if defined, rotates the display 180deg

// I2C address of the LED matrices.  
// We can use one of the LED matrix's as a master in the chain now  
// 0x00 = I'm a master-matrix  (position of the master is not important)
#ifdef COLORDUINO
int matrixAddress[] = {0x00, 0x70, 0x71, 0x72, 0x73};
byte defaultBalances[3] = {25, 63, 63};
#else
int matrixAddress[] = {0x70, 0x71, 0x72, 0x73, 0x74};
#endif
const int matrixBalances[][3] PROGMEM = 
            {{0x80, 0, 0},   // using 0x80 keeps the default data
             {0x80, 0, 0},
             {0x80, 0, 0},
             {0x80, 0, 0},
             {0x80, 0, 0}};
             
unsigned long frameTimeout = 160UL; // (ms) time between each frame
byte fontColor[3] = {0,15,127};      // font color

//********** CONFIGURATION ENDS

//********** FONT-SPECIFIC CODE BEGINS

#define FontName  font5x8 // this font bitmap is not divided into pages, we can safely
                          // ignore the page_width property
#define FONT_COLS (6)     // including a gap
#define FONT_ROWS (8)

typedef struct fontProperties {
  unsigned char ch_width;
  unsigned char ch_height;
  unsigned char ch_start;
  unsigned char ch_total;
  int page_width;
} FontProperties;

FontProperties font;
unsigned char ch_cache = 0;  //= NUL
unsigned char font_cache[]= {0x00, 0x00, 0x00, 0x00, 0x00};

// Read font map into cache and returns the proportional width of the font
int font_CheckCache(unsigned char ch)
{
  static int prop_w;

  if ((ch < font.ch_start) || (ch > font.ch_start+font.ch_total-1))
    return 0;
  if ( ch != ch_cache )
  {
    int index =  ((ch - font.ch_start) * font.ch_width) + 6;      

    memcpy_P(font_cache, FontName+index, font.ch_width);
    ch_cache = ch;
    prop_w = FONT_COLS;

    // Format proportional fonts, remove left/right blank columns
    if (font_cache[0] == 0x00)
    {
      int i;
      for (i=0; i<font.ch_width-1; i++)
        font_cache[i] = font_cache[i+1];
      font_cache[i] = 0x00;
      prop_w--;
      if (font_cache[3] == 0x00)
        prop_w--;
    } else
    if (font_cache[4] == 0x00)
      prop_w--;
  }
  return prop_w;
}

// returns true if (row,col) cell is ON in font. (0,0) is bottom left
bool font_GetCell(unsigned char ch, int row, int col)
{  
  if ((ch < font.ch_start) || (ch > font.ch_start+font.ch_total-1))
    return false;
  return (col < font.ch_width) ? font_cache[col] & (0x01 << row) : false;
}

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

void DisplayChar(int matrix, char ch, int width, int row, int col)
{
  // copies the characters font definition into the Buffer at matrix, row, col  
  for (int r = 0; r < FONT_ROWS; r++)
    for (int c = 0; c < width; c++)
      if (0 <= (r+row) && (r+row) < 8 && 8*matrix <= (c+col) && (c+col) < 8*(matrix+1))
        Display[matrix][r+row][(c+col) % 8] = font_GetCell(ch, r, c)?1:0;
}

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
#ifdef COLORDUINO
  if ( GetMatrixAddress(matrix) == 0x00 )
  {
#ifdef DEBUG
    Serial.println("ShowBuffer (Local:"+String(matrix)+")");
#endif
    // master-matrix, just flip the local read/write buffer
    Colorduino.FlipPage();
    return;
  }
#endif //COLORDUINO

#ifdef DEBUG
    Serial.println("ShowBuffer (Remote:"+String(matrix)+")");
#endif
// flip the buffers
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x01);
  Wire.endTransmission();
  delay(1);
}

void SendDisplay(int matrix)
{
#ifdef COLORDUINO
  if ( GetMatrixAddress(matrix) == 0x00 )
  {
#ifdef DEBUG
    Serial.println("SendDisplay (Local:"+String(matrix)+")");
#endif
    // master-matrix, just bitmap to my own LED matrix
    PixelRGB* pChannel = Colorduino.GetPixel(0,0);
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++)
      {
#ifdef DISPLAY_ROTATED    
        if (Display[matrix][7-row][7-col])
#else      
        if (Display[matrix][row][col])
#endif 
        {     
          pChannel->r = fontColor[0];
          pChannel->g = fontColor[1];
          pChannel->b = fontColor[2];
        } else {
          pChannel->r = 0;
          pChannel->g = 0;
          pChannel->b = 0;
        }
        pChannel++;
      }
    return;
  }
#endif //COLORDUINO

#ifdef DEBUG
    Serial.println("SendDisplay (Remote:"+String(matrix)+")");
#endif
  // sends the Display data to the given slave LED matrix
  // uses a colour and bitmasks
  StartFastCmd(matrix);
  byte data[9];
  for (int row = 0; row < 8; row++)
  {
    data[row+1] = 0x00;
    for (int col = 0; col < 8; col++)
#ifdef DISPLAY_ROTATED    
      if (Display[matrix][7-row][7-col])
#else      
      if (Display[matrix][row][col])
#endif      
        data[row+1] |= 0x01 << col;
  }
  // format the data field for Cmd 0x11 (FAST Write)
  int idx = 0;
  data[0] = B00000010;  // write black background, DON'T display at end
  WriteData(matrix, fontColor[0], fontColor[1], fontColor[2]);  // blue foreground
  for (int i=0; i<3; i++) {
    WriteData(matrix, data[idx], data[idx+1], data[idx+2]);
    idx += 3;
  }
}

bool Configure(int matrix)
{
  int idx = GetMatrixIndex(matrix);
  byte r = pgm_read_byte(matrixBalances[idx]);
  byte g = pgm_read_byte(matrixBalances[idx]+1);
  byte b = pgm_read_byte(matrixBalances[idx]+2);

#ifdef COLORDUINO
  if (GetMatrixAddress(matrix) == 0x00) { 
#ifdef DEBUG
    Serial.println("Config (Local:"+String(matrix)+")");
#endif
    if (r != 0x80) {
      // master-matrix and cmd = set new color balances   
      defaultBalances[0]=r;
      defaultBalances[1]=g;
      defaultBalances[2]=b;
      Colorduino.SetWhiteBal(defaultBalances);
    }
    return true;
  }
#endif //COLORDUINO

#ifdef DEBUG
  Serial.println("Config (Remote:"+String(matrix)+")");
#endif
  // write the balances to slave, true if got expected response from matrix
  StartBuffer(matrix);
  WriteData(matrix, r, g, b);
  return SetBalance(matrix);
}

//********** STRING HANDLING AND SCROLLING CODE BEGINS

char marqueStr[100];  // the text we're scrolling
// marqueWin is the logical position of the first visible char showing in the display
// it has a virtual buffer (leading blanks) indicated by a +pos number, it is 
// decremented until it reaches the marker -(marqueEnd).
int marqueWin; 
int marqueEnd;

// populate matrix from the the visiable portion of the marqueStr 
void UpdateDisplay(int matrix)
{
#ifdef DEBUG
  Serial.println("UpdateDisplay (Matrix:"+String(matrix)+")");
#endif
  // updates the display of scrolled text
  memset(Display[matrix], 0, sizeof(Display[matrix]));

  int winPtr = 0;
  for (int chIdx = 0; chIdx < (int)strlen(marqueStr); chIdx++)
  {
    unsigned char ch = marqueStr[chIdx];
    int w = font_CheckCache(ch);
    DisplayChar(matrix, ch, w, (8-FONT_ROWS)/2, marqueWin + winPtr);
    winPtr += w;
  }
  marqueEnd = winPtr;
}

bool ScrollText()
{
  // shift the text, false if scrolled off
  marqueWin--;
  if (marqueWin < -marqueEnd)
  {
    // restart
    return false;
  }
  return true;
}

const char string_0[] PROGMEM = {"The quick brown fox jumps over the lazy dog."};
const char string_1[] PROGMEM = {"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"};
const char string_2[] PROGMEM = {"`abcdefghijklmnopqrstuvwxyz{|}~\x7f"};
const char string_3[] PROGMEM = {"!\"#$%&'()*+,-./0123456789:;<=>?"};
const char string_4[] PROGMEM = {"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f"};
const char string_5[] PROGMEM = {"\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"};
const char string_6[] PROGMEM = {"\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf"};

void UpdateText()
{
  static const char* const string_table[] = { string_0, string_1, string_2, string_3, string_4, string_5, string_6};
  static int i = 0;

#ifdef DEBUG
  Serial.println("*** UpdateText ***");
#endif
  strcpy_P(marqueStr, string_table[i++]); 
  marqueWin = 8*(LED_MATRIX_COUNT);

  if (i >= sizeof(string_table)/2)
    i = 0;
}

//********** COLOR TOOLS CODE BEGINS

//Converts an HSV color to RGB color
void HSVtoRGB(unsigned char hue, unsigned char sat, unsigned char val, 
              unsigned char *red, unsigned char *grn, unsigned char *blu) 
{
  float r, g, b, h, s, v; //this function works with floats between 0 and 1
  float f, p, q, t;
  int i;

  h = (float)(hue / 256.0);
  s = (float)(sat / 256.0);
  v = (float)(val / 256.0);

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
  *red = (int)(r * 255.0);
  *grn = (int)(g * 255.0);
  *blu = (int)(b * 255.0);
}

//********** ARDUINO MAIN CODE BEGINS

void setup()
{
#ifdef DEBUG
  Serial.begin(115200);
#endif
  
  // Read font properties
  unsigned int temp;
  temp = pgm_read_word(FontName);
  font.ch_width = (temp) & 0xff;
  font.ch_height = (temp>>8) & 0xff;
  temp = pgm_read_word(FontName+2);
  font.ch_start = (temp) & 0xff;  
  font.ch_total = (temp>>8) & 0xff;
  temp = pgm_read_word(FontName+4);
  font.page_width = ((temp & 0xff) * 100) + ((temp>>8) & 0xff);
  
#ifdef DEBUG
  Serial.println( "Font Width: "+String(font.ch_width)+" Height: "+String(font.ch_height)+
                  " Range:"+String(font.ch_start)+"+"+String(font.ch_total)+"Page Width:"+
                  String(font.page_width) );
#endif

#ifdef COLORDUINO
  // initialize the led matrix controller
  Colorduino.Init(); 
  Colorduino.SetWhiteBal(defaultBalances);
#endif //COLORDUINO
  
#if defined(ESP8266)
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
  UpdateText();
}

void loop()
{
  static unsigned long frameTimestamp = 0;
  
  if (frameTimestamp == 0)
    frameTimestamp = millis() - 1000;  // guarentees first frame is displayed immediately
    
  // update the LEDs
  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
  {
    SendDisplay(matrix);
  }
  
  // wait until the next time frame
  long frameDelay = (long)frameTimeout - (long)(millis() - frameTimestamp);
  if (frameDelay > 0) 
    delay(frameDelay);
  frameTimestamp = millis();

  // flip to displaying the new pattern
  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
  {
    ShowBuffer(matrix);
  }

  // update the display with the scrolled text
  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
  {
    UpdateDisplay(matrix);
  }

  // scroll the text left
  if (!ScrollText())
  {
    // scrolled off: new text
    UpdateText();
  }
}
