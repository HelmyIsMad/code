import os
import numpy as np
import librosa
import tensorflow as tf
from tensorflow.keras import layers, models

# Parameters
DATASET_PATH = 'dataset'
SAMPLE_RATE = 16000
DURATION = 1  # seconds
N_MFCC = 13
FRAME_SIZE = 2048
HOP_LENGTH = 512

def prepare_data():
    X, y = [], []
    speakers = sorted(os.listdir(DATASET_PATH))
    label_map = {speaker: i for i, speaker in enumerate(speakers)}
    
    for speaker in speakers:
        speaker_dir = os.path.join(DATASET_PATH, speaker)
        for file in os.listdir(speaker_dir):
            if file.endswith('.wav'):
                path = os.path.join(speaker_dir, file)
                audio, _ = librosa.load(path, sr=SAMPLE_RATE, duration=DURATION)
                # Pad/Truncate to ensure consistent length
                if len(audio) < SAMPLE_RATE:
                    audio = np.pad(audio, (0, SAMPLE_RATE - len(audio)))
                
                mfcc = librosa.feature.mfcc(y=audio, sr=SAMPLE_RATE, n_mfcc=N_MFCC, 
                                            n_fft=FRAME_SIZE, hop_length=HOP_LENGTH)
                X.append(mfcc.T) # Shape: (time_steps, n_mfcc)
                y.append(label_map[speaker])
                
    return np.array(X), np.array(y), speakers

# 1. Load Data
X, y, speaker_names = prepare_data()
X = X[..., np.newaxis] # Add channel dimension for CNN

# 2. Build Model (Depthwise Separable CNN for efficiency)
model = models.Sequential([
    layers.Input(shape=(X.shape[1], X.shape[2], 1)),
    layers.Conv2D(16, (3, 3), activation='relu', padding='same'),
    layers.MaxPooling2D((2, 2)),
    layers.SeparableConv2D(32, (3, 3), activation='relu', padding='same'),
    layers.GlobalAveragePooling2D(),
    layers.Dense(len(speaker_names), activation='softmax')
])

model.compile(optimizer='adam', loss='sparse_categorical_crossentropy', metrics=['accuracy'])
model.fit(X, y, epochs=50, batch_size=16, validation_split=0.2)

# 3. Convert to TFLite
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
tflite_model = converter.convert()

# 4. Save as C Header
with open("model_data.h", "w") as f:
    f.write("const unsigned char speaker_model_tflite[] = {\n")
    for i, val in enumerate(tflite_model):
        f.write(f"0x{val:02x}, " + ("\n" if (i + 1) % 12 == 0 else ""))
    f.write("\n};\n")
    f.write(f"const int speaker_model_tflite_len = {len(tflite_model)};")

print("Model generated: model_data.h")