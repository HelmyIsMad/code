#include <Arduino.h>
#include "model.h"

#define PIN_SCK PB10
#define PIN_WS  PA4
#define PIN_SD  PA3
#define PIN_KEY PA0
#define PIN_LED PC13

#define SCK_HIGH() (GPIOB->BSRR = GPIO_PIN_10)
#define SCK_LOW()  (GPIOB->BSRR = (uint32_t)GPIO_PIN_10 << 16)
#define WS_HIGH()  (GPIOA->BSRR = GPIO_PIN_4)
#define WS_LOW()   (GPIOA->BSRR = (uint32_t)GPIO_PIN_4 << 16)
#define READ_SD()  (GPIOA->IDR & GPIO_PIN_3)

#define HALF_PERIOD() do { __NOP(); __NOP(); __NOP(); } while(0)

#define DC_BLOCK_ALPHA 0.995f
#define NOISE_THRESHOLD 50
#define SAMPLE_RATE 16000
#define DOWNSAMPLE 8
#define MAX_SAMPLES (5000)

enum State { IDLE, RECORDING, CLASSIFYING };
volatile State state = IDLE;
volatile uint32_t recordingCount = 0;
uint32_t lastMicros = 0;
int16_t audioBuffer[MAX_SAMPLES];

int32_t dcState = 0;
int16_t lastSample = 0;
uint32_t lastKeyPress = 0;
uint32_t sampleCounter = 0;

void setup() {
  Serial.begin(500000);
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_WS, OUTPUT);
  pinMode(PIN_SD, INPUT);
  pinMode(PIN_KEY, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_SCK, LOW);
  digitalWrite(PIN_WS, HIGH);
  digitalWrite(PIN_LED, HIGH);
  Serial.println("Ready");
}

inline void clockPulse() {
  SCK_HIGH();
  HALF_PERIOD();
  SCK_LOW();
  HALF_PERIOD();
}

int16_t readSample() {
  int32_t data = 0;
  WS_LOW();
  clockPulse();
  for (int i = 0; i < 24; i++) {
    SCK_HIGH();
    HALF_PERIOD();
    data = (data << 1) | (READ_SD() ? 1 : 0);
    SCK_LOW();
    HALF_PERIOD();
  }
  for (int i = 0; i < 7; i++) clockPulse();
  WS_HIGH();
  for (int i = 0; i < 32; i++) clockPulse();
  if (data & 0x800000) data |= 0xFF000000;
  return (int16_t)(data >> 2);
}

int16_t applyFilters(int16_t sample) {
  dcState = sample - lastSample + (int32_t)(DC_BLOCK_ALPHA * dcState);
  lastSample = sample;
  sample = (int16_t)dcState;
  if (abs(sample) < NOISE_THRESHOLD) return 0;
  return sample;
}

void extractFeatures(const int16_t* audio, int numSamples, float* features) {
  for (int i = 0; i < NUM_FEATURES; i++) features[i] = 0;
  if (numSamples < 1000) return;
  
  float maxVal = 0;
  for (int i = 0; i < numSamples; i++) {
    float v = fabsf((float)audio[i]);
    if (v > maxVal) maxVal = v;
  }
  if (maxVal < 1) maxVal = 1;
  
  float norm[5000];
  for (int i = 0; i < numSamples; i++) {
    norm[i] = (float)audio[i] / maxVal;
  }
  
  int seg_size = numSamples / 16;
  int idx = 0;
  
  for (int i = 0; i < 16; i++) {
    float segSum = 0;
    for (int j = i*seg_size; j < (i+1)*seg_size; j++) segSum += fabsf(norm[j]);
    features[idx++] = segSum / seg_size;
  }
  
  for (int i = 0; i < 16; i++) {
    float segSum = 0;
    for (int j = i*seg_size; j < (i+1)*seg_size; j++) segSum += norm[j] * norm[j];
    features[idx++] = sqrtf(segSum / seg_size);
  }
  
  float sum = 0, sumSq = 0, maxV = 0, minV = 1e10f;
  for (int i = 0; i < numSamples; i++) {
    float v = fabsf(norm[i]);
    sum += v; sumSq += v*v;
    if (v > maxV) maxV = v;
    if (v < minV) minV = v;
  }
  features[idx++] = sum / numSamples;
  features[idx++] = sqrtf(sumSq/numSamples - (sum/numSamples)*(sum/numSamples));
  features[idx++] = maxV;
  features[idx++] = minV;
  
  int zcr_seg = numSamples / 8;
  for (int i = 0; i < 8; i++) {
    int zc = 0;
    for (int j = i*zcr_seg; j < (i+1)*zcr_seg - 1; j++)
      if ((norm[j] > 0 && norm[j+1] <= 0) || (norm[j] < 0 && norm[j+1] >= 0)) zc++;
    features[idx++] = (float)zc / zcr_seg;
  }
  
  int lags[] = {20, 30, 40, 50, 60, 70, 80, 90, 100, 120};
  for (int l = 0; l < 10; l++) {
    int lag = lags[l];
    float ac = 0, ac0 = 0;
    for (int i = 0; i < numSamples - lag; i++) {
      ac += norm[i] * norm[i + lag];
      ac0 += norm[i] * norm[i];
    }
    features[idx++] = (ac0 > 0) ? (ac / ac0) : 0;
  }
  
  static float fftMag[512];
  int fftSize = 1024;
  if (numSamples < fftSize) fftSize = numSamples;
  for (int k = 0; k < fftSize/2; k++) {
    float real = 0, imag = 0;
    for (int n = 0; n < fftSize; n++) {
      float w = 0.5f * (1.0f - cosf(2.0f * PI * n / fftSize));
      float angle = -2.0f * PI * k * n / fftSize;
      real += w * norm[n] * cosf(angle);
      imag += w * norm[n] * sinf(angle);
    }
    fftMag[k] = real * real + imag * imag;
  }
  int nBands = 16;
  int bandSize = (fftSize/2) / nBands;
  for (int b = 0; b < nBands; b++) {
    float bandSum = 0;
    for (int k = b*bandSize; k < (b+1)*bandSize; k++) bandSum += fftMag[k];
    features[idx++] = bandSum / bandSize;
  }
}

int nearestCentroid(const float* features) {
  float scaled[NUM_FEATURES];
  for (int i = 0; i < NUM_FEATURES; i++) scaled[i] = (features[i] - mean[i]) / scale[i];
  
  float minDist = 1e10f;
  int predicted = 0;
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    float dist = 0;
    for (int j = 0; j < NUM_FEATURES; j++) {
      float diff = scaled[j] - centroids[i * NUM_FEATURES + j];
      dist += diff * diff;
    }
    if (dist < minDist) { minDist = dist; predicted = i; }
  }
  return predicted;
}

void loop() {
  uint32_t now = micros();
  uint32_t keyState = digitalRead(PIN_KEY);
  
  if (keyState == LOW && (millis() - lastKeyPress) > 200) {
    lastKeyPress = millis();
    
    if (state == IDLE) {
      for (int i = 0; i < MAX_SAMPLES; i++) audioBuffer[i] = 0;
      state = RECORDING;
      recordingCount = 0;
      sampleCounter = 0;
      dcState = 0;
      lastSample = 0;
      digitalWrite(PIN_LED, LOW);
      Serial.println("Recording started...");
    } else if (state == RECORDING) {
      state = CLASSIFYING;
      digitalWrite(PIN_LED, HIGH);
      Serial.print("Stopped (");
      Serial.print(recordingCount);
      Serial.println(" samples)");
      
      float feat[NUM_FEATURES];
      extractFeatures(audioBuffer, recordingCount, feat);
      int pred = nearestCentroid(feat);
      Serial.print("Result: ");
      Serial.println(SPEAKER_NAMES[pred]);
      
      state = IDLE;
      recordingCount = 0;
    }
  }
  
  if (state == RECORDING) {
    if (now - lastMicros >= 62) {
      lastMicros += 62;
      sampleCounter++;
      if (sampleCounter % DOWNSAMPLE == 0) {
        int16_t sample = readSample();
        sample = applyFilters(sample);
        if (recordingCount < MAX_SAMPLES) audioBuffer[recordingCount++] = sample;
      }
    }
  }
  
  if (state == RECORDING && recordingCount >= MAX_SAMPLES) {
    state = CLASSIFYING;
    digitalWrite(PIN_LED, HIGH);
    Serial.println("Buffer full. Classifying...");
    
    float feat[NUM_FEATURES];
    extractFeatures(audioBuffer, recordingCount, feat);
    int pred = nearestCentroid(feat);
    Serial.print("Result: ");
    Serial.println(SPEAKER_NAMES[pred]);
    
    state = IDLE;
    recordingCount = 0;
  }
}