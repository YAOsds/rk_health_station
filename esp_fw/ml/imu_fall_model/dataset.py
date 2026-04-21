from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass
class SisFallEnhancedDataset:
    root: Path

    _split_sizes = {
        "train": 77871,
        "val": 20233,
        "test": 18884,
    }
    _window_shape = (256, 6)
    _num_classes = 3

    def load_split(self, split: str):
        count = self._split_sizes[split]
        x = np.fromfile(self.root / f"x_{split}_3", dtype=np.float32)
        y = np.fromfile(self.root / f"y_{split}_3", dtype=np.uint8)
        return (
            x.reshape(count, *self._window_shape),
            y.reshape(count, self._num_classes),
        )

    def load_class_weights(self):
        _, y_train = self.load_split("train")
        class_counts = y_train.sum(axis=0).astype(np.float32)
        majority_count = float(class_counts.max())
        return majority_count / class_counts
