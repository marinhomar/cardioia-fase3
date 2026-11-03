# Parte 1: armazenamento e processamento no próprio aparelho

**CardioIA Conectada, Fase 3** · Marlon Paulino Marinho, RM566793 · Grupo 67  
Simulação: https://wokwi.com/projects/479676068207522817 · Código: `esp32/sketch.ino`

## O que eu montei

Simulei no Wokwi um monitor cardíaco vestível. A ideia é a de uma pulseira que uma pessoa com problema no coração usaria em casa. O "cérebro" é um ESP32, uma plaquinha com Wi-Fi muito usada em projetos de IoT, ligado a estas peças:

| Peça | Pino | Para que serve |
|---|---|---|
| Sensor DHT22 | 15 | Mede a temperatura (e a umidade). É o sensor obrigatório. |
| Botão vermelho | 4 | Cada toque conta como um batimento. É o meu segundo sensor. |
| Chave deslizante | 5 | Funciona como o Wi-Fi: para a direita está online, para a esquerda, offline. |
| LED vermelho | 2 | Acende quando aparece um sinal de risco. |
| LED azul | 18 | Acende quando o aparelho está conectado e enviando. |

Escolhi o botão como segundo sensor porque o próprio enunciado sugere esse caminho para simular os batimentos. O código guarda o horário dos últimos 6 toques e calcula a média do intervalo entre eles. Um toque por segundo, por exemplo, dá 60 batimentos por minuto. Se ninguém tocar por 6 segundos, o aparelho registra 0, que aqui significa "sem pulso detectado".

## Como o aparelho trabalha

A cada 2 segundos na simulação (no uso real, eu usaria 10 segundos), o ESP32 repete o mesmo ciclo:

1. Lê a temperatura e calcula os batimentos.
2. Confere os limites que defini: febre acima de 38 °C, coração acelerado (taquicardia) acima de 120 batimentos por minuto e coração lento (bradicardia) abaixo de 50. Se algum for ultrapassado, o LED vermelho acende na hora, com ou sem internet.
3. Guarda a leitura na memória. Isso acontece sempre, mesmo quando há conexão.
4. Olha a chave do Wi-Fi. Se estiver offline, só avisa no Monitor Serial que guardou mais uma leitura. Se estiver online, envia as leituras guardadas, da mais antiga para a mais nova, e apaga cada uma depois de enviada.

## A reserva para quando a internet cai

Essa é a parte que considero mais importante, porque é ela que garante que nada se perde.

### Onde guardar

O enunciado sugere o SPIFFS, que é a memória de arquivos do ESP32. Só que, como o próprio material explica, no simulador essa memória é apagada quando a simulação para. Por isso guardei as leituras numa fila na memória RAM e usei o Monitor Serial como registro do que foi enviado. Num ESP32 de verdade, a mesma lógica gravaria no SPIFFS ou num cartão microSD.

### Quanto guardar

Defini espaço para 3.000 leituras. Cada uma ocupa 12 bytes (número da leitura, horário, temperatura, umidade e batimentos), então o total fica perto de 36 KB, cerca de 11% dos uns 320 KB de RAM que o ESP32 deixa livres para dados. Com uma leitura a cada 10 segundos, 3.000 leituras cobrem mais de 8 horas sem internet. Pensei no caso mais comum: a pessoa dorme, o roteador cai de madrugada e só volta de manhã.

### Quando a memória enche

A leitura mais antiga dá lugar à mais nova, e um contador registra quantas foram descartadas. Fiz essa escolha porque, para quem cuida da pessoa, saber como ela está agora vale mais do que saber como estava 8 horas atrás.

### Enviar sem travar

Quando a conexão volta, o aparelho envia no máximo 25 leituras de cada vez e volta a medir. Assim, mesmo com milhares de leituras na fila, ele não para de vigiar a pessoa enquanto sincroniza.

### Saber se algo se perdeu

Cada leitura sai com um número em sequência e com o tempo que ficou esperando (em segundos). Se aparecer um buraco na numeração, alguma leitura faltou. Pelo tempo de espera, o painel consegue mostrar que aquele dado "veio da reserva" e não é ao vivo.

## O teste

Iniciei a simulação com a chave para a esquerda (offline). O aparelho guardou 20 leituras. No meio do caminho, subi a temperatura do sensor para 38,9 °C e toquei o botão bem rápido. O próprio ESP32 classificou as leituras como FEBRE e FEBRE + TAQUICARDIA e acendeu o LED vermelho, sem nenhuma conexão.

Os batimentos perto de 250 por minuto nos prints não são um erro: toquei o botão bem rápido para provocar o alerta de taquicardia.

Depois passei a chave para a direita. O Monitor Serial mostrou "ONLINE: sincronizando 20 leituras guardadas". Em seguida as leituras saíram na ordem (1, 2, 3...), cada uma com o tempo que tinha esperado (39 s, 37 s, 35 s...). No fim apareceu "buffer vazio: tudo sincronizado". Nenhuma leitura se perdeu.

![Monitor Serial com as leituras guardadas offline; as últimas aparecem como febre e taquicardia, e o LED vermelho está aceso](../prints/01_wokwi_offline_buffer_alerta.jpg)

![Monitor Serial ao voltar a conexão: as 20 leituras guardadas são enviadas em ordem, cada uma com o seu tempo de espera](../prints/02_wokwi_online_sincronizando.jpg)

![Fim da sincronização: o Monitor Serial avisa que a reserva ficou vazia e as leituras novas passam a sair ao vivo](../prints/02b_wokwi_sync_concluido.jpg)

## Para testar você mesmo

Abra o link do Wokwi e inicie a simulação. Deixe a chave para a esquerda por alguns segundos e acompanhe o contador do buffer subindo. Toque o botão e mexa na temperatura do DHT22. Depois passe a chave para a direita e veja as leituras guardadas saindo pelo Monitor Serial.
