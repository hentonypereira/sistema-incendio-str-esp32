# Sistema de Detecção e Combate a Incêndio com ESP32 e FreeRTOS

Projeto desenvolvido para a disciplina de **Sistemas de Tempo Real (STR)**. O objetivo é implementar um protótipo de sistema embarcado capaz de detectar condições de alerta e incêndio a partir de dados de temperatura e fumaça gerados por simulação no **Fire Dynamics Simulator (FDS)**.

O sistema utiliza um **ESP32 simulado no Wokwi**, executando tarefas com **FreeRTOS**, e recebe os dados da simulação por meio de um script Python chamado `injetor.py`, que transmite os valores ao ESP32 via comunicação **TCP/IP**.

## Objetivo do projeto

Desenvolver um sistema de tempo real para monitoramento de incêndio, capaz de:

* Receber dados simulados de temperatura e fumaça;
* Identificar estados de operação do sistema;
* Acionar alarmes e atuadores;
* Responder a eventos manuais, como emergência e reset;
* Demonstrar o uso de tasks, prioridades, interrupções e semáforos no FreeRTOS.

## Estados do sistema

O sistema possui três estados principais:

| Estado     | Descrição                                                 |
| ---------- | --------------------------------------------------------- |
| `NORMAL`   | Temperatura e fumaça abaixo dos limiares definidos        |
| `ALERTA`   | Fumaça acima do limiar, indicando condição suspeita       |
| `INCENDIO` | Temperatura acima do limiar ou emergência manual acionada |

## Limiares utilizados

Os limiares adotados no projeto são:

```cpp
#define LIMIAR_TEMPERATURA 60.0
#define LIMIAR_FUMACA      3.4
```

A temperatura é medida em graus Celsius, enquanto a fumaça é representada em `%/m`, unidade de obscuração utilizada nos dados gerados pelo FDS.

## Tecnologias utilizadas

* ESP32
* FreeRTOS
* PlatformIO
* Wokwi
* Python
* TCP/IP
* Fire Dynamics Simulator
* LCD I2C 16x2

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
├── scripts/
│   └── injetor.py
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

## Arquivos principais

| Arquivo                    | Descrição                                                     |
| -------------------------- | ------------------------------------------------------------- |
| `src/main.cpp`             | Código principal executado no ESP32                           |
| `scripts/injetor.py`       | Script Python responsável por enviar os dados do FDS ao ESP32 |
| `data/flaming_fire.csv`    | Dados do cenário de incêndio com chama                        |
| `data/smoldering_fire.csv` | Dados do cenário de combustão lenta                           |
| `diagram.json`             | Diagrama do circuito usado no Wokwi                           |
| `wokwi.toml`               | Configuração da simulação e do redirecionamento TCP           |
| `platformio.ini`           | Configuração do projeto PlatformIO                            |

## Funcionamento geral

O funcionamento do sistema pode ser resumido da seguinte forma:

```text
FDS → CSV → injetor.py → TCP/IP → ESP32 → FreeRTOS → Atuadores
```

O script `injetor.py` lê os arquivos CSV gerados pelo FDS e envia os dados no seguinte formato:

```text
T:25.0,S:3.45
```

Onde:

* `T` representa a temperatura em graus Celsius;
* `S` representa a obscuração de fumaça em `%/m`.

No ESP32, a `TaskTCP` recebe esses dados e atualiza as variáveis globais de temperatura e fumaça. Em seguida, a `TaskEstado` avalia os limiares e define o estado atual do sistema. A `TaskAtuadores` aciona LEDs, buzzer, sprinkler e relé conforme o estado definido.

## Tasks implementadas

| Task                  | Período / Comportamento | Função                                            |
| --------------------- | ----------------------: | ------------------------------------------------- |
| `TaskTCP`             |                   50 ms | Receber dados TCP do `injetor.py`                 |
| `TaskEstado`          |                  500 ms | Avaliar os limiares e definir o estado do sistema |
| `TaskAtuadores`       |                   50 ms | Acionar LEDs, buzzer, sprinkler e relé            |
| `TaskLCD`             |                 1000 ms | Atualizar as informações no LCD                   |
| `TaskBotaoEmergencia` |              Aperiódica | Tratar o botão de emergência                      |
| `TaskBotaoReset`      |              Aperiódica | Tratar o botão de reset                           |

As tasks periódicas utilizam `vTaskDelayUntil()` para manter uma periodicidade mais precisa. As tasks dos botões são acionadas por interrupções e semáforos.

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

## Como executar o projeto

### 1. Abrir o projeto

Abra a pasta do projeto no VS Code com a extensão PlatformIO instalada.

### 2. Iniciar a simulação no Wokwi

Compile e execute o projeto no Wokwi. O ESP32 deve conectar à rede simulada `Wokwi-GUEST` e iniciar o servidor TCP na porta `8080`.

### 3. Executar o injetor Python

Com a simulação rodando, execute no terminal:

```bash
python scripts/injetor.py --cenario flaming --velocidade 1
```

Para o cenário de combustão lenta:

```bash
python scripts/injetor.py --cenario smoldering --velocidade 1
```

## Cenários disponíveis

| Cenário      | Arquivo                    | Característica                                            |
| ------------ | -------------------------- | --------------------------------------------------------- |
| `flaming`    | `data/flaming_fire.csv`    | Incêndio com chama, com aumento rápido da temperatura     |
| `smoldering` | `data/smoldering_fire.csv` | Combustão lenta, com aumento mais significativo de fumaça |

## Autores

Projeto desenvolvido por estudantes da disciplina **CIC0248 - Sistemas de Tempo Real - Turma 01**, da Universidade de Brasília (UnB).

| Autor                       | E-mail                                                  |
| --------------------------- | ------------------------------------------------------- |
| Hentony Alves Pereira       | [190029480@aluno.unb.br](mailto:190029480@aluno.unb.br) |
| Cauê Araujo Euzebio         | [211028195@aluno.unb.br](mailto:211028195@aluno.unb.br) |
| Carlos Gustavo Muniz Simões | [190042311@aluno.unb.br](mailto:190042311@aluno.unb.br) |
| Matheus Rodrigues Ferreira  | [170111229@aluno.unb.br](mailto:170111229@aluno.unb.br) |


## Licença

Este projeto foi desenvolvido para fins acadêmicos.
