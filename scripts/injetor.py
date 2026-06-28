import socket
import csv
import time
import argparse # Biblioteca que permite passar argumentos pelo terminal.


# ==========================================================
# CONFIGURAÇÃO DAS SIMULAÇÕES DISPONÍVEIS
# ==========================================================


# Dicionário de simulações
SIMULACOES = {
    "flaming": "data/flaming_fire.csv",
    "smoldering": "data/smoldering_fire.csv"
}


# ==========================================================
# CONFIGURAÇÃO DA CONEXÃO TCP/IP COM O ESP32 SIMULADO
# ==========================================================
# O Wokwi encaminha localhost:8080 para a porta 8080
# do ESP32 virtual através da configuração net.forward.
# ==========================================================

ESP32_IP = "127.0.0.1"
ESP32_PORTA = 8080


# ==========================================================
# ARGUMENTOS DO PROGRAMA
# ==========================================================

# Criação do interpretador de argumentos

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

arquivo_csv = SIMULACOES[args.cenario]


# ==========================================================
# CONEXÃO COM O ESP32
# ==========================================================

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# Evita travamento indefinido caso o ESP32 não responda
sock.settimeout(0.5)

print("====================================")
print("Injetor FDS -> ESP32")
print("====================================")
print(f"Cenário selecionado: {args.cenario}")
print(f"Arquivo CSV: {arquivo_csv}")
print(f"Velocidade: {args.velocidade}x")
print("====================================")

print("Conectando ao ESP32...")

sock.connect((ESP32_IP, ESP32_PORTA))

print("Conectado!")


# ==========================================================
# LEITURA E ENVIO DOS DADOS DO CSV
# ==========================================================
# Formato esperado do CSV:
#
# Linha 1:
# s,C,%/m
#
# Linha 2:
# Time,Termopar_01,Detetor_Fumaca
#
# Linhas seguintes:
# tempo,temperatura,obscuracao_fumaca
# ==========================================================

try:
    # O with garante que o arquivo será fechado automaticamente depois.
    with open(arquivo_csv, "r") as arquivo:

        leitor = csv.reader(arquivo)

        # Pula a linha das unidades: s,C,%/m
        next(leitor)

        # Pula a linha dos nomes: Time,Termopar_01,Detetor_Fumaca
        next(leitor)

        tempo_anterior = None

        for linha in leitor:

            if len(linha) < 3:
                continue

            # O float() entende automaticamente notação científica,
            # por exemplo: 2.000000E+001 -> 20.0
            tempo = float(linha[0])
            temperatura = float(linha[1])
            fumaca = float(linha[2])  # obscuração em %/m

            # Monta a mensagem enviada ao ESP32.
            pacote = f"T:{temperatura:.1f},S:{fumaca:.2f}\n"

            sock.sendall(
                pacote.encode("utf-8")
            )

            print(
                f"[{tempo:.2f}s] -> {pacote.strip()}"
            )

            try:
                resposta = sock.recv(128)

                if resposta:
                    print(
                        "ESP32:",
                        resposta.decode("utf-8", errors="ignore").strip()
                    )

            except socket.timeout:
                print("ESP32: sem resposta")

            # Reproduz a evolução temporal do FDS.
            # Se o CSV tem período de 0,1 s, o envio também ocorre
            # aproximadamente a cada 0,1 s quando velocidade = 1.0.
            # O código atual reproduz a frequência do CSV, ajustada pelo fator de velocidade.
            if tempo_anterior is not None:
                intervalo = tempo - tempo_anterior

                if intervalo > 0:
                    time.sleep(intervalo / args.velocidade)

            tempo_anterior = tempo

except FileNotFoundError:
    print(f"ERRO: arquivo não encontrado: {arquivo_csv}")

except KeyboardInterrupt:
    print("\nTransmissão interrompida pelo usuário.")

finally:
    sock.close()
    print("Conexão encerrada.")