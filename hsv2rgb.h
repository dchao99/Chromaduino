// A simple gamma look-up-table, stored in Program Memory
// To access the table, must use: pgm_read_byte()

#ifndef _INC_COLORDUINO_HSV2RGB_H_
#define _INC_COLORDUINO_HSV2RGB_H_

const uint8_t PROGMEM gamma8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,
    3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,
    6,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10, 10, 11, 11, 11, 12,
   12, 13, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19,
   20, 20, 21, 22, 22, 23, 23, 24, 25, 25, 26, 26, 27, 28, 28, 29,
   30, 30, 31, 32, 33, 33, 34, 35, 35, 36, 37, 38, 39, 39, 40, 41,
   42, 43, 43, 44, 45, 46, 47, 48, 49, 49, 50, 51, 52, 53, 54, 55,
   56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
   73, 74, 75, 76, 77, 78, 79, 81, 82, 83, 84, 85, 87, 88, 89, 90,
   91, 93, 94, 95, 97, 98, 99,100,102,103,105,106,107,109,110,111,
  113,114,116,117,119,120,121,123,124,126,127,129,130,132,133,135,
  137,138,140,141,143,145,146,148,149,151,153,154,156,158,159,161,
  163,165,166,168,170,172,173,175,177,179,181,182,184,186,188,190,
  192,194,196,197,199,201,203,205,207,209,211,213,215,217,219,221,
  223,225,227,229,231,234,236,238,240,242,244,246,248,251,253,255 };
  
//********** COLOR TOOLS CODE BEGINS

//Rainbow spectrum pallete tweaks:
// increase yellow: boosting red and lowering green (60-120deg)
// reduce cyan: lowering green and blue (120-240deg)
// increase indigo: lowering red and blue (240-360deg)
// increase red: orange by lowering green (0-120deg)

// Converts an HSV color to RGB color
// hue between 0 to +1536
void HSVtoRGB(void *pChannel, int hue, uint8_t sat, uint8_t val) 
{
  uint8_t  r, g, b;
  uint16_t s1, v1;
  unsigned char *pRGB = (unsigned char *)pChannel;

  hue %= 1536;              //between -1535 to +1535
  if (hue<0) hue += 1536;   //between 0 to +1535

  uint8_t lo = hue & 255;   // Low byte  = primary/secondary color mix
  uint8_t inv = 255 - lo;
  switch(hue >> 8) {        // High byte = sextant of colorwheel
    case 0 : r = 255         ;                ; b =   0     ;         // R to Y
             g = (uint16_t)lo*3>>2 & 255                    ; break ;
    case 1 : r = 255-(lo>>1) ;g = 191         ; b =   0     ; break ; // Y to G
    case 2 : r = inv>>1      ;g = 191         ; b =   0     ; break ; // G to C
    case 3 : r =   0         ;                ; b = lo      ;         // C to B
             g = (uint16_t)inv*3>>2 & 255     ;             ; break ;
    case 4 : r = lo>>1       ; g =   0        ;             ;         // B to M
             b = 255 - ((uint16_t)lo*3>>2 & 255)            ; break ; 
    case 5 : r = 255-(inv>>1); g =   0        ; b = inv>>2  ; break ; // M to R
    default: r =   0         ; g =   0        ; b =   0     ; break ; // black
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
  //v1 = val + 1;
  v1 = pgm_read_byte(&gamma8[val]) + 1;
  *(pRGB++) = (r*v1) >> 8;
  *(pRGB++) = (g*v1) >> 8;
  *(pRGB++) = (b*v1) >> 8;
}
  #endif
