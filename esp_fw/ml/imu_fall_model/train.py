from __future__ import annotations

from argparse import ArgumentParser
from pathlib import Path

import numpy as np
import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset

from dataset import SisFallEnhancedDataset
from model import FallNet1D


def build_loss_class_weights(class_weights: np.ndarray) -> torch.Tensor:
    return torch.tensor(np.sqrt(class_weights), dtype=torch.float32)



def main():
    parser = ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch-size", type=int, default=256)
    args = parser.parse_args()

    ds = SisFallEnhancedDataset(args.dataset)
    x_train, y_train = ds.load_split("train")
    x_val, y_val = ds.load_split("val")
    class_weights = build_loss_class_weights(ds.load_class_weights())

    train_loader = DataLoader(
        TensorDataset(torch.from_numpy(x_train[:, None, :, :]), torch.from_numpy(y_train.argmax(axis=1))),
        batch_size=args.batch_size,
        shuffle=True,
    )
    val_loader = DataLoader(
        TensorDataset(torch.from_numpy(x_val[:, None, :, :]), torch.from_numpy(y_val.argmax(axis=1))),
        batch_size=args.batch_size,
        shuffle=False,
    )

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = FallNet1D().to(device)
    loss_fn = nn.CrossEntropyLoss(weight=class_weights.to(device))
    optimizer = torch.optim.Adam(model.parameters(), lr=1e-3)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    best_val_loss = float("inf")
    for epoch in range(args.epochs):
        model.train()
        for xb, yb in train_loader:
            xb = xb.to(device)
            yb = yb.to(device)
            optimizer.zero_grad()
            loss = loss_fn(model(xb), yb)
            loss.backward()
            optimizer.step()

        model.eval()
        losses = []
        with torch.no_grad():
            for xb, yb in val_loader:
                xb = xb.to(device)
                yb = yb.to(device)
                losses.append(loss_fn(model(xb), yb).item())
        val_loss = float(np.mean(losses))
        print(f"epoch={epoch + 1} val_loss={val_loss:.4f}")
        if val_loss < best_val_loss:
            best_val_loss = val_loss
            torch.save(model.state_dict(), args.output_dir / "fallnet_waist_3class.pt")


if __name__ == "__main__":
    main()
