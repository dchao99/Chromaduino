/* 
 * Example of using I2C to drive a Chromaduino/Funduino LED matrix slave.
 * Plasma Morphing Demo
 * Lode's Computer Graphics Tutorial: https://lodev.org/cgtutor/plasma.html
 * v8 David Chao (18-09-12)
 */

#include <Wire.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#endif //ESP8266

#ifdef __AVR__
 #include <avr/pgmspace.h>
#elif defined(ESP8266)
 #include <pgmspace.h>
#else
 #define PROGMEM
#endif

#ifdef DEBUG_ESP_PORT     //this is Arduino IDE's debug option, use it if defined
 #define DEBUG_PORT DEBUG_ESP_PORT
#else
 #define DEBUG_PORT Serial //comment out to disable debugging, or choose Serial1
#endif

#ifdef DEBUG_PORT
 #define DEBUG_PRINTF(...) DEBUG_PORT.printf_P( __VA_ARGS__ )
#else
 #define DEBUG_PRINTF(...)
#endif

#ifdef DEBUG_PORT
 #define DEBUG_PRINT(...) DEBUG_PORT.print( __VA_ARGS__ )
#else
 #define DEBUG_PRINT(...)
#endif

#ifdef DEBUG_PORT //select our own debug options here
//#define DEBUG_PLASMA
//#define DEBUG_TIMING
#endif

#define ColorduinoScreenWidth  (8)
#define ColorduinoScreenHeight (8)

#define MatrixCount (3)     // number of LED matrices connected
#define DISPLAY_ROTATED     // rotates display 180deg from PCB silkscreen orientation

#define LIB8STATIC __attribute__ ((unused)) static inline
#include "trig8.h"              // Copied from FastLED library 
#include "hsv2rgb.h"

#if defined(ESP8266)
 #include "homepage.h"
#endif //ESP8266

// Arrays representing the LEDs we are displaying.
// our palette index is from -3072 to +3072, the array element is integer.
int mDisplay[MatrixCount][ColorduinoScreenHeight][ColorduinoScreenWidth];

#include "comm.h"
#include "font5x7.h"

//********** CONFIGURATION BEGINS

// Arduino: Use default SCL and SDA, nothing to do here
// ESP8266: SCL_PIN = D4, SDA_PIN = D3

#if defined(ESP8266)
 #define SCL_PIN D4
 #define SDA_PIN D3
 #define RST_PIN D2              // D2 connected to DTR on the Colorduino
#else  //Arduino UNO
 #define RST_PIN A0              // A0 connected to DTR on the UNO
#endif //ESP8266

//ESP8266 supports master mode up to approximately 450KHz
//https://arduino-esp8266.readthedocs.io/en/latest/libraries.html#i2c-wire-library
#define WIRE_BUS_SPEED 400000L  // I2C Bus Speed = 400kHz, about 2ms per packet

// Choose special effect(s) to enable, only ONE effect at the moment
#define EFFECT_DEEPSLEEP

const byte matrixBalances[][3] PROGMEM = 
           { {63, 63, 63},    // do gamma correction in-house
             {63, 63, 63},
             {63, 63, 63},
             {0x80, 0, 0} };  // using 0x80 keeps the default data

typedef enum {
  PLASMA,   // Plasma Morphing (DEFAULT)
  SCROLLING // Scrolling Text
} display_mode;

display_mode  currentMode;
unsigned long timeShift=3600000;   //initial seed
unsigned long frameTimestamp;      //frame timer
const byte defaultScrollSpeed=140; //(ms) 
byte fontColor[3] = {0,19,127};    //font color {r,g,b}

// web socket variables
#define led_value   webData[0]     //brightness <- web socket data
#define frame_delay webData[1]     //(ms) between each frame <- web socket data
byte   webData[4] = {180,140,0,0}; //[2][3] not used
bool   effectEnable = false;       //Run special LED Effect
String homeString = "";            //need to copy the homepage from PROGMEM to here to make it dynamic
byte prev_delay;

// To show off our hardware PWM LED driver (capable of displaying 16M colors) 
// increase the scaling of the plasma to see more details of the color transition.
// When using fast sin16(uint_16), need to convert from radians to uint_16 
// (1 rad = 57.2958 degree).  Default sin() uses radians
const unsigned int PlasmaScale = 1043;  // = 57.2958 * 65536 / 360 / (10) <- scaler

// Wi-Fi Settings
const char* ssid     = "San Leandro";   // your wireless network name (SSID)
const char* password = "nintendo";      // your Wi-Fi network password

#if defined(ESP8266)
// Initialize network class objects
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
ESP8266HTTPUpdateServer httpUpdater;
#endif //ESP8266

//********** CONFIGURATION ENDS

//********** MATRIX CONFIG AND DISPLAY CODE BEGINS

bool Configure(int matrix)
{
  int idx = GetMatrixIndex(matrix);
  byte balRGB[3];
  balRGB[0] = pgm_read_byte(matrixBalances[idx]);
  balRGB[1] = pgm_read_byte(matrixBalances[idx]+1);
  balRGB[2] = pgm_read_byte(matrixBalances[idx]+2);
  DEBUG_PRINTF(PSTR("Config (%u) RGB=%02x%02x%02x\n"),matrix,balRGB[0], balRGB[1], balRGB[2]);
  // write the balances to slave, true if got expected response from matrix
  StartBuffer(matrix);
  WriteData(matrix, balRGB);
  return SetBalance(matrix);
}

// Function: SendDisplay
// sends the Display data to the given slave LED matrix
void SendDisplay(int matrix)
{
  StartBuffer(matrix);

  // The Arduino wire library uses 32-byte buffer, to prevent overflow,
  // we must not send more than 32 bytes in one packet.  
  byte dataBuffer[32];
  byte *pRGB = dataBuffer;
  int c = 0;
  for (int row = 0; row < ColorduinoScreenHeight; row++)
  {
    for (int col = 0; col < ColorduinoScreenWidth; col++)
    {      
      #ifdef DISPLAY_ROTATED    
      int hue = mDisplay[matrix][row][col];
      #else
      int hue = mDisplay[matrix][ColorduinoScreenHeight-1-row][ColorduinoScreenWidth-1-col];
      #endif
      Rainbow2RGB( pRGB, hue, 255, led_value);
      pRGB += 3;
      if (++c >= 10) {
        pRGB = dataBuffer;
        WriteBlock( matrix, pRGB, 30 );
        c = 0;
      }
    }
  }
  if (c > 0)  {
    pRGB = dataBuffer;
    WriteBlock( matrix, pRGB, c*3 );
  }
}

// Function: SendDisplayFast
// Sends the Display data to the given slave LED matrix uses a single color and bitmasks
void SendDisplayFast(int matrix)
{
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
  for (int i=0; i<3; i++) {
    WriteData(matrix, dataBuffer+idx);
    idx += 3;
  }
}
#if defined(ESP8266)

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

//********** WEB SERVER AND WEB SOCKET CODE BEGINS

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) 
{
  switch(type) {
    case WStype_DISCONNECTED:
      DEBUG_PRINTF(PSTR("[%u] Disconnected!\n"), num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        DEBUG_PRINTF(PSTR("[%u] Connected from %u.%u.%u.%u url: %s\n"), num, ip[0], ip[1], ip[2], ip[3], payload);
        // send message to client
        webSocket.sendTXT(num, "Connected");
      }
      break;
    case WStype_TEXT:
      DEBUG_PRINTF(PSTR("[%u] Received Text: %s\n"), num, payload);
      if(payload[0] == '#') {
        // Convert payload into 4 separate bytes-size variables
        *(uint32_t*)webData = (uint32_t) strtoul((const char *) &payload[1], NULL, 16);
      } 
      else if (payload[0] == 'E') {   // the browser sends an E when the LED effect is enabled
        effectEnable = true;
      } 
      else if (payload[0] == 'N') {   // the browser sends an N when the LED effect is disabled
        effectEnable = false;
      }
      break;
  }
}

void startWiFi()
{
  DEBUG_PRINT(F("Hostname: "));
  DEBUG_PRINT(WiFi.hostname());
  DEBUG_PRINT(F("\nConnecting ."));

  WiFi.mode(WIFI_STA); 
  if (WiFi.status() != WL_CONNECTED) 
    WiFi.begin(ssid, password);
      
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DEBUG_PRINT(".");
  }

  IPAddress ip = WiFi.localIP();
  DEBUG_PRINTF(PSTR("\nConnected to %s\n"), ssid);
  DEBUG_PRINTF(PSTR("IP address: %u.%u.%u.%u\n"), ip[0], ip[1], ip[2], ip[3]);
}

void handleRoot() 
{
  patchHomePage(homeString, *(unsigned long*)webData);  
  server.send(200, "text/html", homeString);
}

void handleScroll() 
{ 
  String message = "Scrolling Display\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  marqueStr[0] = 0;
  for (uint8_t i = 0; i < server.args(); i++) {
    String s1 = server.argName(i);
    String s2 = server.arg(i);
    message += " " + s1 + ": " + s2 + "\n";
    if (s1 == "text")
      strcpy(marqueStr, s2.c_str()); 
  }

  DEBUG_PRINT(message+"\n");
    
  if (marqueStr[0] != 0) {
    message += "\nText found: ";
    message += marqueStr;
    message += "\n";
  }
  server.send(200, "text/plain", message);
 
  marqueWin = ColorduinoScreenWidth*MatrixCount;
  prev_delay = frame_delay;
  frame_delay = defaultScrollSpeed;   //reset timer to the text scrolling speed
  frameTimestamp -= 5000;             //guarentee first frame is displayed immediately
  currentMode = SCROLLING; 
}

void startServer()
{
  homeString = constructHomePage(*(unsigned long*)webData);

  // handle homepage
  server.on("/", handleRoot);

  // start scrolling text
  server.on("/scroll", handleScroll);
    
  // update firmware
  httpUpdater.setup(&server);
  
  server.begin();
}
#endif //ESP8266

//********** PLASMA MORPHING CODE BEGINS

// Original equation from: https://lodev.org/cgtutor/plasma.html
// const float PlasmaScale = 10.0; 
// float value = sin((col+time) / scaling) + sin(dist(col, row, 64.0, 64.0) / scaling) 
//             + sin((row+time/7.0) / scaling) + sin(dist(col, row, 192.0, 100.0) / scaling);
// int hue = (int)(value * 128 * 6); 

void plasma_morphing(unsigned long time)
{
  for (int row = 0; row < 8; row++) 
  {
    for (int col = 0; col < 8*MatrixCount; col++)
    {
      long value = (long)sin16((col+time) * PlasmaScale) 
                 + (long)sin16((unsigned int)(dist(col, row, 64, 64) * PlasmaScale)) 
                 + (long)sin16((row*PlasmaScale + time*PlasmaScale/7) )
                 + (long)sin16((unsigned int)(dist(col, row, 192, 100) * PlasmaScale));
      //map to -3072 to +3072 then onto 4 palette loops (0 to 1536) x 4
      int hue = (int)(( value*3 ) >> 7);
      #ifdef DEBUG_PLASMA
      DEBUG_PORT.print(String(hue)+",");
      #endif
      mDisplay[col/8][row][col%8] =  hue;
    }
    #ifdef DEBUG_PLASMA
    DEBUG_PORT.println();
    #endif
  } 
}

float dist(int a, int b, int c, int d) 
{
  return sqrt((c-a)*(c-a)+(d-b)*(d-b));
}

//********** ARDUINO MAIN CODE BEGINS

void setup()
{
  #ifdef DEBUG_PORT
  DEBUG_PORT.begin(115200);
  delay(10);
  DEBUG_PORT.println();
  DEBUG_PORT.println();
  #endif //DEBUG

  // Initialise I2C
  #if defined(ESP8266)
  Wire.begin(SDA_PIN, SCL_PIN);
  #else //Arduino UNO
  Wire.begin();
  #endif //ESP8266
  Wire.setClock(WIRE_BUS_SPEED);

  // reset the board(s)
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(1);
  digitalWrite(RST_PIN, HIGH);

  // keep trying to set the WhiteBal until all slaves are awake
  for (int matrix = 0; matrix < MatrixCount; matrix++) 
  {
    do delay(100); while (!Configure(matrix));
  }

  #if defined(ESP8266)
  
  font_Init();
  
  // Make up new hostname from our Chip ID (The MAC addr)
  // Note: Max length for hostString is 32, increase array if hostname is longer
  char hostString[16] = {0};
  sprintf_P(hostString, PSTR("esp8266_%06x"), ESP.getChipId()); 
  WiFi.hostname(hostString);

  //** no longer required **
  //WiFi unable to re-connect fix: https://github.com/esp8266/Arduino/issues/2186
  //WiFi.persistent(false);  
  //WiFi.mode(WIFI_OFF); 
  startWiFi();  

  // Start webSocket service
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Start MDNS Service
  if(MDNS.begin(hostString)) {
    DEBUG_PRINTF(PSTR("MDNS responder started\n"));
  }
     
  startServer();

  // Add service to MDNS
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);
  
  DEBUG_PRINTF(PSTR("HTTPServer ready! Open http://%s.local in your browser\n"), hostString);
  DEBUG_PRINTF(PSTR("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n"), hostString);
  #endif //ESP8266

  // Start timer
  currentMode = PLASMA;
  frameTimestamp = millis();
}

void loop()
{
  #if defined(ESP8266)
  // web server housekeeping
  webSocket.loop();
  server.handleClient();
  #endif //ESP8266
  
  unsigned long now = millis();

  if (now - frameTimestamp >= frame_delay )
  {
    #ifdef DEBUG_TIMING
    DEBUG_PORT.println("New frame = "+String(now));
    #endif
    frameTimestamp = now;
    
    switch (currentMode) 
    {
      case PLASMA:
        // kick plasma morphing animation
        plasma_morphing(timeShift++);
        #ifdef DEBUG_TIMING
        DEBUG_PORT.println("End of Plasma = "+String(millis()));
        #endif

        // update the LEDs
        for (int matrix = 0; matrix < MatrixCount; matrix++) {
          SendDisplay(matrix);
        }
        #ifdef DEBUG_TIMING
        DEBUG_PORT.println("End of Send = "+String(millis()));
        #endif
        break;

      case SCROLLING:
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

        // scroll the text left
        if (!ScrollText()) {
          frame_delay = prev_delay;
          currentMode = PLASMA;   //scrolled off to the end: back to plasma mode
        }
        break;

      default:
        break;
    } //switch

    // flip to displaying the new pattern
    for (int matrix = 0; matrix < MatrixCount; matrix++) {
      ShowBuffer(matrix);
    }
  }
    
  if (effectEnable == true) {
    #ifdef EFFECT_DEEPSLEEP 
    effectAllLedOff();
    ESP.deepSleep(0);   // Put ESP8266 into sleep infinitely
    #endif
  }
}

#ifdef EFFECT_DEEPSLEEP 
void ClearDisplay(int matrix)
{
  // fill the buffer and display it
  StartBuffer(matrix);
  byte colorRGB[3] = {0, 0, 0};
  for (int row = 0; row < ColorduinoScreenHeight; row++)
    for (int col = 0; col < ColorduinoScreenWidth; col++)
      WriteData(matrix,colorRGB);
}

void effectAllLedOff() 
{
  for (int matrix = 0; matrix < MatrixCount; matrix++)
    ClearDisplay(matrix);
  for (int matrix = 0; matrix < MatrixCount; matrix++)
    ShowBuffer(matrix);
}
#endif //EFFECT_DEEPSLEEP 
