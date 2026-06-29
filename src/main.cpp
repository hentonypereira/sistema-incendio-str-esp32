#include <Arduino.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ======================================================
// PINOS
// ======================================================

#define LED_INCENDIO       2
#define LED_ALERTA         4
#define BUZZER             17
#define SPRINKLER          18
#define RELE_ENERGIA       19
#define BOTAO_EMERGENCIA   32
#define BOTAO_RESET        33

// ======================================================
// LIMIARES DO SISTEMA
// ======================================================

#define LIMIAR_TEMPERATURA 60.0
#define LIMIAR_FUMACA      3.4   // %/m - obscuracao de fumaca

// ======================================================
// CONFIGURACAO DA REDE WI-FI
// ======================================================

const char* SSID = "Wokwi-GUEST";
const char* PASSWORD = "";

// Porta 8080:
// usada pelo injetor.py para enviar os dados FDS ao ESP32.
WiFiServer server(8080);

// Porta 8081:
// usada por um script logger_tasks.py para receber as metricas das tasks.
// O injetor.py NAO precisa ser alterado.
WiFiServer serverLog(8081);

// LCD I2C: endereco 0x27, 16 colunas, 2 linhas.
// 0x27 e o endereco do LCD no barramento I2C.
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ======================================================
// ESTADOS DO SISTEMA
// ======================================================

enum EstadoSistema
{
  NORMAL,
  ALERTA,
  INCENDIO
};

// ======================================================
// VARIAVEIS GLOBAIS DO SISTEMA
// ======================================================

// O volatile avisa ao compilador que essas variaveis podem mudar
// fora do fluxo normal daquela task.

volatile float temperatura = 25.0;

// Esta variavel representa a obscuracao da fumaca em %/m.
volatile float fumaca = 0.0;

// Estado atual da maquina de estados.
volatile EstadoSistema estadoAtual = NORMAL;

// Flag para emergencia manual.
volatile bool emergenciaManual = false;

// Semaforos usados pelas interrupcoes dos botoes.
SemaphoreHandle_t semEmergencia;
SemaphoreHandle_t semReset;

// ======================================================
// METRICAS DAS TASKS PERIODICAS
// ======================================================
//
// Nesta versao, medimos apenas as tasks periodicas:
//
// TaskTCP
// TaskEstado
// TaskAtuadores
// TaskLCD
//
// Para cada task, sao medidos:
//
// 1. maior periodo real entre ativacoes;
// 2. maior tempo de execucao;
// 3. quantidade de deadlines perdidos;
// 4. quantidade de periodos atrasados;
// 5. quantidade de ciclos avaliados.
//
// Para simplificar a analise temporal do projeto:
//
// deadline relativo = periodo
//
// Ou seja:
//
// D = T
//
// Exemplo:
// se a TaskEstado tem periodo de 50 ms,
// entao seu deadline relativo tambem e 50 ms.

typedef struct
{
  // Periodo nominal da task, definido no projeto.
  uint32_t periodoUs;

  // Deadline relativo da task.
  // Nesta versao, deadlineUs = periodoUs.
  uint32_t deadlineUs;

  // Instante em que a task iniciou no ciclo anterior.
  // Usado para calcular o periodo real.
  uint32_t ultimoInicioUs;

  // Maior periodo real medido entre duas ativacoes consecutivas.
  uint32_t maiorPeriodoUs;

  // Maior tempo de execucao medido.
  uint32_t maiorExecucaoUs;

  // Conta quantas vezes o tempo de execucao passou do deadline.
  uint32_t deadlinesPerdidos;

  // Conta quantas vezes o periodo real passou do periodo esperado.
  uint32_t periodosAtrasados;

  // Total de ciclos avaliados.
  uint32_t ciclos;
} MetricaTask;


// Deadlines iguais aos periodos: D = T.
MetricaTask metTCP       = {50000,  50000,  0, 0, 0, 0, 0, 0};
MetricaTask metEstado    = {50000,  50000,  0, 0, 0, 0, 0, 0};
MetricaTask metAtuadores = {50000,  50000,  0, 0, 0, 0, 0, 0};
MetricaTask metLCD       = {100000, 100000, 0, 0, 0, 0, 0, 0};


// Tolerancia de 1 ms para pequenas variacoes normais.
//
// Exemplo:
// uma task de 50 ms so sera considerada atrasada
// se o periodo real passar de 51 ms.
const uint32_t TOLERANCIA_US = 1000;


// ======================================================
// FUNCOES DE MEDICAO DAS TASKS
// ======================================================

void atualizarMetrica(
  MetricaTask &metrica,
  uint32_t inicioUs,
  uint32_t fimUs
)
{
  // Tempo que a task gastou executando neste ciclo.
  uint32_t tempoExecucaoUs = fimUs - inicioUs;

  // Calcula o periodo real entre duas ativacoes consecutivas
  // da mesma task.
  if (metrica.ultimoInicioUs != 0)
  {
    uint32_t periodoRealUs = inicioUs - metrica.ultimoInicioUs;

    if (periodoRealUs > metrica.maiorPeriodoUs)
    {
      metrica.maiorPeriodoUs = periodoRealUs;
    }

    // Conta atraso de periodo.
    // Isso nao e exatamente "deadline perdido".
    // E uma medida de jitter/atraso de ativacao.
    if (periodoRealUs > metrica.periodoUs + TOLERANCIA_US)
    {
      metrica.periodosAtrasados++;
    }
  }

  // Atualiza o inicio anterior para o proximo ciclo.
  metrica.ultimoInicioUs = inicioUs;

  // Guarda o maior tempo de execucao observado.
  if (tempoExecucaoUs > metrica.maiorExecucaoUs)
  {
    metrica.maiorExecucaoUs = tempoExecucaoUs;
  }

  // Deadline perdido:
  // nesta abordagem simples, a task perde deadline se o
  // tempo de execucao for maior que o deadline relativo.
  if (tempoExecucaoUs > metrica.deadlineUs)
  {
    metrica.deadlinesPerdidos++;
  }

  metrica.ciclos++;
}


// ======================================================
// FUNCOES DE ENVIO DAS METRICAS POR TCP
// ======================================================
//
// As metricas sao enviadas por uma segunda conexao TCP,
// na porta 8081.
//
// O injetor.py continua usando apenas a porta 8080.
// Portanto, nao e necessario modificar o injetor.py.

void enviarMetricaTask(
  WiFiClient &client,
  const char *nome,
  MetricaTask &metrica
)
{
  if (!client || !client.connected())
  {
    return;
  }

  char linha[180];

  // Formato enviado ao logger_tasks.py:
  //
  // tempo_ms,task,periodo_us,deadline_us,
  // maior_periodo_us,maior_execucao_us,
  // deadlines_perdidos,periodos_atrasados,ciclos
  //
  // Exemplo:
  // 1000,TaskEstado,50000,50000,50200,340,0,0,20

  snprintf(
    linha,
    sizeof(linha),
    "%lu,%s,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
    (unsigned long) millis(),
    nome,
    (unsigned long) metrica.periodoUs,
    (unsigned long) metrica.deadlineUs,
    (unsigned long) metrica.maiorPeriodoUs,
    (unsigned long) metrica.maiorExecucaoUs,
    (unsigned long) metrica.deadlinesPerdidos,
    (unsigned long) metrica.periodosAtrasados,
    (unsigned long) metrica.ciclos
  );

  client.print(linha);
}


// ======================================================
// INTERRUPCAO - BOTAO DE EMERGENCIA
// ======================================================

void IRAM_ATTR ISRBotaoEmergencia()
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  xSemaphoreGiveFromISR(
    semEmergencia,
    &xHigherPriorityTaskWoken
  );

  if (xHigherPriorityTaskWoken)
  {
    portYIELD_FROM_ISR();
  }
}

// ======================================================
// INTERRUPCAO - BOTAO RESET
// ======================================================

void IRAM_ATTR ISRBotaoReset()
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  xSemaphoreGiveFromISR(
    semReset,
    &xHigherPriorityTaskWoken
  );

  if (xHigherPriorityTaskWoken)
  {
    portYIELD_FROM_ISR();
  }
}

// ======================================================
// T1 - RECEPCAO TCP
// ======================================================

void TaskTCP(void *pvParameters)
{
  // Cria o objeto que representa a conexao TCP com o injetor.py.
  WiFiClient client;

  // Guarda o instante de referencia da ultima ativacao da task.
  TickType_t xLastWakeTime = xTaskGetTickCount();

  // Periodo da TaskTCP: 50 ms.
  const TickType_t xPeriod = pdMS_TO_TICKS(50);

  for (;;)
  {
    // Inicio da medicao temporal desta instancia da task.
    uint32_t inicioUs = micros();

    // Verifica se ainda nao existe um cliente TCP valido
    // ou se o cliente anterior foi desconectado.
    if (!client || !client.connected())
    {
      // Verifica se ha algum cliente TCP tentando se conectar ao servidor.
      // Essa chamada nao fica esperando uma conexao.
      client = server.available();

      if (client)
      {
        // Timeout curto para evitar bloqueios longos no readStringUntil().
        client.setTimeout(5);
      }
    }

    // Verifica se existe um cliente TCP valido
    // e se ele continua conectado ao ESP32.
    if (client && client.connected())
    {
      // Enquanto houver dados disponiveis enviados pelo cliente TCP,
      // a task continua lendo os pacotes recebidos.
      while (client.available())
      {
        // Le uma linha recebida pela conexao TCP ate encontrar '\n'.
        // Exemplo de linha recebida: T:25.0,S:3.45
        String linha = client.readStringUntil('\n');

        // Remove espacos, quebras de linha e caracteres extras.
        linha.trim();

        // Pacote esperado:
        // T:25.0,S:3.45
        if (linha.startsWith("T:"))
        {
          // Procura a posicao da virgula que separa temperatura e fumaca.
          int idx = linha.indexOf(',');

          if (idx > 0)
          {
            // Extrai a temperatura.
            // substring(2, idx) pega o trecho depois de "T:" ate antes da virgula.
            temperatura = linha.substring(2, idx).toFloat();

            // Extrai a fumaca.
            // idx + 3 pula a virgula e o trecho "S:".
            fumaca = linha.substring(idx + 3).toFloat();
          }
        }
      }
    }

    // Fim da medicao temporal desta instancia da task.
    uint32_t fimUs = micros();

    atualizarMetrica(
      metTCP,
      inicioUs,
      fimUs
    );

    // Aguarda ate o proximo ciclo de 50 ms.
    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
}

// ======================================================
// T3 - MAQUINA DE ESTADOS
// ======================================================

void TaskEstado(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();

  // Periodo da TaskEstado: 50 ms.
  const TickType_t xPeriod = pdMS_TO_TICKS(50);

  for (;;)
  {
    uint32_t inicioUs = micros();

    if (emergenciaManual)
    {
      estadoAtual = INCENDIO;
    }
    else if (temperatura >= LIMIAR_TEMPERATURA)
    {
      estadoAtual = INCENDIO;
    }
    else if (fumaca >= LIMIAR_FUMACA)
    {
      estadoAtual = ALERTA;
    }
    else
    {
      estadoAtual = NORMAL;
    }

    uint32_t fimUs = micros();

    atualizarMetrica(
      metEstado,
      inicioUs,
      fimUs
    );

    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
}

// ======================================================
// T4 - BOTAO DE EMERGENCIA
// ======================================================

void TaskBotaoEmergencia(void *pvParameters)
{
  for (;;)
  {
    if (xSemaphoreTake(semEmergencia, portMAX_DELAY) == pdTRUE)
    {
      emergenciaManual = true;
      estadoAtual = INCENDIO;

      // Debounce simples.
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
}

// ======================================================
// T5 - BOTAO RESET
// ======================================================

void TaskBotaoReset(void *pvParameters)
{
  for (;;)
  {
    if (xSemaphoreTake(semReset, portMAX_DELAY) == pdTRUE)
    {
      emergenciaManual = false;

      if (temperatura < LIMIAR_TEMPERATURA && fumaca < LIMIAR_FUMACA)
      {
        estadoAtual = NORMAL;
      }

      // Debounce simples.
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
}

// ======================================================
// T6 - LCD
// ======================================================

void TaskLCD(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();

  // Atualiza o LCD a cada 100 ms.
  // O LCD fica mais responsivo, mas continua fora da cadeia critica.
  const TickType_t xPeriod = pdMS_TO_TICKS(100);

  char linha1[17];
  char linha2[17];

  char linha1Anterior[17] = "";
  char linha2Anterior[17] = "";

  for (;;)
  {
    uint32_t inicioUs = micros();

    // Copia os valores globais para variaveis locais.
    float tempLocal = temperatura;
    float fumacaLocal = fumaca;
    EstadoSistema estadoLocal = estadoAtual;
    bool emergenciaLocal = emergenciaManual;

    // Monta a primeira linha com temperatura e fumaca.
    snprintf(
      linha1,
      sizeof(linha1),
      "T:%5.1f F:%4.1f",
      tempLocal,
      fumacaLocal
    );

    // Monta a segunda linha de acordo com o estado do sistema.
    switch (estadoLocal)
    {
      case NORMAL:
        snprintf(linha2, sizeof(linha2), "%-16s", "NORMAL");
        break;

      case ALERTA:
        snprintf(linha2, sizeof(linha2), "%-16s", "ALERTA FUMACA");
        break;

      case INCENDIO:
        if (emergenciaLocal)
        {
          snprintf(linha2, sizeof(linha2), "%-16s", "INC MANUAL");
        }
        else
        {
          snprintf(linha2, sizeof(linha2), "%-16s", "INCENDIO");
        }
        break;
    }

    // Para evitar piscadas e reduzir trafego I2C,
    // so escreve no LCD quando a linha muda.
    if (strcmp(linha1, linha1Anterior) != 0)
    {
      lcd.setCursor(0, 0);
      lcd.print(linha1);
      strcpy(linha1Anterior, linha1);
    }

    if (strcmp(linha2, linha2Anterior) != 0)
    {
      lcd.setCursor(0, 1);
      lcd.print(linha2);
      strcpy(linha2Anterior, linha2);
    }

    uint32_t fimUs = micros();

    atualizarMetrica(
      metLCD,
      inicioUs,
      fimUs
    );

    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
}

// ======================================================
// T7/T8/T9 - ATUADORES
// ======================================================

void TaskAtuadores(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();

  // Periodo da TaskAtuadores: 50 ms.
  const TickType_t xPeriod = pdMS_TO_TICKS(50);

  for (;;)
  {
    uint32_t inicioUs = micros();

    switch (estadoAtual)
    {
      case NORMAL:
        digitalWrite(LED_ALERTA, LOW);
        digitalWrite(LED_INCENDIO, LOW);
        digitalWrite(SPRINKLER, LOW);
        digitalWrite(RELE_ENERGIA, LOW);
        noTone(BUZZER);
        break;

      case ALERTA:
        digitalWrite(LED_ALERTA, HIGH);
        digitalWrite(LED_INCENDIO, LOW);
        digitalWrite(SPRINKLER, LOW);
        digitalWrite(RELE_ENERGIA, LOW);
        tone(BUZZER, 1000);
        break;

      case INCENDIO:
        digitalWrite(LED_ALERTA, LOW);
        digitalWrite(LED_INCENDIO, HIGH);
        digitalWrite(SPRINKLER, HIGH);
        digitalWrite(RELE_ENERGIA, HIGH);
        tone(BUZZER, 2000);
        break;
    }

    uint32_t fimUs = micros();

    atualizarMetrica(
      metAtuadores,
      inicioUs,
      fimUs
    );

    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
}

// ======================================================
// T10 - LOGGER DE METRICAS
// ======================================================
//
// Esta task envia as metricas pela porta TCP 8081.
// Ela e separada do injetor.py.
//
// Como e apenas uma task de teste/monitoramento, sua prioridade
// deve ser baixa para nao atrapalhar as tasks principais.

void TaskLoggerMetricas(void *pvParameters)
{
  WiFiClient clientLog;

  TickType_t xLastWakeTime = xTaskGetTickCount();

  // Envia um resumo das metricas a cada 1 segundo.
  const TickType_t xPeriod = pdMS_TO_TICKS(200);

  for (;;)
  {
    // Se ainda nao ha cliente conectado, verifica se o logger_tasks.py
    // tentou se conectar na porta 8081.
    if (!clientLog || !clientLog.connected())
    {
      clientLog = serverLog.available();
    }

    // Se o logger esta conectado, envia uma linha para cada task medida.
    if (clientLog && clientLog.connected())
    {
      enviarMetricaTask(clientLog, "TaskTCP", metTCP);
      enviarMetricaTask(clientLog, "TaskEstado", metEstado);
      enviarMetricaTask(clientLog, "TaskAtuadores", metAtuadores);
      enviarMetricaTask(clientLog, "TaskLCD", metLCD);
    }

    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
}

// ======================================================
// SETUP
// ======================================================

void setup()
{
  // Nao usamos Serial nesta versao.
  // As metricas sao enviadas pela porta TCP 8081.

  pinMode(LED_INCENDIO, OUTPUT);
  pinMode(LED_ALERTA, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(SPRINKLER, OUTPUT);
  pinMode(RELE_ENERGIA, OUTPUT);

  pinMode(BOTAO_EMERGENCIA, INPUT_PULLUP);
  pinMode(BOTAO_RESET, INPUT_PULLUP);

  digitalWrite(LED_INCENDIO, LOW);
  digitalWrite(LED_ALERTA, LOW);
  digitalWrite(SPRINKLER, LOW);
  digitalWrite(RELE_ENERGIA, LOW);
  noTone(BUZZER);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Conectando...");

  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(250);
  }

  // Inicia os dois servidores TCP:
  // 8080 -> dados do injetor.py
  // 8081 -> metricas das tasks
  server.begin();
  serverLog.begin();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi OK");
  lcd.setCursor(0, 1);
  lcd.print("8080/8081 OK");

  semEmergencia = xSemaphoreCreateBinary();
  semReset = xSemaphoreCreateBinary();

  attachInterrupt(
    digitalPinToInterrupt(BOTAO_EMERGENCIA),
    ISRBotaoEmergencia,
    FALLING
  );

  attachInterrupt(
    digitalPinToInterrupt(BOTAO_RESET),
    ISRBotaoReset,
    FALLING
  );

  // Prioridades:
  //
  // Emergencia e Reset sao eventos manuais importantes.
  // Depois vem a cadeia critica:
  //
  // TaskTCP -> TaskEstado -> TaskAtuadores
  //
  // LCD e Logger ficam com prioridades menores.

  xTaskCreate(TaskBotaoEmergencia, "Emergencia", 2048, NULL, 7, NULL);
  xTaskCreate(TaskBotaoReset, "Reset", 2048, NULL, 6, NULL);

  xTaskCreate(TaskTCP, "TCP", 4096, NULL, 5, NULL);
  xTaskCreate(TaskEstado, "Estado", 4096, NULL, 4, NULL);
  xTaskCreate(TaskAtuadores, "Atuadores", 2048, NULL, 3, NULL);

  xTaskCreate(TaskLCD, "LCD", 4096, NULL, 2, NULL);

  xTaskCreate(TaskLoggerMetricas, "LoggerMetricas", 4096, NULL, 1, NULL);
}

void loop()
{
  // O sistema e executado pelas tasks do FreeRTOS.
}
