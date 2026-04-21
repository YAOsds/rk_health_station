from pathlib import Path

import torch

from quantize_espdl import build_calibration_loader, quantize_onnx_model



def test_build_calibration_loader_produces_batched_imu_windows():
    loader = build_calibration_loader(
        calibration_dir=Path("/home/elf/workspace/imu_fall_detect/sisfall-enhanced"),
        max_samples=4,
        batch_size=2,
    )

    batch = next(iter(loader))
    assert isinstance(batch, torch.Tensor)
    assert batch.shape == (2, 1, 256, 6)
    assert batch.dtype == torch.float32


def test_quantize_model_uses_esp_ppq_api(monkeypatch):
    captured = {}
    fake_loader = type("FakeLoader", (), {"dataset": [0, 1, 2]})()

    def fake_quantize(**kwargs):
        captured.update(kwargs)

    monkeypatch.setattr("quantize_espdl.build_calibration_loader", lambda **kwargs: fake_loader)
    monkeypatch.setattr("quantize_espdl.espdl_quantize_onnx", fake_quantize)

    quantize_onnx_model(
        onnx_path=Path("/tmp/fallnet.onnx"),
        output_path=Path("/tmp/imu_fall_waist_3class.espdl"),
        calibration_dir=Path("/tmp/calibration"),
        max_samples=3,
        batch_size=2,
    )

    assert captured["onnx_import_file"] == str(Path("/tmp/fallnet.onnx"))
    assert captured["espdl_export_file"] == str(Path("/tmp/imu_fall_waist_3class.espdl"))
    assert captured["input_shape"] == [1, 1, 256, 6]
    assert captured["target"] == "esp32s3"
    assert captured["calib_steps"] == 3
    assert captured["calib_dataloader"] is fake_loader
