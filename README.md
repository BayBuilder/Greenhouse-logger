# Greenhouse Temp & Humidity Logger

Simple project to track the temperature and humidity inside a greenhouse as well as the outdoor temperature.

## Hardware
Arduino Pro Micro  
LCD Display (4x20, I2C)  
Real-Time Clock (RTC) Module (DS3231, I2C)  
SD Card Module (SPI)  
Temp sensor (DS18B20, 1-wire)  
Temp / Humidity Sensor (DHT22, single-wire)  

The RTC module also includes a 4k x 8bit EEPROM that this project doesn't use.  
The DHT22 sensor isn't appropriate for this application as very high humidity can damage it. A better choice would be the DHT20 or SHT30.

### Wiring

| Function | Pro Micro | LCD | RTC | SD | DS18B20 | DHT22 |
|---|---|---|---|---|---|---|
| SDA | 2 | J1 SDA | SDA ||||
| SCL | 3 | J1 SCL | SCL ||||
| CS | 10 |||CS|||
| MISO | 14 ||| MISO |||
| SCLK | 15 ||| SCK |||
| MOSI | 16 ||| MOSI |||
| 1-Wire | 9 |||| Yellow ||
| Single Wire | 6 ||||| Out|
| +5V | VCC | J1 VCC | VCC | VCC | Red | + |
| GND | GND | J1 GND | GND | GND | Black | - |

## Software Libraries

| Function | Library | Notes |
|---|---|---|
| LCD | LiquidCrystal_I2C | I2C Address: 0x27 |
| RTC | DS3231 | I2C Address: 0x68 |
| SD Card | SdFat | Cannot share SPI w/o hardware modifications|
| Temperature (DS18B20) | DallasTemperature | OneWire protocol |
| Temp / Humidity (DHT22) | DHT | Custom single wire protocol |

# Operation
On startup, the SD card is checked for a file named "YYYY-MM-DD-TempLog.csv", where YYYY-MM-DD is the current date as read from the RTC. If the file isn't found, it is created and header information written into it. This is now the LOGFILE.

Temperature and humidity readings are taken every DATA_LOG_MILLIS milliseconds and stored in a local buffer. When the buffer is full, it is written to the LOGFILE. To prevent SD card corruption, a warning message appears on the display 3 seconds before the write occurs so the user can avoid unplugging the logger.

By default, readings are taken every minute, with the buffer being written to the LOGFILE every 34 minutes.

# Date and Time
The date and time can be set using the Arduino's serial monitor. Communications run at 9600 baud, which is shown on the LCD during power up. 

Sending "Dyy/mm/dd" will set the date to yy/mm/dd  
Sending "Thh:mm:ss" will set the time to hh:mm:ss  

The time is in 24-hour format. Due to program space limits, the day of the month allows any value from 1 to 31, so it is possible to set an impossible date such as February 30 or November 31, __which may result in undefined operation__. You have been warned.
