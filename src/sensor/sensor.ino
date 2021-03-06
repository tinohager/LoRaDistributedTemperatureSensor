//Bitmaps
#include "bitmaps.h"

//Libraries for LoRa
#include <SPI.h>
#include "LoRa.h"

//Libraries for BME280
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

//Display
#include "display.h"

//Configuration
#include "configuration.h"

//Prepare BME280 Sensor
#define SDA 21
#define SCL 22

Adafruit_BME280 bme;
//Prepare a second I2C wire for Sensor
TwoWire I2Cone = TwoWire(1);

//Prepare LoRa
#define BAND    868E6

//Sensor config
#define ROLLING_AVERAGE_DATAPOINT_NUMBER 20

float temperature;
float humidity;
float pressure;

float averageTemperature;
float averageHumidity;

float lastSentTemperature;
float lastSentHumidity;

char temperatureRounded[4];

String moduleUniqueidentifier;
float temperatureCompensation;

unsigned long previousMillis = 0UL;
unsigned long sensorInterval = 1000UL; //1s

void initializeLoRa() {
  displayClear();
  displaySmallText(0, 0, "Initialize");
  displayLargeText(0, 20, "LoRa");
  displayDraw();

  Serial.println(F("Initialize LoRa"));
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(18, 14, 26);

  if (!LoRa.begin(BAND))
  {
    Serial.println(F("LoRa initialization failed!"));
    while (1);
  }

  //Lora Send Configuration examples
  //https://github.com/dragino/Arduino-Profile-Examples/blob/master/libraries/Dragino/examples/LoRa/LoRa_Simple_Client_Arduino/LoRa_Simple_Client_Arduino.ino

  LoRa.setPreambleLength(8);
  LoRa.setSignalBandwidth(31.25E3);
  LoRa.setCodingRate4(8); //ranges from 5-8, default 5
  LoRa.setSpreadingFactor(9);
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN); //Supported values are 2 to 20 for PA_OUTPUT_PA_BOOST_PIN
  LoRa.setSyncWord(0x91); //ranges from 0-0xFF, default 0x34, see API docs

  //Debugging
  //LoRa.dumpRegisters(Serial);

  delay(500);
}

void initializeTemperatureSensor() {
  displayClear();
  displaySmallText(0, 0, "Initialize");
  displayLargeText(0, 20, "T&H");
  displayNormalText(56, 28, "Sensor");
  displayDraw();
  
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

  //Set value from config
  float temperatureCompensation = getTemperatureCompensation();
  bme.setTemperatureCompensation(temperatureCompensation);

  Serial.print("TemperatureCompensation: ");
  Serial.println(temperatureCompensation);

  averageTemperature = bme.readTemperature();
  averageHumidity = bme.readHumidity();

  delay(500);
}

void showLogo() {
  displayClear();
  displayLogo();
  displayDraw();
  delay(1500);
}

void showModuleInfo() {
  displayClear();
  displaySmallText(0, 0, "Module Identifier");
  displayLargeText(0, 20, moduleUniqueidentifier);
  displayDraw();
  
  delay(2000);
}

void setConfiguration() {
  //TODO: set display text "set configuration via serial"
  
  while (!Serial.available()) {
    Serial.println(F("Configuration mode active, please set ModuleUniqueidentifier via 'config.muid=XXX'"));
    delay(2000);
  }

  String uniqueidentifier = Serial.readString();
  Serial.println(uniqueidentifier);

  setModuleUniqueidentifier(uniqueidentifier);

  delay(100);
  ESP.restart();
}

double calculateAverageTemperature (double currentTemperature) {
  averageTemperature -= averageTemperature / ROLLING_AVERAGE_DATAPOINT_NUMBER;
  averageTemperature += currentTemperature / ROLLING_AVERAGE_DATAPOINT_NUMBER;
}

double calculateAverageHumidity (double currentHumidity) {
  averageHumidity -= averageHumidity / ROLLING_AVERAGE_DATAPOINT_NUMBER;
  averageHumidity += currentHumidity / ROLLING_AVERAGE_DATAPOINT_NUMBER;
}

void sendLoraPackage() {
  Serial.println("Send temperature via LoRa");
  
  int isReady = LoRa.beginPacket();
  Serial.println("LoRa isReady:" + String(isReady));
  LoRa.print(moduleUniqueidentifier + "#t:" + String(temperature) + "#h:" + String(humidity));
  LoRa.endPacket();
  
  Serial.println("Send done");
}

void setup() {
  //Prepare Serial connection
  Serial.begin(115200);
  Serial.setTimeout(250);
  Serial.println("Initialize system");

  moduleUniqueidentifier = getModuleUniqueidentifier();
  if (moduleUniqueidentifier.length() == 0) {
    setConfiguration();
  }
  
  resetOledDisplay();
  initializeOledDisplay();
  showLogo();
  showModuleInfo();
  initializeLoRa();
  initializeTemperatureSensor();

  xTaskCreate(
    taskSendLora,       /* Task function. */
    "sendLoraTask",     /* String with name of task. */
    10000,             /* Stack size in words. */
    NULL,              /* Parameter passed as input of the task */
    2,                 /* Priority of the task. */
    NULL);             /* Task handle. */

  Serial.println("System is ready");
}

void taskSendLora( void * pvParameters ) {

  //wait for first sensor read
  delay(2000);
  unsigned int loopCounter = 0;
  
  for(;;) {

    int differenceTemperature = abs((lastSentTemperature - averageTemperature) * 100);
    Serial.print("Temperature Difference:" + String(differenceTemperature));
    Serial.print(" (L:");
    Serial.print(lastSentTemperature * 100, 4);
    Serial.print("/A:");
    Serial.print(averageTemperature * 100, 4);
    Serial.println(")");
    
    if (differenceTemperature > 10 || loopCounter > 30) //send info on a change of 0.1% or after 5 minutes
    {
      sendLoraPackage();
      lastSentTemperature = averageTemperature;
      loopCounter = 0;
    }

    loopCounter++;
    delay(10000);
  }
}

void loop() {

  unsigned long currentMillis = millis();

  if (Serial.available() > 0) {
    String command = Serial.readString();
    if (command.equalsIgnoreCase("reset")) {
      ESP.restart();
    }
  
    if (command.equalsIgnoreCase("send")) {
      sendLoraPackage();
    }
    
    if (command.startsWith("config.muid=")) {
      Serial.println("set muid config");
      String value = command.substring(12);
  
      setModuleUniqueidentifier(value);
    }
  
    if (command.startsWith("config.tc=")) {
      Serial.println("set tc config");
      String value = command.substring(10);
  
      temperatureCompensation = value.toFloat();
  
      setTemperatureCompensation(temperatureCompensation);
      bme.setTemperatureCompensation(temperatureCompensation);
    }
  }

  if (currentMillis - previousMillis > sensorInterval) {
    bme.takeForcedMeasurement();
    temperature = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure = bme.readPressure() / 100.0F;
  
    calculateAverageTemperature(temperature);
    calculateAverageHumidity(humidity);
  
    Serial.print("Temperature:" + String(temperature) + ", ");
    Serial.print("Humidity:" + String(humidity) + ", ");
    Serial.println("Pressure:" + String(pressure / 100.0F));
  
    displayClear();
    dtostrf(temperature, 2, 1, temperatureRounded);
    displayExtraLargeText(0, 15, String(temperatureRounded));
    displayNormalText(100, 0, "o");
    displaySmallText(0, 52, "Humidity: " + String(averageHumidity) + "%");
    displayDraw();
  
    previousMillis = currentMillis;
  }
}
