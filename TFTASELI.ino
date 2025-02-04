#include "TFT_eSPI.h"
#include "Free_Fonts.h"

TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);

// Sample variables (replace with your actual sensor readings)
float temperature = 25.0;
float humidity = 60.0;
float voltage = 220.0;
float frequency = 50.0;

#define LCD_BACKLIGHT (72Ul)

void setup() {
  // Initialize TFT display
  tft.begin();
  tft.init();
  tft.setRotation(3);
  spr.createSprite(TFT_HEIGHT, TFT_WIDTH);
  spr.setRotation(3);
  
  // Initialize backlight
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);
  
  // Clear screen
  tft.fillScreen(TFT_BLACK);
}

void drawLayout() {
  // Draw dividing lines
  tft.drawFastVLine(160, 25, 220, TFT_DARKCYAN);
  tft.drawFastHLine(0, 135, 320, TFT_DARKCYAN);
  tft.drawFastHLine(0, 25, 320, TFT_DARKCYAN);
}

void displayQuadrant1() {
  // Temperature display (top left)
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
}

void displayQuadrant2() {
  // Voltage display (top right)
  spr.setFreeFont(&FreeSans9pt7b);
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
}

void displayQuadrant3() {
  // Frequency display (bottom right)
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
}

void displayQuadrant4() {
  // Humidity display (bottom left)
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

void loop() {
  // Draw the basic layout
  drawLayout();
  
  // Update each quadrant
  displayQuadrant1();
  displayQuadrant2();
  displayQuadrant3();
  displayQuadrant4();
  
  // Add a small delay to prevent screen flickering
  delay(100);
}
