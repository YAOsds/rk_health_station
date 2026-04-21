from __future__ import annotations

from argparse import ArgumentParser
from pathlib import Path
import json

import numpy as np
import torch
from torch.utils.data import DataLoader, TensorDataset

from esp_ppq.api import espdl_quantize_onnx

from dataset import SisFallEnhancedDataset


INPUT_SHAPE = [1, 1, 256, 6]


def collate_calibration_batch(batch):
    tensors = [item[0] if isinstance(item, (list, tuple)) else item for item in batch]
    return torch.stack([tensor.float() for tensor in tensors], dim=0)


def build_calibration_loader(calibration_dir: Path, max_samples: int, batch_size: int) -> DataLoader:
    dataset = SisFallEnhancedDataset(calibration_dir)
    x_train, _ = dataset.load_split("train")
    sample_count = min(max_samples, x_train.shape[0])
    if sample_count <= 0:
        raise ValueError("max_samples must be greater than 0")

    windows = np.ascontiguousarray(x_train[:sample_count, None, :, :], dtype=np.float32)
    tensor_dataset = TensorDataset(torch.from_numpy(windows))
    return DataLoader(
        tensor_dataset,
        batch_size=min(batch_size, sample_count),
        shuffle=False,
        collate_fn=collate_calibration_batch,
    )


def quantize_onnx_model(
    onnx_path: Path,
    output_path: Path,
    calibration_dir: Path,
    max_samples: int = 128,
    batch_size: int = 8,
    target: str = "esp32s3",
    device: str = "cpu",
    error_report: bool = False,
):
    calibration_loader = build_calibration_loader(
        calibration_dir=calibration_dir,
        max_samples=max_samples,
        batch_size=batch_size,
    )
    calib_steps = len(calibration_loader.dataset)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    espdl_quantize_onnx(
        onnx_import_file=str(onnx_path),
        espdl_export_file=str(output_path),
        calib_dataloader=calibration_loader,
        calib_steps=calib_steps,
        input_shape=INPUT_SHAPE,
        target=target,
        collate_fn=collate_calibration_batch,
        device=device,
        error_report=error_report,
        verbose=1,
    )


def main():
    parser = ArgumentParser()
    parser.add_argument("--onnx", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--calibration-dir", type=Path, required=True)
    parser.add_argument("--max-samples", type=int, default=128)
    parser.add_argument("--batch-size", type=int, default=8)
    parser.add_argument("--target", default="esp32s3")
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--error-report", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    config = {
        "onnx": str(args.onnx),
        "output": str(args.output),
        "calibration_dir": str(args.calibration_dir),
        "max_samples": args.max_samples,
        "batch_size": args.batch_size,
        "target": args.target,
        "device": args.device,
        "error_report": args.error_report,
    }
    print(json.dumps(config, indent=2))
    if args.dry_run:
        return

    quantize_onnx_model(
        onnx_path=args.onnx,
        output_path=args.output,
        calibration_dir=args.calibration_dir,
        max_samples=args.max_samples,
        batch_size=args.batch_size,
        target=args.target,
        device=args.device,
        error_report=args.error_report,
    )


if __name__ == "__main__":
    main()
