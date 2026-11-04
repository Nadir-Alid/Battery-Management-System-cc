/* USER CODE BEGIN Header */
/**
 * @file           : main.c
 * @brief          : STM32F411CEU6 communicating with BQ76922 BMS
 * SCREENS
 * LIVE = Voltages, SOC, Current (Dedicated Line), PassQ, Balancing
 * LIVE2 = Pack, LD, Stack voltage, Current, FET state
 * TEMP = PCB temperature + BATTS thermistor
 * FAULTS = Safety flags, UV/OV/OCD/SCD, Alarm word
 * CONTROL = FET enable/off, Shutdown, Reset PassQ, Clear PF
 */

/* USER CODE END Header */
// Includes
#include "main.h"

// Private includes
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "GraphicDisplay.h"
#include "GraphicDisplay_Fonts.h"
#include "bq76922_driver.h"
#include "bms_rtos.h"
#include "FreeRTOS.h"
#include "task.h"
/* USER CODE END Includes */

// Private typedef
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

// Private define
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c3;

SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
st7565_t    St7565  = {0};
static uint8_t display_framebuffer[128U * 64U / 8U];
static bool display_ok = true;
gd_t        Graphic = {0};

/* UI-owned copies; the battery task owns the actual driver/counter. */
static BmsSnapshot UiSnapshot;
static bool snapshot_received;
static const char *ui_data_message = "WAITING FOR DATA";
static bool command_pending;
static uint32_t pending_id, next_command_id;

static uint8_t bq_ok;

// Screen & Popup definitions
typedef enum {
    SCR_LIVE = 0,
    SCR_PACK,
    SCR_TEMP,
    SCR_FAULTS,
    SCR_CONTROL,
    SCR_COUNT
} Screen_t;

static const char *scr_titles[SCR_COUNT] = {
    "LIVE", "LIVE2", "TEMP", "FAULTS", "CONTROL"
};

static Screen_t  cur_screen   = SCR_LIVE;
static uint8_t   needs_redraw = 1;

static bool      show_charge_msg = false;
static uint32_t  charge_msg_start_tick = 0;
static bool      is_charging = false;


static const char *ctrl_labels[BMS_ACTION_COUNT] = {
    "FET Enable", "All FETs Off", "Shutdown", "Reset PassQ",
    "PF Reset", "OCD Recover", "SCD Recover",
};

static int8_t  ctrl_cursor = 0;
static char    status_msg[22] = "";
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_I2C3_Init(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void delayUS(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) {}
}

void    _dsp_a0   (uint8_t s){ HAL_GPIO_WritePin(RS_GPIO_Port, RS_Pin,  (GPIO_PinState)s); }
void    _dsp_cs   (uint8_t s){ HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin,  (GPIO_PinState)s); }
void    _dsp_reset(uint8_t s){ HAL_GPIO_WritePin(GPIOA,        RSE_Pin, (GPIO_PinState)s); }
uint8_t _dsp_write(uint8_t *d, uint32_t l){ return (uint8_t)HAL_SPI_Transmit(&hspi1, d, (uint16_t)l, 100); }

static uint8_t btn_menu_last  = 1;
static uint8_t btn_enter_last = 1;
static uint8_t btn_up_last    = 1;
static uint8_t btn_down_last  = 1;

static uint8_t Btn(GPIO_TypeDef *port, uint16_t pin, uint8_t *last)
{
    uint8_t now  = (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 0u : 1u;
    uint8_t edge = (*last == 1u) && (now == 0u);
    *last = now;
    return edge;
}

/*
 * DRAW FUNCTIONS
*/
static void RefreshDisplay(void)
{
    display_ok = (GD_UpdateScreen(&Graphic) == GD_OK);
    if (!display_ok) ++BmsRtosStats.display_failures;
}

static void Title(const char *text)
{
    GD_FillRectangle(&Graphic, 0, 0, 127, 11, GD_WHITE);
    GD_SetCursor(&Graphic, 2, 2);
    GD_WriteString(&Graphic, (char *)text, Font_7x10, GD_BLACK);
    char idx[8];
    snprintf(idx, sizeof(idx), "%d/%d", (int)cur_screen + 1, SCR_COUNT);
    GD_SetCursor(&Graphic, 100, 2);
    GD_WriteString(&Graphic, idx, Font_7x10, GD_BLACK);
}

/* ── SCR_LIVE ── */
static void Draw_Live(void)
{
    char buf[32];
    GD_Fill(&Graphic, GD_BLACK);
    Title(scr_titles[SCR_LIVE]);

    if (!bq_ok) {
        GD_SetCursor(&Graphic, 10, 28);
        GD_WriteString(&Graphic, (char *)ui_data_message, Font_7x10, GD_WHITE);
        RefreshDisplay();
        return;
    }

    uint16_t c1 = UiSnapshot.telemetry.cell_mv[0];
    uint16_t c2 = UiSnapshot.telemetry.cell_mv[1];
    uint16_t c5 = UiSnapshot.telemetry.cell_mv[2];
    int16_t  ma = UiSnapshot.telemetry.pack_current_ma;

    /* Line 1: C1 and C2 */
    snprintf(buf, sizeof(buf), "1:%d.%03dV 2:%d.%03dV",
             c1 / 1000, c1 % 1000,
             c2 / 1000, c2 % 1000);
    GD_SetCursor(&Graphic, 0, 14);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    /* Line 2: C5 and High-Res Fractional SOC */
    snprintf(buf, sizeof(buf), "5:%d.%03dV S:%d.%01d%%",
             c5 / 1000, c5 % 1000,
             (int)UiSnapshot.soc_percent,
             (int)(UiSnapshot.soc_percent * 10.0f) % 10);
    GD_SetCursor(&Graphic, 0, 24);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    /* Line 3: Dedicated Current Line */
    snprintf(buf, sizeof(buf), "I : %dmA", ma);
    GD_SetCursor(&Graphic, 0, 34);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    /* Line 4: High-Res Coulomb Counter in exact mAh */
    snprintf(buf, sizeof(buf), "Q: %d.%01dmAh",
             (int)UiSnapshot.remaining_mah,
             (int)(UiSnapshot.remaining_mah * 10.0f) % 10);
    GD_SetCursor(&Graphic, 0, 44);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    /* Line 5: Active Balancing */
    char bal_str[22];
    if (!UiSnapshot.balancing_valid) {
        snprintf(bal_str, sizeof(bal_str), "BAL: pending");
    } else snprintf(bal_str, sizeof(bal_str), "BAL:1:%d 2:%d 5:%d",
             (UiSnapshot.telemetry.cb_active_mask & (1 << 0)) ? 1 : 0,
             (UiSnapshot.telemetry.cb_active_mask & (1 << 1)) ? 1 : 0,
             (UiSnapshot.telemetry.cb_active_mask & (1 << 4)) ? 1 : 0);
    GD_SetCursor(&Graphic, 0, 54);
    GD_WriteString(&Graphic, bal_str, Font_7x10, GD_WHITE);

    RefreshDisplay();
}

/* ── SCR_PACK (LIVE2) ── */
static void Draw_Pack(void)
{
    char buf[22];
    GD_Fill(&Graphic, GD_BLACK);
    Title(scr_titles[SCR_PACK]);

    if (!bq_ok) {
        GD_SetCursor(&Graphic, 10, 28);
        GD_WriteString(&Graphic, (char *)ui_data_message, Font_7x10, GD_WHITE);
        RefreshDisplay();
        return;
    }

    snprintf(buf, sizeof(buf), "PACK: %d.%03dV",
             UiSnapshot.telemetry.pack_mv / 1000, UiSnapshot.telemetry.pack_mv % 1000);
    GD_SetCursor(&Graphic, 0, 14);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    snprintf(buf, sizeof(buf), "STK : %d.%03dV",
             UiSnapshot.telemetry.stack_mv / 1000, UiSnapshot.telemetry.stack_mv % 1000);
    GD_SetCursor(&Graphic, 0, 24);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    snprintf(buf, sizeof(buf), "LD  : %d.%03dV",
             UiSnapshot.telemetry.ld_mv / 1000, UiSnapshot.telemetry.ld_mv % 1000);
    GD_SetCursor(&Graphic, 0, 34);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    int16_t ma = UiSnapshot.telemetry.pack_current_ma;
    snprintf(buf, sizeof(buf), "I   : %dmA", ma);
    GD_SetCursor(&Graphic, 0, 44);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    snprintf(buf, sizeof(buf), "FET : C:%c  D:%c",
             UiSnapshot.faults.chg_on  ? 'O' : 'X',
             UiSnapshot.faults.dsg_on  ? 'O' : 'X');
    GD_SetCursor(&Graphic, 0, 54);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    RefreshDisplay();
}

/* ── SCR_TEMP ── */
static void Draw_Temp(void)
{
    char buf[22];
    GD_Fill(&Graphic, GD_BLACK);
    Title(scr_titles[SCR_TEMP]);

    if (!bq_ok) {
        GD_SetCursor(&Graphic, 10, 28);
        GD_WriteString(&Graphic, (char *)ui_data_message, Font_7x10, GD_WHITE);
        RefreshDisplay();
        return;
    }

    int16_t die = UiSnapshot.telemetry.int_temp_dc;
    int16_t ts1 = UiSnapshot.telemetry.ts1_temp_dc;

    snprintf(buf, sizeof(buf), "PCB  : %c%d.%01d C",
             die < 0 ? '-' : ' ',
             (int)(die < 0 ? -die : die) / 10,
             (int)(die < 0 ? -die : die) % 10);
    GD_SetCursor(&Graphic, 0, 20);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    snprintf(buf, sizeof(buf), "BATTS: %c%d.%01d C",
             ts1 < 0 ? '-' : ' ',
             (int)(ts1 < 0 ? -ts1 : ts1) / 10,
             (int)(ts1 < 0 ? -ts1 : ts1) % 10);
    GD_SetCursor(&Graphic, 0, 38);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    RefreshDisplay();
}

/* ── SCR_FAULTS ── */
static void Draw_Faults(void)
{
    char buf[22];
    GD_Fill(&Graphic, GD_BLACK);
    Title(scr_titles[SCR_FAULTS]);

    if (!bq_ok) {
        GD_SetCursor(&Graphic, 10, 28);
        GD_WriteString(&Graphic, (char *)ui_data_message, Font_7x10, GD_WHITE);
        RefreshDisplay();
        return;
    }

    snprintf(buf, sizeof(buf), "UV:%c OV:%c",
             UiSnapshot.faults.uv_fault  ? '!' : '-',
             UiSnapshot.faults.ov_fault  ? '!' : '-');
    GD_SetCursor(&Graphic, 0, 14);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    snprintf(buf, sizeof(buf), "OCD:%c SCD:%c",
             UiSnapshot.faults.ocd_fault ? '!' : '-',
             UiSnapshot.faults.scd_fault ? '!' : '-');
    GD_SetCursor(&Graphic, 0, 26);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    snprintf(buf, sizeof(buf), "PROT:%s",
             UiSnapshot.faults.protections_triggered ? "ACTIVE" : "OK");
    GD_SetCursor(&Graphic, 0, 38);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    snprintf(buf, sizeof(buf), "ALM:0x%04X", UiSnapshot.faults.alarm_status);
    GD_SetCursor(&Graphic, 0, 54);
    GD_WriteString(&Graphic, buf, Font_7x10, GD_WHITE);

    RefreshDisplay();
}

/* ── SCR_CONTROL ── */
static void Draw_Control(void)
{
    GD_Fill(&Graphic, GD_BLACK);
    Title(scr_titles[SCR_CONTROL]);

    int start = ctrl_cursor - 1;
    if (start < 0) start = 0;
    if (start > BMS_ACTION_COUNT - 3) start = BMS_ACTION_COUNT - 3;

    for (int i = 0; i < 3; i++) {
        int idx = start + i;
        if (idx >= BMS_ACTION_COUNT) break;
        int16_t y = (int16_t)(14 + i * 14);
        if (idx == ctrl_cursor) {
            GD_FillRectangle(&Graphic, 0, y - 1, 127, y + 11, GD_WHITE);
            GD_SetCursor(&Graphic, 4, y);
            GD_WriteString(&Graphic, (char *)ctrl_labels[idx], Font_7x10, GD_BLACK);
        } else {
            GD_SetCursor(&Graphic, 4, y);
            GD_WriteString(&Graphic, (char *)ctrl_labels[idx], Font_7x10, GD_WHITE);
        }
    }

    GD_SetCursor(&Graphic, 0, 54);
    GD_WriteString(&Graphic, status_msg, Font_7x10, GD_WHITE);

    RefreshDisplay();
}

/* ── Dispatch ── */
static void Redraw(void)
{
    // Hijack the rendering pipeline to show the Charger overlay if active
    if (show_charge_msg) {
        GD_Fill(&Graphic, GD_BLACK);
        // Draw an  outline box
        GD_DrawRectangle(&Graphic, 4, 20, 123, 44, GD_WHITE);
        GD_SetCursor(&Graphic, 12, 28);
        GD_WriteString(&Graphic, "CHARGE DETECTED!", Font_7x10, GD_WHITE);
        RefreshDisplay();
        return;
    }

    switch (cur_screen) {
        case SCR_LIVE:    Draw_Live();    break;
        case SCR_PACK:    Draw_Pack();    break;
        case SCR_TEMP:    Draw_Temp();    break;
        case SCR_FAULTS:  Draw_Faults();  break;
        case SCR_CONTROL: Draw_Control(); break;
        default: break;
    }
}
/* USER CODE END 0 */


/* All display operations, including initialization, run in the UI task. */
static void UiDelay(uint32_t ms)
{
    /* One extra tick guarantees at least ms elapsed at arbitrary tick phase. */
    vTaskDelay(pdMS_TO_TICKS(ms) + 1U);
}

static bool UiDisplayInit(void)
{
  st7565_params_t St7565_Params = {
      .Mode                    = ST7565_MODE_SPI,
      .bUseExternalFrameBuffer = true,
      .u32Heigth               = 64,
      .u32Width                = 128,
      .delayMs                 = UiDelay,
      .spiA0                   = _dsp_a0,
      .spiCs                   = _dsp_cs,
      .spiReset                = _dsp_reset,
      .spiWrite                = _dsp_write,
      .mtxLock                 = NULL,
      .mtxUnlock               = NULL,
  };
  gd_params_t Graphic_Params = {
      .DisplayDriver  = Gd_Driver_ST7565,
      .DisplayHandle  = (void *)(&St7565),
      .u32Height      = 64,
      .u32Width       = 128,
      .mtxLock        = NULL,
      .mtxUnlock      = NULL,
  };

    if (!St7565._intern.bInitialized && ST7565_Init(&St7565, &St7565_Params) != ST7565_OK)
        return false;
    if (ST7565_SetFrameBuffer(&St7565, display_framebuffer) != ST7565_OK) return false;
    return GD_Init(&Graphic, &Graphic_Params) == GD_OK;
}

void BmsUiTask(void *argument)
{
    (void)argument;
    bool display_ready = false, display_attempted = false;
    uint32_t last_display_attempt = 0;
    TickType_t release = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(BMS_UI_INTERVAL_MS);
    for (;;) {
        if (BmsTakeSnapshot(&UiSnapshot)) {
            snapshot_received = true;
            needs_redraw = 1;
        }
        uint32_t now = HAL_GetTick();
        uint8_t previous_ok = bq_ok;
        bq_ok = snapshot_received && BmsSnapshotUsable(&UiSnapshot, now);
        if (!snapshot_received) ui_data_message = "WAITING FOR DATA";
        else if (!UiSnapshot.valid) ui_data_message = "BMS DATA ERROR";
        else ui_data_message = "BMS DATA STALE";
        if (previous_ok != bq_ok) needs_redraw = 1;

        BmsCommandResult result;
        while (BmsTakeCommandResult(&result)) {
            if (command_pending && result.id == pending_id) {
                command_pending = false;
                snprintf(status_msg, sizeof(status_msg), "%s: %s",
                         ctrl_labels[result.action], result.status == BQ_OK ? "OK" : "ERR");
                needs_redraw = 1;
            }
        }

        if (bq_ok && UiSnapshot.charging) {
            if (!is_charging) {
                is_charging = true;
                show_charge_msg = true;
                charge_msg_start_tick = now;
                needs_redraw = 1;
            }
        } else {
            is_charging = false;
            if (show_charge_msg) { show_charge_msg = false; needs_redraw = 1; }
        }
        if (show_charge_msg && (uint32_t)(now - charge_msg_start_tick) >= 3000U) {
            show_charge_msg = false;
            needs_redraw = 1;
        }

        uint8_t press_up = Btn(GPIOB, BUTTON_UP_Pin, &btn_up_last);
        uint8_t press_down = Btn(GPIOA, BUTTON_DOWN_Pin, &btn_down_last);
        uint8_t press_enter = Btn(GPIOB, BUTTON_ENTER_Pin, &btn_enter_last);
        uint8_t press_menu = Btn(GPIOB, BUTTON_MENU_Pin, &btn_menu_last);
        if (press_up || press_down || press_enter || press_menu) {
            show_charge_msg = false;
            needs_redraw = 1;
            if (press_up) {
                if (cur_screen == SCR_CONTROL) {
                    if (--ctrl_cursor < 0) ctrl_cursor = BMS_ACTION_COUNT - 1;
                } else cur_screen = (Screen_t)(cur_screen == 0 ? SCR_COUNT - 1 : cur_screen - 1);
                if (!command_pending) status_msg[0] = '\0';
            }
            if (press_down) {
                if (cur_screen == SCR_CONTROL) ctrl_cursor = (ctrl_cursor + 1) % BMS_ACTION_COUNT;
                else cur_screen = (Screen_t)((cur_screen + 1) % SCR_COUNT);
                if (!command_pending) status_msg[0] = '\0';
            }
            if (press_menu) {
                cur_screen = SCR_LIVE;
                if (!command_pending) status_msg[0] = '\0';
            }
            if (press_enter && cur_screen == SCR_CONTROL) {
                if (command_pending) {
                    snprintf(status_msg, sizeof(status_msg), "BUSY: pending");
                } else {
                    uint32_t id = ++next_command_id;
                    if (BmsSubmitCommand((BmsAction)ctrl_cursor, id)) {
                        pending_id = id;
                        command_pending = true;
                        snprintf(status_msg, sizeof(status_msg), "PENDING: %.9s", ctrl_labels[ctrl_cursor]);
                    } else snprintf(status_msg, sizeof(status_msg), "QUEUE FULL");
                }
            }
        }

        /* Failed LCD initialization never prevents battery monitoring. */
        if (!display_ready && (!display_attempted || (uint32_t)(now - last_display_attempt) >= 1000U)) {
            display_attempted = true;
            last_display_attempt = now;
            display_ready = UiDisplayInit();
            needs_redraw = 1;
            if (!display_ready) ++BmsRtosStats.display_failures;
        }
        if (display_ready && needs_redraw) {
            Redraw();
            needs_redraw = display_ok ? 0 : 1;
        }
        BmsRtosStats.ui_stack_free_words = uxTaskGetStackHighWaterMark(NULL);
        if ((TickType_t)(xTaskGetTickCount() - release) >= period) {
            ++BmsRtosStats.ui_deadline_misses;
            release = xTaskGetTickCount();
        }
        vTaskDelayUntil(&release, period);
    }
}

int main(void)
{
    if (HAL_Init() != HAL_OK) Error_Handler();
    SystemClock_Config();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_I2C3_Init();
    BmsRtosStart();
    Error_Handler();
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 64;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */

  /* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
  hi2c3.Instance = I2C3;
  hi2c3.Init.ClockSpeed = 100000;
  hi2c3.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, CS_Pin|RSE_Pin|RS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : CS_Pin RSE_Pin RS_Pin */
  GPIO_InitStruct.Pin = CS_Pin|RSE_Pin|RS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : BUTTON_DOWN_Pin */
  GPIO_InitStruct.Pin = BUTTON_DOWN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BUTTON_DOWN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BUTTON_MENU_Pin BUTTON_ENTER_Pin BUTTON_UP_Pin */
  GPIO_InitStruct.Pin = BUTTON_MENU_Pin|BUTTON_ENTER_Pin|BUTTON_UP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : ALERT_Pin */
  GPIO_InitStruct.Pin = ALERT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ALERT_GPIO_Port, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
