from pathlib import Path

import numpy as np

from dataset import SisFallEnhancedDataset



def test_loader_decodes_binary_splits_and_weights():
    dataset = SisFallEnhancedDataset(
        Path("/home/elf/workspace/imu_fall_detect/sisfall-enhanced")
    )

    x_train, y_train = dataset.load_split("train")
    x_val, y_val = dataset.load_split("val")
    weights = dataset.load_class_weights()

    assert x_train.shape == (77871, 256, 6)
    assert y_train.shape == (77871, 3)
    assert x_val.shape == (20233, 256, 6)
    assert y_val.shape == (20233, 3)
    assert weights.shape == (3,)
    np.testing.assert_allclose(weights, np.array([1.0, 107.668106, 33.483913], dtype=np.float32), rtol=1e-5)
    assert tuple(y_train[0].tolist()) in {(1, 0, 0), (0, 1, 0), (0, 0, 1)}
