#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "Wire.h"

// Initialize objects
RTC_SAMD51 rtc;
SHT31 sht;
SoftwareSerial serial(D2, D3);      
// For ESP communication
SoftwareSerial SerialMod(D1, D0);   // For Modbus
ModbusMaster node;
// Structure for Modbus readings
} READING;
typedef struct {
  float V;  // Voltage
  float F;  // Frequency

// Timing variables
const long SENSOR_READ_INTERVAL = 20000;    // 20 seconds for sensor reading
const long SEND_DATA_INTERVAL = 21000;      // 21 seconds for data sending
const long WIFI_CHECK_INTERVAL = 5000;      // 5 seconds for WiFi check
const unsigned long ESP_TIMEOUT = 5000;     // 5 seconds ESP timeout
const unsigned long ACK_TIMEOUT = 5000;     // 5 seconds ACK timeout
unsigned long lastSensorRead = 0;
unsigned long lastDataSend = 0;
unsigned long lastEspResponse = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastSendAttempt = 0;

// Status variables
bool isESPWiFiConnected = false;
bool waitingForAck = false;
String lastSentData = "";
char filename[] = "/prabroro.csv";

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
  
  // 2. CHECK ESP WIFI STATUS
  if (currentMillis - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
    lastWiFiCheck = currentMillis;
    checkESPWiFiStatus();
  }
  
  // 3. DATA SENDING TO ESP
  if (currentMillis - lastDataSend >= SEND_DATA_INTERVAL) {
    lastDataSend = currentMillis;
    sendDataToESP();
  }
  
  // 4. CHECK FOR ESP RESPONSES
  handleESPResponse();
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

void checkESPWiFiStatus() {
  // Request WiFi status from ESP
  serial.println("CHECK_WIFI");
  
  // Wait for response with timeout
  unsigned long startTime = millis();
  while (!serial.available() && millis() - startTime < 1000) {
    // Wait for up to 1 second
  }
  
  if (serial.available()) {
    String response = serial.readStringUntil('\n');
    response.trim();
    
    if (response.startsWith("WIFI:")) {
      isESPWiFiConnected = (response.substring(5).toInt() == 1);
      lastEspResponse = millis();
      Serial.print("ESP WiFi Status: ");
      Serial.println(isESPWiFiConnected ? "Connected" : "Disconnected");
    }
  } else {
    // No response from ESP
    isESPWiFiConnected = false;
    Serial.println("No response from ESP - assuming disconnected");
  }
}

void sendDataToESP() {
  // Jika sedang menunggu ACK, cek timeout
  if (waitingForAck) {
    if (millis() - lastSendAttempt > ACK_TIMEOUT) {
      Serial.println("ACK timeout - retrying");
      waitingForAck = false;
    } else {
      return; // Masih menunggu ACK, jangan kirim data baru
    }
  }

  if (!isESPWiFiConnected) {
    Serial.println("ESP WiFi disconnected - skipping data transmission");
    return;
  }
  
  File readFile = SD.open(filename);
  if (!readFile || !readFile.available()) {
    if (readFile) readFile.close();
    return;
  }
  
  // Skip header
  readFile.readStringUntil('\n');
  
  if (!readFile.available()) {
    readFile.close();
    return;
  }
  
  // Baca data pertama
  String dataLine = readFile.readStringUntil('\n');
  readFile.close();
  
  if (dataLine.length() > 0) {
    // Parse CSV data
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
      
      // Format dan kirim dengan ID
      String dataToSend = String("DATA#") + volt + "#" + freq + "#" + temp + "#" + hum;
      serial.println(dataToSend);
      
      // Simpan data yang dikirim dan set status waiting
      lastSentData = dataLine;
      waitingForAck = true;
      lastSendAttempt = millis();
      
      Serial.println("Data sent to ESP, waiting for ACK");
    }
  }
}

void handleESPResponse() {
  if (serial.available()) {
    String response = serial.readStringUntil('\n');
    response.trim();
    
    if (response.startsWith("WIFI:")) {
      isESPWiFiConnected = (response.substring(5).toInt() == 1);
      lastEspResponse = millis();
      Serial.print("ESP WiFi Status: ");
      Serial.println(isESPWiFiConnected ? "Connected" : "Disconnected");
    }
    else if (response == "ACK") {
      if (waitingForAck) {
        Serial.println("ACK received - removing sent data");
        removeAcknowledgedData();
        waitingForAck = false;
      }
    }
    else if (response == "NACK") {
      Serial.println("NACK received - will retry");
      waitingForAck = false;
    }
  }
}

void removeAcknowledgedData() {
  if (lastSentData.length() == 0) return;
  
  File readFile = SD.open(filename);
  if (!readFile) return;
  
  // Baca semua data kecuali yang sudah terkirim
  String newContent = "";
  String header = readFile.readStringUntil('\n');
  newContent = header + "\n";
  
  while (readFile.available()) {
    String line = readFile.readStringUntil('\n');
    if (line != lastSentData) {
      newContent += line + "\n";
    }
  }
  readFile.close();
  
  // Tulis ulang file
  SD.remove(filename);
  File writeFile = SD.open(filename, FILE_WRITE);
  if (writeFile) {
    writeFile.print(newContent);
    writeFile.close();
    Serial.println("File updated after ACK");
  }
  
  lastSentData = ""; // Reset sent data
}
