#include <Arduino.h>

I2S_HandleTypeDef hi2s2;

void HAL_I2S_MspInit(I2S_HandleTypeDef* hi2s) {
  if (hi2s->Instance == SPI2) {
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIOB->MODER &= ~((0x3 << 24) | (0x3 << 26) | (0x3 << 30));
    GPIOB->MODER |= ((0x2 << 24) | (0x2 << 26) | (0x2 << 30));

    GPIOB->AFR[1] &= ~((0xF << 12) | (0xF << 16) | (0xF << 28));
    GPIOB->AFR[1] |= ((0x5 << 12) | (0x5 << 16) | (0x5 << 28));
  }
}

void setup() {
  Serial.begin(921600);
  delay(2000);
  Serial.println("START");
  
  pinMode(PC13, OUTPUT);
  digitalWrite(PC13, HIGH);
  
  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_MASTER_RX;
  hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_32B;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_16K;
  hi2s2.Init.CPOL = I2S_CPOL_HIGH;
  hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  hi2s2.Init.ClockSource = I2S_CLOCK_PLL;

  Serial.println("INIT");
  
  if (HAL_I2S_Init(&hi2s2) != HAL_OK) {
    Serial.println("I2S_FAIL");
    while(1) {
      digitalWrite(PC13, !digitalRead(PC13));
      delay(100);
    }
  }

  Serial.println("READY");
}

void loop() {
  uint32_t raw;
  if (HAL_I2S_Receive(&hi2s2, (uint16_t*)&raw, 1, 10) == HAL_OK) {
    int16_t sample = (int16_t)(raw >> 16);
    sample >>= 1;
    Serial.write((uint8_t*)&sample, sizeof(int16_t));
  }
}