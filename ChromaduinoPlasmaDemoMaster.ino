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
#include "homepage.h"
#endif //ESP8266

#define LED_MATRIX_COUNT   (2)  // number of LED matrices connected
#define DISPLAY_ROTATED         // rotates display 180deg from PCB silkscreen orientation
#include "comm.h"

#define LIB8STATIC __attribute__ ((unused)) static inline
#include "trig8.h"              // Copied from FastLED library 
#include "hsv2rgb.h"

//#define DEBUG

// Choose one special effect, only ONE effect right now
#define EFFECT_DEEPSLEEP

//********** CONFIGURATION BEGINS

// Arduino: Use default SCL and SDA, nothing to do here
// ESP8266: SCL_PIN = A5, SDA_PIN = A4

#if defined(ESP8266)
#define SCL_PIN D4
#define SDA_PIN D3
#define RST_PIN D2              // D2 connected to DTR on the Colorduino
#else  //Arduino UNO
#define RST_PIN A0              // A0 connected to DTR on the UNO
#endif //ESP8266

#define WIRE_BUS_SPEED 400000L  // I2C Bus Speed = 400kHz

#define ColorduinoScreenWidth  (8)
#define ColorduinoScreenHeight (8)

const byte matrixBalances[][3] PROGMEM = 
           { {63, 63, 63},    // do gamma correction in-house
             {63, 63, 63},
             {0x80, 0, 0} };  // using 0x80 keeps the default data

unsigned long timeShift=128000;    //initial seed
#define brightness   webData[0]    //brightness <- set from web page
#define frameTimeout webData[1]    //(ms/2) between each frame <- set from web page
unsigned long frameTimestamp;      //frame timer
const unsigned int WebPatchInterval=2000;  //2 sec, webpage patching interval 
unsigned long lastPatchTime;       //webpage patching timer

// web socket variables
byte   webData[4] = {180,120};   // [0]=Value, [1]=Speed, [2][3] not used
bool   effectEnable = false;    // Run special LED Effect
String homeString = "";         // need to copy the homepage from PROGMEM to here to make it dynamic

// To show off our hardware PWM LED driver (capable of displaying 16M colors) 
// increase the scaling of the plasma to see more details of the color transition.
// When using fast sin16(uint_16), need to convert from radians to uint_16 
// (1 rad = 57.2958 degree).  Default sin() uses radians
const unsigned int PlasmaScale = 1043;  // = 57.2958 * 65536 / 360 / (10) <- scaler

// Arrays representing the LEDs we are displaying.
// this palette is -3072 to +3072, an integer type
int Display[LED_MATRIX_COUNT][ColorduinoScreenHeight][ColorduinoScreenWidth];

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
  byte balanceRGB[3];
  balanceRGB[0] = pgm_read_byte(matrixBalances[idx]);
  balanceRGB[1] = pgm_read_byte(matrixBalances[idx]+1);
  balanceRGB[2] = pgm_read_byte(matrixBalances[idx]+2);
  #ifdef DEBUG
  char ch[8];
  sprintf(ch, "%02x%02x%02x", balanceRGB[0], balanceRGB[1], balanceRGB[2]);
  Serial.println("Config ("+String(matrix)+") RGB="+ch);
  #endif
  // write the balances to slave, true if got expected response from matrix
  StartBuffer(matrix);
  WriteData(matrix, balanceRGB);
  return SetBalance(matrix);
}

void SendDisplay(int matrix)
{
  // sends the Display data to the given slave LED matrix
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
      int hue = Display[matrix][row][col];
      #else      
      int hue = Display[matrix][ColorduinoScreenHeight-1-row][ColorduinoScreenWidth-1-col];
      #endif
      HSVtoRGB( pRGB, hue, 255, brightness);
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

//********** WEB SERVER AND WEB SOCKET CODE BEGINS

#if defined(ESP8266)
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) 
{
  switch(type) {
    case WStype_DISCONNECTED:
      #ifdef DEBUG
      Serial.printf("[%u] Disconnected!\n", num);
      #endif
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        #ifdef DEBUG        
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        #endif
        // send message to client
        webSocket.sendTXT(num, "Connected");
      }
      break;
    case WStype_TEXT:
      #ifdef DEBUG
      Serial.printf("[%u] Received Text: %s\n", num, payload);
      #endif
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
  #ifdef DEBUG
  Serial.print(F("Hostname: "));
  Serial.println(WiFi.hostname());
  Serial.print(F("Connecting ."));
  #endif
  
  if (WiFi.status() != WL_CONNECTED) 
    WiFi.begin(ssid, password);
      
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    #ifdef DEBUG
    Serial.print(".");
    #endif
  }

  #ifdef DEBUG
  Serial.println();
  Serial.print(F("Connected to "));
  Serial.println(ssid);
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
  #endif
}

void startServer()
{
  // handle index
  homeString = constructHomePage(*(unsigned long*)webData);
  server.on("/", []() {
    // send index.html
    server.send(200, "text/html", homeString);
  });

  // update firmware
  httpUpdater.setup(&server);
  
  server.begin();
}
#endif //ESP8266

//********** ARDUINO MAIN CODE BEGINS

// Original equation from: https://lodev.org/cgtutor/plasma.html
// const float PlasmaScale = 10.0; 
// float value = sin((col+time) / scaling) + sin(dist(col, row, 64.0, 64.0) / scaling) 
//             + sin((row+time/7.0) / scaling) + sin(dist(col, row, 192.0, 100.0) / scaling);
// int hue = (int)(value * 128 * 6); 

void plasma_morph(unsigned long time)
{
  for (int row = 0; row < 8; row++) 
  {
    for (int col = 0; col < 8*LED_MATRIX_COUNT; col++)
    {
      long value = (long)sin16((col+time) * PlasmaScale) 
                 + (long)sin16((unsigned int)(dist(col, row, 64, 64) * PlasmaScale)) 
                 + (long)sin16((row*PlasmaScale + time*PlasmaScale/7) )
                 + (long)sin16((unsigned int)(dist(col, row, 192, 100) * PlasmaScale));
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
  delay(10);
  Serial.println();
  Serial.println();
  #if defined(ESP8266)
  Serial.setDebugOutput(false);  // Use extra debugging details?
  #endif //ESP8266
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
  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++) 
  {
    do delay(100); while (!Configure(matrix));
  }

  #if defined(ESP8266)
  // Make up new hostname from our Chip ID (The MAC addr)
  // Note: Max length for hostString is 32, increase array if hostname is longer
  char hostString[16] = {0};
  sprintf(hostString, "esp8266_%06x", ESP.getChipId()); 
  WiFi.hostname(hostString);

  //** no longer required **
  //WiFi unable to re-connect fix: https://github.com/esp8266/Arduino/issues/2186
  //WiFi.persistent(false);  
  //WiFi.mode(WIFI_OFF); 
  //WiFi.mode(WIFI_STA); 
  startWiFi();  

  // Start webSocket service
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Start MDNS Service
  if(MDNS.begin(hostString)) {
    #ifdef DEBUG
    Serial.println("MDNS responder started");
    #endif
  }
     
  startServer();

  // Add service to MDNS
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);
  #ifdef DEBUG
  Serial.printf("HTTPServer ready! Open http://%s.local in your browser\n", hostString);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", hostString);
  #endif //DEBUG
  #endif //ESP8266

  frameTimestamp = lastPatchTime = millis();
}

void loop()
{
  #if defined(ESP8266)
  // web server housekeeping
  webSocket.loop();
  server.handleClient();
  #endif //ESP8266
  
  unsigned long now = millis();

  #if defined(ESP8266)
  if ( now - lastPatchTime >= WebPatchInterval ) {
    #ifdef DEBUG
    Serial.println("Patch web page = "+String(now));
    #endif
    lastPatchTime = now;
    patchHomePage(homeString, *(unsigned long*)webData);
  }
  #endif //ESP8266
  
  if (now - frameTimestamp >= frameTimeout )
  {
    #ifdef DEBUG
    Serial.println("New frame = "+String(now));
    #endif
    frameTimestamp = now;

    // kick plasma morphing animation
    plasma_morph(timeShift++);
    #ifdef DEBUG
    Serial.println("End of Plasma = "+String(millis()));
    #endif

    // update the LEDs
    for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
    {
      SendDisplay(matrix);
    }
    #ifdef DEBUG
    Serial.println("End of Send = "+String(millis()));
    #endif
    
    // flip to displaying the new pattern
    for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
    {
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
  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
    ClearDisplay(matrix);
  for (int matrix = 0; matrix < LED_MATRIX_COUNT; matrix++)
    ShowBuffer(matrix);
}
#endif //EFFECT_DEEPSLEEP 
