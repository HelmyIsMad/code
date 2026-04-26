# python test_speaker_model.py <audio_file.wav>
# python test_speaker_model.py --all  (test all osama samples)

import os
import sys
import numpy as np
import pickle
import librosa
import tensorflow as tf

os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
tf.get_logger().setLevel("ERROR")

DATASET_DIR = r"C:\Users\Helmy\Desktop\Coding\Embedded\prod"
MODEL_PATH = os.path.join(DATASET_DIR, "models", "speaker_model.keras")
SCALER_PATH = os.path.join(DATASET_DIR, "models", "scaler.pkl")
THRESH_PATH = os.path.join(DATASET_DIR, "models", "threshold.pkl")

def extract_features(file_path, n_mfcc=20, duration=3.0, sr=16000):
    y, sr = librosa.load(file_path, duration=duration, sr=sr)
    mfccs = librosa.feature.mfcc(y=y, sr=sr, n_mfcc=n_mfcc)
    delta = librosa.feature.delta(mfccs)
    delta2 = librosa.feature.delta(mfccs, order=2)
    features = np.concatenate([
        np.mean(mfccs, axis=1),
        np.std(mfccs, axis=1),
        np.mean(delta, axis=1),
        np.std(delta, axis=1),
        np.mean(delta2, axis=1),
        np.std(delta2, axis=1),
    ])
    return features

def predict(file_path):
    if not os.path.exists(MODEL_PATH):
        print(f"Error: Model not found at {MODEL_PATH}")
        print("Run train_speaker_model.py first.")
        sys.exit(1)

    model = tf.keras.models.load_model(MODEL_PATH)
    with open(SCALER_PATH, "rb") as f:
        scaler = pickle.load(f)
    with open(THRESH_PATH, "rb") as f:
        threshold = pickle.load(f)

    feat = extract_features(file_path).reshape(1, -1)
    feat_scaled = scaler.transform(feat)
    pred = model.predict(feat_scaled, verbose=0)
    mse = float(np.mean(np.square(feat_scaled - pred)))
    label = "OSAMA" if mse <= threshold else "NOT OSAMA"
    confidence = max(0, (threshold - mse) / threshold) * 100 if mse <= threshold else max(0, (mse - threshold) / mse) * 100
    return label, mse, threshold

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python test_speaker_model.py <audio_file.wav>")
        print("       python test_speaker_model.py --all  (test all osama samples)")
        sys.exit(1)

    if sys.argv[1] == "--all":
        test_dir = os.path.join(DATASET_DIR, "dataset", "osama")
        print("=" * 55)
        print(f"{'File':<40} {'Prediction':<12} {'MSE':<8}")
        print("=" * 55)
        for f in sorted(os.listdir(test_dir)):
            if f.endswith(".wav"):
                path = os.path.join(test_dir, f)
                label, mse, threshold = predict(path)
                print(f"{f:<40} {label:<12} {mse:>8.5f}")
        print("=" * 55)
        print(f"Threshold: {threshold:.5f}")
    else:
        file_path = sys.argv[1]
        if not os.path.exists(file_path):
            print(f"Error: File not found: {file_path}")
            sys.exit(1)
        label, mse, threshold = predict(file_path)
        print(f"File: {file_path}")
        print(f"Prediction: {label}")
        print(f"MSE: {mse:.5f} (threshold: {threshold:.5f})")