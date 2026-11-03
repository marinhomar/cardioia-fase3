"""
cliente_rest.py
---------------
O cliente que conversa com a API. É o "robô" do Ir Além 1.

Para cada leitura do cenário de teste ele faz quatro coisas:
  1. ENVIA a leitura para a API (POST /leituras)
  2. RECEBE de volta as últimas leituras do paciente (GET /leituras)
  3. AVALIA o risco com as regras de regras_risco.py
  4. Se houver alerta, DISPARA um e-mail para a equipe (alerta_email.py)

As leituras têm o mesmo formato que o ESP32 publica no MQTT
(temp, umid, bpm, seq). O cenário simula uma pessoa que fica bem,
tem febre, depois o coração acelera, o sensor perde o pulso e,
por fim, tudo volta ao normal.

Para rodar: primeiro a API e o correio de teste, depois este arquivo.
O jeito mais fácil é usar executar_demo.py, que faz tudo de uma vez.
"""

import os
import time

import requests
from dotenv import load_dotenv

import alerta_email
import regras_risco

load_dotenv()
API = os.getenv("API_URL", "http://127.0.0.1:5000")
PACIENTE = "CARDIO-001"

# (temperatura °C, batimentos por minuto, o que está acontecendo)
CENARIO = [
    (36.6, 78, "normal"), (36.7, 82, "normal"), (36.8, 80, "normal"),
    (38.4, 92, "começa a febre"), (39.6, 104, "febre alta"),
    (38.9, 136, "coração acelera"), (38.7, 158, "coração muito acelerado"),
    (37.4, 0, "sensor sem pulso"), (37.3, 0, "sensor sem pulso"), (37.3, 0, "sensor sem pulso"),
    (36.9, 74, "pulso volta"), (36.7, 76, "normal"),
]


def enviar(leitura):
    """POST: manda a leitura para a API e devolve o que a API guardou."""
    resposta = requests.post(f"{API}/leituras", json=leitura, timeout=5)
    resposta.raise_for_status()      # se não vier 2xx, para aqui com erro claro
    return resposta.json()


def receber_ultimas(paciente, quantidade):
    """GET: pede à API as últimas leituras do paciente."""
    resposta = requests.get(f"{API}/leituras",
                            params={"paciente": paciente, "ultimas": quantidade}, timeout=5)
    resposta.raise_for_status()
    return resposta.json()


def main(pausa=0.3):
    print(f"API em {API}")
    print(requests.get(f"{API}/saude", timeout=5).json())
    print()
    print(f"{'nº':>3} | {'temp':>5} | {'bpm':>4} | {'situação':<26} | resultado")
    print("-" * 90)

    emails = 0
    for seq, (temp, bpm, situacao) in enumerate(CENARIO, start=1):
        guardada = enviar({"paciente_id": PACIENTE, "seq": seq, "temp": temp, "umid": 55, "bpm": bpm})

        # Recebe da API as leituras mais recentes. A última é a que acabamos de enviar.
        recentes = receber_ultimas(PACIENTE, regras_risco.LIMITES["sem_pulso_leituras"])
        atual, historico = recentes[-1], recentes[:-1]

        alertas = regras_risco.avaliar(atual, historico)
        nivel = regras_risco.nivel_geral(alertas)

        resultado = nivel.upper()
        if alertas:
            resultado += ": " + ", ".join(a["tipo"] for a in alertas)
            if alerta_email.enviar_alerta(atual, alertas):
                resultado += "  -> e-mail enviado"
                emails += 1
            else:
                resultado += "  -> já avisado há pouco, sem novo e-mail"

        print(f"{guardada['numero']:>3} | {temp:>5.1f} | {bpm:>4} | {situacao:<26} | {resultado}")
        time.sleep(pausa)

    total = len(receber_ultimas(PACIENTE, 1000))
    print("-" * 90)
    print(f"Fim: {total} leituras guardadas na API e {emails} e-mails de alerta enviados.")


if __name__ == "__main__":
    main()
