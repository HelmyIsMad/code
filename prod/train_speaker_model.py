import os
import numpy as np
import librosa
import tensorflow as tf
from tensorflow import keras # type: ignore
from tensorflow.keras import layers # type: ignore
from sklearn.preprocessing import StandardScaler
import warnings
warnings.filterwarnings("ignore")

DATASET_DIR = r"C:\Users\Helmy\Desktop\Coding\Embedded\prod"
NEW_OSAMA = os.path.join(DATASET_DIR, "dataset", "osama")
os.makedirs(os.path.join(DATASET_DIR, "models"), exist_ok=True)

os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
tf.get_logger().setLevel("ERROR")

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

X = []
for f in sorted(os.listdir(NEW_OSAMA)):
    if f.endswith(".wav"):
        path = os.path.join(NEW_OSAMA, f)
        feat = extract_features(path)
        X.append(feat)
        print(f"Loaded {f} -> shape {feat.shape}")

X = np.array(X)
n_samples, n_feat = X.shape
print(f"\nTotal samples: {n_samples}, Feature dim: {n_feat}")

scaler = StandardScaler()
X_scaled = scaler.fit_transform(X)

n_aug = 300
np.random.seed(42)
noise_levels = np.linspace(0.02, 0.3, n_aug)
X_aug = np.vstack([X_scaled + nl * np.random.randn(n_feat) for nl in np.tile(noise_levels, n_samples)])
X_train = np.vstack([X_scaled, X_aug])
print(f"Train set: {len(X_train)} (original={n_samples}, augmented={n_aug * n_samples})")

def build_ae():
    model = keras.Sequential([
        layers.Input(shape=(n_feat,)),
        layers.Dense(64, activation="relu"),
        layers.Dense(32, activation="relu"),
        layers.Dense(16, activation="relu"),
        layers.Dense(8, activation="relu"),
        layers.Dense(16, activation="relu"),
        layers.Dense(32, activation="relu"),
        layers.Dense(64, activation="relu"),
        layers.Dense(n_feat),
    ])
    model.compile(optimizer=keras.optimizers.Adam(0.001), loss="mse")
    return model

model = build_ae()
print("\nTraining FC Autoencoder...")
history = model.fit(X_train, X_train, epochs=300, batch_size=16, verbose=0)
print(f"Final reconstruction loss: {history.history['loss'][-1]:.6f}")

X_pred = model.predict(X_train, verbose=0)
train_mse = np.mean(np.square(X_train - X_pred), axis=1)
mean_mse = np.mean(train_mse)
std_mse = np.std(train_mse)
threshold = mean_mse + 2 * std_mse
print(f"MSE mean={mean_mse:.6f}, std={std_mse:.6f}, threshold={threshold:.6f}")

print("\nTraining data reconstruction:")
for i, f in enumerate(sorted(os.listdir(NEW_OSAMA))):
    mse = np.mean(np.square(X_scaled[i] - X_pred[i]))
    ok = "OSAMA" if mse <= threshold else "NOT OSAMA"
    print(f"  {f}: mse={mse:.6f} -> {ok}")

model.save(os.path.join(DATASET_DIR, "models", "speaker_model.keras"))
with open(os.path.join(DATASET_DIR, "models", "scaler.pkl"), "wb") as f:
    import pickle
    pickle.dump(scaler, f)
with open(os.path.join(DATASET_DIR, "models", "threshold.pkl"), "wb") as f:
    pickle.dump(threshold, f)
print(f"\nModel saved to {DATASET_DIR}\\models\\speaker_model.keras")
print(f"Threshold: {threshold:.6f}")