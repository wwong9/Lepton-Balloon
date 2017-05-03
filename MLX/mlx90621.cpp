// This library is public domain. Whee!!!
// Avery Bodenstein (averybod@gmail.com)

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <math.h>
#include "mlx90621.h"

/*
 * Initialization funtions
*/
// instantiate MLX with default parameters
MLX90621::MLX90621(void) {
  _configParam = 0;
  _error = 0;
}
// instantiate MLX with user parameters
MLX90621::MLX90621(uint16_t configParam) {
  _configParam = configParam;
  _error = 0;
}
// initialize MLX sensor
uint8_t MLX90621::init(void) {
  uint8_t i;
  // begin i2c interface
  _adapter_nr = 1;
  snprintf(_i2cFilename,19,"/dev/i2c-%d",_adapter_nr);
  _I2C = initI2C();
  // POR
  
  // wait 5ms
  usleep(5000);
  // read EEPROM table
  printf("Reading EEPROM...\n");
  readEEPROM(_EEPROM_Data);
  // store calibration constants
  for(i=0;i<64;i++){
    _dA[i] = _EEPROM_Data[i];
    _B[i] = _EEPROM_Data[i+64];
    _dalpha[i] = _EEPROM_Data[i+128];
  }
  _osc_trim = _EEPROM_Data[247];
  // Ta compensation values
  _K_t1 = (_EEPROM_Data[219]<<8) | _EEPROM_Data[218];
  _K_t2 = (_EEPROM_Data[221]<<8) | _EEPROM_Data[220];
  _V_th = (_EEPROM_Data[223]<<8) | _EEPROM_Data[222];
  _K_ts = _EEPROM_Data[210];
  // To compensation values
  _A_com = (_EEPROM_Data[209]<<8) | _EEPROM_Data[208];
  _dAs = (_EEPROM_Data[217] & 0xF0) >> 4;
  _dBs = _EEPROM_Data[217] & 0x0F;
  _emissivity = (_EEPROM_Data[229]<<8) | _EEPROM_Data[228];
  _dalpha_cp = (_EEPROM_Data[215]<<8) | _EEPROM_Data[214];
  _TGC = _EEPROM_Data[216];
  _alpha0 = (_EEPROM_Data[225]<<8) | _EEPROM_Data[224];
  _alpha0_s = _EEPROM_Data[226];
  _dalpha_s = _EEPROM_Data[227];
  _ksta = (_EEPROM_Data[231]<<8) | _EEPROM_Data[230];
  // convert 2s compliment to unsigned
  _K_t1 = uint16_t(_K_t1);
  _K_t2 = uint16_t(_K_t2);
  _V_th = uint16_t(_V_th);
  //printf("Unsigned Ta Values:\n");
  //Serial.print(_K_t1);Serial.print(',');Serial.print(_K_t2);Serial.print(',');Serial.println(_V_th);
  // only use configParam from EEPROM if no user supplied value.
  if (_configParam == 0){
    _configParam = (_EEPROM_Data[246]<<8) | _EEPROM_Data[245];
  }
  // write oscillator trim value into 0x93
  printf("Writing Oscillator trim value...\n");
  writeData(0x04,_osc_trim,0xAA);
  printf("Done.\n");
  setConfig(_configParam);
  // set Brown Out flag to 1 (0x92B10)
}
// open i2c interface
int MLX90621::initI2C(void){
  // check if i2c is already open
  if(_I2C == 0){
    _I2C = open(_i2cFilename, O_RDWR);
    if (_I2C<0){
      printf("Failed to initialize I2C Bus\n");
      return 0;
    }
  }
  return _I2C;
}
// close i2c interface
void MLX90621::closeI2C(void){
  // check if i2c is open
  if(_I2C != 0){
    close(_I2C);
    _I2C = 0;
  }
}
// change configuration parameter
uint8_t MLX90621::setConfig(uint16_t configParam){
  // write configuration parameter to 0x92
  printf("Writing Configuration Parameter...\n");
  writeData(0x03,_configParam,0x55);
  printf("Done.\n");
  // extract ADC resolution for clarity
  uint8_t adcRes = ((_configParam & 48) >> 4);
  // compensate Ta parameters based on config parameter
  _V_th_c = double(_V_th) / (1 << (3 - adcRes));
  _K_t1_c = double(_K_t1) / (1 << ((_K_ts & 240) >> 4));
  _K_t1_c = _K_t1_c / (1 << (3 - adcRes));
  _K_t2_c = double(_K_t2) / (1L << (10 + (_K_ts & 15)));
  _K_t2_c = _K_t2_c / (1 << (3 - adcRes));
  // compensate To parameters based on config parameter
  _ksta = _ksta/(1L << 20);
  int8_t ks4ee = _EEPROM_Data[196];
  uint8_t ks_s = _EEPROM_Data[192] & 0x0F;
  _Ks4 = ks4ee/(1L << (ks_s+8));
  uint8_t i = 0;
  for(i=0;i<64;i++){
    _Ai[i] = ((_A_com + _dA[i]) * (1 << _dAs))/(1 << (3 - adcRes));
    _Bi[i] = (_B[i])/(1 << (_dBs + 3 - adcRes));
    _alpha[i] = ((_alpha0/(1<<_alpha0_s))+(_dalpha[i]/(1<<_dalpha_s)))/(1 << (3 - adcRes));
  }
}
/*
 * Calculation Functions
*/
// calculates actual cold junction temp from raw data
double MLX90621::calcTa(uint16_t rawTemp) {
  double Ta = 0;
  double sqrRt = pow(pow(_K_t1_c,2) - 4.0 * _K_t2_c * (_V_th_c-rawTemp),0.5);
  Ta = ((-_K_t1_c+sqrRt)/(2.0*_K_t2_c))+25.0;
  _Ta = Ta;
  return Ta;
}
// calculates temperature seen by single pixel
double MLX90621::calcTo(uint16_t rawTemp, uint8_t loc){
    // Calculate Compensated IR signal
    // compensate for offset
    double V_ir_c = rawTemp - (_Ai[loc] + _Bi[loc] * (_Ta - 25.0) );
    // compensate for thermal gradient
    V_ir_c = V_ir_c - (double(_TGC) / 32.0);
    // compensate for emissivity
    V_ir_c = V_ir_c / _emissivity;
    printf("V_ir_c\n");
    //Serial.println(V_ir_c);
    // Calculate compensated alpha
    double alpha_c = (1.0 + _ksta*(_Ta-25.0)) * (_alpha[loc] - _TGC*_alpha_cp);
    // calculate Sx
    double Tak = pow(_Ta+273.15,4);
    double Sx = _Ks4 * pow(pow(alpha_c,3)*V_ir_c+pow(alpha_c,4)*Tak,0.25);
    // calculate final temperature
    double To = pow(V_ir_c/(alpha_c*(1-_Ks4*273.15)+Sx),0.25) - 273.15; 
}
/*
 * Data Interaction functions
*/
// reads cold junction temp from MLX
uint16_t MLX90621::readTamb(){
  printf("Reading T_amb\n");
  uint16_t T_amb;
  // buffers
  uint8_t inbuf[2];
  uint8_t outbuf[4];
  // message structs
  struct i2c_rdwr_ioctl_data packets;
  struct i2c_msg messages[2];
  // construct output message
  outbuf[0] = 0x02;
  outbuf[1] = 0x40;
  outbuf[2] = 0x00;
  outbuf[3] = 0x01;
  // begin i2c interface
  _I2C = initI2C();
  // output struct
  messages[0].addr = MLX_ADDR;
  messages[0].flags = 0;
  messages[0].len = sizeof(outbuf);
  messages[0].buf = outbuf;

  // output struct
  messages[1].addr = MLX_ADDR;
  messages[1].flags = I2C_M_RD/* | I2C_M_NOSTARTi*/;
  messages[1].len = sizeof(inbuf);
  messages[1].buf = inbuf;

  // send request to kernel
  packets.msgs = messages;
  packets.nmsgs = 2;
  if(ioctl(_I2C, I2C_RDWR, &packets) < 0){
    // unable to send data
    printf("Unable to send data\n");
    return 0;
  }

  // LSB first
  T_amb = inbuf[0];
  T_amb = T_amb | (inbuf[1] << 8);
  // calculate true T_amb
  T_amb = calcTa(T_amb);
  // return ambient temp
  return T_amb;
}
// read EEPROM
void MLX90621::readEEPROM(uint8_t dataBuf[EEPROM_SIZE]) {
  printf("in readEEPROM\n");
  // begin i2c interface
  _I2C = initI2C();
  // read EEPROM table
  uint16_t i,j;
  // write address to begin reading at
  printf("Setting Slave Address\n");
  // use EEPROM address
  if (ioctl(_I2C, I2C_SLAVE, EEPROM_ADDR) < 0){
    // could not set device as slave
    printf("Could not find device\n");
  }
  // recieve bytes and write into recieve buffer
  printf("Reading Data\n");
  dataBuf[0] = 0x00;
  if (write(_I2C, dataBuf, 1) != 1){
    // write failed
    printf("Write Failed\n");
  }
  printf("Reading Data\n");
  if (read(_I2C, dataBuf, EEPROM_SIZE) != EEPROM_SIZE){
    // read failed
    printf("Read Failed\n");
  }
  // output table for debugging
  printf("EEPROM Contents:\n");
  for(j=0;j<16;j++){
    for(i=0;i<16;i++){
      printf("%X,",dataBuf[16*j+i]);
    }
    printf("\n");
  }
  printf("Done Reading\n");
}
// full frame read
void MLX90621::readFrame(uint16_t dataBuf[64]){
  uint8_t i;
  uint16_t temp;
  // message structs
  struct i2c_rdwr_ioctl_data packets;
  struct i2c_msg messages[2];
  // buffers
  uint8_t inBuf[128];
  uint8_t outBuf[4];
  // initialize I2C interface
  _I2C = initI2C();
  // construct output message
  outBuf[0] = 0x02; // command
  outBuf[1] = 0x00; // start address
  outBuf[2] = 0x01; // address step
  outBuf[3] = 0x40; // number of reads
  // begin i2c interface
  _I2C = initI2C();
  // output struct
  messages[0].addr = MLX_ADDR;
  messages[0].flags = 0;
  messages[0].len = sizeof(outBuf);
  messages[0].buf = outBuf;

  // output struct
  messages[1].addr = MLX_ADDR;
  messages[1].flags = I2C_M_RD/* | I2C_M_NOSTARTi*/;
  messages[1].len = sizeof(inBuf);
  messages[1].buf = inBuf;

  // send request to kernel
  packets.msgs = messages;
  packets.nmsgs = 2;
  if(ioctl(_I2C, I2C_RDWR, &packets) < 0){
    // unable to send data
    printf("Unable to send data\n");
    return;
  }

  // join 8 bit transactions into 16 bit values
  for(i=0;i<64;i++){
    temp = inBuf[2*i];
    temp = temp | (inBuf[2*i+1] << 8);
    dataBuf[i] = temp;
  }
  // close I2C interface
  closeI2C();
}
// single column frame read
void MLX90621::readFrame_sc(uint16_t dataBuf[64]) {
  uint8_t column,i;
  uint8_t tempBuf[8];
  uint16_t temp;
  // initialize I2C interface
  _I2C = initI2C();
  // iterate through columns
  for(column = 0;column<16;column++){
    // write command to read single column
    writeCmd(0x02,column*4,0x01,0x04);
    // request 8 bytes (single column)
    if (read(_I2C, tempBuf, 8) != 8){
      // read failed
      printf("Read Failed\n");
    }
    // join 8 bit transactions into 16 bit values
    for(i=0;i<4;i++){
      temp = tempBuf[2*i];
      temp = temp | (tempBuf[2*i+1] << 8);
      dataBuf[4*column+i] = temp;
    }
  }
}
// single row frame read
void MLX90621::readFrame_sl(uint16_t dataBuf[64]) {
  uint8_t row,i;
  uint16_t temp;
  for(row = 0;row<4;row++){
    // write command to read single row
    writeCmd(0x02,row*16,0x01,0x10);
    // request 32 bytes (single row)
    //Wire.requestFrom(MLX_ADDR,32);
    for(i=0;i<16;i++){
      //temp = Wire.read();
      //temp = temp | (Wire.read() << 8);
      //Serial.print(temp,HEX);Serial.print(',');
      //delay(2);
      dataBuf[16*row+i] = temp;
    }
    //Serial.println();
  }
}
// write command to MLX sensor
void MLX90621::writeCmd(uint8_t cmd, uint8_t offset, uint8_t ad_step, uint8_t nReads) {
  // begin i2c interface
  _I2C = initI2C();
  // use MLX address
  if (ioctl(_I2C, I2C_SLAVE, MLX_ADDR) < 0){
    // could not set device as slave
    printf("Could not find device\n");
  }
  // write command
  uint8_t buf[4];
  buf[0] = cmd;
  buf[1] = offset;
  buf[2] = ad_step;
  buf[3] = nReads;
  if(write(_I2C, buf, 4) != 4){
    // write failed
    printf("Write Failed\n");
  }
}
// write data to MLX sensor
void MLX90621::writeData(uint8_t cmd, uint16_t data, uint8_t check) {
  // initialize i2c interface
  _I2C = initI2C();
  // use MLX address
  if (ioctl(_I2C, I2C_SLAVE, MLX_ADDR) < 0){
    // could not set device as slave
    printf("Could not find device\n");
  }
  // write command
  uint8_t buf[5];
  buf[0] = cmd;
  buf[1] = (data&0xff)-check;
  buf[2] = data&0xff;
  buf[3] = (data>>8)-check;
  buf[4] = data>>8;
  if(write(_I2C, buf, 5) != 5){
    // write failed
    printf("Write Failed\n");
  }
}

/*
 * General Utility functions
*/
/*
// decrypt I2C errors
void MLX90621::checkError(uint8_t error) {
  if(error){
    Serial.println("Error Establishing Connection With MLX");
    // decode error
    switch(error){
      case 1: Serial.println("Data too long");
              break;
      case 2: Serial.println("Recieved Nack on address");
              break;
      case 3: Serial.println("Recieved Nack on data");
              break;
      default: Serial.println("Unknown error");
    }
    // capture process
    while(1);
  }
}
*/

