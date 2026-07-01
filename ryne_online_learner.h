/**
 * ============================================================
 *  RYNE Online Learner — Gaussian Naive Bayes
 *  Lightweight ML system untuk vibe detection yang adaptive
 *
 *  Fitur:
 *    - Online learning (belajar real-time di device)
 *    - Personalized model per user
 *    - Incremental training (Welford's algorithm)
 *    - Persistent storage (SPIFFS)
 *    - Fallback ke heuristic jika data belum cukup
 *
 *  Memory usage: ~5-10KB PSRAM
 * ============================================================
 */

#ifndef RYNE_ONLINE_LEARNER_H
#define RYNE_ONLINE_LEARNER_H

#include <Arduino.h>
#include <math.h>
#include <Preferences.h>

// ──────────────────────────────────────────────────
// CONSTANTS
// ──────────────────────────────────────────────────
#define RYNE_NB_NUM_CLASSES       8
#define RYNE_NB_NUM_FEATURES      3
#define RYNE_NB_MIN_SAMPLES       15    // Min data sebelum mulai prediksi
#define RYNE_NB_STORAGE_PREFIX    "ryne_nb"

// Vibe classes (harus match dengan enum di main file)
enum RyneVibeClass {
  RVIBE_SEMANGAT   = 0,
  RVIBE_SENDU      = 1,
  RVIBE_BOSEN      = 2,
  RVIBE_NOSTALGIK  = 3,
  RVIBE_FOKUS      = 4,
  RVIBE_GELISAH    = 5,
  RVIBE_SANTAI     = 6,
  RVIBE_EXCITED    = 7
};

// ──────────────────────────────────────────────────
// GAUSSIAN NAIVE BAYES LEARNER
// ──────────────────────────────────────────────────
class RyneNBLearner {
private:
  // ═══ Per-class statistics (8 vibe categories × 3 features)
  float mean_[RYNE_NB_NUM_CLASSES][RYNE_NB_NUM_FEATURES];
  float M2_[RYNE_NB_NUM_CLASSES][RYNE_NB_NUM_FEATURES];  // For Welford's var
  int   count_[RYNE_NB_NUM_CLASSES];                      // Sample count per class
  
  int   total_samples_;
  bool  is_ready_;  // Ready untuk prediksi?
  
  // ═══ Helper functions
  float gaussian_pdf_(float x, float mean, float var);
  void  normalize_features_(float& loudness, float& skip, float& vol_change);
  
public:
  RyneNBLearner();
  
  // ═══ Training (online)
  void train(float loudness, float skip_freq, float vol_change, int vibe_label);
  
  // ═══ Prediction
  int  predict(float loudness, float skip_freq, float vol_change);
  float get_confidence();
  
  // ═══ Storage
  void save_to_preferences();
  void load_from_preferences();
  void reset_model();
  
  // ═══ Getters
  int   get_total_samples() { return total_samples_; }
  bool  is_ready() { return is_ready_; }
  int   get_class_samples(int class_idx) { 
    return (class_idx >= 0 && class_idx < RYNE_NB_NUM_CLASSES) 
           ? count_[class_idx] : 0; 
  }
  
  // ═══ Debug
  void print_stats();
};

#endif  // RYNE_ONLINE_LEARNER_H
