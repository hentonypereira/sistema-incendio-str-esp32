# Sistema de Tempo Real para Detecção de Incêndio com ESP32, FreeRTOS e Validação por Dados de Simulação FDS

Projeto desenvolvido para a disciplina de **CIC0248 - Sistemas de Tempo Real (STR)**. O objetivo é implementar e avaliar um protótipo de sistema embarcado em tempo real capaz de detectar condições de alerta e incêndio a partir de dados de temperatura e fumaça gerados por simulação no **Fire Dynamics Simulator (FDS)**.

O sistema utiliza um **ESP32 simulado no Wokwi**, executando tarefas com **FreeRTOS**, e recebe os dados da simulação por meio do script Python `injetor.py`, que transmite os valores ao ESP32 via comunicação **TCP/IP**.

Além da lógica de detecção e atuação, o projeto inclui uma etapa de **validação temporal das tasks periódicas**, registrando métricas como período real, tempo de execução, deadlines perdidos e períodos atrasados em um arquivo `.csv`.

---

## Objetivo do projeto

Desenvolver um sistema de tempo real para monitoramento de incêndio, capaz de:

- Receber dados simulados de temperatura e fumaça;
- Identificar estados de operação do sistema;
- Acionar alarmes e atuadores;
- Responder a eventos manuais, como emergência e reset;
- Demonstrar o uso de tasks, prioridades, interrupções e semáforos no FreeRTOS;
- Avaliar experimentalmente o cumprimento dos períodos e deadlines das tasks periódicas.

---

## Funcionamento geral

O funcionamento principal do sistema pode ser resumido como:

```text
FDS → CSV → injetor.py → TCP/IP → ESP32 → FreeRTOS → Atuadores
```

O script `injetor.py` lê os arquivos `.csv` gerados pelo FDS e envia os dados ao ESP32 no seguinte formato:

```text
T:25.0,S:3.45
```

Onde:

- `T` representa a temperatura em graus Celsius;
- `S` representa a obscuração de fumaça em `%/m`.

No ESP32, a `TaskTCP` recebe os dados e atualiza as variáveis globais de temperatura e fumaça. A `TaskEstado` avalia os limiares e define o estado atual do sistema. A `TaskAtuadores` aciona LEDs, buzzer, sprinkler e relé conforme o estado definido.

---

## Comunicação TCP/IP

O projeto utiliza duas portas TCP diferentes:

| Porta | Função |
|---:|---|
| `8080` | Receber dados de temperatura e fumaça enviados pelo `injetor.py` |
| `8081` | Enviar métricas temporais das tasks para o `logger_tasks.py` |

A separação das portas permite manter o `injetor.py` responsável apenas pela simulação dos dados do FDS, enquanto o `logger_tasks.py` fica responsável apenas pela coleta das métricas temporais.

Fluxo completo com logger:

```text
injetor.py      → porta 8080 → ESP32
logger_tasks.py ← porta 8081 ← ESP32
```

---

## Estados do sistema

O sistema possui três estados principais:

| Estado | Descrição |
|---|---|
| `NORMAL` | Temperatura e fumaça abaixo dos limiares definidos |
| `ALERTA` | Fumaça acima do limiar, indicando condição suspeita |
| `INCENDIO` | Temperatura acima do limiar ou emergência manual acionada |

---

## Limiares utilizados

Os limiares adotados no projeto são:

```cpp
#define LIMIAR_TEMPERATURA 60.0
#define LIMIAR_FUMACA      3.4
```

A temperatura é medida em graus Celsius, enquanto a fumaça é representada em `%/m`, unidade de obscuração utilizada nos dados gerados pelo FDS.

---

## Tecnologias utilizadas

- ESP32
- FreeRTOS
- PlatformIO
- Wokwi
- Python
- TCP/IP
- Fire Dynamics Simulator (FDS)
- Smokeview (SMV)
- LCD I2C 16x2

---

## Estrutura do projeto

```text
sistema-incendio-str-esp32/
│
├── data/
│   ├── flaming_fire.csv
│   └── smoldering_fire.csv
│
├── docs/
│   └── arquivos de documentação, relatório e slides
│
├── include/
│
├── lib/
│
├── logs/
│   └── metricas_tasks.csv
│
├── scripts/
│   ├── injetor.py
│   └── logger_tasks.py
│
├── src/
│   └── main.cpp
│
├── test/
│
├── .gitignore
├── diagram.json
├── platformio.ini
└── wokwi.toml
```

---

## Arquivos principais

| Arquivo | Descrição |
|---|---|
| `src/main.cpp` | Código principal executado no ESP32 |
| `scripts/injetor.py` | Script Python responsável por enviar os dados do FDS ao ESP32 |
| `scripts/logger_tasks.py` | Script Python responsável por receber métricas temporais das tasks e salvar em `.csv` |
| `logs/metricas_tasks.csv` | Arquivo gerado com os resultados das métricas temporais |
| `data/flaming_fire.csv` | Dados do cenário de incêndio com chama |
| `data/smoldering_fire.csv` | Dados do cenário de combustão lenta |
| `diagram.json` | Diagrama do circuito usado no Wokwi |
| `wokwi.toml` | Configuração da simulação e do redirecionamento TCP |
| `platformio.ini` | Configuração do projeto PlatformIO |

---

## Tasks implementadas

| Task | Tipo | Período / Comportamento | Deadline adotado | Prioridade | Função |
|---|---|---:|---:|---:|---|
| `TaskBotaoEmergencia` | Aperiódica | Evento por interrupção | Resposta ao evento | 7 | Tratar o botão de emergência |
| `TaskBotaoReset` | Aperiódica | Evento por interrupção | Resposta ao evento | 6 | Tratar o botão de reset |
| `TaskTCP` | Periódica | 50 ms | 50 ms | 5 | Receber dados TCP do `injetor.py` |
| `TaskEstado` | Periódica | 50 ms | 50 ms | 4 | Avaliar os limiares e definir o estado do sistema |
| `TaskAtuadores` | Periódica | 50 ms | 50 ms | 3 | Acionar LEDs, buzzer, sprinkler e relé |
| `TaskLCD` | Periódica | 100 ms | 100 ms | 2 | Atualizar as informações no LCD |
| `TaskLoggerMetricas` | Periódica / monitoramento | 200 ms | Não crítico | 1 | Enviar métricas temporais para o `logger_tasks.py` |

As tasks periódicas utilizam `vTaskDelayUntil()` para manter uma periodicidade mais precisa. As tasks dos botões são acionadas por interrupções e semáforos.

Para as tasks periódicas medidas, foi adotado:

```text
deadline relativo = período
D = T
```

---

## Interrupções e semáforos

Os botões de emergência e reset são tratados por interrupções. Quando um botão é pressionado, a respectiva ISR libera um semáforo, acordando a task responsável pelo tratamento do evento.

Fluxo simplificado:

```text
Botão pressionado
      ↓
Interrupção
      ↓
ISR
      ↓
Liberação do semáforo
      ↓
Task acorda
      ↓
Sistema executa a ação
```

Esse método evita polling constante dos botões e melhora o tempo de resposta para eventos críticos.

---

## Avaliação temporal das tasks

O projeto inclui uma instrumentação simples para avaliar experimentalmente o comportamento temporal das tasks periódicas.

Para cada task medida, são registrados:

- Maior período real entre ativações;
- Maior tempo de execução;
- Quantidade de deadlines perdidos;
- Quantidade de períodos atrasados;
- Número de ciclos avaliados.

A estrutura básica da medição é:

```text
inicio = micros()
executa o código real da task
fim = micros()
tempo_execucao = fim - inicio
```

O período real é calculado por:

```text
periodo_real = inicio_atual - inicio_anterior
```

Essas medições são enviadas pelo ESP32 via TCP, na porta `8081`, e salvas pelo script `logger_tasks.py`.

---

## Arquivo de métricas

O arquivo gerado é:

```text
logs/metricas_tasks.csv
```

Formato do CSV:

```csv
tempo_ms,task,periodo_us,deadline_us,maior_periodo_us,maior_execucao_us,deadlines_perdidos,periodos_atrasados,ciclos
```

Exemplo de linha:

```csv
8389,TaskTCP,50000,50000,50001,89,0,0,101
```

Interpretação:

- A task avaliada foi a `TaskTCP`;
- O período definido foi `50000 us`, ou seja, 50 ms;
- O deadline definido também foi 50 ms;
- O maior período real observado foi `50001 us`;
- O maior tempo de execução observado foi `89 us`;
- Não houve deadlines perdidos;
- Não houve períodos atrasados;
- Foram avaliados 101 ciclos até aquele instante.

---

## Limitações da avaliação temporal

A medição implementada avalia os **deadlines individuais das tasks periódicas**.

Ela ainda não mede diretamente o tempo de resposta fim-a-fim do sistema, isto é, o intervalo entre:

```text
amostra crítica recebida
      ↓
mudança de estado
      ↓
acionamento dos atuadores
```

Portanto, os valores obtidos representam o pior caso observado experimentalmente durante os testes, mas não constituem uma análise formal de WCET/WCRT.

---

## Configuração do Wokwi

O arquivo `wokwi.toml` deve conter o redirecionamento das duas portas TCP:

```toml
[wokwi]
version = 1

firmware = ".pio/build/esp32dev/firmware.bin"
elf = ".pio/build/esp32dev/firmware.elf"

[[net.forward]]
from = "localhost:8080"
to = "target:8080"

[[net.forward]]
from = "localhost:8081"
to = "target:8081"
```

---

## Como executar o projeto

### 1. Abrir o projeto

Abra a pasta do projeto no VS Code com a extensão PlatformIO instalada.

### 2. Compilar o projeto

Na raiz do projeto, execute:

```bash
pio run
```

### 3. Iniciar a simulação no Wokwi

Inicie a simulação no Wokwi. O ESP32 deve conectar à rede simulada `Wokwi-GUEST` e iniciar os servidores TCP nas portas `8080` e `8081`.

No LCD, deve aparecer uma indicação semelhante a:

```text
WiFi OK
8080/8081 OK
```

### 4. Executar o logger das tasks

Em um terminal, execute:

```bash
python scripts/logger_tasks.py
```

O script ficará aguardando as métricas enviadas pelo ESP32 e salvará o arquivo:

```text
logs/metricas_tasks.csv
```

### 5. Executar o injetor Python

Em outro terminal, com a simulação rodando, execute:

```bash
python scripts/injetor.py --cenario flaming --velocidade 1
```

Para o cenário de combustão lenta:

```bash
python scripts/injetor.py --cenario smoldering --velocidade 1
```

---

## Cenários disponíveis

| Cenário | Arquivo | Característica |
|---|---|---|
| `flaming` | `data/flaming_fire.csv` | Incêndio com chama, com aumento rápido da temperatura |
| `smoldering` | `data/smoldering_fire.csv` | Combustão lenta, com aumento mais significativo de fumaça |

---

## Resultados esperados

Durante a execução, espera-se que o arquivo `logs/metricas_tasks.csv` apresente linhas periódicas para cada task medida:

```csv
tempo_ms,task,periodo_us,deadline_us,maior_periodo_us,maior_execucao_us,deadlines_perdidos,periodos_atrasados,ciclos
8389,TaskTCP,50000,50000,50001,89,0,0,101
8394,TaskEstado,50000,50000,50001,8,0,0,101
8399,TaskAtuadores,50000,50000,50016,26,0,0,101
8403,TaskLCD,100000,100000,100000,86261,0,0,51
```

Um resultado com:

```text
deadlines_perdidos = 0
periodos_atrasados = 0
```

indica que, nos testes realizados, as tasks cumpriram seus deadlines individuais.

---

## Autores

Projeto desenvolvido por estudantes da disciplina **CIC0248 - Sistemas de Tempo Real - Turma 01**, da Universidade de Brasília (UnB).

| Autor | E-mail |
|---|---|
| Hentony Alves Pereira | [190029480@aluno.unb.br](mailto:190029480@aluno.unb.br) |
| Cauê Araujo Euzebio | [211028195@aluno.unb.br](mailto:211028195@aluno.unb.br) |
| Carlos Gustavo Muniz Simões | [190042311@aluno.unb.br](mailto:190042311@aluno.unb.br) |
| Matheus Rodrigues Ferreira | [170111229@aluno.unb.br](mailto:170111229@aluno.unb.br) |

---

## Licença

Este projeto foi desenvolvido para fins acadêmicos.
