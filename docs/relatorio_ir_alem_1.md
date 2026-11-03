# Ir Além 1: API REST e alerta automático por e-mail

**CardioIA Conectada, Fase 3** · Marlon Paulino Marinho, RM566793 · Grupo 67  
Código: pasta `ir_alem_1/` do repositório

## O que eu quis resolver

Neste desafio extra, criei um pequeno "robô" em Python que recebe as leituras de sinais vitais por uma API, decide se há risco e avisa a equipe de saúde por e-mail. Assim o alerta chega mesmo quando ninguém está olhando o painel, como de madrugada ou num plantão cheio.

## As peças

Separei o trabalho em arquivos pequenos, cada um com uma função. `api_nuvem.py` é a API REST (feita com Flask) que guarda e devolve as leituras. `cliente_rest.py` é o cliente que conversa com ela usando a biblioteca `requests`. `regras_risco.py` decide se há risco, e `alerta_email.py` manda o aviso. Para testar sem depender de nada externo, `servidor_email_teste.py` funciona como um servidor de e-mail no próprio computador, e `executar_demo.py` liga tudo com um único comando.

## O fluxo, passo a passo

Para cada leitura, o cliente faz quatro coisas:

1. **Envia** a leitura para a API com um pedido `POST /leituras`. A API responde com o código 201 ("criado") e devolve a leitura já numerada e com a hora de chegada. Se faltar o código da pessoa, a temperatura ou os batimentos, ou se algum valor não for número, a API responde 400 ("requisição inválida").
2. **Recebe** de volta as três leituras mais recentes da mesma pessoa, com `GET /leituras?paciente=CARDIO-001&ultimas=3`. O cliente usa sempre o que a API devolve, e não uma cópia própria, para trabalhar com os mesmos dados que qualquer outro sistema veria.
3. **Avalia** o risco com as regras abaixo.
4. **Avisa** por e-mail quando há alerta.

As leituras têm o mesmo formato que o ESP32 publica no MQTT. O cliente segue um roteiro de 12 leituras (normal, febre, coração acelerado, sem pulso e recuperação) para que o teste passe por todas as regras.

## As regras de risco

| Situação | Atenção | Crítico |
|---|---|---|
| Febre | acima de 38,0 °C | a partir de 39,5 °C |
| Coração acelerado (taquicardia) | acima de 120 por minuto | acima de 150 por minuto |
| Coração lento (bradicardia) | abaixo de 50 por minuto, com pulso | não usei |
| Sem pulso | não usei | 3 leituras seguidas com 0 batimentos |

A regra do pulso pede três leituras seguidas. Uma leitura zerada pode ser só o sensor mal encaixado no pulso. Três seguidas já justificam alguém ir conferir a pessoa e o aparelho.

## O e-mail de alerta

O envio usa `smtplib`, `email.message` e `ssl`, as bibliotecas vistas no capítulo de RPA. O e-mail explica o alerta em linguagem simples, mostra temperatura e batimentos, sugere verificar a pessoa e lembra que não substitui a avaliação de profissionais de saúde.

Para não lotar a caixa da equipe, o mesmo alerta (mesma pessoa, mesmo tipo, mesmo nível) não é repetido em menos de 10 minutos. Se a situação piora, de "atenção" para "crítico", o e-mail sai na hora. Quem recebe avisos demais começa a ignorar todos, e isso é perigoso num hospital.

Para testar sem mandar nada para fora, o e-mail vai, por padrão, para um correio de teste no próprio computador, que fala o mesmo protocolo de um servidor real e salva cada mensagem como `.eml`. Para envio real (ex.: Gmail com "senha de app"), basta preencher o `.env` a partir do `env_exemplo.txt`; a senha fica fora do código e do GitHub.

## O resultado da demonstração

Rodei `python executar_demo.py` (saída completa em `ir_alem_1/saida_exemplo/execucao_demo.txt`). Trecho:

```
 nº |  temp |  bpm | situação                   | resultado
  4 |  38.4 |   92 | começa a febre             | ATENÇÃO: FEBRE  -> e-mail enviado
  5 |  39.6 |  104 | febre alta                 | CRÍTICO: FEBRE  -> e-mail enviado
  6 |  38.9 |  136 | coração acelera            | ATENÇÃO: FEBRE, TAQUICARDIA  -> e-mail enviado
  7 |  38.7 |  158 | coração muito acelerado    | CRÍTICO: FEBRE, TAQUICARDIA  -> e-mail enviado
  9 |  37.3 |    0 | sensor sem pulso           | NORMAL
 10 |  37.3 |    0 | sensor sem pulso           | CRÍTICO: SEM_PULSO  -> e-mail enviado
Fim: 12 leituras guardadas na API e 5 e-mails de alerta enviados.
```

As 12 leituras foram guardadas e lidas de volta pela API. Cinco geraram alerta e cada uma virou um e-mail. Na leitura 6, a febre já tinha sido avisada, então o e-mail falou só da taquicardia. As leituras 8 e 9, sem pulso, ainda não geraram alerta; a 10, terceira seguida, gerou. Dois e-mails recebidos estão em `saida_exemplo/`, e o `teste_regras.py` confere os limites, inclusive casos de fronteira como 38,0 °C, que ainda não é febre.

