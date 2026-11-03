"""
teste_regras.py
---------------
Confere as regras de risco com casos simples. Rode: python teste_regras.py
Se aparecer "Todas as regras passaram", está tudo certo.
"""

from regras_risco import avaliar, nivel_geral


def tipos(leitura, historico=()):
    return [a["tipo"] for a in avaliar(leitura, list(historico))]


normal = {"temp": 36.6, "bpm": 80}
assert tipos(normal) == []
assert nivel_geral(avaliar(normal, [])) == "normal"

assert tipos({"temp": 38.0, "bpm": 80}) == []                 # 38,0 ainda não é febre
assert tipos({"temp": 38.1, "bpm": 80}) == ["FEBRE"]
assert nivel_geral(avaliar({"temp": 39.5, "bpm": 80}, [])) == "crítico"

assert tipos({"temp": 36.6, "bpm": 121}) == ["TAQUICARDIA"]
assert nivel_geral(avaliar({"temp": 36.6, "bpm": 151}, [])) == "crítico"
assert tipos({"temp": 36.6, "bpm": 45}) == ["BRADICARDIA"]

sem = {"temp": 36.6, "bpm": 0}
assert tipos(sem) == []                                       # uma leitura sem pulso: ainda não alerta
assert tipos(sem, [sem]) == []                                # duas: ainda não
assert tipos(sem, [sem, sem]) == ["SEM_PULSO"]                # três seguidas: alerta

assert tipos({"temp": 38.9, "bpm": 136}) == ["FEBRE", "TAQUICARDIA"]

print("Todas as regras passaram.")
