#include <Arduino.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
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

// Servidor TCP na porta 8080
WiFiServer server(8080);

// LCD I2C: endereco 0x27, 16 colunas, 2 linhas
// 0x27 é o endereço do LCD no barramento I2C.
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ======================================================
// ESTADOS DO SISTEMA
// ======================================================

enum EstadoSistema {
  NORMAL,
  ALERTA,
  INCENDIO
};

// O volatile avisa ao compilador que essas variáveis podem mudar fora do fluxo normal daquela task.

// Variaveis globais utilizadas no sistema
// Variaveis atualizadas pela recepcao TCP
volatile float temperatura = 25.0;

// Esta variavel representa a obscuracao da fumaca em %/m
volatile float fumaca = 0.0;

// Estado atual da maquina de estados
volatile EstadoSistema estadoAtual = NORMAL;

// Flag para emergencia manual
volatile bool emergenciaManual = false;


// Variáveis que guardam a referência/handle para o semáforo
SemaphoreHandle_t semEmergencia;
SemaphoreHandle_t semReset;

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
  // Cria o objeto que representa a conexão TCP com o injetor.py.
  WiFiClient client; 

  // Essa função retorna o número atual de ticks desde que o sistema começou a rodar.  
  // Guarda o instante de referência da última ativação da task.
  TickType_t xLastWakeTime = xTaskGetTickCount(); 

  // Define que a task deve ter período de 50 ms.
  const TickType_t xPeriod = pdMS_TO_TICKS(50);

  for (;;)
  {
    // Verifica se ainda não existe um cliente TCP válido
    // ou se o cliente anterior foi desconectado.
    if (!client || !client.connected())
    {
      // Verifica se há algum cliente TCP tentando se conectar ao servidor.
      // Essa chamada não fica esperando uma conexão; se não houver cliente,
      // retorna um objeto inválido/vazio e a task tenta novamente no próximo ciclo.
      client = server.available();
    }

    // Verifica se existe um cliente TCP válido
    // e se ele continua conectado ao ESP32.
    if (client && client.connected())
    {
      // Enquanto houver dados disponíveis enviados pelo cliente TCP,
      // a task continua lendo os pacotes recebidos.
      // client.available() verifica se há dados não lidos no buffer TCP.
      while (client.available())
      {
        // Lê uma linha recebida pela conexão TCP até encontrar '\n'.
        // Exemplo de linha recebida: T:25.0,S:3.45
        String linha = client.readStringUntil('\n');

        // Remove espaços, quebras de linha e caracteres extras.
        linha.trim();

        // Pacote esperado:
        // T:25.0,S:3.45
        // Extração de informações do pacote recebido.
        if (linha.startsWith("T:"))
        {
          // Procura a posição da vírgula que separa temperatura e fumaça.
          int idx = linha.indexOf(',');

          // Se a vírgula foi encontrada em uma posição válida,
          // o pacote pode ser interpretado.
          if (idx > 0)
          {
            // Extrai a temperatura da string.
            // substring(2, idx) pega o trecho depois de "T:" até antes da vírgula.
            temperatura =
              linha.substring(2, idx).toFloat();

            // Extrai a fumaça da string.
            // idx + 3 pula a vírgula e o trecho "S:".
            fumaca =
              linha.substring(idx + 3).toFloat();

            // ==================================================
            // DEBUG SERIAL DESATIVADO
            // ==================================================
            // Serial.println("PACOTE RECEBIDO:");
            // Serial.println(linha);

            // Serial.print("Temperatura = ");
            // Serial.print(temperatura);
            // Serial.println(" C");

            // Serial.print("Fumaca = ");
            // Serial.print(fumaca);
            // Serial.println(" %/m");

            // Não é enviada resposta "OK" ao injetor.py.
            // O injetor atua como uma fonte de dados simulada,
            // equivalente a sensores enviando medições ao ESP32.
          }
          else
          {
            // ==================================================
            // DEBUG SERIAL DESATIVADO
            // ==================================================
            // Serial.println("ERRO: pacote sem virgula");

            // Não é enviada resposta "ERRO" ao injetor.py.
            // Pacotes inválidos são simplesmente ignorados nesta versão.
          }
        }
      }
    }

    // Bloqueia a task até o próximo período de execução.
    // Como xPeriod = 50 ms, a TaskTCP tenta executar a cada 50 ms.
    // Aguarda até o próximo ciclo de 50 ms, mantendo a periodicidade da task.
    // xLastWakeTime é atualizado pela própria função.
    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
} 

// ======================================================
// T3 - MAQUINA DE ESTADOS
// ======================================================

void TaskEstado(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xPeriod = pdMS_TO_TICKS(100);

  for (;;)
  {
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

    // ==================================================
    // DEBUG SERIAL DESATIVADO
    // ==================================================
    // switch (estadoAtual)
    // {
    //   case NORMAL:
    //     Serial.println("ESTADO: NORMAL");
    //     break;
    //
    //   case ALERTA:
    //     Serial.println("ESTADO: ALERTA");
    //     break;
    //
    //   case INCENDIO:
    //     Serial.println("ESTADO: INCENDIO");
    //     break;
    // }

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

      // ==================================================
      // DEBUG SERIAL DESATIVADO
      // ==================================================
      // Serial.println("BOTAO DE EMERGENCIA PRESSIONADO");
      // Serial.println("ESTADO FORCADO: INCENDIO");

      // Debounce simples
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

      // ==================================================
      // DEBUG SERIAL DESATIVADO
      // ==================================================
      // Serial.println("BOTAO RESET PRESSIONADO");

      if (temperatura < LIMIAR_TEMPERATURA && fumaca < LIMIAR_FUMACA)
      {
        estadoAtual = NORMAL;

        // ==================================================
        // DEBUG SERIAL DESATIVADO
        // ==================================================
        // Serial.println("RESET ACEITO: SISTEMA NORMAL");
      }
      else
      {
        // ==================================================
        // DEBUG SERIAL DESATIVADO
        // ==================================================
        // Serial.println("RESET ACEITO, MAS AINDA HA CONDICAO DE RISCO");
      }

      // Debounce simples
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

  // Atualiza o LCD a cada 250 ms.
  // Antes estava 1000 ms, o que causava atraso visível na atualização.
  const TickType_t xPeriod = pdMS_TO_TICKS(250);

  char linha1[17];
  char linha2[17];

  for (;;)
  {
    // Copia os valores globais para variáveis locais.
    // Isso evita ler variáveis volatile várias vezes durante a montagem da tela.
    float tempLocal = temperatura;
    float fumacaLocal = fumaca;
    EstadoSistema estadoLocal = estadoAtual;
    bool emergenciaLocal = emergenciaManual;

    // Monta a primeira linha com temperatura e fumaça.
    // O formato %-16s ou o preenchimento com espaços evita sobra de caracteres antigos.
    snprintf(
      linha1,
      sizeof(linha1),
      "T:%5.1f F:%4.1f",
      tempLocal,
      fumacaLocal
    );

    lcd.setCursor(0, 0);
    lcd.print(linha1);

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

    lcd.setCursor(0, 1);
    lcd.print(linha2);

    // Aguarda até o próximo ciclo de atualização do LCD.
    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
}

// ======================================================
// T7/T8/T9 - ATUADORES
// ======================================================

void TaskAtuadores(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xPeriod = pdMS_TO_TICKS(50);

  for (;;)
  {
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

    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
}

// ======================================================
// SETUP
// ======================================================

void setup()
{
  // ==================================================
  // DEBUG SERIAL DESATIVADO
  // ==================================================
  // Serial.begin(115200);

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

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Conectando...");

  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(250);
  }

  server.begin();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi OK");

  // ==================================================
  // DEBUG SERIAL DESATIVADO
  // ==================================================
  // Serial.print("IP: ");
  // Serial.println(WiFi.localIP());

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

  xTaskCreate(TaskBotaoEmergencia, "Emergencia", 2048, NULL, 5, NULL);
  xTaskCreate(TaskBotaoReset, "Reset", 2048, NULL, 4, NULL);
  xTaskCreate(TaskTCP, "TCP", 4096, NULL, 3, NULL);
  xTaskCreate(TaskEstado, "Estado", 4096, NULL, 3, NULL);
  xTaskCreate(TaskAtuadores, "Atuadores", 2048, NULL, 2, NULL);
  xTaskCreate(TaskLCD, "LCD", 4096, NULL, 1, NULL);
}

void loop()
{
}