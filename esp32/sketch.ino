/*
  CardioIA Conectada — Fase 3 (FIAP, 2º ano)
  Monitor cardíaco vestível simulado no Wokwi com ESP32.

  O que este código faz, em ordem:
   1. Lê a temperatura e a umidade do DHT22 (sensor 1).
   2. Calcula o BPM a partir de um botão (sensor 2): cada toque = 1 batimento.
   3. Guarda cada leitura num buffer circular na memória do próprio ESP32 (Edge).
   4. Olha a chave deslizante, que simula o Wi-Fi (variável booleana wifiSimulado).
      - Desligada (OFFLINE): continua coletando e guardando. Nada se perde.
      - Ligada (ONLINE): envia tudo o que está guardado (Serial + MQTT) e esvazia o buffer.
   5. Acende o LED vermelho quando há alerta (febre, taquicardia ou bradicardia).

  Broker MQTT: broker.hivemq.com (HiveMQ público, porta 1883, sem login).
  Tópicos:  cardioia/rm566793/vitais   (uma leitura por mensagem, JSON)
            cardioia/rm566793/status   (online/offline e ocupação do buffer)

  Autor: Marlon Paulino Marinho — RM566793
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"

// ---------------- Pinos ----------------
const int PINO_DHT     = 15;  // DHT22: temperatura e umidade
const int PINO_BOTAO   = 4;   // botão de batimento (INPUT_PULLUP, outro lado no GND)
const int PINO_CHAVE   = 5;   // chave deslizante: simula Wi-Fi ligado/desligado
const int PINO_LED_AL  = 2;   // LED vermelho: alerta clínico
const int PINO_LED_NET = 18;  // LED azul: online (enviando para a nuvem)

// ---------------- Rede e MQTT ----------------
const char* WIFI_SSID   = "Wokwi-GUEST";   // rede virtual do Wokwi (sem senha)
const char* WIFI_SENHA  = "";
const char* MQTT_HOST   = "broker.hivemq.com";
const int   MQTT_PORTA  = 1883;
const char* TOPICO_DADOS  = "cardioia/rm566793/vitais";
const char* TOPICO_STATUS = "cardioia/rm566793/status";

// ---------------- Regras clínicas (limites definidos pelo projeto) ----------------
const float TEMP_FEBRE    = 38.0;  // °C: acima disso = febre
const int   BPM_TAQUI     = 120;   // acima disso = taquicardia
const int   BPM_BRADI     = 50;    // abaixo disso (com pulso detectado) = bradicardia

// ---------------- Tempo ----------------
const unsigned long INTERVALO_LEITURA_MS = 2000;  // demo: 1 leitura a cada 2 s
                                                   // no produto real: 1 a cada 10 s
const unsigned long JANELA_BPM_MS = 6000;          // sem toque há 6 s = sem leitura de pulso

// ---------------- Buffer circular (resiliência offline) ----------------
// Por que 3000 amostras? Cada leitura ocupa 12 bytes -> 36 KB de RAM (o ESP32 tem ~320 KB).
// No produto real (1 leitura a cada 10 s), 3000 amostras = 8 h 20 min sem internet:
// cobre uma noite inteira de sono ou um turno, o pior caso do nosso paciente vestível.
// Se encher, a leitura MAIS ANTIGA é sobrescrita (FIFO) e contamos a perda.
const int CAPACIDADE = 3000;

struct Leitura {
  uint32_t seq;      // número da leitura (para achar buracos no histórico)
  uint32_t t_ms;     // momento da leitura, em ms desde que o aparelho ligou
  int16_t  temp10;   // temperatura x10 (36.8 °C -> 368) para caber em 2 bytes
  uint8_t  umid;     // umidade em %
  uint8_t  bpm;      // batimentos por minuto (0 = sem pulso detectado)
};

Leitura buffer[CAPACIDADE];
int inicio = 0;           // posição da leitura mais antiga
int qtd = 0;              // quantas leituras estão guardadas
uint32_t perdidas = 0;    // quantas foram sobrescritas por falta de espaço
uint32_t seqAtual = 0;

// ---------------- Batimentos (sensor 2: botão) ----------------
// A interrupção grava o horário de cada toque. Com os últimos toques calculamos o BPM.
const int MAX_TOQUES = 6;
volatile unsigned long toques[MAX_TOQUES];
volatile int nToques = 0;
volatile unsigned long ultimoToque = 0;

void IRAM_ATTR aoTocar() {
  unsigned long agora = millis();
  if (agora - ultimoToque < 200) return;   // anti-repique: ignora toques < 200 ms (máx. 300 bpm)
  ultimoToque = agora;
  if (nToques < MAX_TOQUES) {
    toques[nToques++] = agora;
  } else {                                 // desliza a janela: descarta o toque mais velho
    for (int i = 1; i < MAX_TOQUES; i++) toques[i - 1] = toques[i];
    toques[MAX_TOQUES - 1] = agora;
  }
}

int calcularBPM() {
  noInterrupts();
  int n = nToques;
  unsigned long primeiro = n > 0 ? toques[0] : 0;
  unsigned long ultimo   = n > 0 ? toques[n - 1] : 0;
  interrupts();
  if (n < 2 || millis() - ultimo > JANELA_BPM_MS) {
    if (n > 0 && millis() - ultimo > JANELA_BPM_MS) { noInterrupts(); nToques = 0; interrupts(); }
    return 0;                              // sem pulso recente = sem leitura
  }
  // média dos intervalos entre toques -> batimentos por minuto
  return (int)(60000.0 * (n - 1) / (ultimo - primeiro) + 0.5);
}

// ---------------- Objetos ----------------
DHTesp dht;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
unsigned long ultimaLeitura = 0;
unsigned long ultimaTentativaMqtt = 0;
bool estavaOnline = false;

// ---------------- Buffer: guardar e ler ----------------
void guardar(const Leitura& l) {
  if (qtd < CAPACIDADE) {
    buffer[(inicio + qtd) % CAPACIDADE] = l;
    qtd++;
  } else {                                 // cheio: sobrescreve a mais antiga
    buffer[inicio] = l;
    inicio = (inicio + 1) % CAPACIDADE;
    perdidas++;
  }
}

String classificar(float temp, int bpm) {
  if (temp > TEMP_FEBRE && bpm > BPM_TAQUI) return "FEBRE+TAQUICARDIA";
  if (temp > TEMP_FEBRE)                    return "FEBRE";
  if (bpm > BPM_TAQUI)                      return "TAQUICARDIA";
  if (bpm > 0 && bpm < BPM_BRADI)           return "BRADICARDIA";
  return "OK";
}

String paraJSON(const Leitura& l) {
  float temp = l.temp10 / 10.0;
  unsigned long atraso = (millis() - l.t_ms) / 1000;   // há quantos segundos foi medida
  String j = "{";
  j += "\"id\":\"cardioia-rm566793\",";
  j += "\"seq\":" + String(l.seq) + ",";
  j += "\"temp\":" + String(temp, 1) + ",";
  j += "\"umid\":" + String(l.umid) + ",";
  j += "\"bpm\":" + String(l.bpm) + ",";
  j += "\"alerta\":\"" + classificar(temp, l.bpm) + "\",";
  j += "\"atraso_s\":" + String(atraso);
  j += "}";
  return j;
}

// ---------------- Conectividade ----------------
bool wifiSimulado() {
  // Variável booleana pedida no enunciado: a chave deslizante decide se há "internet".
  return digitalRead(PINO_CHAVE) == HIGH;
}

void conectarWiFiReal() {
  Serial.print("[REDE] Conectando ao Wi-Fi do simulador");
  WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " ok" : " falhou (seguimos offline)");
}

bool garantirMqtt() {
  if (mqtt.connected()) return true;
  if (millis() - ultimaTentativaMqtt < 5000) return false;   // tenta de novo a cada 5 s, sem travar
  ultimaTentativaMqtt = millis();
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return false; }
  String clientId = "cardioia-rm566793-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("[MQTT] Conectando ao broker... ");
  if (mqtt.connect(clientId.c_str(), TOPICO_STATUS, 0, true, "{\"estado\":\"desconectado\"}")) {
    Serial.println("ok");
    return true;
  }
  Serial.println("falhou, estado=" + String(mqtt.state()));
  return false;
}

void publicarStatus(const char* estado) {
  if (!mqtt.connected()) return;
  String s = "{\"estado\":\"" + String(estado) + "\",\"buffer\":" + String(qtd) +
             ",\"capacidade\":" + String(CAPACIDADE) + ",\"perdidas\":" + String(perdidas) + "}";
  mqtt.publish(TOPICO_STATUS, s.c_str(), true);   // retain: o painel vê o último estado ao abrir
}

// Envia tudo o que está guardado e "apaga o arquivo local" (esvazia o buffer).
// No máximo 25 por volta do loop, para o aparelho não travar se houver milhares guardadas.
void sincronizar() {
  int enviadas = 0;
  int pendentesAntes = qtd;
  while (qtd > 0 && enviadas < 25) {
    String json = paraJSON(buffer[inicio]);
    if (!mqtt.publish(TOPICO_DADOS, json.c_str())) {
      Serial.println("[MQTT] envio falhou; a leitura continua guardada");
      return;                              // só apaga depois de enviar com sucesso
    }
    Serial.println("[NUVEM] " + json);
    inicio = (inicio + 1) % CAPACIDADE;
    qtd--;
    enviadas++;
  }
  if (pendentesAntes > 1 && qtd == 0) {   // só avisa quando havia atraso acumulado
    Serial.println("[SYNC] buffer vazio: tudo sincronizado");
    publicarStatus("online");
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_CHAVE, INPUT);
  pinMode(PINO_LED_AL, OUTPUT);
  pinMode(PINO_LED_NET, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(PINO_BOTAO), aoTocar, FALLING);
  dht.setup(PINO_DHT, DHTesp::DHT22);

  Serial.println();
  Serial.println("=== CardioIA Conectada | RM566793 ===");
  Serial.println("Chave para a DIREITA = online | ESQUERDA = offline");
  Serial.println("Toque o botao no ritmo do coracao para gerar o BPM");

  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setBufferSize(512);
  conectarWiFiReal();
}

void loop() {
  bool online = wifiSimulado() && garantirMqtt();
  if (mqtt.connected()) mqtt.loop();
  digitalWrite(PINO_LED_NET, online ? HIGH : LOW);

  if (online != estavaOnline) {            // mudou de estado: avisa no Serial e no broker
    if (online) {
      Serial.println("[REDE] ONLINE: sincronizando " + String(qtd) + " leituras guardadas");
      publicarStatus("online");
    } else {
      Serial.println("[REDE] OFFLINE: coletando e guardando no ESP32");
    }
    estavaOnline = online;
  }

  // 1) Coleta: acontece SEMPRE, com ou sem internet
  if (millis() - ultimaLeitura >= INTERVALO_LEITURA_MS) {
    ultimaLeitura = millis();
    TempAndHumidity th = dht.getTempAndHumidity();
    if (isnan(th.temperature) || isnan(th.humidity)) {
      Serial.println("[SENSOR] falha na leitura do DHT22");
    } else {
      Leitura l;
      l.seq    = ++seqAtual;
      l.t_ms   = millis();
      l.temp10 = (int16_t)lround(th.temperature * 10);
      l.umid   = (uint8_t)constrain(lround(th.humidity), 0, 100);
      l.bpm    = (uint8_t)constrain(calcularBPM(), 0, 255);
      guardar(l);

      String alerta = classificar(th.temperature, l.bpm);
      digitalWrite(PINO_LED_AL, alerta != "OK" ? HIGH : LOW);

      if (!online) {
        Serial.printf("[OFFLINE] leitura #%lu guardada | %.1f C | %u bpm | %s | buffer %d/%d | perdidas %lu\n",
                      (unsigned long)l.seq, th.temperature, l.bpm, alerta.c_str(), qtd, CAPACIDADE,
                      (unsigned long)perdidas);
      }
    }
  }

  // 2) Envio: só quando há conexão
  if (online) sincronizar();
}
