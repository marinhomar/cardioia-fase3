"""
servidor_email_teste.py
-----------------------
Um "correio de mentira" que roda no próprio computador.

Ele recebe e-mails pelo protocolo SMTP, como um servidor de verdade, mas
em vez de entregar para alguém, salva cada mensagem como arquivo .eml na
pasta caixa_de_entrada/. Arquivos .eml abrem no Outlook, no Thunderbird
ou em qualquer editor de texto.

Para rodar sozinho:  python servidor_email_teste.py   (escuta em 127.0.0.1:1025)
"""

import time
from datetime import datetime
from pathlib import Path

from aiosmtpd.controller import Controller

PASTA = Path(__file__).parent / "caixa_de_entrada"


class SalvarEmArquivo:
    async def handle_DATA(self, server, session, envelope):
        PASTA.mkdir(exist_ok=True)
        nome = datetime.now().strftime("%Y%m%d_%H%M%S_%f") + ".eml"
        (PASTA / nome).write_bytes(envelope.original_content or envelope.content)
        print(f"   [correio de teste] e-mail recebido para {', '.join(envelope.rcpt_tos)} -> {nome}")
        return "250 OK"


def iniciar(porta=1025):
    controle = Controller(SalvarEmArquivo(), hostname="127.0.0.1", port=porta)
    controle.start()
    return controle


if __name__ == "__main__":
    c = iniciar()
    print("Correio de teste ouvindo em 127.0.0.1:1025. Ctrl+C para parar.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        c.stop()
