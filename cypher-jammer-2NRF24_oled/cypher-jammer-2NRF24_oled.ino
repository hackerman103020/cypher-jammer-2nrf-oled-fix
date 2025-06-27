#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2_for_Adafruit_GFX.h>
// BT
#include <BluetoothSerial.h>
// WIFI
#include <WiFi.h>
// NRF24
#include "RF24.h"
#include <SPI.h>
#include <ezButton.h>
#define Debounce_Time 25

#define VSPI_SCK 14
#define VSPI_MISO 13
#define VSPI_MOSI 12
#define VSPI2_SCK 18
#define VSPI2_MISO 19
#define VSPI2_MOSI 23

// NrF24 Triple Module Pin Setup
SPIClass vspi(VSPI);
SPIClass hspi(HSPI);
// radio(CE, CS)
RF24 radio(15, 21);   // Radio 1: CE=15, CS=21
RF24 radio2(26, 25);  // Radio 2: CE=26, CS=25

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SSD1306_I2C_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
// U8g2 for Adafruit GFX
U8G2_FOR_ADAFRUIT_GFX u8g2_for_adafruit_gfx;

// LED
#define LED_PIN 27
// Buttons
#define UP_BUTTON_PIN 22
#define DOWN_BUTTON_PIN 32
#define SELECT_BUTTON_PIN 33

ezButton UP_BUTTON(UP_BUTTON_PIN);
ezButton DOWN_BUTTON(DOWN_BUTTON_PIN);
ezButton SELECT_BUTTON(SELECT_BUTTON_PIN);

// STATE MANAGEMENT
enum AppState {
  STATE_MENU,
  STATE_BT_JAM,
  STATE_DRONE_JAM,
  STATE_WIFI_JAM,
  STATE_WIFI_CHANNEL_SELECT,
  STATE_MULTI_JAM,
  STATE_TEST_NRF,
};
// Global variable to keep track of the current state
AppState currentState = STATE_MENU;
const int MIN_WIFI_CHANNEL = 1;
const int MAX_WIFI_CHANNEL = 14;
int selectedWiFiChannel = 1;

// VARIABLES
enum MenuItem {
  BT_JAM,
  DRONE_JAM,
  WIFI_JAM,
  MULTI_JAM,
  TEST_NRF,
  NUM_MENU_ITEMS
};

bool buttonPressed = false;
bool buttonEnabled = true;
uint32_t lastDrawTime;
uint32_t lastButtonTime;
int firstVisibleMenuItem = 0;
MenuItem selectedMenuItem = BT_JAM;

// Use this instead of delay()
void nonBlockingDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    yield();
  }
}

void displayInfo(String title, String info1 = "", String info2 = "", String info3 = "") {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 4);
  display.println(title);
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setCursor(4, 18);
  display.println(info1);
  display.setCursor(4, 28);
  display.println(info2);
  display.setCursor(4, 38);
  display.println(info3);
  display.display();
}

// ------- NRF24 SETUP ------------
void initRadios() {
  vspi.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI);
  hspi.begin(VSPI2_SCK, VSPI2_MISO, VSPI2_MOSI);
  
  if (radio.begin(&vspi)) {
    Serial.println("Radio 1 Started");
    radio.setAutoAck(false);
    radio.stopListening();
    radio.setRetries(0, 0);
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setDataRate(RF24_2MBPS);
    radio.setCRCLength(RF24_CRC_DISABLED);
    radio.printPrettyDetails();
    radio.startConstCarrier(RF24_PA_MAX, 45);
    displayInfo("Radio 1 Test", "Passed", "Ready To Go!");
  } else {
    Serial.println("Radio 1 Failed to start");
    displayInfo("Test Results", "FAILED", "Check Wiring!");
  }
  delay(3000);
  
  if (radio2.begin(&hspi)) {
    Serial.println("Radio 2 Started");
    radio2.setAutoAck(false);
    radio2.stopListening();
    radio2.setRetries(0, 0);
    radio2.setPALevel(RF24_PA_MAX, true);
    radio2.setDataRate(RF24_2MBPS);
    radio2.setCRCLength(RF24_CRC_DISABLED);
    radio2.printPrettyDetails();
    radio2.startConstCarrier(RF24_PA_MAX, 45);
    displayInfo("Radio 2 Results", "Passed", "Ready To Go!");
  } else {
    Serial.println("Radio 2 Failed to start");
    displayInfo("Radio 2 Results", "FAILED", "Check Wiring!");
  }
}

// Stop jamming function
void stopJamming() {
  radio.stopConstCarrier();
  radio2.stopConstCarrier();
  Serial.println("Jamming stopped");
}

void btJam() {
  radio2.setChannel(random(81));
  radio.setChannel(random(81));
  delayMicroseconds(random(60));
}

void droneJam() {
  radio2.setChannel(random(126));
  radio.setChannel(random(126));
  delayMicroseconds(random(60));
}

// MODIFIED: Now takes channel parameter
void wifiJam(int channel) {
  radio.setChannel(channel);
  radio2.setChannel(channel);
  Serial.print("Jamming WiFi Channel: ");
  Serial.println(channel);
}

// ------- NRF24 SETUP END ------------

// ------- GENERAL CONFIGURATION ------------
void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.display();
  delay(2000);
  display.clearDisplay();
}

void drawWiFiChannelSelect() {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 4);
  display.println("Select WiFi Channel");
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setCursor(4, 30);
  display.print("Channel: ");
  display.print(selectedWiFiChannel);
  display.setCursor(4, 50);
  display.println("Press SELECT to start");
  display.display();
}

void drawMenu() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  u8g2_for_adafruit_gfx.setFont(u8g2_font_baby_tf);
  display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(5, 4);
  display.setTextSize(1);
  display.println("Home");

  const char *menuLabels[NUM_MENU_ITEMS] = {
    "BT Jammer", "Drone Jammer", "Wifi Jammer", "Multi Ch Jam", "NRF Test"
  };

  display.setTextColor(SSD1306_WHITE);
  for (int i = 0; i < 2; i++) {
    int menuIndex = (firstVisibleMenuItem + i) % NUM_MENU_ITEMS;
    int16_t x = 5;
    int16_t y = 20 + (i * 20);

    if (selectedMenuItem == menuIndex) {
      display.fillRect(0, y - 2, SCREEN_WIDTH, 15, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(x, y);
    display.setTextSize(1);
    display.println(menuLabels[menuIndex]);
  }
  display.display();
}

void drawBorder() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
}

bool isButtonPressed(uint8_t pin) {
  if (digitalRead(pin) == LOW) {
    delay(100);
    if (digitalRead(pin) == LOW) {
      return true;
    }
  }
  return false;
}

void handleMenuSelection() {
  static bool buttonPressed = false;

  if (!buttonPressed) {
    if (UP_BUTTON.isPressed()) {
      selectedMenuItem = static_cast<MenuItem>((selectedMenuItem == 0) ? (NUM_MENU_ITEMS - 1) : (selectedMenuItem - 1));

      if (selectedMenuItem == (NUM_MENU_ITEMS - 1)) {
        firstVisibleMenuItem = NUM_MENU_ITEMS - 2;
      } else if (selectedMenuItem < firstVisibleMenuItem) {
        firstVisibleMenuItem = selectedMenuItem;
      }

      Serial.println("UP button pressed");
      drawMenu();
      buttonPressed = true;
    } else if (DOWN_BUTTON.isPressed()) {
      selectedMenuItem = static_cast<MenuItem>((selectedMenuItem + 1) % NUM_MENU_ITEMS);

      if (selectedMenuItem == 0) {
        firstVisibleMenuItem = 0;
      } else if (selectedMenuItem >= (firstVisibleMenuItem + 2)) {
        firstVisibleMenuItem = selectedMenuItem - 1;
      }

      Serial.println("DOWN button pressed");
      drawMenu();
      buttonPressed = true;
    } else if (SELECT_BUTTON.isPressed()) {
      Serial.println("SELECT button pressed");
      executeSelectedMenuItem();
      buttonPressed = true;
    }
  } else {
    buttonPressed = false;
  }
}

// Menu Functions
void executeSelectedMenuItem() {
  switch (selectedMenuItem) {
    case BT_JAM:
      currentState = STATE_BT_JAM;
      Serial.println("BT JAM button pressed");
      displayInfo("BT JAMMER", "ACTIVATING RADIOS", "Starting....");
      initRadios();
      nonBlockingDelay(2000);
      displayInfo("BT JAMMER", "RADIOS ACTIVE", "Running....");
      while (!isButtonPressed(SELECT_BUTTON_PIN)) {
        btJam();
      }
      stopJamming(); // Stop jamming when exiting
      break;
      
    case WIFI_JAM:
      currentState = STATE_WIFI_CHANNEL_SELECT;
      selectedWiFiChannel = 1;
      drawWiFiChannelSelect();
      break;
      
    case DRONE_JAM:
      currentState = STATE_DRONE_JAM;
      Serial.println("DRONE JAM button pressed");
      displayInfo("DRONE JAMMER", "ACTIVATING RADIOS", "Starting....");
      initRadios();
      nonBlockingDelay(2000);
      displayInfo("DRONE JAMMER", "RADIOS ACTIVE", "Running....");
      while (!isButtonPressed(SELECT_BUTTON_PIN)) {
        droneJam();
      }
      stopJamming(); // Stop jamming when exiting
      break;
      
    case MULTI_JAM:
      currentState = STATE_MULTI_JAM;
      Serial.println("MULTI CH JAM button pressed");
      displayInfo("MULTI CH JAMMER", "ACTIVATING RADIOS", "Starting....");
      initRadios();
      nonBlockingDelay(2000);
      displayInfo("MULTI CH JAMMER", "RADIOS ACTIVE", "Running....");
      while (!isButtonPressed(SELECT_BUTTON_PIN)) {
        droneJam();
      }
      stopJamming(); // Stop jamming when exiting
      break;
      
    case TEST_NRF:
      currentState = STATE_TEST_NRF;
      Serial.println("TEST_NRF button pressed");
      initRadios();
      nonBlockingDelay(2000);
      while (!isButtonPressed(SELECT_BUTTON_PIN)) {
        // Just wait, no jamming
      }
      stopJamming(); // Stop jamming when exiting
      break;
  }
}

void handleWiFiChannelSelect() {
  static bool buttonPressed = false;

  if (!buttonPressed) {
    if (UP_BUTTON.isPressed()) {
      selectedWiFiChannel = (selectedWiFiChannel == MAX_WIFI_CHANNEL) ? MIN_WIFI_CHANNEL : selectedWiFiChannel + 1;
      Serial.print("Channel up: ");
      Serial.println(selectedWiFiChannel);
      drawWiFiChannelSelect();
      buttonPressed = true;
    } else if (DOWN_BUTTON.isPressed()) {
      selectedWiFiChannel = (selectedWiFiChannel == MIN_WIFI_CHANNEL) ? MAX_WIFI_CHANNEL : selectedWiFiChannel - 1;
      Serial.print("Channel down: ");
      Serial.println(selectedWiFiChannel);
      drawWiFiChannelSelect();
      buttonPressed = true;
    } else if (SELECT_BUTTON.isPressed()) {
      Serial.println("Starting WiFi jamming");
      currentState = STATE_WIFI_JAM;
      initRadios();
      displayInfo("WiFi Jammer", "Jamming Channel", String(selectedWiFiChannel).c_str());
      buttonPressed = true;
    }
  } else {
    buttonPressed = false;
  }
}

// ------- GENERAL CONFIGURATION END ------------
void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin(5, 4);
  Wire.setClock(100000);
  delay(3000);
  initDisplay();
  
  // Disable unnecessary wireless interfaces
  //esp_bt_controller_deinit();
  esp_wifi_stop();
  esp_wifi_deinit();

  pinMode(UP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DOWN_BUTTON_PIN, INPUT_PULLUP);
  pinMode(SELECT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  
  SELECT_BUTTON.setDebounceTime(Debounce_Time);
  UP_BUTTON.setDebounceTime(Debounce_Time);
  DOWN_BUTTON.setDebounceTime(Debounce_Time);
  Serial.println("buttons init'd");

  // Initialize U8g2_for_Adafruit_GFX
  u8g2_for_adafruit_gfx.begin(display);
  
  // Display splash screens
  displayInfoScreen();
  delay(3000);
  drawMenu();
}

void displayInfoScreen() {
  display.clearDisplay();
  u8g2_for_adafruit_gfx.setFont(u8g2_font_baby_tf);  // Set back to small font
  u8g2_for_adafruit_gfx.setCursor(0, 22);
  u8g2_for_adafruit_gfx.print("Welcome to 2.4GHZ JAMR!");

  u8g2_for_adafruit_gfx.setCursor(0, 30);
  u8g2_for_adafruit_gfx.print("This is a cool cyber tool.");

  u8g2_for_adafruit_gfx.setCursor(0, 38);
  u8g2_for_adafruit_gfx.print("I perform 2.4ghz attacks.");

  u8g2_for_adafruit_gfx.setCursor(0, 54);
  u8g2_for_adafruit_gfx.print("Have fun & be safe ~_~;");

  display.display();
}

void loop() {
  SELECT_BUTTON.loop();
  UP_BUTTON.loop();
  DOWN_BUTTON.loop();
  
  switch (currentState) {
    case STATE_MENU:
      handleMenuSelection();
      break;
      
    case STATE_BT_JAM:
      if (isButtonPressed(SELECT_BUTTON_PIN)) {
        currentState = STATE_MENU;
        drawMenu();
        nonBlockingDelay(500);
      }
      break;
      
    case STATE_WIFI_JAM:
      // Continuously jam the selected WiFi channel
      wifiJam(selectedWiFiChannel);
      
      if (isButtonPressed(SELECT_BUTTON_PIN)) {
        stopJamming();
        currentState = STATE_MENU;
        drawMenu();
        nonBlockingDelay(500);
      }
      break;
      
    case STATE_DRONE_JAM:
      if (isButtonPressed(SELECT_BUTTON_PIN)) {
        currentState = STATE_MENU;
        drawMenu();
        nonBlockingDelay(500);
      }
      break;
      
    case STATE_MULTI_JAM:
      if (isButtonPressed(SELECT_BUTTON_PIN)) {
        currentState = STATE_MENU;
        drawMenu();
        nonBlockingDelay(500);
      }
      break;
      
    case STATE_TEST_NRF:
      if (isButtonPressed(SELECT_BUTTON_PIN)) {
        currentState = STATE_MENU;
        drawMenu();
        nonBlockingDelay(500);
      }
      break;
      
    case STATE_WIFI_CHANNEL_SELECT:
      handleWiFiChannelSelect();
      break;
  }
}
