#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <EEPROM.h>

// Definições dos pinos
#define BUZZER D0
#define Channel_1 D1
#define Channel_2 D2
#define Channel_3 D3
#define Channel_4 D4
#define LED_BLUE_PIN D5
#define LED_RGB_RED_PIN D6
#define LED_RGB_GREEN_PIN D7
#define LED_RGB_BLUE_PIN D8
#define THERMISTOR_PIN A0

// Instâncias do servidor e WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

Ticker ticker;

bool buzzer_on = false;
bool blue_led_on = false;

// Constantes para o termistor
const double dVCC = 3.3;             // NodeMCU on board 3.3v vcc
const double dR2 = 10000;            // 10k ohm series resistor
const double dAdcResolution = 1023;  // 10-bit adc
const double dA = 0.001129148;       // thermistor equation parameters
const double dB = 0.000234125;
const double dC = 0.0000000876741;

// Funções para salvar e carregar credenciais da EEPROM
void saveCredentials(const char *ssid, const char *password) {
  EEPROM.begin(64);
  for (int i = 0; i < 32; ++i) {
    EEPROM.write(i, ssid[i]);
    EEPROM.write(32 + i, password[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

void loadCredentials(char *ssid, char *password) {
  EEPROM.begin(64);
  for (int i = 0; i < 32; ++i) {
    ssid[i] = EEPROM.read(i);
    password[i] = EEPROM.read(32 + i);
  }
  EEPROM.end();
}

// Função para manipular eventos WebSocket
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {

    Serial.println("Cliente conectado");
    client->text("Conexão bem-sucedida");

  } else if (type == WS_EVT_DISCONNECT) {

    Serial.println("Cliente desconectado");

  } else if (type == WS_EVT_DATA) {

    Serial.println("Dados recebidos:");
    for (size_t i = 0; i < len; i++) {
      Serial.print((char)data[i]);
    }
    Serial.println();

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {

      Serial.print(F("deserializeJson() falhou: "));
      Serial.println(error.f_str());

      return;
    }

    JsonObject command = doc.as<JsonObject>();
    handleCommands(command, client);
  }
}

void buzzer_alarm() {
  tone(BUZZER, 500);
  delay(500);
  tone(BUZZER, 1500);
  delay(500);
}

// Função para manipular comandos recebidos
void handleCommands(JsonObject command, AsyncWebSocketClient *client) {
  const char *cmd = command["command"];

  if (strcmp(cmd, "setLED") == 0) {

    int red = command["red"];
    int green = command["green"];
    int blue = command["blue"];

    analogWrite(LED_RGB_RED_PIN, red);
    analogWrite(LED_RGB_GREEN_PIN, green);
    analogWrite(LED_RGB_BLUE_PIN, blue);

    client->text("LED RGB atualizado");

  } else if (strcmp(cmd, "setBlueLED") == 0) {
    blue_led_on = command["state"];
    Serial.println("LED azul atualizado");

    // Cria um JSON para enviar de volta ao cliente
    DynamicJsonDocument responseDoc(200);
    responseDoc["status"] = "LED azul atualizado";
    String response;
    serializeJson(responseDoc, response);

    client->text(response);

  } else if (strcmp(cmd, "setAlarm") == 0) {
    buzzer_on = command["state"];
    Serial.println("Alarme atualizado");

    // Cria um JSON para enviar de volta ao cliente
    DynamicJsonDocument responseDoc(200);
    responseDoc["status"] = "Alarme atualizado";
    String response;
    serializeJson(responseDoc, response);

    client->text(response);
  }
}

// Função para enviar temperatura
void sendTemperature() {
  double dVout, dRth, dTemperature, dAdcValue;

  dAdcValue = analogRead(THERMISTOR_PIN);
  dVout = (dAdcValue * dVCC) / dAdcResolution;
  dRth = (dVCC * dR2 / dVout) - dR2;

  // Steinhart-Hart Thermistor Equation:
  // Temperature in Kelvin = 1 / (A + B[ln(R)] + C[ln(R)]^3)
  // where A = 0.001129148, B = 0.000234125 and C = 8.76741*10^-8
  // Temperature in kelvin
  dTemperature = 1 / (dA + (dB * log(dRth)) + (dC * pow(log(dRth), 3)));

  // Temperature in degree celsius
  dTemperature = dTemperature - 273.15;

  DynamicJsonDocument doc(200);
  doc["temperature"] = dTemperature;

  String output;
  serializeJson(doc, output);

  ws.textAll(output);
}

void clear_EEPROM_MEMORY() {
  // Inicializa a EEPROM
  EEPROM.begin(512);  // Tamanho da EEPROM é 512 bytes

  // Limpa a EEPROM
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0);  // Define todos os bytes como 0
  }
  EEPROM.commit();  // Certifique-se de que as alterações sejam salvas

  Serial.println("EEPROM limpa!");
}

void setup() {
  Serial.begin(115200);

  // Ative em caso de testes, caso a memoria EEPROM ñ seja limpa ou não utilize o EEPROM.end(); para liberar a memoria apos o uso pode aver problemas a longo prazo ou durante os testes ja que a memoria EEPROM só possui 512 bytes
  // clear_EEPROM_MEMORY()

  pinMode(LED_BLUE_PIN, OUTPUT);
  pinMode(LED_RGB_RED_PIN, OUTPUT);
  pinMode(LED_RGB_GREEN_PIN, OUTPUT);
  pinMode(LED_RGB_BLUE_PIN, OUTPUT);

  digitalWrite(LED_BLUE_PIN, LOW);
  digitalWrite(LED_RGB_RED_PIN, LOW);
  digitalWrite(LED_RGB_GREEN_PIN, LOW);
  digitalWrite(LED_RGB_BLUE_PIN, LOW);

  char ssid[32];
  char password[32];
  loadCredentials(ssid, password);

  WiFi.begin(ssid, password);
  unsigned long startTime = millis();

  Serial.println("");

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conectado ao Wi-Fi");
    Serial.print("Endereço IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" ");
    Serial.println("Falha na conexão. Iniciando modo de configuração de rede...");
    Serial.println("Conecte-se a rede ESP8266_Config e sete o SSID e SENHA")
    WiFi.softAP("ESP8266_Config");
    Serial.print("Endereço IP do para conexão: ");
    Serial.println(WiFi.softAPIP());

    // Configuração do servidor HTTP para configuração de SSID e senha
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      String html = "<html><body><form action='/save' method='post'>";
      html += "SSID: <input type='text' name='ssid'><br>";
      html += "Password: <input type='text' name='password'><br>";
      html += "<input type='submit' value='Save'>";
      html += "</form></body></html>";
      request->send(200, "text/html", html);
    });

    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
      String new_ssid = request->arg("ssid");
      String new_password = request->arg("password");
      saveCredentials(new_ssid.c_str(), new_password.c_str());
      request->send(200, "text/html", "Configurações salvas. Reiniciando...");
      delay(1000);
      ESP.restart();
    });
  }

  // Configuração do WebSocket
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  // Inicia o servidor
  server.begin();
  Serial.println("Servidor HTTP iniciado");

  // Configuração do ticker para envio periódico de temperatura
  ticker.attach(1, sendTemperature);  // Enviar dados de temperatura a cada 5 segundos
}

void loop() {
  ws.cleanupClients();

  digitalWrite(LED_BLUE_PIN, blue_led_on ? HIGH : LOW);

  if (buzzer_on) {
    buzzer_alarm();
  } else {
    noTone(BUZZER);
  }
}
