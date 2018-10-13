// Example of using I2C to drive a Chromoduino/Funduino LED matrix slave: scrolling text, 
// using faster command (0x11)
//
// Mark Wilson Jul '18
// v8 David Chao 18-09-11: 
//    Use lincomatic's Colorduino Library https://github.com/dchao99/Colorduino
//    Use Adafruit's classic 5x8font
//    Allow one of the matrix's in the chain to become a master 

//#define COLORDUINO       // running this code on a Colordurino as master?

#include <Wire.h>

#ifdef __AVR__
 #include <avr/pgmspace.h>
#elif defined(ESP8266)
 #include <pgmspace.h>
#else
 #define PROGMEM
#endif

#if defined(ESP8266)
 #include <ESP8266WiFi.h>
#endif
#ifdef COLORDUINO
 #include <Colorduino.h>
#else
 #define ColorduinoScreenWidth  (8)
 #define ColorduinoScreenHeight (8)
#endif //COLORDUINO

#ifdef DEBUG_ESP_PORT     //this is Arduino IDE's debug option, use it if defined
 #define DEBUG_PORT DEBUG_ESP_PORT
#else
 #define DEBUG_PORT Serial //comment out to disable debugging
#endif

#ifdef DEBUG_PORT
 #define DEBUG_PRINT(...) DEBUG_PORT.print( __VA_ARGS__ )
#else
 #define DEBUG_PRINT(...)
#endif

#ifdef DEBUG_PORT //choose our own debug options here
//#define DEBUG_SCROLLING
#define DEBUG_TIMING
#endif

#define MatrixCount (3)     // number of LED matrices connected
#define DISPLAY_ROTATED     // rotates display 180deg from PCB silkscreen orientation

// Arrays representing the LEDs we are displaying.
// Note: Simple on/off here, but could be palette indices
byte mDisplay[MatrixCount][ColorduinoScreenHeight][ColorduinoScreenWidth];

#include "comm.h"
#include "font5x7.h"

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

#define WIRE_BUS_SPEED 400000L  // I2C Bus Speed = 400kHz, about 2ms per packet

#ifdef COLORDUINO
const byte defaultBalances[3] = {35, 55, 63};
#endif
const byte matrixBalances[][3] PROGMEM = 
           { {0x80, 0, 0},   // using 0x80 keeps the default data
             {0x80, 0, 0},
             {0x80, 0, 0},
             {0x80, 0, 0} };
             
byte fontColor[3] = {0,19,127};   //font color {r,g,b}

unsigned int frameTimeout = 160;  //(ms) between each frame
unsigned long frameTimestamp;     //frame timer

//********** CONFIGURATION ENDS

//********** MATRIX CONFIG AND DISPLAY CODE BEGINS

bool Configure(int matrix)
{
  int idx = GetMatrixIndex(matrix);
  byte balRGB[3];
  balRGB[0] = pgm_read_byte(matrixBalances[idx]);
  balRGB[1] = pgm_read_byte(matrixBalances[idx]+1);
  balRGB[2] = pgm_read_byte(matrixBalances[idx]+2);
  char ch[8];
  sprintf(ch, "%02x%02x%02x", balRGB[0], balRGB[1], balRGB[2]);
  
  #ifdef COLORDUINO
  if (GetMatrixAddress(matrix) == 0x00) 
  { 
    DEBUG_PRINT("Config (Local:"+String(matrix)+") RGB="+ch+"\n");
    
    if (balRGB[0] != 0x80) {
      // master-matrix and cmd = set new color balances   
      Colorduino.SetWhiteBal(balRGB);
    }
    return true;
  }
  #endif //COLORDUINO

  DEBUG_PRINT("Config (Remote:"+String(matrix)+") RGB="+ch+"\n");

  // write the balances to slave, true if got expected response from matrix
  StartBuffer(matrix);
  WriteData(matrix, balRGB);
  return SetBalance(matrix);
}

void SendDisplayFast(int matrix)
{
  #ifdef COLORDUINO
  if ( GetMatrixAddress(matrix) == 0x00 )
  {
    // master-matrix, just bitmap to my own LED matrix
    PixelRGB* pChannel = Colorduino.GetPixel(0,0);
    for (int row = 0; row < ColorduinoScreenHeight; row++)
      for (int col = 0; col < ColorduinoScreenWidth; col++)
      {
        #ifdef DISPLAY_ROTATED    
        if (mDisplay[matrix][row][col])
        #else   
        if (mDisplay[matrix][ColorduinoScreenHeight-1-row][ColorduinoScreenWidth-1-col])
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

  // sends the Display data to the given slave LED matrix
  // uses a single color and bitmasks
  StartFastCmd(matrix);
  byte dataBuffer[9];
  for (int row = 0; row < 8; row++)
  {
    dataBuffer[row+1] = 0x00;
    for (int col = 0; col < 8; col++)
      #ifdef DISPLAY_ROTATED    
      if (mDisplay[matrix][row][col])
      #else      
      if (mDisplay[matrix][ColorduinoScreenHeight-1-row][ColorduinoScreenWidth-1-col])
      #endif      
        dataBuffer[row+1] |= 0x01 << col;
  }
  // format the data field for Cmd 0x11 (FAST Write)
  int idx = 0;
  dataBuffer[0] = B00000010;     // write black background, DON'T display at end
  WriteData(matrix, fontColor);  // foreground color
  WriteBlock(matrix, dataBuffer, 9);
}

//********** STRING HANDLING AND SCROLLING CODE BEGINS

char marqueStr[100];  // the text we're scrolling
// marqueWin is the logical position of the first visible char showing in the display
// it has a virtual buffer (leading blanks) indicated by a +pos number, it is 
// decremented until it reaches the marker -(marqueEnd).
int marqueWin; 
int marqueEnd;

// Function: DisplayTextFromMarque
// Populate matrix from the the visiable portion of the marqueStr 
void DisplayTextFromMarque(int matrix)
{
  // updates the display of scrolled text
  memset(mDisplay[matrix], 0, sizeof(mDisplay[matrix]));

  int col = 0;
  for (int chIdx = 0; chIdx < (int)strlen(marqueStr); chIdx++)
  {
    byte ch = marqueStr[chIdx];
    int w = font_CheckCache(ch);
    DisplayChar(matrix, ch, w, (8-font_Height())/2, marqueWin+col);
    col += w;
  }
  marqueEnd = col;
}

// Function: ScrollText
// Shift the visible portion of the window one column until no more text left to display
bool ScrollText()
{
  #ifdef DEBUG_SCROLLING
  DEBUG_PORT.println("Scroll: "+String(marqueWin)+" "+String(marqueEnd));
  #endif
  
  // shift the text, false if scrolled off
  marqueWin--;
  if (marqueWin < -marqueEnd) {
    return false;  // restart
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

  strcpy_P(marqueStr, string_table[i++]); 
  marqueWin = ColorduinoScreenWidth*MatrixCount;

  #if defined(ESP8266)
  if (i >= sizeof(string_table)/4)  i = 0;
  #else
  if (i >= sizeof(string_table)/2)  i = 0;
  #endif //ESP8266

  DEBUG_PRINT("Display Text = "+(String)marqueStr+"\n");
}

//********** ARDUINO MAIN CODE BEGINS

void setup()
{
  #ifdef DEBUG_PORT
  Serial.begin(115200);
  delay(10);
  Serial.println();
  Serial.println();
  #endif

  font_Init();

  #ifdef COLORDUINO
  // initialize the led matrix controller
  Colorduino.Init(); 
  Colorduino.SetWhiteBal(defaultBalances);
  #endif //COLORDUINO
  
  #if defined(ESP8266)
  WiFi.mode( WIFI_OFF );      // Turn off WiFi, we don't need it yet
  WiFi.forceSleepBegin();
  Wire.begin(SDA_PIN, SCL_PIN);
  #endif //ESP8266
  
  Wire.begin();
  Wire.setClock(WIRE_BUS_SPEED);
  
  // reset the board(s)
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(1);
  digitalWrite(RST_PIN, HIGH);

  // keep trying to set the balance until until all slaves are awake
  for (int matrix = 0; matrix < MatrixCount; matrix++)
  {
    do delay(100); while (!Configure(matrix));
  }
  UpdateText();
  frameTimestamp = millis() - 5000;   //guarentee first frame is displayed immediately
}

void loop()
{
  unsigned long now = millis();

  if (now - frameTimestamp >= frameTimeout)
  {
    #ifdef DEBUG_TIMING
    DEBUG_PORT.println("New frame = "+String(now));
    #endif
    frameTimestamp = now;

    // update the display with the scrolled text
    for (int matrix = 0; matrix < MatrixCount; matrix++) {
      DisplayTextFromMarque(matrix);
    }
    #ifdef DEBUG_TIMING
    DEBUG_PORT.println("End of DisplayText = "+String(millis()));
    #endif

    // update the LEDs
    for (int matrix = 0; matrix < MatrixCount; matrix++) {
      SendDisplayFast(matrix);
    }
    #ifdef DEBUG_TIMING
    DEBUG_PORT.println("End of Send = "+String(millis()));
    #endif

    // flip to displaying the new pattern
    for (int matrix = 0; matrix < MatrixCount; matrix++) {
      ShowBuffer(matrix);
    }
    
    // scroll the text left
    if (!ScrollText()) {
      UpdateText();   // scrolled off to the end: new text
    }

  }
}
