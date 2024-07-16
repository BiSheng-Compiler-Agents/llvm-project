
import numpy as np
import pandas as pd

import os

BISHENG_ACPO_DIR = os.environ.get("BISHENG_ACPO_DIR")
if BISHENG_ACPO_DIR is None:
  print("Please export BISHENG_ACPO_DIR")
  exit()

x_norm = pd.read_csv(os.path.join(BISHENG_ACPO_DIR, "models/model_v1/x_norm.csv"))
x_max_arr = np.array(x_norm.iloc[0])
x_min_arr = np.array(x_norm.iloc[1])

IR2Vec_output = os.path.join(BISHENG_ACPO_DIR, "ir2vec.output")
with open(IR2Vec_output, 'r') as ir_vec_file:
  prog_vec = list(map(float, ir_vec_file.read().split('\t')[: -1]))
prog_vec_arr = (np.array(prog_vec) - x_min_arr) / (x_max_arr - x_min_arr)

with open(IR2Vec_output, "w") as f:
  for number in prog_vec_arr:
    f.write(str(number))
    f.write('\t')