/*
  PCS3732 - Experimento PWM com ESP32-C3
  Controle de intensidade de LED externo e posição de servomotor via interface web.

  O erro original ocorria porque as funções montarPagina() e handleRoot()
  estavam sendo chamadas antes de serem declaradas/definidas. Este arquivo
  já possui os protótipos e todas as funções necessárias.

  Ligações sugeridas:
    LED externo: GPIO 4 -> resistor 220/330 ohm -> anodo do LED; catodo -> GND
    Servo: sinal -> GPIO 5; VCC -> 5 V externo ou 5 V da placa; GND em comum com ESP32

  Observação: ajuste PIN_LED_PWM e PIN_SERVO_PWM conforme a montagem da sua placa.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#if __has_include(<esp_arduino_version.h>)
  #include <esp_arduino_version.h>
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
  #define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// ===================== CONFIGURAÇÃO DE HARDWARE =====================
const uint8_t PIN_LED_PWM   = 4;   // Pino do LED externo
const uint8_t PIN_SERVO_PWM = 5;   // Pino de sinal do servomotor

// Se o LED ficar invertido, troque para false.
const bool LED_ATIVO_ALTO = true;

// PWM do LED
const uint8_t  LED_RES_BITS = 10;        // 0 a 1023
const uint16_t LED_FREQ_MIN = 5;        // Hz
const uint16_t LED_FREQ_MAX = 500;      // Hz

// PWM do servo
const uint8_t  SERVO_RES_BITS = 14;      // boa precisão e compatível com ESP32-C3
const uint16_t SERVO_FREQ_HZ  = 50;      // servo padrão usa período de 20 ms
const uint16_t SERVO_MIN_US   = 500;     // calibrar conforme o servo
const uint16_t SERVO_MAX_US   = 2400;    // calibrar conforme o servo

// Canais usados apenas no Arduino-ESP32 core 2.x.
// Usamos canais afastados para evitar compartilhamento de timer entre LED e servo.
#if ESP_ARDUINO_VERSION_MAJOR < 3
const uint8_t LED_PWM_CHANNEL   = 0;
const uint8_t SERVO_PWM_CHANNEL = 2;
#endif

// ===================== CONFIGURAÇÃO DE REDE =====================
const char* SSID_AP = "ESP32_PWM_LED_SERVO";
const char* PASS_AP = "12345678";       // mínimo de 8 caracteres

WebServer server(80);

// ===================== ESTADO ATUAL DO SISTEMA =====================
int brilhoAtual = 50;     // 0 a 100 %
int freqAtual   = 1000;   // Hz
int anguloAtual = 90;     // 0 a 180 graus
int pulsoServoAtual = 1500;

// ===================== PROTÓTIPOS: CORRIGEM O ERRO DE ESCOPO =====================
String montarPagina(int brilho, int freq, int angulo);
String montarJsonEstado();

void handleRoot();
void handleControl();
void handleSet();
void handleState();
void handleNotFound();

void configurarPwmInicial();
void aplicarParametrosAtuais();
void aplicarLed(int brilhoPercentual, int frequenciaHz);
void aplicarServo(int anguloGraus);
void lerParametrosDaRequisicao();
uint32_t dutyMax(uint8_t resolucaoBits);

// ===================== FUNÇÕES AUXILIARES DE PWM =====================
uint32_t dutyMax(uint8_t resolucaoBits) {
  return (1UL << resolucaoBits) - 1UL;
}

void configurarPwmInicial() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_LED_PWM, freqAtual, LED_RES_BITS);
  ledcAttach(PIN_SERVO_PWM, SERVO_FREQ_HZ, SERVO_RES_BITS);
#else
  ledcSetup(LED_PWM_CHANNEL, freqAtual, LED_RES_BITS);
  ledcAttachPin(PIN_LED_PWM, LED_PWM_CHANNEL);

  ledcSetup(SERVO_PWM_CHANNEL, SERVO_FREQ_HZ, SERVO_RES_BITS);
  ledcAttachPin(PIN_SERVO_PWM, SERVO_PWM_CHANNEL);
#endif

  aplicarParametrosAtuais();
}

void aplicarParametrosAtuais() {
  aplicarLed(brilhoAtual, freqAtual);
  aplicarServo(anguloAtual);
}

void aplicarLed(int brilhoPercentual, int frequenciaHz) {
  brilhoAtual = constrain(brilhoPercentual, 0, 100);
  freqAtual = constrain(frequenciaHz, LED_FREQ_MIN, LED_FREQ_MAX);

  const uint32_t maxDuty = dutyMax(LED_RES_BITS);
  uint32_t duty = (uint32_t)((brilhoAtual * maxDuty) / 100UL);

  if (!LED_ATIVO_ALTO) {
    duty = maxDuty - duty;
  }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcChangeFrequency(PIN_LED_PWM, freqAtual, LED_RES_BITS);
  ledcWrite(PIN_LED_PWM, duty);
#else
  ledcSetup(LED_PWM_CHANNEL, freqAtual, LED_RES_BITS);
  ledcWrite(LED_PWM_CHANNEL, duty);
#endif
}

void aplicarServo(int anguloGraus) {
  anguloAtual = constrain(anguloGraus, 0, 180);
  pulsoServoAtual = map(anguloAtual, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
  pulsoServoAtual = constrain(pulsoServoAtual, SERVO_MIN_US, SERVO_MAX_US);

  const uint32_t maxDuty = dutyMax(SERVO_RES_BITS);
  const uint32_t periodoUs = 1000000UL / SERVO_FREQ_HZ; // 20000 us em 50 Hz
  uint32_t duty = (uint32_t)(((uint64_t)pulsoServoAtual * maxDuty) / periodoUs);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_SERVO_PWM, duty);
#else
  ledcWrite(SERVO_PWM_CHANNEL, duty);
#endif
}

void lerParametrosDaRequisicao() {
  int novoBrilho = brilhoAtual;
  int novaFreq = freqAtual;
  int novoAngulo = anguloAtual;

  if (server.hasArg("brilho")) {
    novoBrilho = server.arg("brilho").toInt();
  }
  if (server.hasArg("freq")) {
    novaFreq = server.arg("freq").toInt();
  }
  if (server.hasArg("angulo")) {
    novoAngulo = server.arg("angulo").toInt();
  }

  aplicarLed(novoBrilho, novaFreq);
  aplicarServo(novoAngulo);

  Serial.print("Brilho: ");
  Serial.print(brilhoAtual);
  Serial.print("% | Frequencia LED: ");
  Serial.print(freqAtual);
  Serial.print(" Hz | Servo: ");
  Serial.print(anguloAtual);
  Serial.print(" graus | Pulso: ");
  Serial.print(pulsoServoAtual);
  Serial.println(" us");
}

// ===================== ROTAS HTTP =====================
void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", montarPagina(brilhoAtual, freqAtual, anguloAtual));
}

// Rota compatível com formulário HTML tradicional.
void handleControl() {
  lerParametrosDaRequisicao();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", montarPagina(brilhoAtual, freqAtual, anguloAtual));
}

// Rota usada pela interface via fetch(), sem recarregar a página.
void handleSet() {
  lerParametrosDaRequisicao();
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", montarJsonEstado());
}

void handleState() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", montarJsonEstado());
}

void handleNotFound() {
  server.send(404, "text/plain", "Rota nao encontrada. Acesse http://192.168.4.1/");
}

String montarJsonEstado() {
  String json = "{";
  json += "\"brilho\":";
  json += String(brilhoAtual);
  json += ",";
  json += "\"freq\":";
  json += String(freqAtual);
  json += ",";
  json += "\"angulo\":";
  json += String(anguloAtual);
  json += ",";
  json += "\"pulso_us\":";
  json += String(pulsoServoAtual);
  json += ",";
  json += "\"pin_led\":";
  json += String(PIN_LED_PWM);
  json += ",";
  json += "\"pin_servo\":";
  json += String(PIN_SERVO_PWM);
  json += "}";
  return json;
}

// ===================== PÁGINA WEB RESPONSIVA =====================
String montarPagina(int brilho, int freq, int angulo) {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <meta name="theme-color" content="#0f172a">
  <title>ESP32-C3 PWM</title>
  <style>
    :root {
      --bg: #0f172a;
      --card: #111827;
      --card2: #1f2937;
      --text: #f8fafc;
      --muted: #cbd5e1;
      --line: rgba(255,255,255,.12);
      --accent: #38bdf8;
      --accent2: #22c55e;
      --warn: #f59e0b;
      --danger: #ef4444;
      --shadow: 0 18px 50px rgba(0,0,0,.38);
      --radius: 24px;
    }

    * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }

    html, body {
      margin: 0;
      min-height: 100%;
      background:
        radial-gradient(circle at top left, rgba(56,189,248,.22), transparent 34%),
        radial-gradient(circle at bottom right, rgba(34,197,94,.14), transparent 32%),
        var(--bg);
      color: var(--text);
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      touch-action: manipulation;
    }

    body {
      padding: 18px;
      display: flex;
      justify-content: center;
    }

    .app {
      width: 100%;
      max-width: 620px;
      padding-bottom: 28px;
    }

    .hero {
      padding: 24px 20px 18px;
      border-radius: 28px;
      background: linear-gradient(135deg, rgba(56,189,248,.22), rgba(34,197,94,.12));
      border: 1px solid var(--line);
      box-shadow: var(--shadow);
      margin-bottom: 16px;
    }

    .topline {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      margin-bottom: 14px;
    }

    .badge {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 8px 12px;
      border-radius: 999px;
      background: rgba(15,23,42,.72);
      border: 1px solid var(--line);
      color: var(--muted);
      font-size: 13px;
      font-weight: 700;
      white-space: nowrap;
    }

    .dot {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background: var(--accent2);
      box-shadow: 0 0 18px var(--accent2);
    }

    h1 {
      margin: 0;
      font-size: clamp(28px, 7vw, 42px);
      line-height: 1.02;
      letter-spacing: -.04em;
    }

    .subtitle {
      margin: 10px 0 0;
      color: var(--muted);
      font-size: 15px;
      line-height: 1.45;
    }

    .grid {
      display: grid;
      gap: 16px;
    }

    .card {
      background: rgba(17,24,39,.92);
      border: 1px solid var(--line);
      border-radius: var(--radius);
      padding: 18px;
      box-shadow: var(--shadow);
      backdrop-filter: blur(12px);
    }

    .card h2 {
      margin: 0 0 8px;
      font-size: 20px;
      letter-spacing: -.02em;
    }

    .help {
      margin: 0 0 16px;
      color: var(--muted);
      font-size: 14px;
      line-height: 1.45;
    }

    .value-row {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 12px;
      margin: 12px 0 10px;
    }

    .big-value {
      font-size: clamp(34px, 9vw, 52px);
      line-height: 1;
      font-weight: 900;
      letter-spacing: -.04em;
    }

    .unit {
      color: var(--muted);
      font-weight: 800;
      font-size: 15px;
    }

    input[type="range"] {
      width: 100%;
      height: 42px;
      margin: 8px 0 10px;
      accent-color: var(--accent);
    }

    .number-line {
      display: grid;
      grid-template-columns: 1fr auto;
      gap: 10px;
      align-items: center;
      margin-top: 10px;
    }

    input[type="number"] {
      width: 100%;
      border: 1px solid var(--line);
      border-radius: 16px;
      background: rgba(15,23,42,.86);
      color: var(--text);
      padding: 15px 14px;
      font-size: 18px;
      font-weight: 800;
      outline: none;
    }

    .preset-grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 10px;
      margin-top: 12px;
    }

    .preset-grid.five {
      grid-template-columns: repeat(5, minmax(0, 1fr));
    }

    button {
      border: 0;
      border-radius: 16px;
      padding: 14px 10px;
      min-height: 48px;
      background: var(--card2);
      color: var(--text);
      font-weight: 900;
      font-size: 15px;
      cursor: pointer;
      touch-action: manipulation;
    }

    button:active { transform: scale(.98); }

    .primary {
      width: 100%;
      margin-top: 16px;
      min-height: 58px;
      font-size: 17px;
      background: linear-gradient(135deg, var(--accent), #2563eb);
      color: #00111f;
    }

    .servo-visual {
      display: grid;
      place-items: center;
      margin: 18px 0 8px;
    }

    .dial {
      width: min(72vw, 260px);
      aspect-ratio: 1;
      border-radius: 50%;
      border: 12px solid rgba(255,255,255,.08);
      background: radial-gradient(circle, rgba(255,255,255,.08), rgba(255,255,255,.02));
      position: relative;
      box-shadow: inset 0 0 40px rgba(0,0,0,.28);
    }

    .needle {
      position: absolute;
      width: 44%;
      height: 8px;
      background: linear-gradient(90deg, var(--warn), #fde68a);
      border-radius: 999px;
      top: calc(50% - 4px);
      left: 50%;
      transform-origin: left center;
      transform: rotate(calc(-90deg + var(--ang) * 1deg));
      box-shadow: 0 0 20px rgba(245,158,11,.45);
    }

    .center-dot {
      position: absolute;
      width: 30px;
      height: 30px;
      border-radius: 50%;
      background: var(--text);
      top: calc(50% - 15px);
      left: calc(50% - 15px);
      box-shadow: 0 0 20px rgba(255,255,255,.20);
    }

    .status {
      position: sticky;
      bottom: 12px;
      margin-top: 16px;
      padding: 14px 16px;
      border-radius: 18px;
      background: rgba(15,23,42,.92);
      border: 1px solid var(--line);
      box-shadow: var(--shadow);
      color: var(--muted);
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
      font-size: 14px;
      backdrop-filter: blur(12px);
    }

    .status strong { color: var(--text); }

    .mini {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 10px;
      margin-top: 12px;
    }

    .mini div {
      padding: 12px;
      border-radius: 16px;
      background: rgba(15,23,42,.70);
      border: 1px solid var(--line);
    }

    .mini span {
      display: block;
      color: var(--muted);
      font-size: 12px;
      font-weight: 700;
      margin-bottom: 4px;
    }

    .mini b {
      font-size: 16px;
    }

    @media (max-width: 420px) {
      body { padding: 12px; }
      .hero { padding: 20px 16px 16px; }
      .card { padding: 16px; border-radius: 22px; }
      .preset-grid.five { grid-template-columns: repeat(3, minmax(0, 1fr)); }
      .mini { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <main class="app">
    <section class="hero">
      <div class="topline">
        <span class="badge"><span class="dot"></span> ESP32-C3 online</span>
        <span class="badge">IP: %IP%</span>
      </div>
      <h1>Controle PWM</h1>
      <p class="subtitle">Controle de intensidade do LED externo e posição do servomotor por uma interface web otimizada para celular.</p>
    </section>

    <form class="grid" action="/control" method="get" id="controleForm">
      <section class="card">
        <h2>LED externo</h2>
        <p class="help">Ajuste a razão cíclica do PWM. Quanto maior o duty cycle, maior a intensidade luminosa percebida.</p>

        <div class="value-row">
          <div class="big-value"><span id="brilhoValor">%BRILHO%</span><span class="unit">%</span></div>
          <span class="badge">GPIO %PIN_LED%</span>
        </div>

        <input id="brilho" name="brilho" type="range" min="0" max="100" step="1" value="%BRILHO%" oninput="atualizarTela(); agendarEnvio();">

        <div class="preset-grid">
          <button type="button" onclick="presetBrilho(0)">0%</button>
          <button type="button" onclick="presetBrilho(25)">25%</button>
          <button type="button" onclick="presetBrilho(50)">50%</button>
          <button type="button" onclick="presetBrilho(75)">75%</button>
          <button type="button" onclick="presetBrilho(100)">100%</button>
          <button type="button" onclick="piscarTeste()">Teste</button>
        </div>
      </section>

      <section class="card">
        <h2>Frequência do LED</h2>
        <p class="help">Use frequências baixas para observar piscamento e frequências altas para brilho visualmente contínuo.</p>

        <div class="value-row">
          <div class="big-value"><span id="freqValor">%FREQ%</span><span class="unit"> Hz</span></div>
        </div>

        <div class="number-line">
          <input id="freq" name="freq" type="number" min="5" max="5000" step="10" value="%FREQ%" oninput="corrigirFreq(); atualizarTela(); agendarEnvio();">
          <span class="badge">5 a 5000 Hz</span>
        </div>

        <div class="preset-grid five">
          <button type="button" onclick="presetFreq(5)">5</button>
          <button type="button" onclick="presetFreq(100)">100</button>
          <button type="button" onclick="presetFreq(500)">500</button>
          <button type="button" onclick="presetFreq(1000)">1k</button>
          <button type="button" onclick="presetFreq(5000)">5k</button>
        </div>
      </section>

      <section class="card">
        <h2>Servomotor</h2>
        <p class="help">O servo usa PWM de 50 Hz. O ângulo selecionado é convertido em largura de pulso entre %SERVO_MIN% µs e %SERVO_MAX% µs.</p>

        <div class="value-row">
          <div class="big-value"><span id="anguloValor">%ANGULO%</span><span class="unit">°</span></div>
          <span class="badge">GPIO %PIN_SERVO%</span>
        </div>

        <div class="servo-visual">
          <div class="dial" id="dial" style="--ang:%ANGULO%">
            <div class="needle"></div>
            <div class="center-dot"></div>
          </div>
        </div>

        <input id="angulo" name="angulo" type="range" min="0" max="180" step="1" value="%ANGULO%" oninput="atualizarTela(); agendarEnvio();">

        <div class="preset-grid five">
          <button type="button" onclick="presetAngulo(0)">0°</button>
          <button type="button" onclick="presetAngulo(45)">45°</button>
          <button type="button" onclick="presetAngulo(90)">90°</button>
          <button type="button" onclick="presetAngulo(135)">135°</button>
          <button type="button" onclick="presetAngulo(180)">180°</button>
        </div>
      </section>

      <section class="card">
        <h2>Estado atual</h2>
        <div class="mini">
          <div><span>Brilho</span><b id="resBrilho">%BRILHO%%</b></div>
          <div><span>Frequência LED</span><b id="resFreq">%FREQ% Hz</b></div>
          <div><span>Servo</span><b id="resAngulo">%ANGULO%°</b></div>
        </div>
        <button class="primary" type="submit">Aplicar agora</button>
      </section>
    </form>

    <div class="status" id="statusBox">
      <span id="statusTexto">Pronto para controlar o experimento.</span>
      <strong id="pulsoTexto">%PULSO% µs</strong>
    </div>
  </main>

  <script>
    const brilho = document.getElementById('brilho');
    const freq = document.getElementById('freq');
    const angulo = document.getElementById('angulo');
    const statusTexto = document.getElementById('statusTexto');
    const pulsoTexto = document.getElementById('pulsoTexto');
    let timerEnvio = null;
    let enviando = false;

    function limitar(valor, min, max) {
      valor = Number(valor);
      if (Number.isNaN(valor)) return min;
      return Math.max(min, Math.min(max, valor));
    }

    function pulsoServoEstimado(ang) {
      const min = %SERVO_MIN%;
      const max = %SERVO_MAX%;
      return Math.round(min + (limitar(ang, 0, 180) * (max - min) / 180));
    }

    function corrigirFreq() {
      if (freq.value === '') return;
      freq.value = limitar(freq.value, 5, 5000);
    }

    function atualizarTela() {
      corrigirFreq();
      const b = limitar(brilho.value, 0, 100);
      const f = limitar(freq.value, 5, 5000);
      const a = limitar(angulo.value, 0, 180);

      document.getElementById('brilhoValor').textContent = b;
      document.getElementById('freqValor').textContent = f;
      document.getElementById('anguloValor').textContent = a;
      document.getElementById('resBrilho').textContent = b + '%';
      document.getElementById('resFreq').textContent = f + ' Hz';
      document.getElementById('resAngulo').textContent = a + '°';
      document.getElementById('dial').style.setProperty('--ang', a);
      pulsoTexto.textContent = pulsoServoEstimado(a) + ' µs';
    }

    function montarUrl() {
      return `/set?brilho=${encodeURIComponent(brilho.value)}&freq=${encodeURIComponent(freq.value)}&angulo=${encodeURIComponent(angulo.value)}`;
    }

    function agendarEnvio() {
      clearTimeout(timerEnvio);
      statusTexto.textContent = 'Ajustando...';
      timerEnvio = setTimeout(enviar, 220);
    }

    async function enviar() {
      if (enviando) return;
      enviando = true;
      try {
        const resposta = await fetch(montarUrl(), { cache: 'no-store' });
        const dados = await resposta.json();
        statusTexto.textContent = 'Aplicado no ESP32-C3';
        pulsoTexto.textContent = dados.pulso_us + ' µs';
      } catch (erro) {
        statusTexto.textContent = 'Falha de comunicação. Verifique o Wi-Fi do ESP32.';
      } finally {
        enviando = false;
      }
    }

    function presetBrilho(v) {
      brilho.value = v;
      atualizarTela();
      agendarEnvio();
    }

    function presetFreq(v) {
      freq.value = v;
      atualizarTela();
      agendarEnvio();
    }

    function presetAngulo(v) {
      angulo.value = v;
      atualizarTela();
      agendarEnvio();
    }

    async function piscarTeste() {
      const original = brilho.value;
      presetBrilho(100);
      await new Promise(r => setTimeout(r, 350));
      presetBrilho(0);
      await new Promise(r => setTimeout(r, 350));
      presetBrilho(original);
    }

    document.getElementById('controleForm').addEventListener('submit', function() {
      atualizarTela();
    });

    atualizarTela();
  </script>
</body>
</html>
)rawliteral";

  html.replace("%BRILHO%", String(brilho));
  html.replace("%FREQ%", String(freq));
  html.replace("%ANGULO%", String(angulo));
  html.replace("%PULSO%", String(pulsoServoAtual));
  html.replace("%IP%", WiFi.softAPIP().toString());
  html.replace("%PIN_LED%", String(PIN_LED_PWM));
  html.replace("%PIN_SERVO%", String(PIN_SERVO_PWM));
  html.replace("%SERVO_MIN%", String(SERVO_MIN_US));
  html.replace("%SERVO_MAX%", String(SERVO_MAX_US));

  return html;
}

// ===================== SETUP E LOOP =====================
void setup() {
  Serial.begin(115200);
  delay(400);

  configurarPwmInicial();

  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(SSID_AP, PASS_AP);

  Serial.println();
  Serial.println("====================================");
  Serial.println("ESP32-C3 - PWM LED + Servo");
  Serial.print("Access Point: ");
  Serial.println(apOk ? "criado" : "falhou");
  Serial.print("SSID: ");
  Serial.println(SSID_AP);
  Serial.print("Senha: ");
  Serial.println(PASS_AP);
  Serial.print("Acesse: http://");
  Serial.println(WiFi.softAPIP());
  Serial.println("====================================");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/set", HTTP_GET, handleSet);
  server.on("/state", HTTP_GET, handleState);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Servidor HTTP iniciado.");
}

void loop() {
  server.handleClient();
}
