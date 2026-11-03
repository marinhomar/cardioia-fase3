# CardioIA Conectada — Fase 3: IoT e visualização de dados para a saúde digital

**FIAP — Inteligência Artificial, 2º ano (Fase 3)**  
**Aluno:** Marlon Paulino Marinho — RM566793 (grupo individual)

Protótipo de um **monitor cardíaco vestível** que cobre o fluxo completo de dados em IoT aplicado à saúde:

**captura → processamento local → transmissão → visualização → alerta**

- **Simulação no Wokwi:** https://wokwi.com/projects/479676068207522817
- **Relatório Parte 1 (Edge):** [`docs/relatorio_parte1_edge.pdf`](docs/relatorio_parte1_edge.pdf)
- **Relatório Parte 2 (MQTT + Dashboard):** [`docs/relatorio_parte2_mqtt_dashboard.pdf`](docs/relatorio_parte2_mqtt_dashboard.pdf)

---

## Arquitetura

```
 [ESP32 + DHT22 + botão]  --MQTT-->  [HiveMQ broker]  --MQTT-->  [Node-RED]  -->  [Dashboard]
   Edge: buffer offline              broker.hivemq.com           Fog: regras        gráfico, gauge,
   + alerta local (LED)              cardioia/rm566793/#         de alerta          texto e LED
```

| Componente | Pino | Função |
|---|---|---|
| DHT22 | GPIO 15 | Temperatura e umidade (sensor 1, obrigatório) |
| Botão "Batimento" | GPIO 4 | Cada toque = 1 batimento → BPM (sensor 2) |
| Chave deslizante | GPIO 5 | Simula o Wi-Fi: direita = online, esquerda = offline |
| LED vermelho | GPIO 2 | Alerta clínico (febre, taquicardia, bradicardia) |
| LED azul | GPIO 18 | Online e sincronizando |

## Estrutura do repositório

```
esp32/
  sketch.ino          código C++ comentado (Wokwi / Arduino IDE)
  diagram.json        circuito do Wokwi
  libraries.txt       bibliotecas: DHT sensor library for ESPx, PubSubClient
node-red/
  flow_cardioia.json  fluxo exportado (MQTT -> regras -> dashboard)
docs/
  relatorio_parte1_edge.pdf
  relatorio_parte2_mqtt_dashboard.pdf
prints/               evidências (Wokwi, Serial, Node-RED e dashboard)
```

## Parte 1 — Edge Computing e resiliência offline

- Leitura a cada 2 s na demonstração (no produto real, a cada 10 s).
- **Toda leitura vai primeiro para um buffer circular** de **3.000 amostras** (12 bytes cada, cerca de 36 KB de RAM).
- **Offline:** continua coletando. O Serial mostra `[OFFLINE] leitura #N guardada | buffer N/3000`.
- **Online:** envia da mais antiga para a mais nova (`Serial.println` + MQTT) e só apaga cada leitura depois do envio aceito. Ao final aparece `[SYNC] buffer vazio`.
- **Por que 3.000?** A 1 leitura a cada 10 s, cobre **8 h 20 min sem internet** (uma noite de sono). Se lotar, a mais antiga é sobrescrita e o contador `perdidas` registra o descarte.
- **SPIFFS:** é volátil no simulador (como o próprio enunciado explica). A mesma lógica de buffer gravaria em SPIFFS ou microSD num ESP32 físico.

## Parte 2 — MQTT e dashboard

- **Broker:** `broker.hivemq.com:1883` (HiveMQ público).
- **Tópicos:** `cardioia/rm566793/vitais` (leituras) e `cardioia/rm566793/status` (estado, com *Last Will*).
- **Mensagem:**
  ```json
  {"id":"cardioia-rm566793","seq":42,"temp":36.6,"umid":55,"bpm":84,"alerta":"OK","atraso_s":0}
  ```
- **Dashboard (Node-RED + node-red-dashboard):**
  - gráfico de linha com BPM e temperatura;
  - gauge de temperatura (34–42 °C);
  - texto de estado e LED virtual de alerta;
  - status do dispositivo e ocupação do buffer.

| Regra de alerta | Limite |
|---|---|
| FEBRE | temperatura > 38,0 °C |
| TAQUICARDIA | BPM > 120 |
| BRADICARDIA | BPM < 50 (com pulso detectado) |

## Como executar

**1. Simulação**
1. Abra o link do Wokwi e clique em ▶.
2. Com a chave na **esquerda (offline)**, toque o botão no ritmo de um coração e veja o buffer crescer no Monitor Serial.
3. Clique no DHT22 e suba a temperatura para mais de 38 °C: o LED vermelho acende.
4. Passe a chave para a **direita (online)**: as leituras guardadas são enviadas e o buffer esvazia.

**2. Node-RED**
```bash
npm install -g --unsafe-perm node-red
node-red
```
1. Abra `http://127.0.0.1:1880`.
2. Em **Menu → Manage palette → Install**, instale `node-red-dashboard`.
3. Em **Menu → Import**, importe `node-red/flow_cardioia.json` e clique em **Deploy**.
4. O dashboard fica em `http://127.0.0.1:1880/ui`.

## Evidências

| Offline: buffer e alerta local | Online: sincronização |
|---|---|
| ![](prints/01_wokwi_offline_buffer_alerta.jpg) | ![](prints/02_wokwi_online_sincronizando.jpg) |

![Fluxo Node-RED](prints/03_nodered_fluxo.jpg)

| Febre | Taquicardia | Normal |
|---|---|---|
| ![](prints/05_dashboard_febre.jpg) | ![](prints/06_dashboard_taquicardia.jpg) | ![](prints/04_dashboard_normal.jpg) |

## Segurança e limitações

- O broker público é só para demonstração. Em produção, o caminho é HiveMQ Cloud com TLS (8883), credenciais por dispositivo e ACL por tópico, com as credenciais fora do repositório.
- A mensagem não traz dado pessoal: só o identificador do aparelho (LGPD).
- O BPM é simulado por botão. Os limites são didáticos e não substituem avaliação médica.
