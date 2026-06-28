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
  WiFiClient client;

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xPeriod = pdMS_TO_TICKS(50);

  for (;;)
  {
    if (!client || !client.connected())
    {
      client = server.available();
    }

    if (client && client.connected())
    {
      while (client.available())
      {
        String linha = client.readStringUntil('\n');

        // Remove espaços, quebras de linha e caracteres extras
        linha.trim();

        // Pacote esperado:
        // T:25.0,S:3.45
        if (linha.startsWith("T:"))
        {
          int idx = linha.indexOf(',');

          if (idx > 0)
          {
            temperatura =
              linha.substring(2, idx).toFloat();

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

            // Resposta TCP enviada ao injetor.py
            client.println("OK");
          }
          else
          {
            // ==================================================
            // DEBUG SERIAL DESATIVADO
            // ==================================================
            // Serial.println("ERRO: pacote sem virgula");

            // Resposta TCP enviada ao injetor.py
            client.println("ERRO");
          }
        }
      }
    }

    vTaskDelayUntil(&xLastWakeTime, xPeriod);
  }
}

// ======================================================
// T3 - MAQUINA DE ESTADOS
// ======================================================

void TaskEstado(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xPeriod = pdMS_TO_TICKS(500);

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
  const TickType_t xPeriod = pdMS_TO_TICKS(1000);

  for (;;)
  {
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(temperatura, 1);

    lcd.print(" F:");
    lcd.print(fumaca, 1);

    lcd.setCursor(0, 1);

    switch (estadoAtual)
    {
      case NORMAL:
        lcd.print("NORMAL");
        break;

      case ALERTA:
        lcd.print("ALERTA FUMACA");
        break;

      case INCENDIO:
        if (emergenciaManual)
        {
          lcd.print("INC MANUAL");
        }
        else
        {
          lcd.print("INCENDIO");
        }
        break;
    }

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
  xTaskCreate(TaskAtuadores, "Atuadores", 2048, NULL, 3, NULL);
  xTaskCreate(TaskEstado, "Estado", 4096, NULL, 2, NULL);
  xTaskCreate(TaskLCD, "LCD", 4096, NULL, 1, NULL);
}

void loop()
{
}