"""
api_nuvem.py
------------
Uma API REST pequena que representa a "nuvem" do CardioIA.

Ela guarda as leituras de sinais vitais que chegam e devolve essas leituras
quando alguém pede. Os dados ficam na memória do computador, o que basta
para um protótipo. Num sistema real, aqui entraria um banco de dados.

Endereços (endpoints) que a API oferece:
  GET  /saude                        -> responde se a API está no ar
  POST /leituras                     -> recebe uma leitura nova (JSON)
  GET  /leituras?paciente=ID&ultimas=N -> devolve as últimas N leituras de um paciente
  GET  /leituras/<numero>            -> devolve uma leitura específica

Para rodar sozinha:  python api_nuvem.py   (fica em http://127.0.0.1:5000)
"""

from datetime import datetime
from threading import Lock

from flask import Flask, jsonify, request

app = Flask(__name__)

leituras = []          # aqui ficam todas as leituras recebidas
trava = Lock()         # evita que duas requisições escrevam ao mesmo tempo

CAMPOS_OBRIGATORIOS = ["paciente_id", "temp", "bpm"]


@app.get("/saude")
def saude():
    return jsonify({"status": "ok", "leituras_guardadas": len(leituras)})


@app.post("/leituras")
def receber_leitura():
    dados = request.get_json(silent=True)

    # Se o corpo não for JSON, faltar algum campo ou vier um valor que não é número,
    # devolvemos 400 (requisição inválida)
    if not isinstance(dados, dict):
        return jsonify({"erro": "o corpo da requisição precisa ser um JSON"}), 400
    faltando = [c for c in CAMPOS_OBRIGATORIOS if c not in dados]
    if faltando:
        return jsonify({"erro": f"faltam os campos: {', '.join(faltando)}"}), 400

    try:
        temp = float(dados["temp"])
        bpm = int(dados["bpm"])
    except (TypeError, ValueError):
        return jsonify({"erro": "temp e bpm precisam ser números"}), 400

    with trava:
        leitura = {
            "numero": len(leituras) + 1,
            "paciente_id": str(dados["paciente_id"]),
            "temp": temp,
            "umid": dados.get("umid"),
            "bpm": bpm,
            "seq_dispositivo": dados.get("seq"),
            "recebida_em": datetime.now().isoformat(timespec="seconds"),
        }
        leituras.append(leitura)

    # 201 = "criado com sucesso"; devolvemos a leitura já com o número que ela ganhou
    return jsonify(leitura), 201


@app.get("/leituras")
def listar_leituras():
    paciente = request.args.get("paciente")
    ultimas = request.args.get("ultimas", type=int)

    with trava:
        resultado = [l for l in leituras if paciente is None or l["paciente_id"] == paciente]
    if ultimas:
        resultado = resultado[-ultimas:]
    return jsonify(resultado)


@app.get("/leituras/<int:numero>")
def buscar_leitura(numero):
    with trava:
        for l in leituras:
            if l["numero"] == numero:
                return jsonify(l)
    return jsonify({"erro": "leitura não encontrada"}), 404


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5000)
