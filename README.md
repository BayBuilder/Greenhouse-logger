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
Temperature and humidity readings are taken every DATA_SAMPLE_SECONDS and stored locally. Every DATA_LOG_MINUTES, the current readings are stored in a local buffer. When the buffer is full, its contents are written to the file "TempLog.csv" on the SD card.

As internal / external temperatures aren't expected to change frequently, readings are recorded every 5 minutes, with the SD card updated every 1.8 hours. This reduces the chance of SD card corruption due to power failure or unplugging the logger while data is being written to the SD card. Additionally, a warning is displayed on the LCD screen 5 seconds before each SD card write and disappears once the data has been written.
