//Bitmaps
#include "bitmaps.h"

//Libraries for Permanent memory
#include <Preferences.h>

//Libraries for LoRa
#include <SPI.h>
#include "LoRa.h"

//Libraries for BME280
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

//Libraries for OLED Display SSD1306
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//Prepare Permanent memory
Preferences preferences;

//Prepare BME280 Sensor
#define SDA 21
#define SCL 22

Adafruit_BME280 bme;
//Prepare a second I2C wire for Sensor
TwoWire I2Cone = TwoWire(1);

//Prepare OLED Display SSD1306
#define SCREEN_WIDTH 128 //OLED display width, in pixels
#define SCREEN_HEIGHT 64 //OLED display height, in pixels
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

//Prepare LoRa
#define BAND    868E6

//Sensor config
#define ROLLING_AVERAGE_DATAPOINT_NUMBER 100

float temperature;
float humidity;
float pressure;

float averageTemperature;
float averageHumidity;

float lastSentTemperature;
float lastSentHumidity;

char temperatureRounded[4];
unsigned int loopCounter;

char* moduleUniqueidentifierKey = "muid";
String moduleUniqueidentifier;

void displaySmallText(int positionX, int positionY, String text) {
  display.setTextColor(WHITE);
  display.setCursor(positionX, positionY);
  display.setTextSize(1);
  display.print(text);
}

void displayNormalText(int positionX, int positionY, String text) {
  display.setTextColor(WHITE);
  display.setCursor(positionX, positionY);
  display.setTextSize(2);
  display.print(text);
}

void displayLargeText(int positionX, int positionY, String text) {
  display.setTextColor(WHITE);
  display.setCursor(positionX, positionY);
  display.setTextSize(3);
  display.print(text);
}

void displayExtraLargeText(int positionX, int positionY, String text) {
  display.setTextColor(WHITE);
  display.setCursor(positionX, positionY);
  display.setTextSize(4);
  display.print(text);
}

void resetOledDisplay() {
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);
}

bool loadConfiguration() {
  preferences.begin("config", true);
 
  if (!preferences.isKey(moduleUniqueidentifierKey)) {
    preferences.end();
    
    return false;
  }

  moduleUniqueidentifier = preferences.getString(moduleUniqueidentifierKey, "");
  preferences.end();
  
  return true;
}

void setConfiguration() {
  Serial.println("Start configuration mode, please set ModuleUniqueidentifier");
  while (!Serial.available()) {
    Serial.print(".");
    delay(50);
  }

  String uniqueidentifier = Serial.readString();
  Serial.println(uniqueidentifier);

  preferences.begin("config", false);
  preferences.putString(moduleUniqueidentifierKey, uniqueidentifier);
  preferences.end();

  delay(100);

  ESP.restart();
}

void initializeOledDisplay() {
  Serial.println(F("Initialize SSD1306 display"));
  //Open connection to Display
  Wire.begin(OLED_SDA, OLED_SCL);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }

  display.setRotation(2); //Rotate 180°
}

void initializeLoRa() {
  display.clearDisplay();
  displaySmallText(0, 0, "Initialize");
  displayLargeText(0, 20, "LoRa");
  display.display();

  Serial.println(F("Initialize LoRa"));
  SPI.begin(SCK,MISO,MOSI,SS);
  LoRa.setPins(SS,RST_LoRa,DIO0);
  if (!LoRa.begin(BAND))
  {
    Serial.println(F("LoRa initialization failed!"));
    while (1);
  }
  LoRa.setSpreadingFactor(12);
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);

  delay(500);
}

void initializeTemperatureSensor() {
  display.clearDisplay();
  displaySmallText(0, 0, "Initialize");
  displayLargeText(0, 20, "T+H");
  displayNormalText(56, 28, "Sensor");
  display.display();
  
  //Set Pins for BME280 Sensor
  I2Cone.begin(SDA, SCL, 100000); 
  
  if (!bme.begin(0x76, &I2Cone)) {
    Serial.println(F("Could not find a valid BME280 sensor, check wiring!"));
    while (1);
  }

  //The sensor requires special protection so that it does not falsify the measurement data due to its own heat
  bme.setSampling(Adafruit_BME280::MODE_FORCED,   //Query the sensor data only on command
                    Adafruit_BME280::SAMPLING_X1, //Temperature
                    Adafruit_BME280::SAMPLING_X1, //Pressure
                    Adafruit_BME280::SAMPLING_X1, //Humidity
                    Adafruit_BME280::FILTER_X2);  //Specifies how many samples are required until in the case of an abrupt change in the measured value the data output has followed at least 75% of the change

  bme.takeForcedMeasurement();
  Serial.print("TemperatureCompensation: ");
  Serial.println(bme.getTemperatureCompensation());

  //Set value from config
  bme.setTemperatureCompensation(-0.5F);

  averageTemperature = bme.readTemperature();
  averageHumidity = bme.readHumidity();

  delay(500);
}

void showModuleInfo() {
  display.clearDisplay();
  displaySmallText(0, 0, "Module Identifier");
  displayLargeText(0, 20, moduleUniqueidentifier);
  display.display();
  
  delay(3000);
}

void showLogo() {
  display.clearDisplay();
  display.drawBitmap(
    (display.width()  - BOOT_LOGO_WIDTH ) / 2,
    (display.height() - BOOT_LOGO_HEIGHT) / 2,
    bitmap_nager, BOOT_LOGO_WIDTH, BOOT_LOGO_HEIGHT, 1);
  display.display();
  delay(2000);
}

double calculateAverageTemperature (double currentTemperature) {
  averageTemperature -= averageTemperature / ROLLING_AVERAGE_DATAPOINT_NUMBER;
  averageTemperature += currentTemperature / ROLLING_AVERAGE_DATAPOINT_NUMBER;
}

double calculateAverageHumidity (double currentHumidity) {
  averageHumidity -= averageHumidity / ROLLING_AVERAGE_DATAPOINT_NUMBER;
  averageHumidity += currentHumidity / ROLLING_AVERAGE_DATAPOINT_NUMBER;
}

void setup() {
  //Prepare Serial connection
  Serial.begin(115200);
  Serial.println("Initialize system");
  
  if (!loadConfiguration()) {
    setConfiguration();
  }
  
  resetOledDisplay();
  initializeOledDisplay();
  showLogo();
  showModuleInfo();
  initializeLoRa();
  initializeTemperatureSensor();
}

void loop() {

 if (Serial.available() > 0) {
  String command = Serial.readString();
  if (command == "reset") {
    ESP.restart();
  }
  
  if (command.startsWith("set")) {
    Serial.print(command);

    preferences.begin("config", false);
    preferences.putString(moduleUniqueidentifierKey, "test123"); //split command and save only data part
    preferences.end();
  }
 }
  
  bme.takeForcedMeasurement();
  temperature = bme.readTemperature();
  humidity = bme.readHumidity();
  pressure = bme.readPressure() / 100.0F;

  calculateAverageTemperature(temperature);
  calculateAverageHumidity(humidity);

  if (loopCounter % 20 == 0 || loopCounter == 0)
  {    
    int differenceTemperature = abs((lastSentTemperature - averageTemperature) * 100);
    Serial.print("Temperature Difference:" + String(differenceTemperature));
    Serial.print(" (");
    Serial.print(lastSentTemperature * 100, 4);
    Serial.print("/");
    Serial.print(averageTemperature * 100, 4);
    Serial.println(")");
    
    if (differenceTemperature > 10)
    {
      Serial.println("Send temperature via LoRa");
      LoRa.beginPacket();
      LoRa.print(moduleUniqueidentifier + "#t:" + String(temperature) + "#h:" + String(humidity));
      LoRa.endPacket();

      lastSentTemperature = averageTemperature;
    }
  }

  Serial.print("Temperature:" + String(temperature) + ", ");
  Serial.print("Humidity:" + String(humidity) + ", ");
  Serial.println("Pressure:" + String(pressure / 100.0F));

  display.clearDisplay();
  dtostrf(temperature, 2, 1, temperatureRounded);
  displayExtraLargeText(0, 15, String(temperatureRounded));
  displayNormalText(100, 0, "o");
  displaySmallText(0, 52, "Humidity: " + String(averageHumidity) + "%");
  display.display();

  loopCounter++;
  delay(1000);
}
