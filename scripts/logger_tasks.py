import socket
import os
import time


# ==========================================================
# CONFIGURAÇÕES DO LOGGER
# ==========================================================
# O ESP32 envia as métricas pela porta 8081.
# O Wokwi redireciona localhost:8081 para target:8081.

ESP32_IP = "127.0.0.1"
ESP32_PORTA_LOG = 8081

ARQUIVO_METRICAS = "logs/metricas_tasks.csv"


# ==========================================================
# CABEÇALHO DO ARQUIVO CSV
# ==========================================================
# Cada linha do CSV representa um resumo das métricas de uma task.
#
# tempo_ms:
#   instante, em milissegundos, desde que o ESP32 iniciou.
#
# task:
#   nome da task medida.
#
# periodo_us:
#   período nominal da task, em microssegundos.
#
# deadline_us:
#   deadline relativo adotado para a task.
#   Neste projeto, deadline_us = periodo_us.
#
# maior_periodo_us:
#   maior intervalo real medido entre ativações consecutivas da task.
#
# maior_execucao_us:
#   maior tempo de execução medido para a task.
#
# deadlines_perdidos:
#   quantidade de vezes em que o tempo de execução passou do deadline.
#
# periodos_atrasados:
#   quantidade de vezes em que o período real passou do período nominal
#   mais a tolerância configurada no ESP32.
#
# ciclos:
#   quantidade de ciclos avaliados até aquele instante.

CABECALHO = (
    "tempo_ms,task,periodo_us,deadline_us,"
    "maior_periodo_us,maior_execucao_us,"
    "deadlines_perdidos,periodos_atrasados,ciclos\n"
)


def conectar_com_retry():
    """
    Tenta conectar ao servidor de métricas do ESP32.

    A simulação do Wokwi pode demorar alguns segundos para iniciar a rede.
    Por isso, o script tenta conectar repetidamente até conseguir.
    """

    while True:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((ESP32_IP, ESP32_PORTA_LOG))
            return sock

        except ConnectionRefusedError:
            print("Aguardando o ESP32 aceitar conexão na porta 8081...")
            time.sleep(0.5)

        except OSError:
            print("Erro de conexão. Tentando novamente...")
            time.sleep(0.5)


def main():
    """
    Recebe linhas de métricas enviadas pelo ESP32 e salva no CSV.

    O ESP32 envia linhas neste formato:

    1000,TaskTCP,50000,50000,50300,1200,0,0,20

    O Python apenas grava essas linhas em logs/metricas_tasks.csv.
    """

    os.makedirs("logs", exist_ok=True)

    print("Conectando ao logger de métricas do ESP32...")
    sock = conectar_com_retry()

    print("Conectado ao ESP32.")
    print(f"Salvando métricas em: {ARQUIVO_METRICAS}")

    buffer = ""

    with open(ARQUIVO_METRICAS, "w", encoding="utf-8", newline="") as arquivo:
        arquivo.write(CABECALHO)
        arquivo.flush()

        try:
            while True:
                dados = sock.recv(1024)

                if not dados:
                    print("Conexão encerrada pelo ESP32.")
                    break

                buffer += dados.decode("utf-8", errors="ignore")

                # Como TCP é fluxo de bytes, uma chamada recv() pode receber:
                # - meia linha;
                # - uma linha completa;
                # - várias linhas juntas.
                #
                # Por isso acumulamos em 'buffer' e processamos somente quando
                # aparece uma quebra de linha.
                while "\n" in buffer:
                    linha, buffer = buffer.split("\n", 1)
                    linha = linha.strip()

                    if not linha:
                        continue

                    arquivo.write(linha + "\n")
                    arquivo.flush()

        except KeyboardInterrupt:
            print("\nLogger encerrado pelo usuário.")

        finally:
            sock.close()
            print("Conexão encerrada.")


if __name__ == "__main__":
    main()
