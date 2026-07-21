from __future__ import annotations

import argparse
import json
import urllib.request


def main() -> None:
    parser = argparse.ArgumentParser(description="Publish one status update to the local bridge")
    parser.add_argument("state", choices=["idle", "running", "waiting", "done", "error", "offline"])
    parser.add_argument("--task", default="")
    parser.add_argument("--step", default="")
    parser.add_argument("--progress", type=int, default=0)
    parser.add_argument("--step-index", type=int, default=0)
    parser.add_argument("--step-total", type=int, default=0)
    parser.add_argument("--elapsed", type=int, default=0)
    parser.add_argument("--message", default="")
    parser.add_argument("--error", default="")
    parser.add_argument("--url", default="http://127.0.0.1:8787/status")
    args = parser.parse_args()
    payload = {
        "state": args.state,
        "task": args.task,
        "step": args.step,
        "progress": max(0, min(100, args.progress)),
        "step_index": max(0, args.step_index),
        "step_total": max(0, args.step_total),
        "elapsed_s": max(0, args.elapsed),
        "message": args.message,
        "error": args.error,
    }
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(args.url, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=3) as response:
        print(response.read().decode("utf-8"))


if __name__ == "__main__":
    main()
