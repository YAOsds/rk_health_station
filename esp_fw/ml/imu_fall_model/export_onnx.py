from __future__ import annotations

from argparse import ArgumentParser
from pathlib import Path

import torch

from model import FallNet1D



def main():
    parser = ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    model = FallNet1D()
    model.load_state_dict(torch.load(args.checkpoint, map_location="cpu"))
    model.eval()
    dummy = torch.randn(1, 1, 256, 6)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(
        model,
        dummy,
        args.output,
        input_names=["imu_window"],
        output_names=["logits"],
        opset_version=13,
    )


if __name__ == "__main__":
    main()
