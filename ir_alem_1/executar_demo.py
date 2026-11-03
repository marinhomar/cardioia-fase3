"""
executar_demo.py
----------------
Roda o Ir Além 1 inteiro com um só comando:

    python executar_demo.py

O que acontece:
  1. Liga a API (api_nuvem.py) em segundo plano, em http://127.0.0.1:5000
  2. Liga o correio de teste (servidor_email_teste.py) em 127.0.0.1:1025
  3. Roda o cliente (cliente_rest.py), que envia, recebe, avalia e alerta
  4. Desliga tudo no final

Os e-mails ficam salvos em caixa_de_entrada/ para você abrir e conferir.
"""

import logging
import os
import sys
import threading
from urllib.parse import urlparse

from werkzeug.serving import make_server

import api_nuvem
import cliente_rest
import servidor_email_teste

logging.getLogger("werkzeug").setLevel(logging.ERROR)   # esconde o log de cada requisição


def main():
    porta_api = urlparse(cliente_rest.API).port or 5000
    porta_email = int(os.getenv("SMTP_PORT", "1025"))
    # o correio de teste só é ligado quando o .env não aponta para um servidor de e-mail real
    usar_correio_teste = os.getenv("SMTP_HOST", "127.0.0.1") in ("127.0.0.1", "localhost")
    correio = None
    try:
        api = make_server("127.0.0.1", porta_api, api_nuvem.app)
        if usar_correio_teste:
            correio = servidor_email_teste.iniciar(porta_email)
    except OSError:
        print(f"Não consegui ligar a API na porta {porta_api} ou o correio na porta {porta_email}.")
        print("Alguma delas já está em uso. Feche o outro programa ou troque a porta no arquivo .env.")
        sys.exit(1)
    threading.Thread(target=api.serve_forever, daemon=True).start()
    print("API e correio de teste ligados.\n")

    try:
        cliente_rest.main()
    finally:
        if correio:
            correio.stop()
        api.shutdown()
        print("\nAPI e correio de teste desligados.")
        if correio:
            print(f"E-mails salvos na pasta {servidor_email_teste.PASTA.name}/")


if __name__ == "__main__":
    main()
