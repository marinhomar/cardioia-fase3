"""
alerta_email.py
---------------
Monta e envia o e-mail de alerta usando o protocolo SMTP.

As configurações vêm de variáveis de ambiente (arquivo .env):
  SMTP_HOST, SMTP_PORT, SMTP_SSL, SMTP_USUARIO, SMTP_SENHA,
  EMAIL_REMETENTE, EMAIL_DESTINO

Sem .env, o envio vai para o servidor de e-mail de teste que roda no
próprio computador (127.0.0.1:1025). Assim dá para testar sem conta de
e-mail e sem mandar nada para fora. Para enviar de verdade (ex.: Gmail,
porta 465 com senha de app), basta preencher o .env.

Para não lotar a caixa da equipe, o mesmo alerta (mesmo paciente, mesmo
tipo e mesmo nível) só é reenviado depois de um intervalo (INTERVALO_MINIMO_S).
Se a situação PIORA (de "atenção" para "crítico"), o e-mail sai na hora.
"""

import os
import smtplib
import ssl
import time
from email.message import EmailMessage

INTERVALO_MINIMO_S = 600   # 10 minutos entre e-mails iguais

_ultimo_envio = {}         # (paciente, tipo, nível) -> momento do último e-mail


def _config():
    return {
        "host": os.getenv("SMTP_HOST", "127.0.0.1"),
        "porta": int(os.getenv("SMTP_PORT", "1025")),
        "usar_ssl": os.getenv("SMTP_SSL", "nao").lower() in ("sim", "true", "1"),
        "usuario": os.getenv("SMTP_USUARIO", ""),
        "senha": os.getenv("SMTP_SENHA", ""),
        "remetente": os.getenv("EMAIL_REMETENTE", "monitor@cardioia.local"),
        "destino": os.getenv("EMAIL_DESTINO", "equipe.plantao@cardioia.local"),
    }


def deve_enviar(paciente_id, alertas):
    """Filtra só os alertas que ainda não foram avisados nos últimos 10 minutos.
    Como o nível faz parte da chave, uma piora sempre gera e-mail novo."""
    agora = time.time()
    novos = []
    for a in alertas:
        chave = (paciente_id, a["tipo"], a["nivel"])
        if agora - _ultimo_envio.get(chave, 0) >= INTERVALO_MINIMO_S:
            novos.append(a)
    return novos


def montar_email(leitura, alertas, nivel, cfg):
    msg = EmailMessage()
    msg["From"] = cfg["remetente"]
    msg["To"] = cfg["destino"]
    tipos = " + ".join(a["tipo"] for a in alertas)
    msg["Subject"] = f"[CardioIA] {nivel.upper()}: {tipos} - {leitura['paciente_id']}"

    linhas = "\n".join(f"  - {a['mensagem']} ({a['nivel']})" for a in alertas)
    msg.set_content(
        f"Olá, equipe.\n\n"
        f"O monitor cardíaco vinculado ao código {leitura['paciente_id']} registrou o seguinte:\n\n"
        f"{linhas}\n\n"
        f"Leitura nº {leitura['numero']}, recebida em {leitura['recebida_em']}.\n"
        f"Temperatura: {str(leitura['temp']).replace('.', ',')} °C | Batimentos: {leitura['bpm']} por minuto\n\n"
        f"Sugestão: verificar a pessoa e confirmar a medição.\n\n"
        f"Este é um aviso automático do protótipo CardioIA (projeto acadêmico FIAP).\n"
        f"Ele não substitui a avaliação de profissionais de saúde.\n"
    )
    return msg


def enviar_alerta(leitura, alertas):
    """Envia o e-mail. Devolve True se enviou, False se não havia nada novo para avisar."""
    novos = deve_enviar(leitura["paciente_id"], alertas)
    if not novos:
        return False

    cfg = _config()
    # o assunto mostra o nível dos alertas que estão sendo avisados agora
    nivel = "crítico" if any(a["nivel"] == "crítico" for a in novos) else "atenção"
    msg = montar_email(leitura, novos, nivel, cfg)

    if cfg["usar_ssl"]:
        contexto = ssl.create_default_context()
        with smtplib.SMTP_SSL(cfg["host"], cfg["porta"], context=contexto, timeout=10) as servidor:
            servidor.login(cfg["usuario"], cfg["senha"])
            servidor.send_message(msg)
    else:
        with smtplib.SMTP(cfg["host"], cfg["porta"], timeout=10) as servidor:
            if cfg["usuario"]:
                servidor.starttls(context=ssl.create_default_context())
                servidor.login(cfg["usuario"], cfg["senha"])
            servidor.send_message(msg)

    for a in novos:
        _ultimo_envio[(leitura["paciente_id"], a["tipo"], a["nivel"])] = time.time()
    return True
