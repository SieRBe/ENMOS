#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "Wire.h"
#include <WiFi.h>

// Initialize objects
RTC_SAMD51 rtc;
SHT31 sht;
SoftwareSerial serial(D2, D3);      // For ESP communication
SoftwareSerial SerialMod(D1, D0);   // For Modbus
ModbusMaster node;

// Structure for Modbus readings
typedef struct {
  float V;  // Voltage
  float F;  // Frequency
} READING;

// Timing variables
const long SENSOR_READ_INTERVAL = 20000;    // 20 seconds for sensor reading
const long SEND_DATA_INTERVAL = 21000;      // 21 seconds for data sending
const unsigned long ESP_TIMEOUT = 5000;     // 5 seconds ESP timeout
unsigned long lastSensorRead = 0;
unsigned long lastDataSend = 0;
unsigned long lastEspResponse = 0;

// Status variables
bool isWiFiConnected = false;
char filename[] = "/data.csv";

void setup() {
  // Initialize Serial communications
  Serial.begin(115200);       // Debug serial
  serial.begin(19200);        // ESP communication
  SerialMod.begin(9600);      // Modbus communication
  
  // Initialize I2C devices
  Wire.begin();
  sht.begin(0x44);           // SHT31 sensor
  rtc.begin();
  
  // Set RTC time from compilation
  DateTime now = DateTime(F(__DATE__), F(__TIME__));
  rtc.adjust(now);
  
  // Initialize Modbus
  node.begin(17, SerialMod);
  
  // Initialize SD card
  if (!SD.begin(SDCARD_SS_PIN)) {
    Serial.println("SD card initialization failed!");
    return;
  }
  Serial.println("SD card initialized successfully");
  
  // Create CSV with headers if it doesn't exist
  if (!SD.exists(filename)) {
    File dataFile = SD.open(filename, FILE_WRITE);
    if (dataFile) {
      dataFile.println("Timestamp;Temperature;Humidity;Voltage;Frequency");
      dataFile.close();
      Serial.println("Created new data file with headers");
    }
  }
}

void loop() {
  unsigned long currentMillis = millis();
  
  // 1. SENSOR READING AND CSV STORAGE
  if (currentMillis - lastSensorRead >= SENSOR_READ_INTERVAL) {
    lastSensorRead = currentMillis;
    readAndStoreSensorData();
  }
  
  // 2. DATA SENDING TO ESP
  if (currentMillis - lastDataSend >= SEND_DATA_INTERVAL) {
    lastDataSend = currentMillis;
    sendDataToESP();
  }
  
  // 3. ESP STATUS CHECK
  checkESPStatus();
}

void readAndStoreSensorData() {
  Serial.println("Reading sensors...");
  
  // Read SHT31 sensor
  sht.read();
  float temperature = sht.getTemperature();
  float humidity = sht.getHumidity();
  
  // Read current time
  DateTime now = rtc.now();
  
  // Read Modbus data
  READING r;
  uint8_t result = node.readHoldingRegisters(0002, 10);
  if (result == node.ku8MBSuccess) {
    r.V = (float)node.getResponseBuffer(0x00) / 100;
    r.F = (float)node.getResponseBuffer(0x09) / 100;
  } else {
    r.V = 0;
    r.F = 0;
    Serial.println("Modbus read failed");
  }
  
  // Store data in CSV
  File dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), 
             "%04d-%02d-%02d %02d:%02d:%02d;%.2f;%.2f;%.2f;%.2f\n",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second(),
             temperature, humidity, r.V, r.F);
    
    dataFile.print(buffer);
    dataFile.close();
    Serial.println("Data stored in CSV");
  } else {
    Serial.println("Error opening file for writing");
  }
}

void sendDataToESP() {
  Serial.println("Sending data to ESP...");
  
  File readFile = SD.open(filename);
  if (!readFile) {
    Serial.println("Error opening file for reading");
    return;
  }
  
  if (!readFile.available()) {
    readFile.close();
    Serial.println("No data available to send");
    return;
  }
  
  // Skip header
  String header = readFile.readStringUntil('\n');
  
  if (!readFile.available()) {
    readFile.close();
    Serial.println("No data after header");
    return;
  }
  
  // Read first data line
  String dataLine = readFile.readStringUntil('\n');
  
  if (dataLine.length() > 0) {
    // Parse CSV data
    int pos1 = dataLine.indexOf(';');
    int pos2 = dataLine.indexOf(';', pos1 + 1);
    int pos3 = dataLine.indexOf(';', pos2 + 1);
    int pos4 = dataLine.indexOf(';', pos3 + 1);
    
    if (pos1 != -1 && pos2 != -1 && pos3 != -1 && pos4 != -1) {
      // Extract data values
      String temp = dataLine.substring(pos1 + 1, pos2);
      String hum = dataLine.substring(pos2 + 1, pos3);
      String volt = dataLine.substring(pos3 + 1, pos4);
      String freq = dataLine.substring(pos4 + 1);
      
      // Clean the data
      temp.trim(); hum.trim(); volt.trim(); freq.trim();
      
      // Format and send data to ESP
      String dataToSend = String("1#") + volt + "#" + freq + "#" + temp + "#" + hum;
      serial.println(dataToSend);
      Serial.println("Sent to ESP: " + dataToSend);
      
      // Store remaining data
      String remainingData = "";
      while (readFile.available()) {
        String line = readFile.readStringUntil('\n');
        remainingData += line;
        if (readFile.available()) {
          remainingData += '\n';
        }
      }
      readFile.close();
      
      // Rewrite file with remaining data
      SD.remove(filename);
      File newFile = SD.open(filename, FILE_WRITE);
      if (newFile) {
        newFile.println("Timestamp;Temperature;Humidity;Voltage;Frequency");
        if (remainingData.length() > 0) {
          newFile.print(remainingData);
        }
        newFile.close();
        Serial.println("File updated - sent data removed");
      } else {
        Serial.println("Error rewriting file");
      }
    }
  }
}

void checkESPStatus() {
  if (serial.available()) {
    String response = serial.readStringUntil('\n');
    Serial.print("ESP Response: ");
    Serial.println(response);
    
    if (response.startsWith("WIFI:")) {
      isWiFiConnected = (response.substring(5).toInt() == 1);
      lastEspResponse = millis();
      Serial.print("WiFi Status: ");
      Serial.println(isWiFiConnected ? "Connected" : "Disconnected");
    }
  }
  
  // Check for ESP timeout
  if (millis() - lastEspResponse > ESP_TIMEOUT) {
    isWiFiConnected = false;
    Serial.println("ESP connection timed out");
  }
}
