"""
regras_risco.py
---------------
As regras que decidem se uma leitura é preocupante.

Cada regra olha para a leitura mais recente e, quando precisa, para as
anteriores. O resultado é uma lista de alertas. Lista vazia quer dizer
que está tudo dentro do esperado.

Os limites abaixo são didáticos, escolhidos para o protótipo.
Eles não substituem a avaliação de uma equipe de saúde.
"""

LIMITES = {
    "febre": 38.0,              # °C: acima disso, febre
    "febre_alta": 39.5,         # °C: a partir disso, febre alta (crítico)
    "taquicardia": 120,         # bpm: acima disso, coração acelerado
    "taquicardia_grave": 150,   # bpm: acima disso, crítico
    "bradicardia": 50,          # bpm: abaixo disso (com pulso), coração lento
    "sem_pulso_leituras": 3,    # quantas leituras seguidas com bpm = 0 viram alerta
}


def br(numero):
    """Escreve o número com vírgula, do jeito brasileiro: 38.4 -> '38,4'."""
    return f"{numero:.1f}".replace(".", ",")


def avaliar(leitura, historico):
    """
    leitura   -> dicionário com pelo menos 'temp' e 'bpm'
    historico -> lista com as leituras anteriores do mesmo paciente (a última é a mais nova)

    Devolve uma lista de alertas. Cada alerta tem:
      tipo     -> nome curto do problema
      nivel    -> 'atenção' ou 'crítico'
      mensagem -> frase pronta para mostrar a uma pessoa
    """
    alertas = []
    temp = leitura["temp"]
    bpm = leitura["bpm"]

    # 1) Temperatura
    if temp >= LIMITES["febre_alta"]:
        alertas.append({"tipo": "FEBRE", "nivel": "crítico",
                        "mensagem": f"Febre alta: {br(temp)} °C"})
    elif temp > LIMITES["febre"]:
        alertas.append({"tipo": "FEBRE", "nivel": "atenção",
                        "mensagem": f"Febre: {br(temp)} °C"})

    # 2) Batimentos acelerados
    if bpm > LIMITES["taquicardia_grave"]:
        alertas.append({"tipo": "TAQUICARDIA", "nivel": "crítico",
                        "mensagem": f"Coração muito acelerado: {bpm} batimentos por minuto"})
    elif bpm > LIMITES["taquicardia"]:
        alertas.append({"tipo": "TAQUICARDIA", "nivel": "atenção",
                        "mensagem": f"Coração acelerado: {bpm} batimentos por minuto"})

    # 3) Batimentos lentos (só vale se existe pulso; bpm 0 é tratado na regra 4)
    if 0 < bpm < LIMITES["bradicardia"]:
        alertas.append({"tipo": "BRADICARDIA", "nivel": "atenção",
                        "mensagem": f"Coração lento: {bpm} batimentos por minuto"})

    # 4) Sem pulso por várias leituras seguidas.
    #    Uma leitura com bpm 0 pode ser só o sensor mal posicionado.
    #    Três seguidas já pedem que alguém vá conferir.
    n = LIMITES["sem_pulso_leituras"]
    ultimas = (historico + [leitura])[-n:]
    if len(ultimas) == n and all(l["bpm"] == 0 for l in ultimas):
        alertas.append({"tipo": "SEM_PULSO", "nivel": "crítico",
                        "mensagem": f"Nenhum pulso detectado em {n} leituras seguidas. "
                                    "Verificar a pessoa e o sensor."})

    return alertas


def nivel_geral(alertas):
    """Resume a lista em uma palavra: 'normal', 'atenção' ou 'crítico'."""
    if any(a["nivel"] == "crítico" for a in alertas):
        return "crítico"
    if alertas:
        return "atenção"
    return "normal"
