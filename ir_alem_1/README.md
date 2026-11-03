# Ir Além 1: API REST e alerta por e-mail

Um programa em Python que recebe leituras de sinais vitais por uma API, decide se há risco e avisa a equipe de saúde por e-mail. A explicação completa está no relatório [`../docs/relatorio_ir_alem_1.pdf`](../docs/relatorio_ir_alem_1.pdf).

## Como rodar

Precisa de Python 3.10 ou mais novo.

```bash
pip install -r requirements.txt
python executar_demo.py
```

O `executar_demo.py` liga a API e o correio de teste, roda o cliente com 12 leituras simuladas e desliga tudo no final. Os e-mails ficam em `caixa_de_entrada/`.

Para conferir só as regras de risco:

```bash
python teste_regras.py
```

Se preferir ver cada parte separada, abra três terminais nesta pasta e rode, nesta ordem: `python api_nuvem.py`, `python servidor_email_teste.py` e `python cliente_rest.py`.

## Arquivos

| Arquivo | Papel |
|---|---|
| `api_nuvem.py` | API REST (Flask) que guarda e devolve as leituras |
| `cliente_rest.py` | Envia (POST) e recebe (GET) as leituras, avalia e alerta |
| `regras_risco.py` | Limites de febre, taquicardia, bradicardia e falta de pulso |
| `alerta_email.py` | Monta e envia o e-mail por SMTP, sem repetir o mesmo aviso em menos de 10 minutos |
| `servidor_email_teste.py` | Correio de teste local que salva os e-mails como arquivos `.eml` |
| `executar_demo.py` | Roda tudo com um comando |
| `teste_regras.py` | Testes simples das regras |
| `env_exemplo.txt` | Modelo de configuração para mandar e-mails de verdade (ex.: Gmail) |
| `saida_exemplo/` | A saída de uma execução e dois e-mails recebidos |

## Endereços da API

| Pedido | O que faz |
|---|---|
| `GET /saude` | Diz se a API está no ar |
| `POST /leituras` | Guarda uma leitura nova (JSON com `paciente_id`, `temp` e `bpm`) |
| `GET /leituras?paciente=ID&ultimas=N` | Devolve as últimas N leituras de uma pessoa |
| `GET /leituras/<numero>` | Devolve uma leitura específica |

## Próximo passo possível

Para ligar este robô ao ESP32 de verdade, bastaria acrescentar ao fluxo do Node-RED um nó "http request" que faça o `POST` de cada mensagem MQTT recebida para esta API.
