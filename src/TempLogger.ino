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
 * DHT22 sensor is slow and can only update approx. every 2 seconds. Keep DATA_SAMPLE_SECONDS >=2
 */

#define LCD_UPDATE_SECONDS 1
#define DATA_SAMPLE_SECONDS 5
#define DATA_LOG_MINUTES 5
#define DATA_BUFFER_SIZE 512

//Make sure sample time is > DHT22 sensor update time
#if DATA_SAMPLE_SECONDS < 2
#error DATA_SAMPLE_SECONDS must be >= 2
#endif

struct DataPoint {
  float ds18f;
  float dht22f;
  float dht22h;
};

#define MAX_DATA_POINTS 22
//#define MAX_DATA_POINTS ((DATA_BUFFER_SIZE / sizeof(struct DataPoint)) + 1)

//4x20 I2C LCD Display
#include "LiquidCrystal_I2C.h"
LiquidCrystal_I2C lcd(0x27, 20, 4);  // Address 0x27, 20 columns, 4 rows

//DHT22 Temp/Humidity Sensor
//  DHTPIN is the sensor data pin
//  DHTTYPE DHT22 is for AM2302,AM2321
#include "DHT.h"
#define DHTPIN 6
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

//DS18 Temperature sensor
//  ONE_WIRE_BUS is the sensor data pin
#include "DallasTemperature.h"
#define ONE_WIRE_BUS 9
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18(&oneWire);

//DS3231 RTC Library
#include "DS3231.h"
DS3231 myRTC;
bool rtcCentury = false;  // Set to true for year >= 2100
bool rtc12HrFlag;         // true = 12Hr, false = 24Hr
bool rtcPmFlag;           // true = PM, false = AM

//SD Flash Card Library
//  SD_FAT_TYPE 1 is FAT16/32 (cards up to 64GB)
//  SPT_SPEED_SCK_MHZ can be upped to 50 when not using a breadboard
//  LOG_FILE_NAME number will be incremented on every power cycle
#include "SdFat.h"
#define SD_FAT_TYPE 1
#define SD_CHIP_SELECT 10
#define SPI_SPEED SD_SCK_MHZ(4)
#define LOG_FILE_NAME "TempLog.csv"
SdFat32 sd;
File32 file;
char logFileName[] = LOG_FILE_NAME;

//AT24C32 EEPROM Library
//#include "AT24C"

void readData(float *ds18f, float *dht22f, float *dht22h) {

  //DS18: Send the command for all devices on the bus to perform a temperature conversion:
  ds18.requestTemperatures();
  *ds18f = ds18.getTempFByIndex(0);

  //DHT22: Read Temp & Humidity
  *dht22h = dht.readHumidity();
  *dht22f = dht.readTemperature(true);
}

void printField(Print *pr, int data, char delim) {

  if (data < 10) pr->write('0');
  pr->print(data, DEC);
  if (delim != '\0') pr->write(delim);
}

void printNow(Print *pr, DateTime *now) {
  printField(pr, now->year(), '/');
  printField(pr, now->month(), '/');
  printField(pr, now->day(), ' ');
  printField(pr, now->hour(), ':');
  printField(pr, now->minute(), ':');
  printField(pr, now->second(), '\0');
}

void printDP(Print *pr, struct DataPoint *data, DateTime *time) {

  printNow(pr, time);
  pr->print(",");
  pr->print(data->ds18f, 1);
  pr->print(",");
  pr->print(data->dht22f, 1);
  pr->print(",");
  pr->println(data->dht22h, 1);
}

void lcdPadPrint(float value, int place, int prec) {

  int test = 1;
  
  for (int i=1; i<place; i++) test *= 10;

  if (value >= 0) {
    while (test > 1 && value < test) {
      lcd.print(" ");
      test /= 10;
    }
  } else {
    test /= 10;
    while (test > 1 && value > -test) {
      lcd.print(" ");
      test /= 10;
    }
  }
  lcd.print(value, prec);
}

void updateLCD(DateTime *now, float *ds18f, float *dht22f, float *dht22h) {

  //First line is Date/Time
  lcd.setCursor(0, 0);
  printNow(&lcd, now);

  //Left side is DHT22 Temp and Humidity
  lcd.setCursor(0, 2);
  lcdPadPrint(*dht22f, 3, 1);
  lcd.print("F    ");
  
  lcd.setCursor(0, 3);
  lcdPadPrint(*dht22h, 3, 0);
  lcd.print("%RH  ");

  //Right side is DS18 Temp
  lcd.setCursor(10, 2);
  lcdPadPrint(*ds18f, 3, 1);
  lcd.print("F    ");
}

void logData(DateTime *now, float *ds18f, float *dht22f, float *dht22h) {

  static struct DataPoint data[MAX_DATA_POINTS];
  static DateTime dtArray[MAX_DATA_POINTS];
  static int curDataPoint = 0;

  //Store data in buffer until it's full, then dump it to the SD card
  if (curDataPoint >= MAX_DATA_POINTS) {

    //Buffer is full, dump to SD
    Serial.println(F("Dumping to SD"));
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

  dtArray[curDataPoint] = *now;
  data[curDataPoint].ds18f = *ds18f;
  data[curDataPoint].dht22f = *dht22f;
  data[curDataPoint].dht22h = *dht22h;
  curDataPoint++;
}

void errorHalt(const __FlashStringHelper *message) {

  //Print error
  Serial.println(message);
  sd.printSdError(&Serial);

  //Try to show error on LCD
  lcd.clear();
  lcd.print("SD Error...");
  lcd.setCursor(0, 1);
  lcd.print(message);
  lcd.setCursor(0, 2);
  lcd.print("Halting");

  //Try to save any unwritten data & prevent corruption
  file.close();
  while (true)
    ;
}

// Call back for file timestamps.  Only called for file create and sync().
void dateTime(uint16_t *date, uint16_t *time, uint8_t *ms10) {
  DateTime now = RTClib::now();

  //Print date and time
  Serial.println("File sync");
  printNow(&Serial, &now);
  Serial.println();

  // Return date using FS_DATE macro to format fields.
  *date = FS_DATE(now.year(), now.month(), now.day());

  // Return time using FS_TIME macro to format fields.
  *time = FS_TIME(now.hour(), now.minute(), now.second());

  // Return low time bits in units of 10 ms.
  *ms10 = now.second() & 1 ? 100 : 0;
}

void setup() {
  // Begin serial communication at a baud rate of 9600:
  Serial.begin(9600);

  //4x20 (also initializes I2C library)
  lcd.init();
  lcd.backlight();

  // Start up the DS18 library:
  ds18.begin();

  //DHT
  dht.begin();

  //Time to turn on serial monitor for debugging
  delay(5000);

  //SD
  FsDateTime::setCallback(dateTime);
  if (!sd.begin(SD_CHIP_SELECT, SPI_SPEED)) {
    errorHalt(F("No SD Library Init"));
  }

  //If log file doesn't exist, create it and write the header
  file.close();
  if (!sd.exists(logFileName)) {
    Serial.println(F("logFile doesn't exist, trying to create"));
    if (!file.open(logFileName, O_RDWR | O_CREAT)) {
      errorHalt(F("Error Creating File"));
    }
    Serial.print(logFileName);
    Serial.println(F(" created successfully."));
    file.println(F("YYYY/MM/DD HH:MM:SS,DS18F,DHT22F,DHT22H"));
    file.flush();
    file.close();
  }
}

void loop() {

  float ds18f;   // Temp from probe
  float dht22f;  // Temp from T/H sensor
  float dht22h;  // Humidity from T/H sensor

  bool dataCurrent = false;
  bool logCurrent = false;
  bool logWarning = false;
  int lcdLastSecond = 0;

  //Get initial sensor values
  readData(&ds18f, &dht22f, &dht22h);

  while (true) {
    DateTime now = RTClib::now();

    //Update Temp & Humidity readings every DATA_SAMPLE_SECONDS
    if (now.second() % DATA_SAMPLE_SECONDS == 0) {
      if (!dataCurrent) {
        readData(&ds18f, &dht22f, &dht22h);
        dataCurrent = true;
      }
    } else {
      dataCurrent = false;
    }

    //Update LCD information every LCD_UPDATE_SECONDS
    if (lcdLastSecond != now.second()) {
      updateLCD(&now, &ds18f, &dht22f, &dht22h);
      lcdLastSecond = now.second();
    }

    //Indicate data will be logged in 5s to prevent unplugging (data loss)
    if (now.minute() % (DATA_LOG_MINUTES - 1) == 0) {
      if (now.second() % 55 == 0) {
        if (!logWarning) {
          //update LCD
          lcd.setCursor(0, 1);
          lcd.print(F("WARNING: Writing Log"));
          logWarning = true;
        }
      }
    } else {
      logWarning = false;
    }

    //Record log data every DATA_LOG_MINUTES
    if (now.minute() % DATA_LOG_MINUTES == 0) {
      if (!logCurrent) {
        logData(&now, &ds18f, &dht22f, &dht22h);
        logCurrent = true;

        //Erase write warning from LCD
        lcd.clear();
      }
    } else {
      logCurrent = false;
    }
  }
}