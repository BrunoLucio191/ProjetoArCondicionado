#include <LittleFS.h>
#include <IRremote.hpp>

// --- Configurações de Hardware ---
#define IR_RECEIVER_PIN 34
#define IR_SENDER_PIN   32
#define STATUS_LED_PIN  2
#define MAX_LEN         500  // Máximo de pulsos RAW por sinal (em uint16_t)

// CORREÇÃO E EXPANSÃO: Total de comandos: Ligar(1) + Desligar(1) + Modos(5) + Swing(1) + T16 a T30 (15) = 23
#define NUM_SIGNALS     23 

// --- Definição de Índices (23 Sinais de 0 a 22) ---
// Comandos essenciais (0-1)
#define SIG_LIGAR       0
#define SIG_DESLIGAR    1
// Modos de operação (2-6)
#define SIG_MODO_1      2
#define SIG_MODO_2      3
#define SIG_MODO_3      4
#define SIG_MODO_4      5
#define SIG_MODO_5      6
#define SIG_SWING       7 // Oscilador (7)
// Temperaturas 16 a 30 (8-22)
#define SIG_TEMP_16     8
#define SIG_TEMP_17     9
#define SIG_TEMP_18     10
#define SIG_TEMP_19     11
#define SIG_TEMP_20     12
#define SIG_TEMP_21     13
#define SIG_TEMP_22     14
#define SIG_TEMP_23     15
#define SIG_TEMP_24     16
#define SIG_TEMP_25     17
#define SIG_TEMP_26     18
#define SIG_TEMP_27     19
#define SIG_TEMP_28     20
#define SIG_TEMP_29     21
#define SIG_TEMP_30     22 // O último índice

// --- Buffers de Armazenamento (RAM) ---
uint16_t durations[NUM_SIGNALS][MAX_LEN]; 
unsigned int capturedSignalLength[NUM_SIGNALS] = {0}; 

// --- Buffer Temporário para Captura (Volátil) ---
volatile unsigned long irBufferTemp[MAX_LEN];
volatile unsigned int bufferPositionTemp = 0;

// ********************************************
// ROTINAS DE CAPTURA (ISR) E ENVIO
// ********************************************

void IRAM_ATTR rxIR_Interrupt_Handler() {
  if (bufferPositionTemp < MAX_LEN) {
    irBufferTemp[bufferPositionTemp++] = micros(); 
  }
}

bool captureIR(uint16_t* targetDurations, unsigned int &targetLength, uint32_t waitMs = 5000) {
  Serial.println("Capturando sinal IR... Aponte o controle e aperte o botão.");
  bufferPositionTemp = 0; 

  digitalWrite(STATUS_LED_PIN, HIGH);
  attachInterrupt(digitalPinToInterrupt(IR_RECEIVER_PIN), rxIR_Interrupt_Handler, CHANGE);

  unsigned long start = millis();
  while (millis() - start < waitMs) {
    delay(10);
  }

  detachInterrupt(digitalPinToInterrupt(IR_RECEIVER_PIN));
  digitalWrite(STATUS_LED_PIN, LOW);

  if (bufferPositionTemp > 10) { 
    unsigned int len = (bufferPositionTemp > 0) ? (bufferPositionTemp - 1) : 0;
    if (len > MAX_LEN) len = MAX_LEN;
    
    for (unsigned int i = 0; i < len; i++) {
      unsigned long diff = irBufferTemp[i + 1] - irBufferTemp[i];
      if (diff > 65535UL) targetDurations[i] = 65535;
      else targetDurations[i] = (uint16_t)diff;
    }
    
    targetLength = len;
    Serial.println("Sinal capturado com sucesso!");
    Serial.print("Comprimento: ");
    Serial.println(targetLength);
    return true;
  } else {
    Serial.println("Falha na captura. Tente novamente.");
    targetLength = 0;
    return false;
  }
}

void sendIR(uint16_t* durationsToSend, unsigned int lengthToSend) {
  if (lengthToSend > 0) {
    Serial.println("Enviando sinal IR...");
    IrSender.sendRaw(durationsToSend, lengthToSend, 38); 
    Serial.println("Sinal enviado!");
  } else {
    Serial.println("Nenhum sinal capturado para este comando.");
  }
}

// ********************************************
// PERSISTÊNCIA DE DADOS (LittleFS)
// ********************************************

void salvarTodosOsSinais() {
  // Abre o arquivo no modo "w" (write), que cria o arquivo se não existir e apaga o conteúdo anterior.
  File f = LittleFS.open("/sinais.txt", "w"); 
  
  if (!f) {
    Serial.println("ERRO: Falha ao abrir /sinais.txt para escrita!");
    return;
  }
  
  // Loop principal: percorre todas as 23 linhas (comandos) da matriz 'durations'
  for (int i = 0; i < NUM_SIGNALS; i++) {
    
    // 1. Escreve a TAG (nome do comando)
    if (i == SIG_LIGAR) {
      f.print("ligar:");
    } else if (i == SIG_DESLIGAR) {
      f.print("desligar:");

    // Lógica para os 5 Modos (SIG_MODO_1 até SIG_MODO_5)
    } else if (i >= SIG_MODO_1 && i <= SIG_MODO_5){ 
      int modo = i - SIG_MODO_1 + 1; // Calcula o número do modo (1 a 5)
      f.print("modo");
      f.print(modo);
      f.print(":");

    } else if (i == SIG_SWING) {
      f.print("swing:");

    // Lógica para as Temperaturas (SIG_TEMP_16 até SIG_TEMP_30)
    } else if (i >= SIG_TEMP_16 && i <= SIG_TEMP_30){ 
      int temp = 16 + (i - SIG_TEMP_16); 
      f.print("temp");
      f.print(temp);
      f.print(":");
    }

    // 2. Escreve os DADOS (durações em formato CSV)
    for (unsigned int j = 0; j < capturedSignalLength[i]; j++) {
      f.print(durations[i][j]); 
      if (j < capturedSignalLength[i] - 1) f.print(","); 
    }
    f.println(); // Adiciona quebra de linha ('\n')
  }

  f.close(); 
  Serial.println(">> Todos os sinais foram SALVOS com sucesso em /sinais.txt!");
}

void carregarTodosOsSinais() {
  // Abre o arquivo no modo de leitura ('r')
  File f = LittleFS.open("/sinais.txt", "r");
  if (!f) {
    Serial.println("INFO: Arquivo /sinais.txt não encontrado. Execute a captura para criar.");
    return;
  }
  Serial.println(">> Carregando sinais da memória Flash...");

  // Loop principal de leitura linha por linha
  while (f.available()) {
    String linha = f.readStringUntil('\n');
    linha.trim(); 
    if (linha.length() < 3) continue;
    
    int sep = linha.indexOf(':'); 
    if (sep == -1) continue; 

    String nome = linha.substring(0, sep);
    String lista = linha.substring(sep + 1);
    int index = -1;

    // --- Mapeamento do Índice (Desserialização da TAG) ---
    if (nome == "ligar") index = SIG_LIGAR;
    else if (nome == "desligar") index = SIG_DESLIGAR;
    else if (nome == "swing") index = SIG_SWING;
    
    // Mapeamento dos Modos
    else if (nome.startsWith("modo")) {
      int modo = nome.substring(4).toInt(); 
      if (modo >= 1 && modo <= 5) {
        index = (modo - 1) + SIG_MODO_1; 
      }
    }
    // Mapeamento das Temperaturas 16-30
    else if (nome.startsWith("temp")) {
      int temp = nome.substring(4).toInt(); 
      if (temp >= 16 && temp <= 30) { 
        index = (temp - 16) + SIG_TEMP_16; 
      }
    }

    if (index == -1) continue; // Se a TAG não for reconhecida, ignora

    // ** Parsing do CSV (Conversão de String para Números) **
    int pos = 0;
    unsigned int arrIndex = 0;
    while (pos < lista.length() && arrIndex < MAX_LEN) {
      int comma = lista.indexOf(',', pos);
      String valor;
      if (comma == -1) { 
        valor = lista.substring(pos);
        pos = lista.length(); 
      } else { 
        valor = lista.substring(pos, comma);
        pos = comma + 1; 
      }
      durations[index][arrIndex++] = (uint16_t)valor.toInt(); 
    }
    capturedSignalLength[index] = arrIndex; 
  }
  f.close(); 
  Serial.println(">> Sinais carregados com sucesso. Prontos para uso!");
}

// ********************************************
// UTILITÁRIO
// ********************************************

void printSignal(int idx) {
  if (idx < 0 || idx >= NUM_SIGNALS || capturedSignalLength[idx] == 0) {
    Serial.println("Sinal vazio ou indice invalido.");
    return;
  }
  
  // Imprime a TAG do comando (Adaptado para Modos e Swing)
  if (idx == SIG_LIGAR) Serial.print("ligar: ");
  else if (idx == SIG_DESLIGAR) Serial.print("desligar: ");
  else if (idx == SIG_SWING) Serial.print("swing: ");
  else if (idx >= SIG_MODO_1 && idx <= SIG_MODO_5) {
    int modo = idx - SIG_MODO_1 + 1;
    Serial.print("modo"); Serial.print(modo); Serial.print(": ");
  }
  else {
    int temp = 16 + (idx - SIG_TEMP_16);
    if (temp >= 16 && temp <= 30) { 
        Serial.print("temp"); Serial.print(temp); Serial.print(": ");
    } else {
        Serial.println("Sinal invalido.");
        return;
    }
  }
  
  // Imprime os valores CSV (pulsos de microssegundos)
  for (unsigned int i = 0; i < capturedSignalLength[idx]; i++) {
    Serial.print(durations[idx][i]);
    if (i < capturedSignalLength[idx] - 1) Serial.print(",");
  }
  Serial.println();
}

// ********************************************
// SETUP & LOOP
// ********************************************

void setup() {
  Serial.begin(115200);           
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(IR_RECEIVER_PIN, INPUT);

  IrSender.begin(IR_SENDER_PIN); 

  Serial.println();
  Serial.println("=== IR CAPTURE & SAVE (LittleFS) ===");

  if (!LittleFS.begin(true)) {
    Serial.println("ERRO FATAL: Falha ao montar ou formatar LittleFS.");
  } else {
    Serial.println("LittleFS montado.");
    carregarTodosOsSinais(); 
  }

  // --- Instruções de Uso ATUALIZADAS ---
  Serial.println("\nComandos via Serial:");
  Serial.println("1 -> capturar LIGAR");
  Serial.println("2 -> capturar DESLIGAR");
  Serial.println("M<1-5> -> capturar MODO (ex: 'M1', 'M5')"); 
  Serial.println("SWA -> capturar SWING (Oscilador)");
  Serial.println("16..30 -> capturar temperatura correspondente"); 
  Serial.println("L -> enviar LIGAR");
  Serial.println("D -> enviar DESLIGAR");
  Serial.println("MOD<1-5> -> enviar MODO (ex: 'MOD3')"); 
  Serial.println("SW -> enviar SWING (Oscilador)");
  Serial.println("T <16-30> -> enviar TEMP (ex: 'T 28')"); 
  Serial.println("P <nome> -> printar sinal (ex: 'P 20', 'P M4', 'P SW')"); 
  Serial.println("S -> salvar TODOS os sinais na memoria Flash (LittleFS)");
  Serial.println();

  Serial.println("Pronto. Aguarde comandos ou capture sinais.");
}

void loop() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    // --- COMANDOS DE CAPTURA ---
    if (cmd == "1") {
      while (!captureIR(durations[SIG_LIGAR], capturedSignalLength[SIG_LIGAR])) { delay(200); }
    } else if (cmd == "2") {
      while (!captureIR(durations[SIG_DESLIGAR], capturedSignalLength[SIG_DESLIGAR])) { delay(200); }
    } else if (cmd.equalsIgnoreCase("SWA")) { 
      while (!captureIR(durations[SIG_SWING], capturedSignalLength[SIG_SWING])) { delay(200); }
    } 
    // Captura dos Modos M1 a M5
    else if (cmd.startsWith("M") && cmd.length() == 2) { 
        int modo = cmd.substring(1).toInt();
        if (modo >= 1 && modo <= 5) {
            int idx = (modo - 1) + SIG_MODO_1;
            Serial.print("--- Capturando Sinal MODO "); Serial.print(modo); Serial.println(" ---");
            while (!captureIR(durations[idx], capturedSignalLength[idx])) { delay(200); }
        } else {
            Serial.println("Modo M invalido. Use M1 a M5.");
        }
    }
    // Captura de Temperatura 16-30
    else if (cmd.toInt() >= 16 && cmd.toInt() <= 30 && cmd.length() <= 3) {
      int temp = cmd.toInt();
      int idx = (temp - 16) + SIG_TEMP_16;
      Serial.print("--- Capturando Sinal TEMP "); Serial.print(temp); Serial.println(" ---");
      while (!captureIR(durations[idx], capturedSignalLength[idx])) { delay(200); }
    }
    
    // --- COMANDOS DE ENVIO ---
    else if (cmd.equalsIgnoreCase("L")) {
      sendIR(durations[SIG_LIGAR], capturedSignalLength[SIG_LIGAR]);
    } else if (cmd.equalsIgnoreCase("D")) {
      sendIR(durations[SIG_DESLIGAR], capturedSignalLength[SIG_DESLIGAR]);
    } else if (cmd.equalsIgnoreCase("SW")) { 
      sendIR(durations[SIG_SWING], capturedSignalLength[SIG_SWING]);
    } 
    // Envio dos Modos MOD1 a MOD5
    else if (cmd.startsWith("MOD") && cmd.length() == 4) {
        int modo = cmd.substring(3).toInt();
        if (modo >= 1 && modo <= 5) {
            int idx = (modo - 1) + SIG_MODO_1;
            sendIR(durations[idx], capturedSignalLength[idx]);
        } else {
            Serial.println("Modo MOD invalido. Use MOD1 a MOD5.");
        }
    }
    // Envio de Temperatura T 16-30
    else if (cmd.startsWith("T ")) {
        int temp = cmd.substring(2).toInt();
        if (temp >= 16 && temp <= 30) {
          int idx = (temp - 16) + SIG_TEMP_16;
          sendIR(durations[idx], capturedSignalLength[idx]);
        } else {
          Serial.println("Temperatura T invalida. Use T 16 a T 30.");
        }
    }
    
    // --- COMANDO DE PRINT ---
    else if (cmd.startsWith("P ")) {
      String arg = cmd.substring(2);
      arg.trim();
      if (arg.equalsIgnoreCase("L")) printSignal(SIG_LIGAR);
      else if (arg.equalsIgnoreCase("D")) printSignal(SIG_DESLIGAR);
      else if (arg.equalsIgnoreCase("SW")) printSignal(SIG_SWING);
      // Printar Modo (P M1 a P M5)
      else if (arg.startsWith("M") && arg.length() == 2) {
          int modo = arg.substring(1).toInt();
          if (modo >= 1 && modo <= 5) {
              printSignal((modo - 1) + SIG_MODO_1);
          } else {
              Serial.println("Argumento P Modo invalido. Use P M1 a P M5.");
          }
      }
      // Printar Temperatura (P 16 a P 30)
      else {
        int t = arg.toInt();
        if (t >= 16 && t <= 30) printSignal((t - 16) + SIG_TEMP_16);
        else Serial.println("Argumento P invalido. Use L, D, SW, M1..M5, ou 16..30");
      }
    }
    
    // Comando de SALVAR
    else if (cmd.equalsIgnoreCase("S")) {
      salvarTodosOsSinais();
    }
    
    else {
      Serial.println("Comando invalido. Veja a lista de comandos no boot.");
    }

    Serial.println("\nAguardando proximo comando...");
  }

  delay(10);
}