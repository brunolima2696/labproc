#include <WiFi.h>

// Substitua com as credenciais que preferir para o laboratório
const char* ssid = "Calculadora_ESP32_C3";
const char* password = ""; // Deixando vazio a rede fica aberta para conectar rápido

// Define o servidor na porta 80
WiFiServer server(80);

// Variável para armazenar a requisição HTTP do navegador
String header;

// 1. CONFIGURAÇÃO DOS PINOS EXCLUSIVOS DO ESP32-C3 DEV MODULE
const int LED_BIT0 = 4; // Bit menos significativo (LSB - o da direita)
const int LED_BIT1 = 5; 
const int LED_BIT2 = 6; 
const int LED_BIT3 = 7; // Bit mais significativo (MSB - o da esquerda/sinal)

// Variável para armazenar o aviso visual de estouro de capacidade
String statusOverflow = "";

void setup() {
  // Inicializa o monitor serial (Lembre de ativar "USB CDC On Boot: Enabled" nas ferramentas)
  Serial.begin(115200);

  // Inicializa os 4 pinos configurados como SAÍDA
  pinMode(LED_BIT0, OUTPUT);
  pinMode(LED_BIT1, OUTPUT);
  pinMode(LED_BIT2, OUTPUT);
  pinMode(LED_BIT3, OUTPUT);

  // Garante que todos os LEDs iniciam apagados
  digitalWrite(LED_BIT0, LOW);
  digitalWrite(LED_BIT1, LOW);
  digitalWrite(LED_BIT2, LOW);
  digitalWrite(LED_BIT3, LOW);

  // Configura e inicia o Access Point (Hotspot)
  Serial.print("Configurando Access Point (Wi-Fi)... ");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("IP para acessar no navegador: ");
  Serial.println(IP);
  
  server.begin();
}

void loop() {
  WiFiClient client = server.available(); // Escuta por novos clientes (celular/PC)

  if (client) {
    Serial.println("Novo cliente conectado.");
    String currentLine = ""; // Armazena a linha atual que vem do cliente
    
    while (client.connected()) { // Loop enquanto o cliente estiver conectado
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        header += c;
        
        if (c == '\n') { // Se o caractere for uma quebra de linha
          // Se a linha atual veio em branco, indica o fim da requisição HTTP
          if (currentLine.length() == 0) {
            
            // Envia o cabeçalho de resposta HTTP padrão
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // -------------------------------------------------------------
            // TRATAMENTO DO MIOLO: CALCULO REQUISITADO VIA URL
            // -------------------------------------------------------------
            if (header.indexOf("GET /calc") >= 0) {
              // Localiza as posições das variáveis dentro da String bruta da URL
              int posA = header.indexOf("a=") + 2;
              int posB = header.indexOf("&b=") + 3;
              int posOp = header.indexOf("&op=") + 4;
              int posFim = header.indexOf(" HTTP");
              
              // Recorta as substrings binárias textuais
              String paramA = header.substring(posA, posA + 4);
              String paramB = header.substring(posB, posB + 4);
              String paramOp = header.substring(posOp, posFim);

              // 1. Parsing: Converte as Strings binárias para inteiros usando strtol (Base 2)
              int valA = strtol(paramA.c_str(), NULL, 2);
              int valB = strtol(paramB.c_str(), NULL, 2);

              // 2. Operação Aritmética NATIVA do C (Abstração de alto nível)
              int resultado = (paramOp == "add") ? (valA + valB) : (valA - valB);

              // --- DETECÇÃO DE OVERFLOW EM COMPLEMENTO DE DOIS (4 BITS) ---
              // Converte a interpretação lógica para números sinalizados (-8 a +7)
              int signedA = (valA > 7) ? (valA - 16) : valA;
              int signedB = (valB > 7) ? (valB - 16) : valB;
              int signedRes = (paramOp == "add") ? (signedA + signedB) : (signedA - signedB);

              // Se o resultado extrapolar o range de 4 bits (-8 a +7), ativa o alerta de overflow
              if (signedRes < -8 || signedRes > 7) {
                statusOverflow = "<h2 style='color:red;'>⚠️ ERRO: OVERFLOW DETECTADO! (Invasão do bit de sinal)</h2>";
              } else {
                statusOverflow = "<h2 style='color:green;'>✔️ Cálculo processado com sucesso no hardware!</h2>";
              }

              // 3. Mascaramento (Garante que só aproveitamos os 4 primeiros bits inferiores)
              resultado = resultado & 0x0F;

              // 4. Output para GPIO: Atualiza os pinos físicos fazendo deslocamento de bits
              digitalWrite(LED_BIT0, resultado & 0x01);       // Isola o bit 0
              digitalWrite(LED_BIT1, (resultado >> 1) & 0x01); // Desloca e isola o bit 1
              digitalWrite(LED_BIT2, (resultado >> 2) & 0x01); // Desloca e isola o bit 2
              digitalWrite(LED_BIT3, (resultado >> 3) & 0x01); // Desloca e isola o bit 3
            }

            // -------------------------------------------------------------
            // RENDERIZAÇÃO DA INTERFACE GRÁFICA (HTML/CSS)
            // -------------------------------------------------------------
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<meta charset=\"UTF-8\"><title>Calculadora ESP32-C3</title>");
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println("input, select, button { font-size: 22px; margin: 10px; padding: 8px; }</style></head>");
            
            client.println("<body><h1>Calculadora de 4 Bits (PCS3732)</h1>");
            
            // Injeta dinamicamente a mensagem de sucesso ou o alerta de Overflow
            client.println(statusOverflow);

            // Formulário que cria o gatilho HTTP no formato: /calc?a=0110&b=0010&op=add
            client.println("<form action=\"/calc\" method=\"GET\">");
            client.println("Operando A (4 bits): <input type=\"text\" name=\"a\" maxlength=\"4\" required placeholder=\"ex: 0101\"><br>");
            client.println("Operando B (4 bits): <input type=\"text\" name=\"b\" maxlength=\"4\" required placeholder=\"ex: 0010\"><br>");
            client.println("Operação: <select name=\"op\">");
            client.println("<option value=\"add\">Soma (+)</option>");
            client.println("<option value=\"sub\">Subtração (-)</option>");
            client.println("</select><br><br>");
            client.println("<button type=\"submit\">Enviar para o Processador</button>");
            client.println("</form>");
            
            client.println("</body></html>");
            client.println(); // A resposta HTTP termina com uma linha em branco
            break;
          } else { 
            currentLine = ""; // Se veio uma nova linha, limpa a linha atual
          }
        } else if (c != '\r') { 
          currentLine += c; // Adiciona o caractere recebido à linha atual
        }
      }
    }
    // Limpa a variável do cabeçalho e fecha a conexão com o cliente
    header = "";
    client.stop();
    Serial.println("Cliente desconectado.");
    Serial.println("");
  }
}