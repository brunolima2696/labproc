#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <stdint.h>

/*
  PCS3732 - Laboratorio de Processadores
  Calculadora ESP32-C3 expandida com interface grafica web embarcada.

  Ajuste feito a partir do c2.ino:
  - Mantem a logica das operacoes: soma, subtracao, multiplicacao,
    fatorial e divisao inteira.
  - Mantem as 5 medicoes de tempo por operacao.
  - Adiciona pagina HTML/CSS servida pelo proprio ESP32, no mesmo
    estilo operacional do c1.ino: acesso pelo navegador, formulario
    e resposta visual na propria interface.
*/

// Mesmo padrao do c1 para facilitar o acesso no laboratorio.
const char* ssid = "Calculadora_ESP32_C3";
const char* senha = "";  // Vazio: rede aberta.

WebServer server(80);

// Pinos dos 4 LEDs no ESP32-C3 Dev Module, iguais aos usados no c1.
// LED 0 = bit menos significativo; LED 3 = bit mais significativo mostrado.
const int pinosLeds[] = {4, 5, 6, 7};
const int totalLeds = 4;

const int MAX_BITS_ENTRADA = 16;
const int TOTAL_MEDIDAS = 5;

struct ResultadoOperacao {
  uint64_t valor;
  uint64_t resto;
  bool overflow;
  String erro;
};

struct ResultadoMedido {
  ResultadoOperacao resultado;
  unsigned long tempos[TOTAL_MEDIDAS];
  double media;
  double desvioPadrao;
};

void adicionarCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}

String uint64ParaDecimal(uint64_t valor) {
  if (valor == 0) {
    return "0";
  }

  char buffer[21];  // Maior uint64_t tem 20 digitos decimais.
  buffer[20] = '\0';
  int pos = 19;

  while (valor > 0 && pos >= 0) {
    buffer[pos] = char('0' + (valor % 10));
    valor /= 10;
    pos--;
  }

  return String(&buffer[pos + 1]);
}

bool ehBinarioValido(const String& texto) {
  if (texto.length() == 0 || texto.length() > MAX_BITS_ENTRADA) {
    return false;
  }

  for (size_t i = 0; i < texto.length(); i++) {
    if (texto[i] != '0' && texto[i] != '1') {
      return false;
    }
  }

  return true;
}

uint64_t binarioParaInteiro(const String& texto) {
  uint64_t valor = 0;

  for (size_t i = 0; i < texto.length(); i++) {
    valor = (valor << 1) | (texto[i] == '1' ? 1 : 0);
  }

  return valor;
}

String inteiroParaBinario(uint64_t valor) {
  if (valor == 0) {
    return "0";
  }

  String binario = "";

  while (valor > 0) {
    binario = String((valor & 1) ? "1" : "0") + binario;
    valor >>= 1;
  }

  return binario;
}

String inteiroParaBinario4Bits(uint64_t valor) {
  String binario = "";
  uint8_t valor4Bits = valor & 0x0F;

  for (int i = 3; i >= 0; i--) {
    binario += ((valor4Bits >> i) & 1) ? "1" : "0";
  }

  return binario;
}

String nomeOperacao(const String& operacao) {
  if (operacao == "add") return "Soma";
  if (operacao == "sub") return "Subtracao";
  if (operacao == "mul") return "Multiplicacao";
  if (operacao == "fat") return "Fatorial";
  if (operacao == "div") return "Divisao inteira";
  return "Operacao desconhecida";
}

void mostrarResultado(uint64_t valor) {
  // Os LEDs mostram apenas os 4 bits menos significativos do resultado.
  // Na divisao, o valor mostrado e o quociente.
  for (int i = 0; i < totalLeds; i++) {
    digitalWrite(pinosLeds[i], (valor >> i) & 1);
  }
}

// Multiplicacao por somas sucessivas, para evidenciar a execucao iterativa.
// Criterio de parada: contador i atinge o valor do operando b.
ResultadoOperacao multiplicarIterativo(uint64_t a, uint64_t b) {
  ResultadoOperacao r;
  r.valor = 0;
  r.resto = 0;
  r.overflow = false;
  r.erro = "";

  for (uint64_t i = 0; i < b; i++) {
    if (UINT64_MAX - r.valor < a) {
      r.overflow = true;
      r.erro = "Overflow na multiplicacao.";
      return r;
    }

    r.valor += a;
  }

  return r;
}

// Fatorial por multiplicacoes sucessivas.
// Criterio de parada: contador i chega a 1.
ResultadoOperacao fatorialIterativo(uint64_t n) {
  ResultadoOperacao r;
  r.valor = 1;
  r.resto = 0;
  r.overflow = false;
  r.erro = "";

  // 20! e o maior fatorial que cabe em uint64_t.
  if (n > 20) {
    r.overflow = true;
    r.erro = "Overflow: para uint64_t, use fatorial de 0 ate 20.";
    return r;
  }

  for (uint64_t i = n; i > 1; i--) {
    if (UINT64_MAX / i < r.valor) {
      r.overflow = true;
      r.erro = "Overflow durante o calculo do fatorial.";
      return r;
    }

    r.valor *= i;
  }

  return r;
}

// Divisao inteira por subtracoes sucessivas.
// Criterio de parada: o dividendo restante fica menor que o divisor.
// Saidas: quociente em r.valor e resto em r.resto.
ResultadoOperacao dividirIterativo(uint64_t dividendo, uint64_t divisor) {
  ResultadoOperacao r;
  r.valor = 0;   // quociente
  r.resto = 0;
  r.overflow = false;
  r.erro = "";

  if (divisor == 0) {
    r.erro = "Divisao por zero nao permitida.";
    return r;
  }

  uint64_t restante = dividendo;

  while (restante >= divisor) {
    restante -= divisor;
    r.valor++;
  }

  r.resto = restante;
  return r;
}

ResultadoOperacao executarOperacao(const String& operacao, uint64_t valorA, uint64_t valorB) {
  ResultadoOperacao r;
  r.valor = 0;
  r.resto = 0;
  r.overflow = false;
  r.erro = "";

  if (operacao == "add") {
    if (UINT64_MAX - valorA < valorB) {
      r.overflow = true;
      r.erro = "Overflow na soma.";
      return r;
    }

    r.valor = valorA + valorB;

  } else if (operacao == "sub") {
    if (valorB > valorA) {
      r.overflow = true;
      r.erro = "Resultado negativo nao representado nesta versao binaria sem sinal.";
      return r;
    }

    r.valor = valorA - valorB;

  } else if (operacao == "mul") {
    r = multiplicarIterativo(valorA, valorB);

  } else if (operacao == "fat") {
    r = fatorialIterativo(valorA);

  } else if (operacao == "div") {
    r = dividirIterativo(valorA, valorB);

  } else {
    r.erro = "Operacao invalida.";
  }

  return r;
}

double calcularMedia(unsigned long tempos[], int n) {
  double soma = 0.0;

  for (int i = 0; i < n; i++) {
    soma += tempos[i];
  }

  return soma / n;
}

double calcularDesvioPadrao(unsigned long tempos[], int n, double media) {
  double somaQuadrados = 0.0;

  for (int i = 0; i < n; i++) {
    double diferenca = tempos[i] - media;
    somaQuadrados += diferenca * diferenca;
  }

  return sqrt(somaQuadrados / n);
}

String montarArrayTemposJSON(unsigned long tempos[], int n) {
  String json = "[";

  for (int i = 0; i < n; i++) {
    json += String(tempos[i]);

    if (i < n - 1) {
      json += ",";
    }
  }

  json += "]";
  return json;
}

ResultadoMedido executarComMedidas(const String& operacao, uint64_t valorA, uint64_t valorB) {
  ResultadoMedido medido;

  for (int i = 0; i < TOTAL_MEDIDAS; i++) {
    unsigned long inicio = micros();
    medido.resultado = executarOperacao(operacao, valorA, valorB);
    unsigned long fim = micros();

    medido.tempos[i] = fim - inicio;
  }

  medido.media = calcularMedia(medido.tempos, TOTAL_MEDIDAS);
  medido.desvioPadrao = calcularDesvioPadrao(medido.tempos, TOTAL_MEDIDAS, medido.media);

  return medido;
}

String gerarSelectOperacoes(const String& selecionada) {
  String html = "";
  String ops[] = {"add", "sub", "mul", "fat", "div"};
  String nomes[] = {"Soma (+)", "Subtracao (-)", "Multiplicacao (x)", "Fatorial de A (!)", "Divisao inteira (A / B)"};

  for (int i = 0; i < 5; i++) {
    html += "<option value='" + ops[i] + "'";
    if (ops[i] == selecionada) {
      html += " selected";
    }
    html += ">" + nomes[i] + "</option>";
  }

  return html;
}

String gerarHtmlLeds(uint64_t valor) {
  String html = "<div class='leds'>";

  // Exibe da esquerda para a direita: bit 3, bit 2, bit 1, bit 0.
  for (int i = 3; i >= 0; i--) {
    bool ligado = ((valor >> i) & 1) != 0;
    html += "<div class='ledBox'>";
    html += "<div class='led ";
    html += ligado ? "on" : "off";
    html += "'></div>";
    html += "<span>B" + String(i) + "</span>";
    html += "</div>";
  }

  html += "</div>";
  return html;
}

String gerarListaTempos(const unsigned long tempos[], int n) {
  String html = "<ol class='tempos'>";

  for (int i = 0; i < n; i++) {
    html += "<li>Medida " + String(i + 1) + ": <strong>" + String(tempos[i]) + " us</strong></li>";
  }

  html += "</ol>";
  return html;
}

String gerarResultadoHtml(const String& opA, const String& opB, const String& operacao, const ResultadoMedido& medido) {
  String html = "";
  uint64_t valorA = binarioParaInteiro(opA);
  uint64_t valorB = (operacao == "fat") ? 0 : binarioParaInteiro(opB);

  if (medido.resultado.erro.length() > 0) {
    html += "<section class='card resultado erro'>";
    html += "<h2>Falha no processamento</h2>";
    html += "<p>" + medido.resultado.erro + "</p>";
    html += "</section>";
    return html;
  }

  html += "<section class='card resultado sucesso'>";
  html += "<h2>Resultado processado no ESP32</h2>";
  html += "<p class='statusOk'>Operacao executada com sucesso no microcontrolador.</p>";

  html += "<div class='grid'>";
  html += "<div><span class='label'>Operacao</span><strong>" + nomeOperacao(operacao) + "</strong></div>";
  html += "<div><span class='label'>Operando A</span><strong>" + opA + " = " + uint64ParaDecimal(valorA) + "</strong></div>";

  if (operacao != "fat") {
    html += "<div><span class='label'>Operando B</span><strong>" + opB + " = " + uint64ParaDecimal(valorB) + "</strong></div>";
  } else {
    html += "<div><span class='label'>Operando B</span><strong>Ignorado no fatorial</strong></div>";
  }

  if (operacao == "div") {
    html += "<div><span class='label'>Quociente decimal</span><strong>" + uint64ParaDecimal(medido.resultado.valor) + "</strong></div>";
    html += "<div><span class='label'>Quociente binario</span><strong>" + inteiroParaBinario(medido.resultado.valor) + "</strong></div>";
    html += "<div><span class='label'>Resto decimal</span><strong>" + uint64ParaDecimal(medido.resultado.resto) + "</strong></div>";
    html += "<div><span class='label'>Resto binario</span><strong>" + inteiroParaBinario(medido.resultado.resto) + "</strong></div>";
  } else {
    html += "<div><span class='label'>Resultado decimal</span><strong>" + uint64ParaDecimal(medido.resultado.valor) + "</strong></div>";
    html += "<div><span class='label'>Resultado binario</span><strong>" + inteiroParaBinario(medido.resultado.valor) + "</strong></div>";
  }

  html += "<div><span class='label'>Saida nos LEDs</span><strong>" + inteiroParaBinario4Bits(medido.resultado.valor) + "</strong></div>";
  html += "</div>";

  html += "<h3>Representacao dos LEDs fisicos</h3>";
  html += gerarHtmlLeds(medido.resultado.valor);
  html += "<p class='obs'>Os LEDs exibem apenas os 4 bits menos significativos do resultado. Na divisao, exibem o quociente.</p>";

  html += "<h3>Medicoes de tempo</h3>";
  html += gerarListaTempos(medido.tempos, TOTAL_MEDIDAS);
  html += "<p class='metricas'>Media: <strong>" + String(medido.media, 2) + " us</strong> &nbsp; | &nbsp; Desvio padrao: <strong>" + String(medido.desvioPadrao, 2) + " us</strong></p>";
  html += "</section>";

  return html;
}

String gerarPaginaHtml(const String& opA, const String& opB, const String& operacao, const String& blocoResultado) {
  String valorA = opA.length() > 0 ? opA : "0101";
  String valorB = opB.length() > 0 ? opB : "0010";
  String opSelecionada = operacao.length() > 0 ? operacao : "add";

  String html = "";
  html += "<!DOCTYPE html><html lang='pt-br'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Calculadora ESP32-C3</title>";
  html += "<style>";
  html += "*{box-sizing:border-box}";
  html += "body{margin:0;font-family:Arial,Helvetica,sans-serif;background:#eef2f7;color:#172033;text-align:center}";
  html += "header{background:linear-gradient(135deg,#073b6d,#169fb5);color:white;padding:32px 16px}";
  html += "header h1{margin:0;font-size:30px}";
  html += "header p{margin:10px auto 0;max-width:780px;line-height:1.45}";
  html += ".wrap{max-width:980px;margin:0 auto;padding:24px 14px}";
  html += ".card{background:white;border-radius:16px;box-shadow:0 8px 24px rgba(0,0,0,.12);padding:24px;margin:18px auto;text-align:left}";
  html += ".card h2,.card h3{margin-top:0;color:#073b6d;text-align:center}";
  html += ".formGrid{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:18px}";
  html += "label{display:block;font-weight:700;margin-bottom:7px;color:#26344d}";
  html += "input,select,button{width:100%;font-size:18px;padding:12px;border-radius:10px;border:1px solid #bcc7d6}";
  html += "input:focus,select:focus{outline:none;border-color:#169fb5;box-shadow:0 0 0 3px rgba(22,159,181,.18)}";
  html += ".wide{grid-column:1/-1}";
  html += "button{border:0;background:#0878c9;color:white;font-weight:700;cursor:pointer;transition:.15s}";
  html += "button:hover{background:#045d9d;transform:translateY(-1px)}";
  html += ".hint{font-size:14px;color:#5c677a;margin-top:6px;line-height:1.35}";
  html += ".resultado{border-left:8px solid #1b8f51}";
  html += ".erro{border-left-color:#d62828;background:#fff8f8}";
  html += ".erro h2{color:#b00020}";
  html += ".statusOk{color:#08763d;text-align:center;font-weight:700}";
  html += ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin:16px 0}";
  html += ".grid div{background:#f3f7fb;border:1px solid #dce5ef;border-radius:12px;padding:12px}";
  html += ".label{display:block;font-size:13px;color:#66758c;margin-bottom:4px;text-transform:uppercase;letter-spacing:.04em}";
  html += ".leds{display:flex;justify-content:center;gap:18px;margin:16px 0 8px}";
  html += ".ledBox{text-align:center;font-size:13px;color:#47566d}";
  html += ".led{width:44px;height:44px;border-radius:50%;border:3px solid #333;margin-bottom:6px;box-shadow:inset 0 2px 6px rgba(0,0,0,.35)}";
  html += ".led.on{background:#24d366;box-shadow:0 0 16px #24d366,inset 0 2px 6px rgba(255,255,255,.35)}";
  html += ".led.off{background:#2b2f36}";
  html += ".tempos{max-width:420px;margin:8px auto 0;line-height:1.8}";
  html += ".metricas,.obs{text-align:center;color:#39475e}";
  html += ".rodape{text-align:center;color:#66758c;font-size:13px;margin-top:18px}";
  html += "@media(max-width:700px){.formGrid,.grid{grid-template-columns:1fr}header h1{font-size:24px}.card{padding:18px}}";
  html += "</style>";
  html += "<script>";
  html += "function ajustarB(){var op=document.getElementById('op').value;var box=document.getElementById('campoB');var b=document.getElementById('b');if(op==='fat'){box.style.opacity='.55';b.removeAttribute('required');}else{box.style.opacity='1';b.setAttribute('required','required');}}";
  html += "window.onload=ajustarB;";
  html += "</script>";
  html += "</head><body>";

  html += "<header>";
  html += "<h1>Calculadora Binaria ESP32-C3</h1>";
  html += "<p>Interface grafica embarcada para soma, subtracao, multiplicacao, fatorial e divisao. O processamento ocorre no microcontrolador em C/C++, e os LEDs representam os 4 bits menos significativos da saida.</p>";
  html += "</header>";

  html += "<main class='wrap'>";
  html += "<section class='card'>";
  html += "<h2>Enviar operacao ao processador</h2>";
  html += "<form action='/calc' method='GET'>";
  html += "<div class='formGrid'>";

  html += "<div>";
  html += "<label for='a'>Operando A em binario</label>";
  html += "<input id='a' name='a' type='text' maxlength='16' pattern='[01]{1,16}' required value='" + valorA + "' placeholder='ex: 0101'>";
  html += "<div class='hint'>Use de 1 a 16 bits. No fatorial, este e o valor n.</div>";
  html += "</div>";

  html += "<div id='campoB'>";
  html += "<label for='b'>Operando B em binario</label>";
  html += "<input id='b' name='b' type='text' maxlength='16' pattern='[01]{1,16}' value='" + valorB + "' placeholder='ex: 0010'>";
  html += "<div class='hint'>Usado em soma, subtracao, multiplicacao e divisao. Ignorado no fatorial.</div>";
  html += "</div>";

  html += "<div class='wide'>";
  html += "<label for='op'>Operacao</label>";
  html += "<select id='op' name='op' onchange='ajustarB()'>";
  html += gerarSelectOperacoes(opSelecionada);
  html += "</select>";
  html += "</div>";

  html += "<div class='wide'>";
  html += "<button type='submit'>Enviar para o processador</button>";
  html += "</div>";

  html += "</div>";
  html += "</form>";
  html += "</section>";

  html += blocoResultado;

  html += "<p class='rodape'>Access Point: " + String(ssid) + " | Endereco usual: http://192.168.4.1/</p>";
  html += "</main>";
  html += "</body></html>";

  return html;
}

void responderPaginaInicial() {
  String html = gerarPaginaHtml("", "", "add", "");
  server.send(200, "text/html", html);
}

void calcularHtml() {
  String opA = server.arg("a");
  String opB = server.arg("b");
  String operacao = server.arg("op");

  if (!ehBinarioValido(opA)) {
    String erro = "<section class='card resultado erro'><h2>Entrada invalida</h2><p>Operando A invalido. Use de 1 a 16 bits binarios.</p></section>";
    server.send(400, "text/html", gerarPaginaHtml(opA, opB, operacao, erro));
    return;
  }

  if (operacao != "fat" && !ehBinarioValido(opB)) {
    String erro = "<section class='card resultado erro'><h2>Entrada invalida</h2><p>Operando B invalido. Use de 1 a 16 bits binarios.</p></section>";
    server.send(400, "text/html", gerarPaginaHtml(opA, opB, operacao, erro));
    return;
  }

  uint64_t valorA = binarioParaInteiro(opA);
  uint64_t valorB = (operacao == "fat") ? 0 : binarioParaInteiro(opB);

  ResultadoMedido medido = executarComMedidas(operacao, valorA, valorB);

  if (medido.resultado.erro.length() == 0) {
    mostrarResultado(medido.resultado.valor);
  }

  String blocoResultado = gerarResultadoHtml(opA, opB, operacao, medido);
  server.send(200, "text/html", gerarPaginaHtml(opA, opB, operacao, blocoResultado));
}

void responderOptions() {
  adicionarCORS();
  server.send(204);
}

// Endpoint JSON preservado para testes automatizados ou interface externa.
// Exemplo: /api/calc?a=0101&b=0010&op=mul
void calcularJson() {
  adicionarCORS();

  String opA = server.arg("a");
  String opB = server.arg("b");
  String operacao = server.arg("op");

  if (!ehBinarioValido(opA)) {
    server.send(400, "application/json", "{\"erro\":\"Operando A invalido. Use de 1 a 16 bits binarios.\"}");
    return;
  }

  if (operacao != "fat" && !ehBinarioValido(opB)) {
    server.send(400, "application/json", "{\"erro\":\"Operando B invalido. Use de 1 a 16 bits binarios.\"}");
    return;
  }

  uint64_t valorA = binarioParaInteiro(opA);
  uint64_t valorB = (operacao == "fat") ? 0 : binarioParaInteiro(opB);

  ResultadoMedido medido = executarComMedidas(operacao, valorA, valorB);

  if (medido.resultado.erro.length() > 0) {
    String respostaErro = "{";
    respostaErro += "\"operacao\":\"" + operacao + "\",";
    respostaErro += "\"erro\":\"" + medido.resultado.erro + "\",";
    respostaErro += "\"overflow\":";
    respostaErro += (medido.resultado.overflow ? "true" : "false");
    respostaErro += "}";

    server.send(200, "application/json", respostaErro);
    return;
  }

  mostrarResultado(medido.resultado.valor);

  String resposta = "{";
  resposta += "\"operacao\":\"" + operacao + "\",";

  if (operacao == "div") {
    resposta += "\"quociente_dec\":\"" + uint64ParaDecimal(medido.resultado.valor) + "\",";
    resposta += "\"quociente_bin\":\"" + inteiroParaBinario(medido.resultado.valor) + "\",";
    resposta += "\"resto_dec\":\"" + uint64ParaDecimal(medido.resultado.resto) + "\",";
    resposta += "\"resto_bin\":\"" + inteiroParaBinario(medido.resultado.resto) + "\",";
  } else {
    resposta += "\"resultado_dec\":\"" + uint64ParaDecimal(medido.resultado.valor) + "\",";
    resposta += "\"resultado_bin\":\"" + inteiroParaBinario(medido.resultado.valor) + "\",";
  }

  resposta += "\"leds_4bits\":\"" + inteiroParaBinario4Bits(medido.resultado.valor) + "\",";
  resposta += "\"tempos_us\":" + montarArrayTemposJSON(medido.tempos, TOTAL_MEDIDAS) + ",";
  resposta += "\"media_us\":\"" + String(medido.media, 2) + "\",";
  resposta += "\"desvio_padrao_us\":\"" + String(medido.desvioPadrao, 2) + "\",";
  resposta += "\"overflow\":";
  resposta += (medido.resultado.overflow ? "true" : "false");
  resposta += "}";

  server.send(200, "application/json", resposta);
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < totalLeds; i++) {
    pinMode(pinosLeds[i], OUTPUT);
    digitalWrite(pinosLeds[i], LOW);
  }

  WiFi.softAP(ssid, senha);

  Serial.println();
  Serial.println("--- Calculadora ESP32-C3 inicializada ---");
  Serial.print("Rede Wi-Fi: ");
  Serial.println(ssid);
  Serial.print("IP para acessar no navegador: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, responderPaginaInicial);
  server.on("/calc", HTTP_GET, calcularHtml);
  server.on("/api/calc", HTTP_GET, calcularJson);
  server.on("/calc", HTTP_OPTIONS, responderOptions);
  server.on("/api/calc", HTTP_OPTIONS, responderOptions);

  server.begin();
  Serial.println("Servidor HTTP iniciado na porta 80.");
}

void loop() {
  server.handleClient();
}
