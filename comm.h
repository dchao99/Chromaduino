// Chromaduino
// A driver for a Funduino/Colorduino (master) board: 
// See chromaduino.ino for the I2C protocol definitions

//********** I2C COMMUNICATION CODE BEGINS

#ifndef _INC_COLORDUINO_COMM_H_
#define _INC_COLORDUINO_COMM_H_

// I2C address of the LED matrices.  
// We can use one of the LED matrix's as a master in the chain now  
// 0x00 = I'm master colorduino, use my LED matrix in the chain
const int matrixAddress[] = {0x70, 0x71, 0x72};

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

void WriteData(int matrix, byte* pRGB)
{
  // write a triple
  Wire.beginTransmission(GetMatrixAddress(matrix));
  for (int i = 0; i < 3; i++) 
    Wire.write(*pRGB++);
  Wire.endTransmission();
  delay(1);
}

void WriteBlock(int matrix, byte *pBuf, int count)
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

void ShowBuffer(int matrix)
{
  // flip the buffers
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x01);
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
    
  // Send the command to set the 3 bytes to be balance
  Wire.beginTransmission(GetMatrixAddress(matrix));
  Wire.write((byte)0x02); 
  Wire.endTransmission();
  delay(1);
  return count == 3;  // the slave got 3 bytes
}

//********** I2C COMMUNICATION CODE ENDS

#endif
