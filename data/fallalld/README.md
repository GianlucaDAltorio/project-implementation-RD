# FallAllD inputs

Not committed (about 480 MB for the selected subset). To recreate:

1. Download FallAllD from IEEE DataPort and extract it.
2. Copy `ActivityID2Str.m` from the dataset to this folder.
3. From the repository root, select the files used in this project:

       python src/fall_model/select_subset.py <path_to_FallAllD> data/fallalld/my_subset

The selection keeps the wrist and waist loggers, 16 fall activities and 29 daily activities
(see `select_subset.py`).
