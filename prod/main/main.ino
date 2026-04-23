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

#define N_FFT 256
#define HOP_LENGTH 128
#define N_MELS 10
#define NUM_FRAMES ((MAX_SAMPLES - N_FFT) / HOP_LENGTH + 1)

enum State { IDLE, RECORDING, CLASSIFYING };
volatile State state = IDLE;
volatile uint32_t recordingCount = 0;
uint32_t lastMicros = 0;
int16_t audioBuffer[MAX_SAMPLES];

int32_t dcState = 0;
int16_t lastSample = 0;
uint32_t lastKeyPress = 0;
uint32_t sampleCounter = 0;

static float hann[N_FFT];
static bool hannInit = false;
static float melFilter[N_MELS][N_FFT/2 + 1];
static bool melInit = false;

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
  
  if (!hannInit) {
    for (int i = 0; i < N_FFT; i++) {
      hann[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / (N_FFT - 1)));
    }
    hannInit = true;
  }
  
  if (!melInit) {
    float low = 2595.0f * log10f(1.0f + 100.0f / 2595.0f);
    float high = 2595.0f * log10f(1.0f + 1000.0f / 2595.0f);
    for (int m = 0; m < N_MELS; m++) {
      float f1 = 100.0f + (900.0f * m) / N_MELS;
      float f2 = 100.0f + (900.0f * (m + 1)) / N_MELS;
      float mel1 = 2595.0f * log10f(1.0f + f1 / 2595.0f);
      float mel2 = 2595.0f * log10f(1.0f + f2 / 2595.0f);
      float melC = (mel1 + mel2) * 0.5f;
      float melW = mel2 - mel1;
      for (int k = 0; k <= N_FFT/2; k++) {
        float freq = (float)k * DOWNSAMPLED_RATE / N_FFT;
        float mel = 2595.0f * log10f(1.0f + freq / 2595.0f);
        float bank = 1.0f - fabsf(mel - melC) / (melW * 0.5f + 1e-10f);
        melFilter[m][k] = fmaxf(0, bank);
      }
    }
    melInit = true;
  }
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
  
  float melSum[N_MELS] = {0};
  float melSq[N_MELS] = {0};
  float melMin[N_MELS];
  float melMax[N_MELS];
  for (int i = 0; i < N_MELS; i++) {
    melMin[i] = 1e10f;
    melMax[i] = -1e10f;
  }
  
  int numFrames = (numSamples - N_FFT) / HOP_LENGTH + 1;
  if (numFrames <= 0) return;
  
  float frameData[N_FFT];
  float mag[N_FFT/2 + 1];
  
  for (int frame = 0; frame < numFrames; frame++) {
    int start = frame * HOP_LENGTH;
    
    for (int i = 0; i < N_FFT; i++) {
      frameData[i] = (i < numSamples - start) ? (float)audio[start + i] * hann[i] : 0;
    }
    
    for (int k = 0; k <= N_FFT/2; k++) {
      float real = 0, imag = 0;
      for (int n = 0; n < N_FFT; n++) {
        float angle = -2.0f * PI * k * n / N_FFT;
        real += frameData[n] * cosf(angle);
        imag += frameData[n] * sinf(angle);
      }
      mag[k] = real * real + imag * imag + 1e-10f;
    }
    
    float mel[N_MELS];
    for (int m = 0; m < N_MELS; m++) {
      float sum = 0;
      for (int k = 0; k <= N_FFT/2; k++) {
        sum += mag[k] * melFilter[m][k];
      }
      mel[m] = logf(sum + 1e-10f);
    }
    
    for (int m = 0; m < N_MELS; m++) {
      melSum[m] += mel[m];
      melSq[m] += mel[m] * mel[m];
      if (mel[m] < melMin[m]) melMin[m] = mel[m];
      if (mel[m] > melMax[m]) melMax[m] = mel[m];
    }
  }
  
  for (int m = 0; m < N_MELS; m++) {
    float mean = melSum[m] / numFrames;
    float std = sqrtf(fmaxf(0, melSq[m] / numFrames - mean * mean));
    int idx = m * 4;
    features[idx] = mean;
    features[idx + 1] = std;
    features[idx + 2] = melMin[m];
    features[idx + 3] = melMax[m];
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
        if (recordingCount >= MAX_SAMPLES) {
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
    }
  }
}