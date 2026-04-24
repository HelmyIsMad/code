#include <ArduinoSound.h>
#include <ArduTFLite.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model_data.h"

// Audio Config
#define SAMPLE_RATE 16000
#define BUFFER_SIZE 512
#define RECORD_TIME_MS 1000
#define TOTAL_SAMPLES (SAMPLE_RATE * RECORD_TIME_MS / 1000)

// TFLite Config
constexpr int kTensorArenaSize = 32 * 1024;
uint8_t tensorArena[kTensorArenaSize];

tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;

// Pins from connection.md
const int buttonPin = PA0;
const int ledPin = PC13;

int16_t audioBuffer[TOTAL_SAMPLES];

void setup() {
  Serial.begin(115200);
  pinMode(buttonPin, INPUT);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH); // Off (Active Low)

  // Initialize I2S Audio Input using ArduinoSound library
  if (!AudioInI2S.begin(SAMPLE_RATE, 16)) {
    Serial.println("Failed to initialize I2S input!");
    while (1);
  }

  // Initialize TFLite
  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddSeparableConv2D();
  resolver.AddGlobalAveragePool2D();
  resolver.AddSoftmax();

  static const tflite::Model* model = tflite::GetModel(speaker_model_tflite);
  static tflite::MicroInterpreter static_interpreter(model, resolver, tensorArena, kTensorArenaSize);
  interpreter = &static_interpreter;
  interpreter->AllocateTensors();
  input = interpreter->input(0);

  Serial.println("System Ready. Press button to identify speaker...");
}

void loop() {
  if (digitalRead(buttonPin) == HIGH) {
    recordAndProcess();
  }
}

void recordAndProcess() {
  digitalWrite(ledPin, LOW); // ON
  Serial.println("Recording...");

  int samplesRead = 0;
  while (samplesRead < TOTAL_SAMPLES) {
    if (AudioInI2S.available() > 0) {
      int sample = AudioInI2S.read();
      if (sample != 0 && sample != -1) {
        audioBuffer[samplesRead++] = sample;
      }
    }
  }
  
  digitalWrite(ledPin, HIGH); // OFF
  Serial.println("Processing...");

  // Feature Extraction (Simplified)
  // In a production environment, you would use CMSIS-DSP arm_mfcc_f32 here.
  // For this template, we map the raw normalized data to the input tensor.
  // Note: Model expects (Time, MFCC, 1). This is a placeholder for the feature map.
  fillInputTensor();

  // Run Inference
  if (interpreter->Invoke() != kTfLiteOk) {
    Serial.println("Inference failed!");
    return;
  }

  TfLiteTensor* output = interpreter->output(0);
  
  int speakerID = 0;
  float maxScore = 0;
  for (int i = 0; i < 5; i++) {
    float score = output->data.f[i];
    if (score > maxScore) {
      maxScore = score;
      speakerID = i;
    }
  }

  Serial.print("Detected Speaker: ");
  Serial.println(speakerID);
  Serial.print("Confidence: ");
  Serial.println(maxScore);
  
  delay(1000);
}

void fillInputTensor() {
  // This function should convert audioBuffer -> MFCCs
  // For brevity, we assume the model was trained on data matching this normalization
  for (int i = 0; i < input->bytes / sizeof(float); i++) {
    // Normalize int16 to float -1.0 to 1.0
    input->data.f[i] = (float)audioBuffer[i % TOTAL_SAMPLES] / 32768.0f;
  }
}
