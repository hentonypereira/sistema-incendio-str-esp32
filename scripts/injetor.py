import socket
import csv
import time
import argparse  # Biblioteca que permite passar argumentos pelo terminal.


# ==========================================================
# CONFIGURAÇÃO DAS SIMULAÇÕES DISPONÍVEIS
# ==========================================================

SIMULACOES = {
    "flaming": "data/flaming_fire.csv",
    "smoldering": "data/smoldering_fire.csv"
}


# ==========================================================
# CONFIGURAÇÃO DA CONEXÃO TCP/IP COM O ESP32 SIMULADO
# ==========================================================

ESP32_IP = "127.0.0.1"
ESP32_PORTA = 8080


# ==========================================================
# ARGUMENTOS DO PROGRAMA
# ==========================================================

parser = argparse.ArgumentParser(
    description="Injetor de dados FDS para ESP32 via TCP/IP"
)

parser.add_argument(
    "--cenario",
    choices=["flaming", "smoldering"],
    default="flaming",
    help="Escolhe o cenário de simulação: flaming ou smoldering"
)

parser.add_argument(
    "--velocidade",
    type=float,
    default=1.0,
    help="Fator de velocidade da simulação. 1.0 = tempo real, 2.0 = duas vezes mais rápido"
)

args = parser.parse_args()

if args.velocidade <= 0:
    raise ValueError("A velocidade deve ser maior que zero.")

arquivo_csv = SIMULACOES[args.cenario]


# ==========================================================
# CARREGAMENTO DOS DADOS DO CSV
# ==========================================================

def carregar_amostras(caminho_csv):
    """
    Lê o arquivo CSV do FDS antes do início da transmissão.

    Cada amostra armazenada contém:
    - tempo da simulação;
    - pacote já formatado em texto;
    - pacote já codificado em bytes para envio TCP.
    """

    amostras = []

    with open(caminho_csv, "r") as arquivo:
        leitor = csv.reader(arquivo)

        # Pula a linha das unidades: s,C,%/m
        next(leitor)

        # Pula a linha dos nomes: Time,Termopar_01,Detetor_Fumaca
        next(leitor)

        for linha in leitor:

            if len(linha) < 3:
                continue

            tempo = float(linha[0])
            temperatura = float(linha[1])
            fumaca = float(linha[2])  # obscuração em %/m

            pacote_texto = f"T:{temperatura:.1f},S:{fumaca:.2f}\n"
            pacote_bytes = pacote_texto.encode("utf-8")

            amostras.append(
                {
                    "tempo": tempo,
                    "pacote_texto": pacote_texto,
                    "pacote_bytes": pacote_bytes
                }
            )

    return amostras


def calcular_estatisticas(valores):
    """
    Calcula estatísticas básicas de uma lista de valores.
    """

    if not valores:
        return None

    media = sum(valores) / len(valores)
    menor = min(valores)
    maior = max(valores)
    amplitude = maior - menor

    return media, menor, maior, amplitude


# ==========================================================
# CARREGAMENTO DAS AMOSTRAS
# ==========================================================

try:
    amostras = carregar_amostras(arquivo_csv)

except FileNotFoundError:
    print(f"ERRO: arquivo não encontrado: {arquivo_csv}")
    raise SystemExit(1)

if not amostras:
    print("ERRO: o arquivo CSV não possui amostras válidas.")
    raise SystemExit(1)


# ==========================================================
# INFORMAÇÕES DA SIMULAÇÃO
# ==========================================================

tempo_inicial_csv = amostras[0]["tempo"]
tempo_final_csv = amostras[-1]["tempo"]
duracao_csv = tempo_final_csv - tempo_inicial_csv

# Pré-calcula os instantes relativos desejados em nanossegundos.
# Isso evita fazer contas com ponto flutuante dentro do laço de envio.
for amostra in amostras:
    tempo_relativo = (amostra["tempo"] - tempo_inicial_csv) / args.velocidade
    amostra["tempo_relativo_ns"] = int(tempo_relativo * 1_000_000_000)

print("====================================")
print("Injetor FDS -> ESP32")
print("====================================")
print(f"Cenário selecionado: {args.cenario}")
print(f"Arquivo CSV: {arquivo_csv}")
print(f"Número de amostras: {len(amostras)}")
print(f"Duração do CSV: {duracao_csv:.2f} s")
print(f"Velocidade: {args.velocidade}x")
print("====================================")


# ==========================================================
# CONEXÃO COM O ESP32
# ==========================================================

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# Evita travamento indefinido em caso de problema na conexão.
sock.settimeout(5.0)

# Reduz atrasos em envios pequenos.
# Isso evita que o TCP tente agrupar pequenos pacotes antes de enviá-los.
sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)


try:
    print("Conectando ao ESP32...")

    sock.connect((ESP32_IP, ESP32_PORTA))

    print("Conectado!")
    print("Transmitindo dados...")

    # ======================================================
    # TRANSMISSÃO TEMPORIZADA
    # ======================================================
    # Durante o laço de envio, o código evita prints e cálculos
    # estatísticos para reduzir a interferência na temporização.
    #
    # O laço apenas:
    # 1. calcula o instante desejado;
    # 2. espera até esse instante;
    # 3. registra o instante real de envio;
    # 4. envia o pacote ao ESP32.
    # ======================================================

    tempo_inicial_real_ns = time.perf_counter_ns()

    instantes_envio_ns = [0] * len(amostras)

    for indice, amostra in enumerate(amostras):

        instante_desejado_ns = (
            tempo_inicial_real_ns + amostra["tempo_relativo_ns"]
        )

        agora_ns = time.perf_counter_ns()
        espera_ns = instante_desejado_ns - agora_ns

        if espera_ns > 0:
            time.sleep(espera_ns / 1_000_000_000)

        # Instante imediatamente antes do envio TCP.
        instante_envio_ns = time.perf_counter_ns()

        sock.sendall(
            amostra["pacote_bytes"]
        )

        # Armazena apenas o timestamp bruto.
        # A análise será feita somente após o fim da transmissão.
        instantes_envio_ns[indice] = instante_envio_ns

    print("Transmissão concluída.")


    # ======================================================
    # ANÁLISE TEMPORAL APÓS A TRANSMISSÃO
    # ======================================================

    intervalos_reais_ms = []
    intervalos_esperados_ms = []
    erros_temporais_ms = []

    for i in range(len(amostras)):

        instante_desejado_ns = (
            tempo_inicial_real_ns + amostras[i]["tempo_relativo_ns"]
        )

        erro_temporal_ms = (
            instantes_envio_ns[i] - instante_desejado_ns
        ) / 1_000_000

        erros_temporais_ms.append(erro_temporal_ms)

        if i > 0:
            intervalo_real_ms = (
                instantes_envio_ns[i] - instantes_envio_ns[i - 1]
            ) / 1_000_000

            intervalo_esperado_ms = (
                amostras[i]["tempo_relativo_ns"] -
                amostras[i - 1]["tempo_relativo_ns"]
            ) / 1_000_000

            intervalos_reais_ms.append(intervalo_real_ms)
            intervalos_esperados_ms.append(intervalo_esperado_ms)


    estat_real = calcular_estatisticas(intervalos_reais_ms)
    estat_esperado = calcular_estatisticas(intervalos_esperados_ms)
    estat_erro = calcular_estatisticas(erros_temporais_ms)

    print("====================================")
    print("ANÁLISE TEMPORAL DO ENVIO")
    print("====================================")

    if estat_esperado:
        media, menor, maior, jitter = estat_esperado
        print("Intervalos esperados pelo CSV:")
        print(f"Período médio esperado: {media:.2f} ms")
        print(f"Menor intervalo:        {menor:.2f} ms")
        print(f"Maior intervalo:        {maior:.2f} ms")
        print(f"Variação aproximada:    {jitter:.2f} ms")
        print("------------------------------------")

    if estat_real:
        media, menor, maior, jitter = estat_real
        print("Intervalos reais medidos:")
        print(f"Período médio real:     {media:.2f} ms")
        print(f"Menor intervalo real:   {menor:.2f} ms")
        print(f"Maior intervalo real:   {maior:.2f} ms")
        print(f"Jitter aproximado:      {jitter:.2f} ms")
        print("------------------------------------")

    if estat_erro:
        media, menor, maior, amplitude = estat_erro
        print("Erro temporal em relação ao instante desejado:")
        print(f"Erro médio:             {media:+.3f} ms")
        print(f"Menor erro:             {menor:+.3f} ms")
        print(f"Maior erro:             {maior:+.3f} ms")
        print(f"Amplitude do erro:      {amplitude:.3f} ms")

    print("====================================")


except ConnectionRefusedError:
    print("ERRO: conexão recusada. Verifique se a simulação do Wokwi está rodando.")

except socket.timeout:
    print("ERRO: tempo limite de conexão ou envio excedido.")

except BrokenPipeError:
    print("ERRO: conexão encerrada pelo ESP32 durante o envio.")

except ConnectionResetError:
    print("ERRO: conexão reiniciada pelo ESP32.")

except KeyboardInterrupt:
    print("\nTransmissão interrompida pelo usuário.")

finally:
    sock.close()
    print("Conexão encerrada.")