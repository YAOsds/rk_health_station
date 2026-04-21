from __future__ import annotations

from argparse import ArgumentParser
from pathlib import Path

import torch
from sklearn.metrics import classification_report, confusion_matrix

from dataset import SisFallEnhancedDataset
from model import FallNet1D



def main():
    parser = ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    args = parser.parse_args()

    ds = SisFallEnhancedDataset(args.dataset)
    x_test, y_test = ds.load_split("test")

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = FallNet1D().to(device)
    model.load_state_dict(torch.load(args.checkpoint, map_location=device))
    model.eval()

    with torch.no_grad():
        logits = model(torch.from_numpy(x_test[:, None, :, :]).to(device)).cpu().numpy()
    preds = logits.argmax(axis=1)
    labels = y_test.argmax(axis=1)
    print(confusion_matrix(labels, preds))
    print(classification_report(labels, preds, digits=4))


if __name__ == "__main__":
    main()
