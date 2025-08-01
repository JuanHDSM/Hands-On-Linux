#include <DHT.h>

// Defina os pinos de LED e LDR
int ledPin = 22;
int ldrPin = 2;
int ldrMax = 4000;  // valor máximo calibrado do LDR
int ledValue = 10;  // valor de 0 a 100

#define DHTTYPE DHT11
#define DHTPIN 4

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  dht.begin();
  Serial.begin(9600);
  pinMode(ledPin, OUTPUT);
  pinMode(ldrPin, INPUT);
  Serial.printf("SmartLamp Initialized.\n");
  ledUpdate();
}

void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    processCommand(command);
  }
  delay(100);
}

void processCommand(String command) {
  command.trim();

  if (command == "GET_LDR") {
    int value = ldrGetValue();
    Serial.printf("RES GET_LDR %d\n", value);
  }
  else if (command == "GET_LED") {
    int pwmValue = map(ledValue, 0, 100, 0, 255);
    Serial.printf("RES GET_LED %d\n", pwmValue);
  }
  else if (command.startsWith("SET_LED ")) {
    String valueStr = command.substring(8);
    int value = valueStr.toInt();

    if (value >= 0 && value <= 100) {
      ledValue = value;
      ledUpdate();
      Serial.println("RES SET_LED 1");
    } else {
      Serial.println("RES SET_LED -1");
    }
  }
  else if (command == "GET_TEMP") {
    float temperatura = dht.readTemperature();
    if (isnan(temperatura)) {
      Serial.println("ERR GET_TEMP Sensor error");
    } else {
      Serial.printf("RES GET_TEMP %.1f\n", temperatura);  // °C
    }
  }
  else if (command == "GET_HUM") {
    float umidade = dht.readHumidity();
    if (isnan(umidade)) {
      Serial.println("ERR GET_HUM Sensor error");
    } else {
      Serial.printf("RES GET_HUM %.1f\n", umidade);  // %
    }
  }
  else {
    Serial.println("ERR Unknown command.");
  }
}

void ledUpdate() {
  int pwmValue = map(ledValue, 0, 100, 0, 255);
  analogWrite(ledPin, pwmValue);
}

int ldrGetValue() {
  int raw = analogRead(ldrPin);
  if (ldrMax <= 0) ldrMax = 4000;
  int percent = map(raw, 0, ldrMax, 0, 100);
  return constrain(percent, 0, 100);
}
