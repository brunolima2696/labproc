#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>

const int LDR_PIN = 4;
const int BUTTON_PIN = 5;
const int RGB_LED_PIN = 8;
const int NUM_PIXELS = 1;

const char* WIFI_SSID = "NOME_DO_SEU_WIFI";
const char* WIFI_PASSWORD = "SENHA_DO_SEU_WIFI";

const char* AP_SSID = "Monitoramento_ESP32_GRUPO_E";
const char* AP_PASSWORD = "12345678";

const unsigned long WIFI_TIMEOUT_MS = 20000;
const unsigned long WIFI_RETRY_DELAY_MS = 500;
const unsigned long LDR_LOG_INTERVAL_MS = 1000;
const unsigned long LINKS_INTERVAL_MS = 10000;
const unsigned long PEDESTRIAN_DURATION_MS = 3000;
const unsigned long BUTTON_DEBOUNCE_US = 200000;

const int LDR_LOW_LIGHT_THRESHOLD = 30;

const unsigned long GREEN_DURATION_MS = 3000;
const unsigned long YELLOW_DURATION_MS = 1000;
const unsigned long RED_DURATION_MS = 4000;

WebServer server(80);
Adafruit_NeoPixel ledBuiltin(NUM_PIXELS, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

int ldrRaw = 0;
int ldrPercent = 0;
bool lowLight = false;

volatile bool pedestrianButtonPressed = false;
volatile unsigned long lastInterruptMicros = 0;

unsigned long pedestrianEndMillis = 0;
unsigned long pedestrianCounter = 0;

unsigned long lastLdrLogMillis = 0;
unsigned long lastLinksMillis = 0;

bool normalCycleStarted = false;
unsigned long trafficStateStartMillis = 0;
int trafficState = 0;

String operationMode = "iniciando";
String ledStatus = "desligado";

const char MAIN_PAGE[] PROGMEM = R"HTML(<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <title>Semaforo com LDR e Botao</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">

  <style>
    body {
      font-family: Arial, sans-serif;
      background: #f4f4f4;
      text-align: center;
      margin: 0;
      padding: 30px;
    }

    h1 {
      color: #222;
    }

    .card {
      background: white;
      max-width: 500px;
      margin: 20px auto;
      padding: 25px;
      border-radius: 12px;
      box-shadow: 0 0 10px rgba(0,0,0,0.15);
    }

    .value {
      font-size: 28px;
      font-weight: bold;
      color: #0066cc;
    }

    .status {
      font-size: 20px;
      font-weight: bold;
      margin-top: 10px;
    }

    .normal {
      color: green;
    }

    .alerta {
      color: orange;
    }

    .sos {
      color: red;
    }

    .small {
      color: #555;
      font-size: 14px;
    }
  </style>
</head>

<body>
  <h1>Semaforo Inteligente com LDR</h1>

  <div class="card">
    <h2>Sensor LDR</h2>
    <p>Valor ADC:</p>
    <div class="value" id="ldrRaw">---</div>

    <p>Luminosidade estimada:</p>
    <div class="value" id="ldrPercent">---%</div>

    <p>Condicao:</p>
    <div class="status" id="lightStatus">---</div>
  </div>

  <div class="card">
    <h2>Botao de Travessia</h2>
    <p>Estado:</p>
    <div class="status" id="pedestrianStatus">---</div>

    <p>Total de solicitacoes:</p>
    <div class="value" id="pedestrianCounter">---</div>
  </div>

  <div class="card">
    <h2>Semaforo</h2>
    <p>Modo:</p>
    <div class="status" id="operationMode">---</div>

    <p>LED BuiltIn:</p>
    <div class="status" id="ledStatus">---</div>
  </div>

  <p class="small">Atualizacao automatica a cada 1 segundo.</p>

  <script>
    async function atualizarDados() {
      try {
        const resposta = await fetch('/dados');
        const dados = await resposta.json();

        document.getElementById('ldrRaw').textContent = dados.ldrRaw;
        document.getElementById('ldrPercent').textContent = dados.ldrPercent + '%';

        const lightStatus = document.getElementById('lightStatus');
        if (dados.lowLight) {
          lightStatus.textContent = 'BAIXA LUMINOSIDADE - MODO NOTURNO';
          lightStatus.className = 'status alerta';
        } else {
          lightStatus.textContent = 'Luminosidade normal';
          lightStatus.className = 'status normal';
        }

        const pedestrianStatus = document.getElementById('pedestrianStatus');
        if (dados.pedestrianActive) {
          pedestrianStatus.textContent = 'TRAVESSIA SOLICITADA';
          pedestrianStatus.className = 'status sos';
        } else {
          pedestrianStatus.textContent = 'Inativo';
          pedestrianStatus.className = 'status normal';
        }

        document.getElementById('pedestrianCounter').textContent = dados.pedestrianCounter;
        document.getElementById('operationMode').textContent = dados.operationMode;
        document.getElementById('ledStatus').textContent = dados.ledStatus;
      } catch (erro) {
        console.log('Erro ao atualizar dados:', erro);
      }
    }

    setInterval(atualizarDados, 1000);
    atualizarDados();
  </script>
</body>
</html>
)HTML";

void setLedColor(uint8_t red, uint8_t green, uint8_t blue) {
  static bool initialized = false;
  static uint8_t lastRed = 0;
  static uint8_t lastGreen = 0;
  static uint8_t lastBlue = 0;

  if (initialized && lastRed == red && lastGreen == green && lastBlue == blue) {
    return;
  }

  ledBuiltin.setPixelColor(0, ledBuiltin.Color(red, green, blue));
  ledBuiltin.show();

  lastRed = red;
  lastGreen = green;
  lastBlue = blue;
  initialized = true;
}

void ledOff() {
  setLedColor(0, 0, 0);
}

void ledGreen() {
  setLedColor(0, 255, 0);
}

void ledYellow() {
  setLedColor(255, 200, 0);
}

void ledRed() {
  setLedColor(255, 0, 0);
}

void readLdr() {
  ldrRaw = analogRead(LDR_PIN);
  ldrPercent = map(ldrRaw, 0, 4095, 0, 100);

  if (ldrPercent > 100) {
    ldrPercent = 100;
  } else if (ldrPercent < 0) {
    ldrPercent = 0;
  }

  lowLight = ldrPercent <= LDR_LOW_LIGHT_THRESHOLD;
}

bool pedestrianActive() {
  return millis() < pedestrianEndMillis;
}

void IRAM_ATTR handlePedestrianButtonInterrupt() {
  unsigned long now = micros();

  if (now - lastInterruptMicros > BUTTON_DEBOUNCE_US) {
    pedestrianButtonPressed = true;
    lastInterruptMicros = now;
  }
}

void handleRoot() {
  server.send(200, "text/html", MAIN_PAGE);
}

void handleDados() {
  String json = "{";
  json += "\"ldrRaw\":";
  json += String(ldrRaw);
  json += ",";
  json += "\"ldrPercent\":";
  json += String(ldrPercent);
  json += ",";
  json += "\"lowLight\":";
  json += (lowLight ? "true" : "false");
  json += ",";
  json += "\"pedestrianActive\":";
  json += (pedestrianActive() ? "true" : "false");
  json += ",";
  json += "\"pedestrianCounter\":";
  json += String(pedestrianCounter);
  json += ",";
  json += "\"operationMode\":\"";
  json += operationMode;
  json += "\",";
  json += "\"ledStatus\":\"";
  json += ledStatus;
  json += "\"";
  json += "}";

  server.send(200, "application/json", json);
}

unsigned long trafficStateDuration(int state) {
  if (state == 1) {
    return YELLOW_DURATION_MS;
  }

  if (state == 2) {
    return RED_DURATION_MS;
  }

  return GREEN_DURATION_MS;
}

void nextTrafficState(unsigned long now) {
  if (trafficState == 0) {
    trafficState = 1;
  } else if (trafficState == 1) {
    trafficState = 2;
  } else {
    trafficState = 0;
  }

  trafficStateStartMillis = now;
}

void updateNormalTrafficLight(unsigned long now) {
  if (!normalCycleStarted) {
    trafficState = 0;
    trafficStateStartMillis = now;
    normalCycleStarted = true;
  } else if (now - trafficStateStartMillis >= trafficStateDuration(trafficState)) {
    nextTrafficState(now);
  }

  operationMode = "normal";

  if (trafficState == 0) {
    ledGreen();
    ledStatus = "verde - fluxo liberado";
  } else if (trafficState == 1) {
    ledYellow();
    ledStatus = "amarelo - atencao";
  } else {
    ledRed();
    ledStatus = "vermelho - pare";
  }
}

void updateTrafficLight() {
  unsigned long now = millis();

  if (pedestrianActive()) {
    normalCycleStarted = false;
    ledRed();
    operationMode = "travessia de pedestres";
    ledStatus = "vermelho - travessia solicitada";
    return;
  }

  if (lowLight) {
    normalCycleStarted = false;
    operationMode = "noturno";

    if ((now % 1000) <= 499) {
      ledYellow();
      ledStatus = "amarelo aceso - modo noturno";
    } else {
      ledOff();
      ledStatus = "amarelo apagado - modo noturno";
    }

    return;
  }

  updateNormalTrafficLight(now);
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);

  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.println();
  Serial.println("Nao foi possivel conectar ao Wi-Fi configurado.");

  if (apStarted) {
    Serial.println("Modo Access Point iniciado.");
    Serial.print("SSID: ");
    Serial.println(AP_SSID);
    Serial.print("IP do AP: ");
    Serial.println(WiFi.softAPIP());
  }
}

void printAccessLinks() {
  Serial.println();
  Serial.println("===== LINKS DE ACESSO =====");

  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    Serial.print("Rede criada pelo ESP32: ");
    Serial.println(AP_SSID);
    Serial.print("Acesse: http://");
    Serial.println(WiFi.softAPIP());
    Serial.print("Endpoint JSON: http://");
    Serial.print(WiFi.softAPIP());
    Serial.println("/dados");
  } else {
    Serial.print("Wi-Fi conectado: ");
    Serial.println(WIFI_SSID);
    Serial.print("IP do ESP32: ");
    Serial.println(WiFi.localIP());
    Serial.print("Endpoint JSON: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/dados");
  }

  Serial.println("===========================");
}

void connectWifi() {
  WiFi.mode(WIFI_STA);

  Serial.print("Conectando ao Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
    delay(WIFI_RETRY_DELAY_MS);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("Wi-Fi conectado com sucesso.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("Mascara: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    startAccessPoint();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("--- Iniciando Semaforo Inteligente com LDR e Botao ---");

  ledBuiltin.begin();
  ledBuiltin.setBrightness(80);
  ledOff();

  connectWifi();

  analogReadResolution(12);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handlePedestrianButtonInterrupt, FALLING);

  server.on("/", handleRoot);
  server.on("/dados", handleDados);
  server.begin();

  Serial.println("Servidor HTTP iniciado.");
  printAccessLinks();

  readLdr();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();

  if (pedestrianButtonPressed) {
    noInterrupts();
    pedestrianButtonPressed = false;
    interrupts();

    pedestrianCounter++;
    pedestrianEndMillis = now + PEDESTRIAN_DURATION_MS;

    Serial.println();
    Serial.println("===== TRAVESSIA SOLICITADA =====");
    Serial.println("Botao de travessia detectado por interrupcao.");
    Serial.print("LED vermelho ativo por ");
    Serial.print(3);
    Serial.println(" segundos.");
    Serial.print("Total de solicitacoes: ");
    Serial.println(pedestrianCounter);
    Serial.println("================================");
  }

  if (now - lastLdrLogMillis > 999) {
    lastLdrLogMillis = now;
    readLdr();

    Serial.print("ADC LDR: ");
    Serial.print(ldrRaw);
    Serial.print(" | Luminosidade: ");
    Serial.print(ldrPercent);
    Serial.print("% | Modo noturno: ");
    Serial.print(lowLight ? "SIM" : "NAO");
    Serial.print(" | Travessia ativa: ");
    Serial.println(pedestrianActive() ? "SIM" : "NAO");
  }

  if (now - lastLinksMillis > 9999) {
    lastLinksMillis = now;
    printAccessLinks();
  }

  updateTrafficLight();
}
