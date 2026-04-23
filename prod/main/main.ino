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
#define NOISE_THRESHOLD 500
#define SAMPLE_RATE 16000
#define DOWNSAMPLED_RATE 2000
#define DOWNSAMPLE_RATIO (SAMPLE_RATE / DOWNSAMPLED_RATE)
#define MAX_SAMPLES TEMPLATE_SIZE

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
  if (numSamples < 100) return;
  
  float sum = 0, sumSq = 0, maxVal = 0;
  int zeroCross = 0;
  int16_t prev = audio[0];
  
  for (int i = 0; i < numSamples; i++) {
    float v = (float)audio[i];
    sum += fabsf(v);
    sumSq += v * v;
    if (fabsf(v) > maxVal) maxVal = fabsf(v);
    if ((audio[i] > 0 && prev <= 0) || (audio[i] < 0 && prev >= 0)) zeroCross++;
    prev = audio[i];
  }
  
  float mean = sum / numSamples;
  float std = sqrtf(sumSq / numSamples - mean * mean);
  
  int idx = 0;
  features[idx++] = mean;
  features[idx++] = std;
  features[idx++] = maxVal;
  features[idx++] = (float)zeroCross / numSamples;
  
  // 10 segment energy stats
  int segSize = numSamples / 10;
  for (int s = 0; s < 10; s++) {
    float segSum = 0;
    for (int i = s*segSize; i < (s+1)*segSize && i < numSamples; i++) {
      segSum += fabsf((float)audio[i]);
    }
    features[idx++] = segSum / segSize;
  }
  
  // 10 pitch autocorrelation values
  for (int lag = 10; lag <= 100; lag += 10) {
    float ac = 0, ac0 = 0;
    for (int i = 0; i < numSamples - lag; i++) {
      ac += (float)audio[i] * (float)audio[i + lag];
      ac0 += (float)audio[i] * (float)audio[i];
    }
    if (ac0 > 0) ac /= ac0;
    features[idx++] = ac;
  }
  
  // 9 frequency ratio bands (simplified)
  int seg9 = numSamples / 9;
  for (int s = 0; s < 9; s++) {
    float segSum = 0;
    for (int i = s*seg9; i < (s+1)*seg9 && i < numSamples; i++) {
      segSum += (float)audio[i] * (float)audio[i];
    }
    features[idx++] = segSum;
  }
}

int nearestCentroid(const float* features) {
  float scaled[NUM_FEATURES];
  for (int i = 0; i < NUM_FEATURES; i++) {
    scaled[i] = (features[i] - mean[i]) / scale[i];
  }
  
  float minDist = 1e10f;
  int predicted = 0;
  
  for (int i = 0; i < NUM_SPEAKERS; i++) {
    float dist = 0;
    for (int j = 0; j < NUM_FEATURES; j++) {
      float diff = scaled[j] - centroids[i * NUM_FEATURES + j];
      dist += diff * diff;
    }
    if (dist < minDist) {
      minDist = dist;
      predicted = i;
    }
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
      Serial.print("Recording stopped (");
      Serial.print(recordingCount);
      Serial.println(" samples). Classifying...");
      
      float features[NUM_FEATURES];
      extractFeatures(audioBuffer, recordingCount, features);
      
      int predicted = nearestCentroid(features);
      Serial.print("Result: ");
      Serial.println(SPEAKER_NAMES[predicted]);
      
      state = IDLE;
      recordingCount = 0;
    }
  }
  
  if (state == RECORDING) {
    if (now - lastMicros >= 71) {
      lastMicros += 71;
      sampleCounter++;
      
      if (sampleCounter % DOWNSAMPLE_RATIO == 0) {
        int16_t sample = readSample();
        sample = applyFilters(sample);
        if (recordingCount < MAX_SAMPLES) {
          audioBuffer[recordingCount++] = sample;
        }
      }
    }
  }
  
  if (state == RECORDING && recordingCount >= MAX_SAMPLES) {
    state = CLASSIFYING;
    digitalWrite(PIN_LED, HIGH);
    Serial.println("Buffer full. Classifying...");
    
    float features[NUM_FEATURES];
    extractFeatures(audioBuffer, recordingCount, features);
    
    int predicted = nearestCentroid(features);
    Serial.print("Result: ");
    Serial.println(SPEAKER_NAMES[predicted]);
    
    state = IDLE;
    recordingCount = 0;
  }
}