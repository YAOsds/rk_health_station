import torch

from model import FallNet1D



def test_fallnet_forward_emits_three_class_logits():
    model = FallNet1D(num_channels=6, num_classes=3)
    x = torch.randn(4, 1, 256, 6)
    logits = model(x)

    assert logits.shape == (4, 3)
