/*
  CardioIA Conectada | Fase 3 | FIAP, Inteligência Artificial, 2º ano
  Marlon Paulino Marinho, RM566793, Grupo 67

  Um monitor cardíaco vestível simulado no Wokwi.

  Em poucas palavras, a cada 2 segundos o aparelho:
    1. mede a temperatura no DHT22 e calcula os batimentos pelos toques no botão;
    2. acende o LED vermelho se encontrar febre, coração acelerado ou coração lento;
    3. guarda a leitura na memória, com ou sem internet;
    4. se a chave do "Wi-Fi" estiver ligada, envia o que estiver guardado
       (pelo Monitor Serial e por MQTT) e libera a memória.

  Para onde vão os dados: broker público da HiveMQ (broker.hivemq.com, porta 1883).
    cardioia/rm566793/vitais  -> uma leitura por mensagem, em JSON
    cardioia/rm566793/status  -> se o aparelho está online e quanto tem guardado
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"

// ---------------- Onde cada peça está ligada ----------------
const int PINO_DHT     = 15;  // DHT22: temperatura e umidade
const int PINO_BOTAO   = 4;   // botão: cada toque é um batimento
const int PINO_CHAVE   = 5;   // chave deslizante: funciona como o Wi-Fi
const int PINO_LED_AL  = 2;   // LED vermelho: sinal de risco
const int PINO_LED_NET = 18;  // LED azul: conectado e enviando

// ---------------- Rede e envio por MQTT ----------------
const char* WIFI_SSID   = "Wokwi-GUEST";   // rede virtual do Wokwi (sem senha)
const char* WIFI_SENHA  = "";
const char* MQTT_HOST   = "broker.hivemq.com";
const int   MQTT_PORTA  = 1883;
const char* TOPICO_DADOS  = "cardioia/rm566793/vitais";
const char* TOPICO_STATUS = "cardioia/rm566793/status";

// ---------------- Limites de alerta (didáticos, não substituem avaliação médica) ----------------
const float TEMP_FEBRE    = 38.0;  // °C: acima disso, febre
const int   BPM_TAQUI     = 120;   // batimentos por minuto: acima disso, coração acelerado
const int   BPM_BRADI     = 50;    // abaixo disso (havendo pulso), coração lento

// ---------------- Ritmo das medições ----------------
const unsigned long INTERVALO_LEITURA_MS = 2000;  // na demonstração: uma leitura a cada 2 s
                                                   // no uso real eu usaria uma a cada 10 s
const unsigned long JANELA_BPM_MS = 6000;          // 6 s sem toque = sem pulso detectado

// ---------------- Reserva para quando a internet cai ----------------
// Cabem 3000 leituras. Cada uma ocupa 12 bytes, então são uns 36 KB de memória.
// Com uma leitura a cada 10 s, isso cobre mais de 8 horas sem internet: uma noite inteira.
// Se encher, a leitura mais antiga dá lugar à mais nova e o contador "perdidas" registra.
const int CAPACIDADE = 3000;

struct Leitura {
  uint32_t seq;      // número da leitura: um buraco na sequência mostra que algo se perdeu
  uint32_t t_ms;     // quando foi medida (milissegundos desde que o aparelho ligou)
  int16_t  temp10;   // temperatura vezes 10 (36,8 °C vira 368), para ocupar menos espaço
  uint8_t  umid;     // umidade em %
  uint8_t  bpm;      // batimentos por minuto (0 = nenhum pulso detectado)
};

Leitura buffer[CAPACIDADE];
int inicio = 0;           // posição da leitura mais antiga
int qtd = 0;              // quantas leituras estão guardadas
uint32_t perdidas = 0;    // quantas foram sobrescritas por falta de espaço
uint32_t seqAtual = 0;

// ---------------- Batimentos a partir do botão ----------------
// Cada toque guarda o horário. A média do intervalo entre os últimos toques vira o BPM.
const int MAX_TOQUES = 6;
volatile unsigned long toques[MAX_TOQUES];
volatile int nToques = 0;
volatile unsigned long ultimoToque = 0;

void IRAM_ATTR aoTocar() {
  unsigned long agora = millis();
  if (agora - ultimoToque < 200) return;   // ignora toques com menos de 200 ms de diferença (ruído do botão)
  ultimoToque = agora;
  if (nToques < MAX_TOQUES) {
    toques[nToques++] = agora;
  } else {                                 // lista cheia: sai o toque mais antigo, entra o novo
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
    return 0;                              // nenhum toque recente: sem pulso
  }
  // média do intervalo entre os toques, convertida para batimentos por minuto
  return (int)(60000.0 * (n - 1) / (ultimo - primeiro) + 0.5);
}

// ---------------- Peças de software ----------------
DHTesp dht;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
unsigned long ultimaLeitura = 0;
unsigned long ultimaTentativaMqtt = 0;
bool estavaOnline = false;

// ---------------- Guardar na reserva ----------------
void guardar(const Leitura& l) {
  if (qtd < CAPACIDADE) {
    buffer[(inicio + qtd) % CAPACIDADE] = l;
    qtd++;
  } else {                                 // cheia: a mais antiga dá lugar à nova
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
  unsigned long atraso = (millis() - l.t_ms) / 1000;   // há quantos segundos foi medida (0 = ao vivo)
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

// ---------------- Conexão ----------------
bool wifiSimulado() {
  // O enunciado pede uma variável booleana para o Wi-Fi. Aqui ela vem da chave deslizante.
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
  if (millis() - ultimaTentativaMqtt < 5000) return false;   // tenta de novo a cada 5 s, sem parar de medir
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
  mqtt.publish(TOPICO_STATUS, s.c_str(), true);   // "retain": quem abrir o painel depois já vê o último estado
}

// Envia o que está guardado, da leitura mais antiga para a mais nova, e libera a memória.
// Vão no máximo 25 por vez, para o aparelho continuar medindo mesmo com milhares na fila.
void sincronizar() {
  int enviadas = 0;
  int pendentesAntes = qtd;
  while (qtd > 0 && enviadas < 25) {
    String json = paraJSON(buffer[inicio]);
    if (!mqtt.publish(TOPICO_DADOS, json.c_str())) {
      Serial.println("[MQTT] envio falhou; a leitura continua guardada");
      return;                              // só libera a leitura depois que o envio deu certo
    }
    Serial.println("[NUVEM] " + json);
    inicio = (inicio + 1) % CAPACIDADE;
    qtd--;
    enviadas++;
  }
  if (pendentesAntes > 1 && qtd == 0) {   // só avisa quando havia leituras acumuladas
    Serial.println("[SYNC] buffer vazio: tudo sincronizado");
    publicarStatus("online");
  }
}

// ---------------- Início e repetição ----------------
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

  if (online != estavaOnline) {            // a conexão mudou: avisa no Serial e no broker
    if (online) {
      Serial.println("[REDE] ONLINE: sincronizando " + String(qtd) + " leituras guardadas");
      publicarStatus("online");
    } else {
      Serial.println("[REDE] OFFLINE: coletando e guardando no ESP32");
      publicarStatus("offline");   // avisa o painel antes de soltar a conexão
      mqtt.disconnect();
    }
    estavaOnline = online;
  }

  // 1) Medir: acontece sempre, com ou sem internet
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

      // classifica com o mesmo valor arredondado que vai no JSON, para LED e painel concordarem
      String alerta = classificar(l.temp10 / 10.0, l.bpm);
      digitalWrite(PINO_LED_AL, alerta != "OK" ? HIGH : LOW);

      if (!online) {
        Serial.printf("[OFFLINE] leitura #%lu guardada | %.1f C | %u bpm | %s | buffer %d/%d | perdidas %lu\n",
                      (unsigned long)l.seq, l.temp10 / 10.0, l.bpm, alerta.c_str(), qtd, CAPACIDADE,
                      (unsigned long)perdidas);
      }
    }
  }

  // 2) Enviar: só quando há conexão
  if (online) sincronizar();
}
