
// sets startup configuration 
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <iostream>
#include <fstream>
#include <time.h>
#include <unistd.h>
#include <iomanip>
#include <sys/time.h>
#include <vector>

#include <chrono>
#include "Adafruit_ADS1015.h"
#include "components.cpp"
//#include "PCA9554.hpp"
using namespace std;

// Define useful names for the GPIO pins and the wiringPi interface to them.
// The wiringPi numbering is described with the "gpio readall" command for RPis with wiringPi installed.
// I copy its output for the Pi3 below, and edit in columns with the photon counter uses.
// +-----+-----+------------+---Pi 3---+------------+------+---------+-----+-----+
// | BCM | wPi | PhotonCntr | Physical | PhotonCntr | wPi | BCM |
// +-----+-----+------------+----++----+------------+-----+-----+
// |     |     |  N/C       |  1 || 2  |   5V       |     |     |
// |   2 |   8 |  SDA       |  3 || 4  |   5V       |     |     |
// |   3 |   9 |  SCL       |  5 || 6  |   GND      |     |     |
// |   4 |   7 |  SFTTRG    |  7 || 8  |   TX       | 15  | 14  |
// |     |     |  GND       |  9 || 10 |   RX       | 16  | 15  |
// |  17 |   0 |  TrgExtEn  | 11 || 12 |   PSCL     | 1   | 18  |
// |  27 |   2 |  Trg1En    | 13 || 14 |   GND      |     |     |
// |  22 |   3 |  Trg2Cnt   | 15 || 16 |   Trg2En   | 4   | 23  |
// |     |     |  N/C       | 17 || 18 |   Trg1Cnt  | 5   | 24  |
// |  10 |  12 |  SPI_MOSI  | 19 || 20 |   GND      |     |     |
// |   9 |  13 |  SPI_MISO  | 21 || 22 |   MR       | 6   | 25  |
// |  11 |  14 |  SPI_CLK   | 23 || 24 |   SPI_CE0  | 10  | 8   |
// |     |     |  GND       | 25 || 26 |   SPI_CE1  | 11  | 7   |
// |   0 |  30 |  ISDA      | 27 || 28 |   ISCL     | 31  | 1   |
// |   5 |  21 |  MX1Out    | 29 || 30 |   GND      |     |     |
// |   6 |  22 |  MX2       | 31 || 32 |   MX2Out   | 26  | 12  |
// |  13 |  23 |  Calib     | 33 || 34 |   GND      |     |     |
// |  19 |  24 |  MX1       | 35 || 36 |   MX0      | 27  | 16  |
// |  26 |  25 |  DAQHalt   | 37 || 38 |   OEbar    | 28  | 20  |
// |     |     |  GND       | 39 || 40 |   SLWCLK   | 29  | 21  |
// +-----+-----+------------+---Pi 3---+------------+------+----+
// | BCM | wPi | PhotonCntr | Physical | PhotonCntr | wPi | BCM |
// +-----+-----+------------+----++----+------------+-----+-----+
//
// The ExtraN pins are not used by the board, but they are available for extra purposes, such as self pulsing during tests.

// Global variables that keep track of the total number of bits read and how many read errors occurred.
// All bits are read twice to check for mismatches indicating an error. 
int nBitsRead = 0;
int nReadErrors = 0;

int debug = 0;
bool DoErrorChecks = true;

// Global variables that speed reading.
int addressDepth = 4096; // 12 bits used in the SRAM
int dataCH1, dataCH2; // The bytes most recently read for each channel
int dataBits[2][8];
int dataBlock[2][4096]; // Results of all data reads

int trg1CntAddr, trg2CntAddr; // Address when Trg1Cnt and Trg2Cnt go high.

int eventNum = 0;

int saveWaveforms = 1000; // Prescale for saving waveforms.  1=all
int nEvents = 1000; //number of events
int printScalers = 100; // Prescale for calculating/displaying scalers

int trgCnt[2][2]; // Results of all Trg1Cnt and Trg2Cnt reads. First number is channel, second number is current (0) and previous (1) values
double scalars[2];
bool checkScalars[2]; 
uint16_t nSLWCLK = 0;

chrono::high_resolution_clock::time_point t0[2];

int MX0 = 27;      // OUTPUT. Lowest bit of the multiplexer code.
int MX1 = 24;      // OUTPUT. Middle bit of the multiplexer code.
int MX2 = 22;      // OUTPUT. Highest bit of the multiplexer code.
int MX1Out = 21;   // INPUT. Data bit out of multiplexer for CH1.
int MX2Out = 26;   // INPUT. Data bit out of multiplexer for CH2.
int SLWCLK = 29;   // OUTPUT. Set high when runtning fast clock, and toggle to slowly clock addresses for readout.
int OEbar = 28;    // INPUT. Goes low when ready for readout.
int DAQHalt = 25;  // OUTPUT. Hold high to stay in readout mode.
int Calib = 23;    // OUTPUT. Pulled high to fire a calibration pulse.
int MR = 6;        // OUTPUT. Pulse high to reset counters before starting a new event.
int Trg1Cnt = 5;   // INPUT. Goes high when the trigger counter for CH1 overflows.
int Trg2Cnt = 3;   // INPUT. Goes high when the trigger counter for CH2 overflows.
int SFTTRG = 7;    // OUTPUT. Pulse high to force a software trigger through TrgExt.
int TrgExtEn = 0;  // OUTPUT. Set high to enable external triggers and software triggers.
int Trg1En = 2;    // OUTPUT. Set high to enable CH1 triggers.
int Trg2En = 4;    // OUTPUT. Set high to enable CH2 triggers.
int PSCL = 1;      // OUTPUT (PWM). Used to prescale the number of CH1 OR CH2 triggers accepted.
int PSCLduty = 1;      // OUTPUT (PWM). Used to prescale the number of CH1 OR CH2 triggers accepted.


void setupPins(){
  const char* configFile = "Config.txt";
  wiringPiSetup(); //Setup GPIO Pins, etc. to use wiringPi and wiringPiI2C interface
  pinMode(MX0,OUTPUT);
  pinMode(MX1,OUTPUT);
  pinMode(MX2,OUTPUT);
  pinMode(MX1Out,INPUT);
  pinMode(MX2Out,INPUT);
  pinMode(SLWCLK,OUTPUT);
  pinMode(Calib,OUTPUT);
  pinMode(OEbar,INPUT);
  pullUpDnControl(OEbar,PUD_UP);
  pinMode(MR,OUTPUT);
  pinMode(DAQHalt,OUTPUT);
  pinMode(Trg1Cnt,INPUT);
  pinMode(Trg2Cnt,INPUT);
  pinMode(SFTTRG,OUTPUT);
  pinMode(TrgExtEn,OUTPUT);
  pinMode(Trg1En,OUTPUT);
  pinMode(Trg2En,OUTPUT);
  // pinMode(PSCL,OUTPUT
  pinMode(PSCL,OUTPUT);
}

void setupTest(){

    setupPins();

    digitalWrite(TrgExtEn,1);
    digitalWrite(Trg1En,1);
    digitalWrite(Trg2En,1);
    /*
    * use these to set PWM.
    * pwmSetRange(n) sets the number of steps in each cycle to be n
    * pwmWrite(pin, m) sets the number of "ON" steps to be m
    */
    pinMode(PSCL,PWM_OUTPUT);
    pwmSetMode(PWM_MODE_MS);
    pwmSetRange(1024);
}

void setupMain(){
  setupPins(); 
  // tbd 
}