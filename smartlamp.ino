// Defina os pinos de LED e LDR
int ledPin = 2;
int ldrPin = 34;
int ldrMax = 4000;  // valor máximo calibrado do LDR
int ledValue = 10;  // valor de 0 a 100

void setup() {
  Serial.begin(9600);
  pinMode(ledPin, OUTPUT);
  pinMode(ldrPin, INPUT);
  Serial.printf("SmartLamp Initialized.\n");
  ledUpdate();
}

void loop() {
  int num =0;
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    processCommand(command);
    
  }
  delay(100);
  num+=100;

  if(num%1000){
   Serial.printf("ta conectado e funcionando :) \n");

    }
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