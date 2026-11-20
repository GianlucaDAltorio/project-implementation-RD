# fall_model

Training pipeline for the wrist and fused fall classifiers: FallAllD to features to random forest to C header, with C-versus-Python parity checks. Scripts locate files relative to themselves, so run them from the repository root.

## Wrist model

Requires Python 3 with the packages in `requirements.txt` (tested with Python 3.13, numpy 2.5, pandas 3.0, scipy 1.18, scikit-learn 1.9) and gcc for the parity check.

1. Create the FallAllD subset as described in `data/fallalld/README.md` (`data/fallalld/my_subset_v2`).
2. `python src/fall_model/build_features.py` whole-trial features and the subject split to `data/features/features.csv`.
3. `python src/fall_model/baseline.py` optional whole-trial baseline.
4. `python src/fall_model/wrist_windows.py` impact windows and the 14 features to `data/features/wrist_windows.csv`.
5. `python src/fall_model/wrist_classify.py` cross-validated threshold and test-subject evaluation.
6. Set `THR` in `src/fall_model/export_c.py` to the threshold printed in step 5, then `python src/fall_model/export_c.py` to train the deployed model and write `src/FallDetector_trials/fall_model.h`.
7. `python src/fall_model/parity_test/parity_check.py` checks that the C detector matches the Python model.

## Fused wrist + waist model

1. Create `data/fallalld/my_subset_fusion` as described in `data/fallalld/README.md`.
2. `python src/fall_model/fusion_windows.py` impact windows with wrist features, waist features and the height change.
3. `python src/fall_model/fusion_classify.py` compares wrist-only, waist-only and fused models on the same trials.
4. `python src/fall_model/fusion_export_c.py` picks the threshold, trains the fused model and writes `src/FallDetector_trials/fall_model_fusion.h`.
5. `python src/fall_model/fusion_parity.py` checks that the C code matches Python and that the waist board's feature code is identical to the watch's.

Model summary (v2): wrist logger, 2,515 trials from 13 subjects, all 79 activities. Trigger at 2.5 g, window of 1 s before and 2 s after the peak, 14 features, random forest of 100 trees (depth 6), decision threshold 0.30. Subjects are split by number: validation 7, 10, 13; test 2, 9, 14. The first model (v1) used 1,541 trials, 45 activities and threshold 0.26.
