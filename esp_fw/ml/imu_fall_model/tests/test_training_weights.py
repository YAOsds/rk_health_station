import numpy as np
import torch

from train import build_loss_class_weights


def test_build_loss_class_weights_uses_square_root_scaling():
    weights = build_loss_class_weights(np.array([1.0, 100.0, 25.0], dtype=np.float32))

    assert isinstance(weights, torch.Tensor)
    assert weights.dtype == torch.float32
    assert weights.tolist() == [1.0, 10.0, 5.0]
