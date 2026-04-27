/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : I2S mic → USB CDC audio bridge
  *                   STM32F401RCTx | I2S2 Master RX | 16-bit 16 kHz
  *                   DMA1 Stream3 circular, double-buffer (ping-pong)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_cdc_if.h"   /* CDC_Transmit_FS() */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/*
 * I2S Philips standard always clocks 32-bit frames (L word + R word) at the
 * DMA level, even when DataFormat = 16B.  A mono mic (L/R pin tied low)
 * puts real audio on the LEFT slot and zeros on the RIGHT slot.
 *
 * So the DMA buffer layout per frame is:
 *   [L_sample, R_sample(=0), L_sample, R_sample(=0), ...]
 *
 * AUDIO_FRAMES   – number of mono output samples per half-buffer.
 * DMA_BUF_WORDS  – total int16 words the DMA must fill (2× frames, × 2 halves).
 *
 * USB CDC FS bulk packet ≤ 64 bytes → 32 int16 mono samples per half.
 * Each half therefore holds 32 frames × 2 slots = 64 DMA words.
 */
#define AUDIO_FRAMES      32u                          /* mono samples per half   */
#define DMA_BUF_WORDS     (AUDIO_FRAMES * 2u * 2u)    /* L+R slots, two halves   */
#define HALF_DMA_WORDS    (DMA_BUF_WORDS / 2u)        /* words per DMA half      */
#define OUT_BYTES         (AUDIO_FRAMES * sizeof(int16_t))  /* USB payload bytes  */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2S_HandleTypeDef hi2s2;
DMA_HandleTypeDef hdma_spi2_rx;

/* USER CODE BEGIN PV */

/*
 * Circular DMA buffer – layout per half: [L0, R0, L1, R1, ... L31, R31]
 * Total = AUDIO_FRAMES * 2 slots * 2 halves = 128 int16 words.
 * MUST be in a DMA-accessible RAM region (all SRAM on F401 is DMA-capable).
 */
static int16_t i2s_dma_buf[DMA_BUF_WORDS];

/*
 * Mono output staging buffer: left-channel samples extracted from one half.
 * Filled in the main loop; CDC_Transmit_FS points directly at this.
 */
static int16_t out_buf[AUDIO_FRAMES];

/*
 * Set to non-zero in the DMA callback; cleared after the USB send.
 * Values: 1 = first half ready, 2 = second half ready.
 */
static volatile uint8_t audio_ready = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2S2_Init(void);

/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
 * @brief  DMA half-transfer complete – first half of i2s_dma_buf is ready.
 */
void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI2)
    {
        audio_ready = 1;
    }
}

/**
 * @brief  DMA full-transfer complete – second half of i2s_dma_buf is ready.
 */
void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance == SPI2)
    {
        audio_ready = 2;
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */

    /* MCU Configuration -------------------------------------------------------*/
    HAL_Init();

    /* USER CODE BEGIN Init */
    /* USER CODE END Init */

    SystemClock_Config();

    /* USER CODE BEGIN SysInit */
    /* USER CODE END SysInit */

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_I2S2_Init();
    MX_USB_DEVICE_Init();

    /* USER CODE BEGIN 2 */

    /*
     * Start I2S DMA in circular mode.
     * HAL_I2S_Receive_DMA keeps refilling i2s_dma_buf continuously and fires
     * RxHalfCplt / RxCplt callbacks for the ping-pong mechanism.
     */
    HAL_I2S_Receive_DMA(&hi2s2, (uint16_t *)i2s_dma_buf, DMA_BUF_WORDS);

    /* USER CODE END 2 */

    /* Infinite loop -----------------------------------------------------------*/
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */

        uint8_t ready = audio_ready;        /* snapshot (8-bit read is atomic) */

        if (ready != 0)
        {
            audio_ready = 0;                /* clear flag before transmitting  */

            /*
             * Point at the half that is safe to read:
             *   ready == 1  →  first half  starts at index 0
             *   ready == 2  →  second half starts at index HALF_DMA_WORDS
             *
             * Each half contains AUDIO_FRAMES interleaved L/R pairs:
             *   src[0]=L0, src[1]=R0, src[2]=L1, src[3]=R1, ...
             *
             * Extract only the left-channel samples (even indices).
             */
            int16_t *src = (ready == 1) ? &i2s_dma_buf[0]
                                        : &i2s_dma_buf[HALF_DMA_WORDS];

            for (uint32_t i = 0; i < AUDIO_FRAMES; i++)
            {
                out_buf[i] = src[i * 2];   /* left slot; skip right slot (i*2+1) */
            }

            /*
             * Send raw int16 PCM bytes over USB CDC.
             * CDC_Transmit_FS() is non-blocking; USBD_BUSY means the previous
             * packet hasn't cleared — drop silently rather than stall DMA.
             */
            CDC_Transmit_FS((uint8_t *)out_buf, (uint16_t)OUT_BYTES);
        }
    }
    /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 25;
    RCC_OscInitStruct.PLL.PLLN       = 336;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ       = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief I2S2 Initialization Function
  * @retval None
  */
static void MX_I2S2_Init(void)
{
    /* USER CODE BEGIN I2S2_Init 0 */
    /* USER CODE END I2S2_Init 0 */

    /* USER CODE BEGIN I2S2_Init 1 */
    /* USER CODE END I2S2_Init 1 */

    hi2s2.Instance            = SPI2;
    hi2s2.Init.Mode           = I2S_MODE_MASTER_RX;
    hi2s2.Init.Standard       = I2S_STANDARD_PHILIPS;
    hi2s2.Init.DataFormat     = I2S_DATAFORMAT_16B;
    hi2s2.Init.MCLKOutput     = I2S_MCLKOUTPUT_DISABLE;
    hi2s2.Init.AudioFreq      = I2S_AUDIOFREQ_16K;
    hi2s2.Init.CPOL           = I2S_CPOL_LOW;
    hi2s2.Init.ClockSource    = I2S_CLOCK_PLL;
    hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
    if (HAL_I2S_Init(&hi2s2) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN I2S2_Init 2 */
    /* USER CODE END I2S2_Init 2 */
}

/**
  * @brief  Enable DMA controller clock and configure interrupt
  */
static void MX_DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
}

/**
  * @brief GPIO Initialization Function
  * @retval None
  */
static void MX_GPIO_Init(void)
{
    /* USER CODE BEGIN MX_GPIO_Init_1 */
    /* USER CODE END MX_GPIO_Init_1 */

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* USER CODE BEGIN MX_GPIO_Init_2 */
    /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  Error handler
  * @retval None
  */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    __disable_irq();
    while (1) {}
    /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */