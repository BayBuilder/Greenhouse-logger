/* Greenhouse Temperature and Humidity Logger
 * 
 * TODO: Add relay output to turn on fan when temp exceeds a preset limit
 *
 * Uses 4x20 I2C LCD display
 * Temp from DS18B20 OneWire sensor
 * Temp and humidity from DHT22 single wire sensor
 * SD Card datalogging
 * RTC module for timekeeping
 * External EEPROM (4k x 8bit) for ???
 * Relay module for turning fans on/off (temperature control)
 *
 * Pin  2 - I2C SDA
 * Pin  3 - I2C SCL
 * Pin  6 - DHT22 (DHT22 library)
 * Pin  9 - DS18B20 (OneWire)
 * Pin 10 - SPI CS (SD Card)
 * Pin 14 - SPI MISO
 * Pin 15 - SPI SCLK
 * Pin 16 - SPI MOSI
 *
 * I2C Setup
 *    Address 0x27 - LCD Display (4 rows x 20 columns)
 *    Address 0x68 - DS3231 RTC
 *    Address 0x57 - AT24C32 EEPROM (can be changed w/ solder pads from 0x50 - 0x57)
 *
 * SPI Notes
 *    The SD card module does not correctly release the MISO line when its CS is inactive.
 *    This prevents the use of other SPI devices on the bus unless fixed!!!
 *
 * Update LCD every 1 SECOND with date/time/temp/humidity data
 * Read Temp/Humidity every 5 SECONDS
 * Log data every 5 MINUTES (in buffer)
 * Write buffer to SD card every HOUR (before buffer is full)
 *
 * Fatal errors display on LCD before while(1);
 *
 * NOTES:
 *
 * DHT22 sensor is slow and can only update approx. every 2 seconds.
 */
#include "LiquidCrystal_I2C.h"
#include "DallasTemperature.h"
#include "DHT.h"
#include "DS3231.h"
#include "SdFat.h"

// Time between logged data points
#define DATA_LOG_MILLIS 60000

// Warning time before writing to the SD card
#define SD_WARNING_MILLIS 3000

// Make sure the SD write warning time makes sense
#if SD_WARNING_MILLIS >= (DATA_LOG_MILLIS - 2000)
#error SD_WARNING_MILLIS must be at least 2000ms shorter than DATA_LOG_MILLIS
#endif

struct DataPoint {
  float ds18f;
  float dht22f;
  uint8_t dht22h;
}; // 12 bytes per DataPoint

// Number of readings before writing to the SD card
// Each reading requires 9 (DataPoint) + 6 (DateTime) = 15 bytes
// 34 readings x 18 = 510 bytes of RAM required!
// #define MAX_DATA_POINTS (int)( 512 / (sizeof(DataPoint) + sizeof(DateTime)) )
#define MAX_DATA_POINTS 34

// Globals
DataPoint currentData;
char line[25]; // General print buffer

//4x20 I2C LCD Display
LiquidCrystal_I2C lcd(0x27, 20, 4);  // Address 0x27, 20 columns, 4 rows

//DHT22 Temp/Humidity Sensor
//  DHTPIN is the sensor data pin
//  DHTTYPE DHT22 is for AM2302,AM2321
#define DHTPIN 6
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

//DS18 Temperature sensor
//  ONE_WIRE_BUS is the sensor data pin
#define ONE_WIRE_BUS 9
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18(&oneWire);

//DS3231 RTC Library
DS3231 myRTC;
bool rtcCentury = false;  // Set to true for year >= 2100
bool rtc12HrFlag;         // true = 12Hr, false = 24Hr
bool rtcPmFlag;           // true = PM, false = AM
DateTime now;

//SD Flash Card Library
//  SD_FAT_TYPE 1 is FAT16/32 (cards up to 64GB)
//  SPT_SPEED_SCK_MHZ can be upped to 50 when not using a breadboard
#define SD_FAT_TYPE 1
#define SD_CHIP_SELECT 10
#define SPI_SPEED SD_SCK_MHZ(4)
SdFat32 sd;
File32 file;

// Enough storage for a filename of yyyy-mm-dd-TempLog.csv
char logFileName[25];

void readSensors(void) {

  static unsigned long readingTime = 0;
  float reading;

  // DS18: Read temp and constrain to -99.9 < temp < 199.9
  ds18.requestTemperatures();
  reading = ds18.getTempFByIndex(0);
  currentData.ds18f = constrain(reading, -99.9, 199.9);

  // DHT22: Wait at least 2s between readings (sensor requirement)
  // Read Temp & Humidity and constrain similarly
  // Humidity gets rounded to nearest %
  if ( (millis() - readingTime) > 2000 ) {
      
    reading = dht.readHumidity();
    currentData.dht22h = (uint8_t)round(reading);

    reading = dht.readTemperature(true);
    currentData.dht22f = constrain(reading, -99.9, 199.9);

    readingTime = millis();
  }
}

void printDateTime(Print *pr, DateTime *now) {
  // Print yyyy/mm/dd hh:mm:ss
  snprintf(line, sizeof(line), "%4d/%02d/%02d  %2d:%02d:%02d", now->year(), now->month(), now->day(), now->hour(), now->minute(), now->second());
  pr->print(line);
  return;
}

void printDP(Print *pr, struct DataPoint *data, DateTime *time) {

  // Print yyyy-mm-dd hh:mm:ss,ds18f,dht22f,dht22h

  now = *time;
  printDateTime(pr, time);
  pr->print(",");
  dtostrf(currentData.ds18f, 3, 1, line);
  pr->print(line);
  pr->print(",");
  dtostrf(currentData.dht22f, 3, 1, line);
  pr->print(line);
  pr->print(",");
  pr->println(currentData.dht22h, DEC);
}

void updateLCD() {

  // First line is Date/Time
  lcd.setCursor(0, 0);
  printDateTime(&lcd, &now);

  // Third line is DHT22 Temp and DS18 Temp
  lcd.setCursor(0, 2);
  dtostrf(currentData.dht22f, 5, 1, line);
  lcd.print(line);
  lcd.print("F");

  lcd.setCursor(12,2);
  dtostrf(currentData.ds18f, 5, 1, line);
  lcd.print(line);
  lcd.print("F");
  
  // Fourth line is DHT22 Humidity
  lcd.setCursor(0, 3);
  snprintf(line, sizeof(line), "%3d%%RH", currentData.dht22h);
  lcd.print(line);
}

bool logData() {

  static DateTime dtArray[MAX_DATA_POINTS];
  static struct DataPoint data[MAX_DATA_POINTS];
  static int curDataPoint = 0;

  //If data buffer is full, dump it to the SD card
  if (curDataPoint >= MAX_DATA_POINTS) {

    Serial.println(F("Writing to SD"));
    if (!file.open(logFileName, O_RDWR | O_APPEND)) {
      errorHalt(F("Error Logging Data"));
    }

    for (int i = 0; i < MAX_DATA_POINTS; i++) {
      printDP(&file, &data[i], &dtArray[i]);
      printDP(&Serial, &data[i], &dtArray[i]);
    }
    file.flush();
    file.close();
    curDataPoint = 0;
  }

  // Save current sensor reading to the data buffer
  dtArray[curDataPoint] = now;
  data[curDataPoint] = currentData;
  curDataPoint++;

  // True if data buffer is full, false otherwise
  return (curDataPoint >= MAX_DATA_POINTS);
}

void errorHalt(const __FlashStringHelper *message) {

  //Print error
  Serial.println(message);
  sd.printSdError(&Serial);

  //Show error on LCD
  lcd.setCursor(0, 1);
  lcd.print(F("SD Error...Halting"));
  lcd.setCursor(0,2);
  lcd.print(message);

  //Try to save any unwritten data & prevent corruption
  file.close();
  while (true)
    ;
}

// Call back for file timestamps.  Only called for file create and sync().
void dateTime(uint16_t *date, uint16_t *time, uint8_t *ms10) {

  // Update the date & time
  now = RTClib::now();

  // Print date and time to the serial port
  Serial.println("File sync");
  printDateTime(&Serial, &now);
  Serial.println();

  // Return date using FS_DATE macro to format fields.
  *date = FS_DATE(now.year(), now.month(), now.day());

  // Return time using FS_TIME macro to format fields.
  *time = FS_TIME(now.hour(), now.minute(), now.second());

  // Return low time bits in units of 10 ms.
  *ms10 = now.second() & 1 ? 100 : 0;
}

void readSerial(void) {

  static char buf[15];
  static uint8_t pos = 0;
  uint8_t one, two, three;

  // Read incoming character
  buf[pos] = Serial.read();

  // Complete line received
  if (buf[pos] == '\n') {

    // Convert non-digits to NUL
    for (pos = 1; pos < sizeof(buf); pos++) {
      if (!isdigit(buf[pos])) buf[pos] = '\0';
    }
    
    // We're assuming a pattern of D##/##/## or T##:##:##
    // Where # is any digit and / or : can be any non-digit
    one = atoi(&buf[1]);
    two = atoi(&buf[4]);
    three = atoi(&buf[7]);
    
    // Set date - minimal error checking
    if (buf[0] == 'D') {
      one = constrain(one, 0, 99);
      two = constrain(two, 1, 12);
      three = constrain(three, 1, 31);
      myRTC.setYear(one);
      myRTC.setMonth(two);
      myRTC.setDate(three);
    }

    // Set time
    if (buf[0] == 'T') {
      one = constrain(one, 0, 23);
      two = constrain(two, 0, 59);
      three = constrain(three, 0, 59);
      myRTC.setHour(one);
      myRTC.setMinute(two);
      myRTC.setSecond(three);
    }

    pos = 0;

  } else {
    if ( pos < sizeof(buf) - 1 ) pos++;
  }
}

void setup() {

  // 4x20 (also initializes I2C library)
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print(F("Temp/Humidity Logger"));
  
  // Begin serial communication at a baud rate of 9600:
  lcd.setCursor(0,1);
  lcd.print(F("Serial @ 9600"));
  lcd.setCursor(0,2);
  lcd.print("Logging every");
  lcd.setCursor(0,3);
  lcd.print(DATA_LOG_MILLIS / 1000);
  lcd.print(" s");
  Serial.begin(9600);

  // Wait 5 seconds for serial communiation to start
  delay(5000);

  // Welcome message to the serial port
  Serial.println(F("Temp/Humidity Logger\n"));
  Serial.println(F("Dyy/mm/dd to set date"));
  Serial.println(F("Thh:mm:ss to set time"));

  // Sensor init
  ds18.begin(); // Start up the DS18 library
  dht.begin();  // Start up the DHT library

  // SD card init
  FsDateTime::setCallback(dateTime);
  if (!sd.begin(SD_CHIP_SELECT, SPI_SPEED)) {
    errorHalt(F("No SD Init"));
  }

  // Use 24-Hour time
  myRTC.setClockMode(false);

  // Log filename is yyyy-mm-dd-TempLog.csv 
  now = RTClib::now();
  snprintf(logFileName, sizeof(logFileName), "%4d-%02d-%02d-TempLog.csv", now.year(), now.month(), now.day());
  
  // Create 
  file.close();
  Serial.print(F("Logging to: "));
  Serial.println(logFileName);
  if (!sd.exists(logFileName)) {

    if (!file.open(logFileName, O_RDWR | O_CREAT)) {
      errorHalt(F("Error Creating File"));
    }

    Serial.print(logFileName);
    Serial.println(" created");
    file.println(F("YYYY/MM/DD HH:MM:SS,DS18F,DHT22F,DHT22H"));
    file.flush();
    file.close();
  }

  lcd.clear();
}

void loop() {

  bool writeWarning = false;
  uint8_t lastSecond;
  
  unsigned long dataLogTime = 0;

  // This is out of range for normal operation
  // and should force a sensor read and LCD update
  // on the first iteration of while (true) {}
  lastSecond = 60;
  
  while (true) {
    now = RTClib::now();

    // Read sensor data every second
    // Update LCD every second
    if (lastSecond != now.second()) {
      lastSecond = now.second();
      readSensors();
      updateLCD();      
    }

    // Show warning before data is written to the SD card
    if ( writeWarning && (millis() - dataLogTime) > (DATA_LOG_MILLIS - SD_WARNING_MILLIS) ) {
      lcd.setCursor(0, 1);
      lcd.print(F("WARNING: Writing Log"));
      writeWarning = false;
    }

    //Log sensor data every DATA_LOG_MILLIS
    if ( (millis() - dataLogTime) > DATA_LOG_MILLIS ) {

      writeWarning = logData();

      //Erase SD write warning from LCD
      lcd.setCursor(0, 1);
      lcd.print(F("                    "));

      dataLogTime = millis();
    }

    // Check for incoming serial data
    if (Serial.available()) {
      readSerial();
    }
  } // while (true)
}