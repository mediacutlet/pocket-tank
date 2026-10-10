#!/usr/bin/env python3
"""Export a llama2.c checkpoint as 4-bit weights ("version 3") for the device.

Layout mirrors llama2.c export.py --version 2 (Q8_0) exactly, except the
quantized payload: symmetric 4-bit per group of GROUP_SIZE weights, stored as
packed nibbles (weight 2i -> low nibble, 2i+1 -> high nibble, value q+8 in
0..15 for q in -8..7) followed by fp16 group scales. Norms stay fp32.

    256-byte header: magic "ak42", version=3, 7 config ints, shared_classifier
    u8, group_size i32, then zero pad
    fp32 norms (att norms, ffn norms, final norm)
    per tensor (emb, wq*, wk*, wv*, wo*, w1*, w2*, w3*[, wcls]):
        uint8 nibbles [numel/2]  then  fp16 scales [numel/GS]

Size for the 14.3M student at GS=64: ~7.6 MB (fits the 5-8 MB flash target).
Reader: model/runq4.c (Mac) and firmware/components/llm (ESP32).

  ~/.venvs/aquapets/bin/python export_q4.py out/model_q4.bin --checkpoint out/ckpt_v2w.pt
"""
import argparse, os, struct, sys
import numpy as np
import torch

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "llama2.c"))
from export import load_checkpoint, serialize_fp32  # noqa: E402

GROUP_SIZE = 64


def quantize_q4(w, gs):
    w = w.float().reshape(-1, gs)
    wmax = torch.abs(w).max(dim=1).values
    scale = torch.where(wmax > 0, wmax / 7.0, torch.ones_like(wmax))
    q = torch.clamp(torch.round(w / scale[:, None]), -8, 7).to(torch.int8)
    err = (q.float() * scale[:, None] - w).abs().max().item()
    return q.view(-1), scale, err


def pack_nibbles(q):
    u = (q.to(torch.int16) + 8).to(torch.uint8).numpy()
    return (u[0::2] | (u[1::2] << 4)).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("filepath")
    ap.add_argument("--checkpoint", required=True)
    ap.add_argument("--group-size", type=int, default=GROUP_SIZE)
    a = ap.parse_args()
    model = load_checkpoint(a.checkpoint)
    gs = a.group_size
    weights = [model.tok_embeddings.weight,
               *[l.attention.wq.weight for l in model.layers],
               *[l.attention.wk.weight for l in model.layers],
               *[l.attention.wv.weight for l in model.layers],
               *[l.attention.wo.weight for l in model.layers],
               *[l.feed_forward.w1.weight for l in model.layers],
               *[l.feed_forward.w2.weight for l in model.layers],
               *[l.feed_forward.w3.weight for l in model.layers]]
    shared = torch.equal(model.tok_embeddings.weight, model.output.weight)
    if not shared:
        weights.append(model.output.weight)
    for w in weights:
        assert w.numel() % gs == 0 and (w.numel() // 2) * 2 == w.numel()

    p = model.params
    hidden = model.layers[0].feed_forward.w1.weight.shape[0]
    # the device engine (common/llm/q4_model.c model_layout_valid) refuses a model
    # whose dim or FFN hidden dim is not a multiple of the group size - say so
    # here, not as a "bad model" at boot (dim 384 -> hidden 1024 is fine; a toy
    # dim 128 -> hidden 352 is not)
    if p.dim % gs or hidden % gs:
        sys.exit(f"dim {p.dim} / hidden {hidden} not multiples of {gs}: q4_model.c would refuse this model")
    n_kv = p.n_heads if p.n_kv_heads is None else p.n_kv_heads
    with open(a.filepath, "wb") as f:
        f.write(struct.pack("I", 0x616B3432))
        f.write(struct.pack("i", 3))
        f.write(struct.pack("iiiiiii", p.dim, hidden, p.n_layers, p.n_heads, n_kv, p.vocab_size, p.max_seq_len))
        f.write(struct.pack("B", int(shared)))
        f.write(struct.pack("i", gs))
        f.write(b"\0" * (256 - f.tell()))
        for l in model.layers: serialize_fp32(f, l.attention_norm.weight)
        for l in model.layers: serialize_fp32(f, l.ffn_norm.weight)
        serialize_fp32(f, model.norm.weight)
        worst = 0.0
        for w in weights:
            q, s, err = quantize_q4(w.detach(), gs)
            f.write(pack_nibbles(q).tobytes())
            f.write(s.to(torch.float16).numpy().astype(np.float16).tobytes())
            worst = max(worst, err)
    print(f"wrote {a.filepath}: {os.path.getsize(a.filepath)/1e6:.2f} MB, GS={gs}, "
          f"max group abs error {worst:.4f}, shared_classifier={shared}")


if __name__ == "__main__":
    main()
