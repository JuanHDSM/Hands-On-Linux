// Defina os pinos de LED e LDR
// Defina uma variável com valor máximo do LDR (4000)
// Defina uma variável para guardar o valor atual do LED (10)
int ledPin = ; //Definir Pino
int ledValue = 0; 

int ldrPin = A0; //Definir Pino
// Faça testes no sensor ldr para encontrar o valor maximo e atribua a variável ldrMax
int ldrMax = 0;

void setup() {
    Serial.begin(9600);
    
    pinMode(ledPin, OUTPUT);
    pinMode(ldrPin, INPUT);
    
    Serial.print("SmartLamp Initialized.\n");


}

// Função loop será executada infinitamente pelo ESP32
void loop() {
      String cmd = Serial.readString();  // Lê string da porta serial
      processCommand(cmd);  
}


void processCommand(String command) {
    // compare o comando com os comandos possíveis e execute a ação correspondente    
    
    if(command.equals("SET_LED")){
        // Extrair o valor 
        ledUpdate(0)
        
    }

    if (command.equals("GET_LED")) { // Retornar a intensidade atual do LED
          Serial.printf("RES GET_LED ");
          Serial.println(ledValue);
    } 

    if (command.equals("GET_LDR")) { 

          int ldrValue = ldrGetValue();
          Serial.printf("RES GET_LDR ");
          Serial.println(ldrValue);
    } 
   
}

// Função para atualizar o valor do LED
void ledUpdate(int newvalue) {
    // Valor deve convertar o valor recebido pelo comando SET_LED para 0 e 255
    // Normalize o valor do LED antes de enviar para a porta correspondente
    int ledNewValue = (newvalue * 255) / 100;
    analogWrite(ledPin, ledNewValue);

}

// Função para ler o valor do LDR
int ldrGetValue() {
    // Leia o sensor LDR e retorne o valor normalizado entre 0 e 100
    // faça testes para encontrar o valor maximo do ldr (exemplo: aponte a lanterna do celular para o sensor)       
    // Atribua o valor para a variável ldrMax e utilize esse valor para a normalização

    int lrdValue = analogRead(ldrPin);

    lrdValue = constrain(lrdValue, 0, ldrMax);
    
    // Normaliza para 0–100 
    int normalized = (lrdValue * 100) / ldrMax;

    return normalized;
  
}