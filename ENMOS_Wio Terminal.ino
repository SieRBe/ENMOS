#include <TFT_eSPI.h>
#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "Wire.h"

TFT_eSPI tft = TFT_eSPI();
RTC_SAMD51 rtc;
SHT31 sht;
SoftwareSerial serial(D2, D3);      // For data transmission
SoftwareSerial SerialMod(D1, D0);   // For Modbus
ModbusMaster node;

bool wifiStatus = false;

typedef struct {
  float V;
  float F;
} READING;

unsigned long previousMillis2 = 0;
const long INTERVAL = 11100;  // Interval for data transmission

void setup() {
  Serial.begin(115200);
  serial.begin(19200);
  SerialMod.begin(9600);

 tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  
  Wire.begin();
  sht.begin(0x44);    // SHT31 I2C Address
  rtc.begin();
  
  DateTime now = DateTime(F(__DATE__), F(__TIME__));
  rtc.adjust(now);
  
  node.begin(17, SerialMod);
  
  // Initialize SD card
  if (!SD.begin(SDCARD_SS_PIN)) {
    Serial.println("SD card initialization failed!");
    return;
  }
  Serial.println("SD card initialized.");
}

void updateWiFiStatus() {
  // Draw WiFi status icon/text
  tft.setCursor(0, 0);
  tft.setTextColor(wifiStatus ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.print("WiFi: ");
  tft.println(wifiStatus ? "Connected" : "Disconnected");
}

void loop() {
  // Read sensors
  sht.read();
  float temperature = sht.getTemperature();
  float humidity = sht.getHumidity();
  
  DateTime now = rtc.now();

if (serial.available()) {
    String wifiMessage = serial.readStringUntil('\n');
    if (wifiMessage.startsWith("WIFI:")) {
      wifiStatus = (wifiMessage.substring(5) == "1");
      
      // Optional: Display or log WiFi status
      Serial.print("WiFi Status: ");
      Serial.println(wifiStatus ? "Connected" : "Disconnected");
    }
  }
  
  // Read Modbus data
  READING r;
  uint8_t result = node.readHoldingRegisters(0002, 10);
  if (result == node.ku8MBSuccess) {
    r.V = (float)node.getResponseBuffer(0x00) / 100;
    r.F = (float)node.getResponseBuffer(0x09) / 100;
  } else {
    r.V = 0;
    r.F = 0;
  }

  // Log to SD card
 char filename[25];
snprintf(filename, sizeof(filename), "/wahyu.csv", now.year(), now.month(), now.day());

// Check if file exists, if not, create it with header
if (!SD.exists(filename)) {
    File file = SD.open(filename, FILE_WRITE);
    if (file) {
        // Write CSV header
        file.print("Timestamp,");
        file.print("Frequency,");
        file.print("Humidity,");
        file.print("Temperature,");
        file.println("Voltage");
        file.close();
    }
}

  // Open file to append data
  File file = SD.open(filename, FILE_WRITE);
  if (file) {
    // Include a full timestamp at the beginning of each data entry
    file.printf("%04d-%02d-%02d %02d:%02d:%02d.%03d;%.2f;%.2f;%.2f;%.2f\n",
      now.year(), now.month(), now.day(),
      now.hour(), now.minute(), now.second(), 
      millis() % 1000,  // Adding milliseconds for more precise timing
       r.F, humidity, temperature, r.V);
     
    file.flush();
    file.close();
  }
  
  // Send data via serial at specified intervals
  unsigned long currentMillis1 = millis();
  if (currentMillis1 - previousMillis2 >= INTERVAL) {
    previousMillis2 = currentMillis1;
    
    String datakirim = String("1#") +
                       String(r.V, 1) + "#" +
                       String(r.F, 1) + "#" +
                       String(temperature, 1) + "#" +
                       String(humidity, 1);
    
    Serial.println(datakirim);
    serial.println(datakirim);
  }
  
  delay(1000);  // Small delay to prevent overwhelming the system
}
