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
bool lastSendSuccess = false;  // Track if last send was successful
char filename[] = "/janggar.csv";

void setup() {
  // Initialize Serial communications
  Serial.begin(115200);       // Debug serial
  serial.begin(19200);        // ESP communication
  SerialMod.begin(9600);      // Modbus communication
  
  Wire.begin();
  sht.begin(0x44);           // SHT31 sensor
  rtc.begin();
  
  // Set RTC time from compilation
  DateTime now = DateTime(F(__DATE__), F(__TIME__));
  rtc.adjust(now);
  
  node.begin(17, SerialMod);
  
  // Initialize SD card
  if (!SD.begin(SDCARD_SS_PIN)) {
    Serial.println("SD card initialization failed!");
    return;
  }
  Serial.println("SD card initialized successfully");
  
  // Create CSV with headers if doesn't exist
  if (!SD.exists(filename)) {
    createNewDataFile();
  }
}

void createNewDataFile() {
  File dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile) {
    dataFile.println("Timestamp;Temperature;Humidity;Voltage;Frequency");
    dataFile.close();
    Serial.println("Created new data file with headers");
  }
}

void loop() {
  unsigned long currentMillis = millis();
  
  // 1. Check ESP Status first
  checkESPStatus();
  
  // 2. Sensor Reading and CSV Storage (always runs every 20 seconds)
  if (currentMillis - lastSensorRead >= SENSOR_READ_INTERVAL) {
    lastSensorRead = currentMillis;
    readAndStoreSensorData();
  }
  
  // 3. Data Sending to ESP (only if connected and previous send was successful)
  if (currentMillis - lastDataSend >= SEND_DATA_INTERVAL) {
    lastDataSend = currentMillis;
    if (isWiFiConnected) {
      sendDataToESP();
    } else {
      Serial.println("ESP not connected - Data kept in CSV");
      printStoredDataCount(); // Print how many records are stored
    }
  }
}

void readAndStoreSensorData() {
  Serial.println("Reading sensors...");
  
  // Check SD card space
  if (SD.totalBytes() - SD.usedBytes() < 1024) {  // 1KB threshold
    Serial.println("WARNING: SD Card space low!");
  }
  
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
    printStoredDataCount();
  }
}

void sendDataToESP() {
  if (!isWiFiConnected) {
    Serial.println("ESP not connected - skipping data send");
    return;
  }

  File readFile = SD.open(filename);
  if (!readFile) {
    Serial.println("Error opening file for reading");
    return;
  }
  
  // Skip header
  String header = readFile.readStringUntil('\n');
  
  if (!readFile.available()) {
    readFile.close();
    Serial.println("No data to send");
    return;
  }
  
  // Read first data line
  String dataLine = readFile.readStringUntil('\n');
  bool sendSuccess = false;
  
  if (dataLine.length() > 0) {
    int pos1 = dataLine.indexOf(';');
    int pos2 = dataLine.indexOf(';', pos1 + 1);
    int pos3 = dataLine.indexOf(';', pos2 + 1);
    int pos4 = dataLine.indexOf(';', pos3 + 1);
    
    if (pos1 != -1 && pos2 != -1 && pos3 != -1 && pos4 != -1) {
      String temp = dataLine.substring(pos1 + 1, pos2);
      String hum = dataLine.substring(pos2 + 1, pos3);
      String volt = dataLine.substring(pos3 + 1, pos4);
      String freq = dataLine.substring(pos4 + 1);
      
      temp.trim(); hum.trim(); volt.trim(); freq.trim();
      
      String dataToSend = String("1#") + volt + "#" + freq + "#" + temp + "#" + hum;
      serial.println(dataToSend);
      
      // Wait for acknowledgment from ESP (implement if needed)
      // For now, we'll assume send was successful if ESP is connected
      sendSuccess = true;
      Serial.println("Sent to ESP: " + dataToSend);
    }
  }
  
  if (sendSuccess) {
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
      Serial.println("Successfully removed sent data from CSV");
      printStoredDataCount();
    }
  }
}

void checkESPStatus() {
  if (serial.available()) {
    String response = serial.readStringUntil('\n');
    if (response.startsWith("WIFI:")) {
      bool previousStatus = isWiFiConnected;
      isWiFiConnected = (response.substring(5).toInt() == 1);
      lastEspResponse = millis();
      
      if (previousStatus != isWiFiConnected) {
        Serial.print("WiFi Status changed to: ");
        Serial.println(isWiFiConnected ? "Connected" : "Disconnected");
        printStoredDataCount();
      }
    }
  }
  
  if (millis() - lastEspResponse > ESP_TIMEOUT) {
    if (isWiFiConnected) {
      isWiFiConnected = false;
      Serial.println("ESP connection timed out");
      printStoredDataCount();
    }
  }
}

void printStoredDataCount() {
  int count = 0;
  File dataFile = SD.open(filename);
  if (dataFile) {
    // Skip header
    dataFile.readStringUntil('\n');
    
    while (dataFile.available()) {
      dataFile.readStringUntil('\n');
      count++;
    }
    dataFile.close();
    
    Serial.print("Stored records in CSV: ");
    Serial.println(count);
  }
}
