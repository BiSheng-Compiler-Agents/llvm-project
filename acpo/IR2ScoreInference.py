#===- FIInference.py - ACPO Function Inlining Inference   ---------------===//
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Copyright (C) 2021-2023. Huawei Technologies Co., Ltd. All rights reserved.
#
#===----------------------------------------------------------------------===//
from MLInference import MLInference, ACPO_LOG
import numpy as np
import tensorflow as tf
import tensorflow_probability
import tf_agents
import pandas as pd
import os

class IR2ScoreInference(MLInference):

  def prepare_features(self):
    df = pd.DataFrame(self.features)
    input = np.array(df, dtype=np.float32)
    input = input.reshape(1, len(self.features))
    return input
  
  def rescale_toscore(self, scaled_scores, maxscore, minscore):
    scores = np.zeros(scaled_scores.shape[0])
    for k in range(len(scores)):
        scores[k] = scaled_scores[k] * (maxscore - minscore) + minscore
    return (scores).astype(float)
  
  def inference(self):
    """
        Run an inference pass with an already loaded model and having features ready.
        This is for function inlining only for other inferences please see inference().
        """
    ACPO_LOG("ACPO Model successfully loaded for IR2Score.")

    input = self.prepare_features()
    output = self.infer(tf.constant(input))
    output = output.get(self.output_key)
    if (output is None):
      return {}
    output = output.numpy()
    
    print(self.output_names)
    print(output)
    output_dict = {}
    file_dir = os.path.dirname(os.path.abspath(__file__))
    y_norm = pd.read_csv(os.path.join(file_dir, "models/model_v1/y_norm.csv"))
    score = self.rescale_toscore(output, y_norm.iloc[0].iloc[0], y_norm.iloc[1].iloc[0])
    for i in range(len(self.output_names)):
      output_dict[self.output_names[i]] = score[i]
      ACPO_LOG("Prediction is IR2Score=" +
            str(output_dict.get("IRSCORE")) + "\n")
    return output_dict
