#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include <rpcWiFi.h>
#include "TFT_eSPI.h"
#include "Free_Fonts.h"
#include <RTC_SAMD51.h>
#include <DateTime.h>
#include <NTP.h>

// Display setup
TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);
#define LCD_BACKLIGHT (72Ul)

// RTC setup
RTC_SAMD51 rtc;
bool rtcSynced = false;

// NTP setup
NTP ntp(7); // UTC+7 for Indonesia
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200;     // UTC+7 in seconds
const int daylightOffset_sec = 0;

// Communication setup
SoftwareSerial serial(D2, D3);      // For data transmission
SoftwareSerial SerialMod(D1, D0);   // For Modbus
ModbusMaster node;
SHT31 sht;

// WiFi config
const char* ssid = "lime";
const char* password = "00000000";
bool wifiConnected = false;
unsigned long previousWiFiCheck = 0;
const long WIFI_CHECK_INTERVAL = 14000;
const long RTC_SYNC_INTERVAL = 43200000; // Sync RTC every 12 hours
unsigned long lastRTCSync = 0;

// File and timing
char filename[25] = "mufsin/.csv";
unsigned long previousMillis = 0;
unsigned long previousMillisSensor = 0;
const long INTERVAL = 10000;        // Interval kirim data SD
const long SENSOR_INTERVAL = 30000; // Interval baca sensor

// Display timing
unsigned long previousDisplayMillis = 0;
const long DISPLAY_INTERVAL = 180000; // 3 minutes backlight timer
bool screenOn = true;

// FIFO constants
const int MAX_DATA_ROWS = 100;

void syncRTC() {
    if (WiFi.status() == WL_CONNECTED) {
        ntp.begin();
        ntp.update();
        
        if (ntp.valid()) {
            DateTime ntpTime = DateTime(ntp.year(), ntp.month(), ntp.day(),
                                     ntp.hours(), ntp.minutes(), ntp.seconds());
            rtc.adjust(ntpTime);
            rtcSynced = true;
            Serial.println("RTC synced with NTP server");
            
            // Display sync status
            tft.fillRect(0, 0, 320, 25, TFT_BLACK);
            tft.setTextColor(TFT_GREEN);
            tft.setFreeFont(&FreeSans9pt7b);
            tft.drawString("RTC Synced", 240, 5);
            delay(1000);
        } else {
            Serial.println("Failed to get NTP time");
        }
    }
}

void setup() {
    // Initialize Serial communications
    Serial.begin(115200);
    serial.begin(19200);
    SerialMod.begin(9600);
    
    // Initialize RTC
    rtc.begin();
    
    // Initialize I2C devices
    Wire.begin();
    sht.begin(0x44);
    node.begin(17, SerialMod);
    
    // Initialize SD card
    if (!SD.begin(SDCARD_SS_PIN)) {
        Serial.println("SD card initialization failed!");
        return;
    }
    Serial.println("SD card initialized.");
    
    // Initialize Display
    tft.begin();
    tft.init();
    tft.setRotation(3);
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
    spr.setRotation(3);
    
    pinMode(LCD_BACKLIGHT, OUTPUT);
    digitalWrite(LCD_BACKLIGHT, HIGH);
    
    tft.fillScreen(TFT_BLACK);
    
    // Initialize WiFi and sync RTC
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    // Display WiFi connecting message
    tft.setFreeFont(&FreeSansOblique12pt7b);
    tft.println(" ");
    tft.drawString("Connecting to WiFi...", 8, 5);
    
    // Wait for WiFi connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Syncing RTC...", 8, 5);
        syncRTC();
    }
    
    // Display network scan
    int n = WiFi.scanNetworks();
    tft.fillScreen(TFT_BLACK);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setCursor(87, 22);
    tft.print(n);
    tft.println(" networks found");
    
    for (int i = 0; i < n; i++) {
        tft.println(String(i + 1) + ". " + String(WiFi.SSID(i)) + String(WiFi.RSSI(i)));
    }
    delay(2000);
    tft.fillScreen(TFT_BLACK);
}

void loop() {
    unsigned long currentMillis = millis();
    
    // Periodic RTC sync if WiFi is connected
    if (wifiConnected && (currentMillis - lastRTCSync >= RTC_SYNC_INTERVAL || !rtcSynced)) {
        syncRTC();
        lastRTCSync = currentMillis;
    }
    
    // Check backlight timer
    if (screenOn && currentMillis - previousDisplayMillis >= DISPLAY_INTERVAL) {
        previousDisplayMillis = currentMillis;
        screenOn = false;
        digitalWrite(LCD_BACKLIGHT, LOW);
    }
    
    // Check WiFi connection
    if (currentMillis - previousWiFiCheck >= WIFI_CHECK_INTERVAL) {
        previousWiFiCheck = currentMillis;
        checkWiFiConnection();
    }
    
    // Read sensors and update display
    if (currentMillis - previousMillisSensor >= SENSOR_INTERVAL) {
        previousMillisSensor = currentMillis;
        
        // Read SHT31 sensor
        sht.read();
        float temperature = sht.getTemperature();
        float humidity = sht.getHumidity();
        
        // Read Modbus
        float voltage = 0;
        float frequency = 0;
        uint8_t result = node.readHoldingRegisters(0002, 10);
        if (result == node.ku8MBSuccess) {
            voltage = (float)node.getResponseBuffer(0x00) / 100;
            frequency = (float)node.getResponseBuffer(0x09) / 100;
        }
        
        // Update display
        bool modbusOK = (result == node.ku8MBSuccess);
        bool sensorOK = (humidity != 0);
        updateDisplay(temperature, humidity, voltage, frequency, modbusOK, wifiConnected, sensorOK);

        // Get current timestamp from RTC
        DateTime now = rtc.now();
        char timestamp[20];
        snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
                 now.year(), now.month(), now.day(), 
                 now.hour(), now.minute(), now.second());

        // Handle data storage and transmission
        if (!wifiConnected) {
            // Save to SD card
            if (!SD.exists(filename)) {
                File headerFile = SD.open(filename, FILE_WRITE);
                if (headerFile) {
                    headerFile.println("Timestamp;Temperature;Humidity;Voltage;Frequency;RecordTime");
                    headerFile.close();
                }
            }

            int currentLines = countDataLines();
            if (currentLines >= MAX_DATA_ROWS) {
                // Implement FIFO
                File readFile = SD.open(filename);
                String header = readFile.readStringUntil('\n');
                String remainingData = header + "\n";
                readFile.readStringUntil('\n'); // Skip first data line
                
                while (readFile.available()) {
                    remainingData += readFile.readStringUntil('\n');
                    if (readFile.available()) remainingData += '\n';
                }
                readFile.close();
                
                SD.remove(filename);
                File writeFile = SD.open(filename, FILE_WRITE);
                if (writeFile) {
                    writeFile.print(remainingData);
                    char buffer[128];
                    snprintf(buffer, sizeof(buffer), 
                             "%s;%.2f;%.2f;%.2f;%.2f;%s\n",
                             "backup", temperature, humidity, voltage, frequency, timestamp);
                    writeFile.print(buffer);
                    writeFile.close();
                }
            } else {
                File dataFile = SD.open(filename, FILE_WRITE);
                if (dataFile) {
                    char buffer[128];
                    snprintf(buffer, sizeof(buffer), 
                             "%s;%.2f;%.2f;%.2f;%.2f;%s\n",
                             "backup", temperature, humidity, voltage, frequency, timestamp);
                    dataFile.print(buffer);
                    dataFile.close();
                }
            }
            Serial.println("Data backed up to SD");
        } else {
            // Send data directly via serial
            String datakirim = String("1#") + 
                             String(voltage, 2) + "#" +
                             String(frequency, 2) + "#" +
                             String(temperature, 2) + "#" +
                             String(humidity, 2) + "#" +
                             String(timestamp);
            serial.println(datakirim);
            Serial.println("Data sent: " + datakirim);
        }
    }
}

void updateDisplay(float temperature, float humidity, float voltage, float frequency, 
                  bool modbusOK, bool wifiOK, bool sensorOK) {
    tft.setFreeFont(&FreeSans9pt7b);
    spr.setFreeFont(&FreeSans9pt7b);
    tft.setTextSize(1);

    // Status indicators
    tft.setTextColor(wifiOK ? TFT_GREEN : TFT_RED);
    tft.drawString("WiFi", 8, 5);
    
    tft.setTextColor(modbusOK ? TFT_GREEN : TFT_RED);
    tft.drawString("Modbus", 112, 5);
    
    tft.setTextColor(sensorOK ? TFT_GREEN : TFT_RED);
    tft.drawString("Sensor", 183, 5);

    // Draw dividing lines
    tft.drawFastVLine(160, 25, 220, TFT_DARKCYAN);
    tft.drawFastHLine(0, 135, 320, TFT_DARKCYAN);
    tft.drawFastHLine(0, 25, 320, TFT_DARKCYAN);

    // Temperature (Quadrant 1)
    spr.createSprite(158, 102);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Temp", 55, 8);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(temperature, 1), 25, 36);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("C", 113, 85);
    spr.pushSprite(0, 27);
    spr.deleteSprite();

    // Voltage (Quadrant 2)
    spr.createSprite(158, 102);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Voltage", 50, 8);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(voltage, 1), 11, 36);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("VAC", 105, 85);
    spr.pushSprite(162, 27);
    spr.deleteSprite();

    // Frequency (Quadrant 3)
    spr.createSprite(158, 100);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Freq", 62, 6);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(frequency, 1), 26, 34);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("Hz", 108, 82);
    spr.pushSprite(162, 137);
    spr.deleteSprite();

    // Humidity (Quadrant 4)
    spr.createSprite(158, 100);
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Humidity", 43, 6);
    spr.setTextColor(TFT_GREEN);
    spr.setFreeFont(FSSO24);
    spr.drawString(String(humidity, 1), 25, 34);
    spr.setTextColor(TFT_YELLOW);
    spr.setFreeFont(&FreeSans9pt7b);
    spr.drawString("%RH", 65, 82);
    spr.pushSprite(0, 137);
    spr.deleteSprite();
}

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        Serial.println("WiFi disconnected");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(500);
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.println("WiFi reconnected");
        }
    } else {
        wifiConnected = true;
    }
}

int countDataLines() {
    File readFile = SD.open(filename);
    int lineCount = -1;  // -1 to account for header line
    
    if (readFile) {
        while (readFile.available()) {
            readFile.readStringUntil('\n');
            lineCount++;
        }
        readFile.close();
    }
    return lineCount;
}
