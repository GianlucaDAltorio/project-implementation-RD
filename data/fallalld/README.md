# FallAllD inputs

Not committed (the wrist subset is about 250 MB). To recreate:

1. Download FallAllD from IEEE DataPort and extract it:
   https://ieee-dataport.org/open-access/fallalld-comprehensive-dataset-human-falls-and-activities-daily-living
   (DOI 10.21227/bnya-mn34, open access).
2. Copy `ActivityID2Str.m` from the dataset to this folder.
3. From the repository root, build the subset used by the current model:

       python src/fall_model/select_subset.py <path_to_FallAllD> data/fallalld/my_subset_v2 --devices 2 --sensors AG --activities all

   This keeps the wrist logger (device 2), the accelerometer and gyroscope files only, and
   all 79 activities (1-44 daily, 101-135 falls): 2,515 trials, 5,030 files.

The fused wrist + waist model uses `my_subset_fusion` (the wrist and waist loggers with
their accelerometer, gyroscope and barometer files):

       python src/fall_model/select_subset.py <path_to_FallAllD> data/fallalld/my_subset_fusion --devices 2,3 --sensors AGB --activities all

The first model (v1) used `my_subset`: wrist and waist loggers, all four sensors, 45 activities
(`select_subset.py` with its default selection). Its outputs are in the Git history.

## Source

M. Saleh, M. Abbas and R. Le Bouquin Jeannès, "FallAllD: An Open Dataset of Human Falls and
Activities of Daily Living for Classical and Deep Learning Applications," *IEEE Sensors
Journal*, 2021, doi: 10.1109/JSEN.2020.3018335.
