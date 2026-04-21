from __future__ import annotations

import torch
from torch import nn


class FallNet1D(nn.Module):
    def __init__(self, num_channels: int = 6, num_classes: int = 3):
        if num_channels != 6:
            raise ValueError("FallNet1D expects a 6-axis IMU window input")
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(1, 8, kernel_size=(7, 3), padding=(3, 1)),
            nn.BatchNorm2d(8),
            nn.ReLU(inplace=True),
            nn.Conv2d(8, 16, kernel_size=(5, 1), stride=(2, 1), padding=(2, 0)),
            nn.BatchNorm2d(16),
            nn.ReLU(inplace=True),
            nn.Conv2d(16, 24, kernel_size=(5, 1), stride=(2, 1), padding=(2, 0)),
            nn.BatchNorm2d(24),
            nn.ReLU(inplace=True),
            nn.Conv2d(24, 32, kernel_size=(3, 1), stride=(2, 1), padding=(1, 0)),
            nn.ReLU(inplace=True),
            nn.AdaptiveAvgPool2d((1, 1)),
        )
        self.head = nn.Sequential(
            nn.Flatten(),
            nn.Linear(32, 32),
            nn.ReLU(inplace=True),
            nn.Linear(32, num_classes),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.head(self.features(x))
