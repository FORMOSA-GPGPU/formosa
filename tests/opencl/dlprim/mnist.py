# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import gzip
from pathlib import Path
from urllib.request import urlopen

import torch
from torch import nn
from torch.nn import functional as F
from tqdm import trange


def mnist(root, split, count):
    root.mkdir(parents=True, exist_ok=True)
    tensors = []
    # Use the MNIST mirror also used by torchvision.datasets.MNIST.
    for kind, dimensions in (("images", 3), ("labels", 1)):
        name = f"{split}-{kind}-idx{dimensions}-ubyte.gz"
        path = root / name
        if not path.exists():
            with urlopen(
                "https://ossci-datasets.s3.amazonaws.com/mnist/" + name
            ) as response:
                path.write_bytes(response.read())
        data = bytearray(gzip.decompress(path.read_bytes()))
        offset = 4 * (dimensions + 1)
        assert int.from_bytes(data[:4], "big") == 2048 + dimensions
        tensor = torch.frombuffer(data, dtype=torch.uint8, offset=offset)
        tensors.append(tensor)
    images = tensors[0].reshape(-1, 1, 28, 28)[:count].float() / 255
    labels = tensors[1][:count].long()
    assert len(images) == len(labels) == count and labels.max() < 10
    return F.avg_pool2d(images, 4), labels


def main():
    parser = argparse.ArgumentParser(description="Tiny MNIST CNN on CPU or Formosa")
    parser.add_argument("mode", choices=("train", "infer"))
    parser.add_argument("--device", choices=("cpu", "ocl:0"), default="ocl:0")
    parser.add_argument("--data", type=Path, default=Path(".cache/mnist"))
    parser.add_argument("--model", type=Path, default=Path("build/mnist.pt"))
    parser.add_argument("--samples", type=int, default=8)
    parser.add_argument("--batch-size", type=int, default=4, help="Training batch size")
    parser.add_argument("--epochs", type=int, default=1)
    parser.add_argument("--lr", type=float, default=0.3)
    args = parser.parse_args()
    sample_limit = 60000 if args.mode == "train" else 10000
    if (
        not 0 < args.samples <= sample_limit
        or min(args.batch_size, args.epochs) <= 0
        or not 0 < args.lr < float("inf")
    ):
        parser.error(
            f"Use 1..{sample_limit} samples, positive batch size/epochs, and finite lr > 0"
        )

    if args.device == "ocl:0":
        import pytorch_ocl

    torch.set_num_threads(1)
    torch.manual_seed(42)
    model = nn.Sequential(
        nn.Conv2d(1, 2, 3, stride=2),
        nn.ReLU(),
        nn.Flatten(),
        nn.Linear(18, 10),
    ).to(args.device)
    training = args.mode == "train"
    images, labels = mnist(args.data, "train" if training else "t10k", args.samples)
    if training:
        optimizer = torch.optim.SGD(model.parameters(), lr=args.lr, foreach=False)
        for epoch in range(args.epochs):
            total_loss = 0.0
            for start in trange(
                0,
                len(images),
                args.batch_size,
                desc=f"epoch {epoch + 1}/{args.epochs}",
                unit="batch",
            ):
                x = images[start : start + args.batch_size].to(args.device)
                y = labels[start : start + args.batch_size].to(args.device)
                optimizer.zero_grad(set_to_none=True)
                loss = F.cross_entropy(model(x), y)
                loss.backward()
                optimizer.step()
                if args.device == "ocl:0":
                    torch.ocl.synchronize()
                total_loss += loss.item() * len(x)
            print(
                f"epoch={epoch + 1} loss={total_loss /
                  len(images):.4f}",
                flush=True,
            )
        args.model.parent.mkdir(parents=True, exist_ok=True)
        torch.save({k: v.cpu() for k, v in model.state_dict().items()}, args.model)
        print(f"saved {args.model}", flush=True)
    else:
        model.load_state_dict(
            torch.load(args.model, map_location="cpu", weights_only=True)
        )
        model.eval()
        correct = 0
        with torch.no_grad():
            for image, label in zip(images, labels):
                label = label.item()
                print(f"label = {label}", end=" ", flush=True)
                prediction = model(image.unsqueeze(0).to(args.device)).argmax(1).item()
                print(f"prediction = {prediction}", flush=True)
                correct += prediction == label
        print(
            f"split=test samples={len(images)} correct={correct} "
            f"accuracy={correct / len(images):.2%}",
            flush=True,
        )


if __name__ == "__main__":
    main()
