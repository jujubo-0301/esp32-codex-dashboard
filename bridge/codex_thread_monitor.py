from __future__ import annotations

"""Translate the current local Codex thread into the ESP32 status protocol.

Only a small, safe summary is sent to the board. Conversation text and tool
outputs never leave the PC.
"""

import argparse
import ctypes
import json
import os
import re
import subprocess
import time
import unicodedata
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


# Empty means follow the most recently active Codex task, regardless of which
# project/workspace that task belongs to. Pass --workspace to pin it again.
DEFAULT_WORKSPACE = ""
DEFAULT_SESSION_ROOT = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "sessions"
DEFAULT_SESSION_INDEX = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "session_index.jsonl"
DEFAULT_TASK_NAME = "ESP32 Dashboard 开发"
RECENT_TASK_LIMIT = 4


_last_cpu_sample: tuple[int, int] | None = None
_quota_cache = {"expires": 0.0, "remaining": "--", "reset_date": "--"}
_total_runtime_cache = {"expires": 0.0, "value": 0}


def fetch_quota() -> dict[str, str]:
    """Read the official Codex usage window without exposing auth data."""
    now = time.time()
    if now < _quota_cache["expires"]:
        return {"quota_remaining": _quota_cache["remaining"], "reset_date": _quota_cache["reset_date"]}
    try:
        auth_path = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "auth.json"
        auth = json.loads(auth_path.read_text(encoding="utf-8"))
        token = str(auth.get("tokens", {}).get("access_token", ""))
        if not token:
            raise ValueError("missing access token")
        request = urllib.request.Request(
            "https://chatgpt.com/backend-api/wham/usage",
            headers={"Authorization": f"Bearer {token}", "User-Agent": "Codex status bridge", "Accept": "application/json"},
        )
        with urllib.request.urlopen(request, timeout=5) as response:
            usage = json.loads(response.read().decode("utf-8"))
        window = usage.get("rate_limit", {}).get("primary_window", {}) or {}
        used = float(window.get("used_percent", 0))
        reset_at = float(window.get("reset_at", 0))
        remaining = f"{max(0, min(100, round(100 - used)))}%"
        reset_date = datetime.fromtimestamp(reset_at).astimezone().strftime("%m-%d") if reset_at else "--"
        _quota_cache.update(expires=now + 60, remaining=remaining, reset_date=reset_date)
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError, urllib.error.URLError):
        _quota_cache["expires"] = now + 30
    return {"quota_remaining": _quota_cache["remaining"], "reset_date": _quota_cache["reset_date"]}


def collect_metrics() -> dict[str, int]:
    """Collect lightweight host metrics without adding a third-party dependency."""
    metrics = {"cpu": 0, "ram": 0, "gpu": 0}
    task_manager_metrics_ok = False
    task_manager_cpu = 0

    if os.name == "nt":
        # Match Windows Task Manager's CPU and GPU Engine counters instead of
        # mixing a kernel timer with NVIDIA's separate sampling window.
        ps_script = (
            "$cpu=(Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter \"Name='_Total'\").PercentProcessorTime;"
            "$samples=(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine);"
            "$gpu=0.0;"
            "foreach($g in ($samples | Group-Object { if ($_.Name -match 'phys_(\\d+)') { \"phys_$($Matches[1])\" } else { 'other' } })) {"
            "$v=($g.Group | Measure-Object UtilizationPercentage -Maximum).Maximum; $gpu += $v };"
            "'{0}|{1}' -f [math]::Round($cpu),[math]::Min(100,[math]::Round($gpu))"
        )
        try:
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", ps_script],
                capture_output=True, text=True, timeout=5, check=False,
            )
            values = result.stdout.strip().split("|")
            if len(values) == 2:
                metrics["cpu"] = max(0, min(100, int(float(values[0]))))
                metrics["gpu"] = max(0, min(100, int(float(values[1]))))
                task_manager_cpu = metrics["cpu"]
                task_manager_metrics_ok = True
        except (OSError, ValueError, IndexError, subprocess.TimeoutExpired):
            pass

        global _last_cpu_sample
        class FILETIME(ctypes.Structure):
            _fields_ = [("dwLowDateTime", ctypes.c_uint32), ("dwHighDateTime", ctypes.c_uint32)]

        idle, kernel, user = FILETIME(), FILETIME(), FILETIME()
        if ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
            def value(item: FILETIME) -> int:
                return (item.dwHighDateTime << 32) | item.dwLowDateTime

            idle_now = value(idle)
            total_now = value(kernel) + value(user)
            if _last_cpu_sample:
                idle_delta = idle_now - _last_cpu_sample[0]
                total_delta = total_now - _last_cpu_sample[1]
                if total_delta > 0:
                    metrics["cpu"] = max(0, min(100, round((1 - idle_delta / total_delta) * 100)))
            _last_cpu_sample = (idle_now, total_now)
        if task_manager_metrics_ok:
            metrics["cpu"] = task_manager_cpu

        class MEMORYSTATUSEX(ctypes.Structure):
            _fields_ = [
                ("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
            ]

        memory = MEMORYSTATUSEX()
        memory.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
            metrics["ram"] = int(memory.dwMemoryLoad)

    if not task_manager_metrics_ok:
        try:
            result = subprocess.run(
                ["nvidia-smi", "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"],
                capture_output=True, text=True, timeout=1.5, check=False,
            )
            first = result.stdout.strip().splitlines()[0]
            metrics["gpu"] = max(0, min(100, int(float(first))))
        except (OSError, IndexError, ValueError, subprocess.TimeoutExpired):
            pass
    return metrics


def thread_title_for_session(session: Path, index_path: Path) -> str:
    """Read the Codex thread title; never use conversation text as a title."""
    match = re.search(r"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})", session.name)
    if match and index_path.exists():
        latest_title = ""
        try:
            for line in index_path.open("r", encoding="utf-8"):
                item = json.loads(line)
                if item.get("id") == match.group(1) and item.get("thread_name"):
                    title = str(item["thread_name"]).strip()
                    # The index is append-only and a thread can be renamed.
                    # Keep the newest title instead of the first historical one.
                    latest_title = title
        except (OSError, UnicodeError, json.JSONDecodeError):
            pass
        if latest_title:
            return latest_title if len(latest_title) <= 24 else latest_title[:23] + "..."
    return DEFAULT_TASK_NAME


def latest_conclusion(rows: list[dict]) -> str:
    """Extract one compact conclusion from the latest Codex reply."""
    for row in reversed(rows):
        payload = row.get("payload", {})
        message = ""
        if row.get("type") == "event_msg" and payload.get("type") == "agent_message":
            message = str(payload.get("message", ""))
        elif row.get("type") == "response_item" and payload.get("role") == "assistant":
            content = payload.get("content", [])
            if isinstance(content, list):
                message = " ".join(str(item.get("text", "")) for item in content if isinstance(item, dict))
        if not message.strip():
            continue
        for line in message.splitlines():
            line = line.strip().lstrip("-•* ").strip()
            if not line or line.startswith("#") or line.startswith("::"):
                continue
            line = unicodedata.normalize("NFKC", line)
            # The compact CJK font contains ASCII and CJK ideographs, but not
            # typographic dashes/quotes. Normalize those away before sending
            # text to the device so a result never renders as empty boxes.
            line = "".join(ch for ch in line if ord(ch) < 128 or 0x4E00 <= ord(ch) <= 0x9FFF)
            line = " ".join(line.split())
            return line if len(line) <= 54 else line[:51] + "..."
    return "任务已完成"


def state_only(rows: list[dict]) -> str:
    """Classify a session without collecting host metrics."""
    latest_context = next((row for row in reversed(rows) if row.get("type") == "turn_context"), None)
    current_turn = (latest_context or {}).get("payload", {}).get("turn_id", "")
    started = completed = aborted = waiting = False
    pending_approval_calls: set[str] = set()
    for row in rows:
        payload = row.get("payload", {})
        kind = payload.get("type")
        if kind == "task_started":
            started, completed, aborted, waiting = True, False, False, False
        elif kind in {"approval_requested", "approval_request", "exec_approval_request", "request_user_input"}:
            waiting = True
        elif kind in {"custom_tool_call", "function_call"}:
            # Desktop Codex records permission prompts as an exec/custom tool
            # call whose input invokes request_permissions.  The approval
            # result is intentionally absent while the dialog is pending.
            call_name = str(payload.get("name", "")).lower()
            call_input = str(payload.get("input", "")).lower()
            if "request_permissions" in call_name or "request_permissions" in call_input:
                waiting = True
                call_id = str(payload.get("call_id") or payload.get("id") or "")
                if call_id:
                    pending_approval_calls.add(call_id)
        elif kind == "custom_tool_call_output":
            call_id = str(payload.get("call_id") or "")
            if call_id and call_id in pending_approval_calls:
                pending_approval_calls.discard(call_id)
                waiting = False
        elif kind in {"approval_resolved", "approval_granted", "approval_rejected", "approval_denied"}:
            waiting = False
        elif kind == "task_complete" and (not current_turn or payload.get("turn_id") == current_turn):
            completed = True
        elif kind == "turn_aborted" and (not current_turn or payload.get("turn_id") == current_turn):
            aborted = True
    if aborted:
        return "error"
    if completed:
        return "done"
    if waiting:
        return "waiting"
    return "running" if (started or current_turn) else "idle"


def progress_only(rows: list[dict]) -> int:
    state = state_only(rows)
    if state == "done":
        return 100
    if state == "error":
        return 0
    if state == "waiting":
        return 80
    latest_start = -1
    for index, row in enumerate(rows):
        if row.get("payload", {}).get("type") == "task_started":
            latest_start = index
    activity = 0
    for row in rows[latest_start + 1:]:
        if row.get("payload", {}).get("type") in {
            "agent_reasoning", "custom_tool_call", "function_call",
            "patch_apply_end", "mcp_tool_call_end",
        }:
            activity += 1
    return min(88, max(8, 8 + activity // 3))


def runtime_only(rows: list[dict]) -> int:
    """Return the latest turn duration, live while active, in seconds."""
    latest_context = next((row for row in reversed(rows) if row.get("type") == "turn_context"), None)
    current_turn = (latest_context or {}).get("payload", {}).get("turn_id", "")
    started = completed = aborted = None
    for row in rows:
        payload = row.get("payload", {})
        kind = payload.get("type")
        turn_id = payload.get("turn_id", "")
        if kind == "task_started" and (not current_turn or not turn_id or turn_id == current_turn):
            started, completed, aborted = payload, None, None
        elif kind == "task_complete" and (not current_turn or not turn_id or turn_id == current_turn):
            completed = payload
        elif kind == "turn_aborted" and (not current_turn or not turn_id or turn_id == current_turn):
            aborted = payload

    started_at = float((started or {}).get("started_at") or 0)
    terminal = completed or aborted
    if terminal:
        duration_ms = float(terminal.get("duration_ms") or 0)
        if duration_ms > 0:
            return max(0, round(duration_ms / 1000.0))
        ended_at = float(terminal.get("completed_at") or terminal.get("aborted_at") or 0)
        if started_at and ended_at >= started_at:
            return max(0, round(ended_at - started_at))
    if started_at:
        return max(0, int(time.time() - started_at))
    return 0


def total_runtime_only(rows: list[dict], include_open: bool = True, day_start: float = 0) -> int:
    """Sum every completed and active turn in one Codex task file."""
    starts: dict[str, dict] = {}
    anonymous = 0
    total = 0
    for row in rows:
        payload = row.get("payload", {})
        kind = payload.get("type")
        turn_id = str(payload.get("turn_id", ""))
        if kind == "task_started":
            key = turn_id or f"anonymous-{anonymous}"
            anonymous += 1
            starts[key] = payload
        elif kind in {"task_complete", "turn_aborted"}:
            key = turn_id if turn_id in starts else (next(reversed(starts), None) if starts else None)
            started = starts.pop(key, None) if key else None
            duration_ms = float(payload.get("duration_ms") or 0)
            started_at = float((started or {}).get("started_at") or 0)
            ended_at = float(payload.get("completed_at") or payload.get("aborted_at") or 0)
            if day_start and started_at < day_start:
                if ended_at > day_start:
                    total += max(0, round(ended_at - day_start))
            elif duration_ms > 0:
                total += max(0, round(duration_ms / 1000.0))
            elif started:
                if started_at and ended_at >= started_at:
                    total += round(ended_at - started_at)
    if include_open:
        now = time.time()
        for started in starts.values():
            started_at = float(started.get("started_at") or 0)
            if started_at:
                total += max(0, int(now - max(started_at, day_start)))
    return total


def all_tasks_runtime(root: Path) -> int:
    now = time.time()
    if now < _total_runtime_cache["expires"]:
        return int(_total_runtime_cache["value"])
    day_start = datetime.now().astimezone().replace(hour=0, minute=0, second=0, microsecond=0).timestamp()
    total = 0
    if not root.exists():
        return 0
    for path in root.rglob("rollout-*.jsonl"):
        try:
            is_active = now - path.stat().st_mtime < 300
        except OSError:
            is_active = False
        total += total_runtime_only(read_all(path), include_open=is_active, day_start=day_start)
    _total_runtime_cache.update(expires=now + 2, value=total)
    return total


def recent_tasks(root: Path, index_path: Path, limit: int = RECENT_TASK_LIMIT) -> list[dict]:
    candidates: list[tuple[float, Path]] = []
    if not root.exists():
        return []
    for path in root.rglob("rollout-*.jsonl"):
        try:
            candidates.append((path.stat().st_mtime, path))
        except OSError:
            continue
    result = []
    for _, path in sorted(candidates, reverse=True)[:limit]:
        title = thread_title_for_session(path, index_path)
        rows = read_all(path)
        result.append({
            "name": title,
            "state": state_only(rows),
            "progress": progress_only(rows),
            "elapsed_s": runtime_only(rows),
        })
    return result


def pending_approval_count(root: Path) -> int:
    """Count unresolved permission prompts across all local Codex windows."""
    count = 0
    if not root.exists():
        return 0
    for path in root.rglob("rollout-*.jsonl"):
        try:
            if state_only(read_all(path)) == "waiting":
                count += 1
        except OSError:
            continue
    return count


def aggregate_tasks(tasks: list[dict], fallback_state: str, fallback_progress: int) -> tuple[str, int, int]:
    """Summarize the visible task set for the dashboard status page."""
    if not tasks:
        return fallback_state, fallback_progress, 0
    states = [item.get("state", "idle") for item in tasks]
    if "error" in states:
        overall = "error"
    elif "running" in states:
        overall = "running"
    elif "waiting" in states:
        overall = "waiting"
    elif all(state == "done" for state in states):
        overall = "done"
    else:
        overall = fallback_state
    progress = round(sum(int(item.get("progress", 0)) for item in tasks) / len(tasks))
    elapsed = sum(max(0, int(item.get("elapsed_s", 0))) for item in tasks)
    return overall, max(0, min(100, progress)), elapsed


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def find_session(root: Path, workspace: str, explicit: str | None) -> Path | None:
    if explicit:
        path = Path(explicit).expanduser()
        return path if path.exists() else None

    wanted = os.path.normcase(os.path.normpath(workspace)) if workspace else ""
    candidates: list[tuple[float, Path]] = []
    if not root.exists():
        return None
    for path in root.rglob("rollout-*.jsonl"):
        try:
            with path.open("r", encoding="utf-8") as handle:
                first = json.loads(handle.readline())
            meta = first.get("payload", {})
            cwd = meta.get("cwd")
            if not wanted or (cwd and os.path.normcase(os.path.normpath(cwd)) == wanted):
                candidates.append((path.stat().st_mtime, path))
        except (OSError, UnicodeError, json.JSONDecodeError):
            continue
    return max(candidates, default=(0.0, None))[1]


def post(url: str, payload: dict) -> None:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=3):
        pass


def publish_idle(url: str, session_root: Path, index_path: Path) -> None:
    """Publish a fresh idle snapshot instead of leaving yesterday's data."""
    tasks = recent_tasks(session_root, index_path)
    overall_state, overall_progress, overall_elapsed = aggregate_tasks(tasks, "idle", 0)
    payload = {
        "state": "idle",
        "task": "",
        "step": "等待任务",
        "progress": 0,
        "step_index": 0,
        "step_total": 0,
        "elapsed_s": 0,
        "message": "等待新的任务",
        "error": "",
        "summary": "",
        "clock": datetime.now().astimezone().strftime("%H:%M"),
        "tasks": tasks,
        "overall_state": overall_state,
        "overall_progress": overall_progress,
        "overall_elapsed_s": all_tasks_runtime(session_root),
        "overall_task_count": len(tasks),
        **collect_metrics(),
        **fetch_quota(),
        "updated_at": iso_now(),
    }
    post(url, payload)


def classify(rows: list[dict], previous: dict | None, task_name: str = DEFAULT_TASK_NAME) -> dict:
    conclusion = latest_conclusion(rows)
    clock = datetime.now().astimezone().strftime("%H:%M")
    metrics = collect_metrics()
    latest_context = next((row for row in reversed(rows) if row.get("type") == "turn_context"), None)
    current_turn = (latest_context or {}).get("payload", {}).get("turn_id", "")
    started = None
    completed = None
    aborted = None
    activity_count = 0
    for row in rows:
        payload = row.get("payload", {})
        kind = payload.get("type")
        if kind == "task_started":
            started = payload
            completed = None
            aborted = None
            activity_count = 0
        elif kind == "task_complete" and (not current_turn or payload.get("turn_id") == current_turn):
            completed = payload
        elif kind == "turn_aborted" and (not current_turn or payload.get("turn_id") == current_turn):
            aborted = payload
        elif kind in {"agent_reasoning", "custom_tool_call", "function_call", "patch_apply_end", "mcp_tool_call_end"}:
            activity_count += 1

    if aborted:
        return {
            "state": "error", "task": task_name, "step": "任务中断",
            "progress": 0, "step_index": 0, "step_total": 0,
            "elapsed_s": 0, "message": "任务已中断", "error": "Codex 任务被中断",
            "summary": "任务被中断",
            "clock": clock,
            **metrics,
        }
    if completed:
        return {
            "state": "done", "task": task_name, "step": "已完成",
            "progress": 100, "step_index": 1, "step_total": 1,
            "elapsed_s": 0, "message": "任务已完成", "error": "",
            "summary": conclusion,
            "clock": clock,
            **metrics,
        }

    # A context record without a matching completion means the current turn is
    # still being processed (including after a context compaction).
    active = bool(started or current_turn)
    if active:
        elapsed_hint = int(previous.get("elapsed_s", 0)) if previous else 0
        # This is an activity estimate, not a fabricated exact completion
        # percentage. It advances slowly while the turn remains active.
        progress = min(88, max(8, 8 + activity_count // 3 + elapsed_hint // 8))
        return {
            "state": "running", "task": task_name, "step": "正在处理",
            "progress": progress, "step_index": 1, "step_total": 1,
            "elapsed_s": int(previous.get("elapsed_s", 0)) + 2 if previous else 0,
            "message": "正在处理最新任务", "error": "",
            "summary": "",
            "clock": clock,
            **metrics,
        }
    return {
        "state": "idle", "task": task_name, "step": "等待任务",
        "progress": 0, "step_index": 0, "step_total": 0,
        "elapsed_s": 0, "message": "等待新的任务", "error": "",
        "summary": "",
        "clock": clock,
        **metrics,
    }


def read_all(path: Path) -> list[dict]:
    rows: list[dict] = []
    try:
        with path.open("r", encoding="utf-8") as handle:
            for line in handle:
                try:
                    rows.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
    except (OSError, UnicodeError):
        pass
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description="Publish local Codex task state to the ESP32 bridge")
    parser.add_argument("--workspace", default=DEFAULT_WORKSPACE)
    parser.add_argument("--session-file", default="")
    parser.add_argument("--session-root", default=str(DEFAULT_SESSION_ROOT))
    parser.add_argument("--session-index", default=str(DEFAULT_SESSION_INDEX))
    parser.add_argument("--url", default="http://127.0.0.1:8787/status")
    parser.add_argument("--interval", type=float, default=1.0)
    args = parser.parse_args()

    previous: dict | None = None
    last_signature: tuple[str, int, int] | None = None
    while True:
        session = find_session(Path(args.session_root), args.workspace, args.session_file or None)
        if session:
            try:
                stat = session.stat()
                signature = (str(session), stat.st_size, stat.st_mtime_ns)
            except OSError:
                signature = None
            # Refresh the complete status payload on every interval. This is
            # important while idle: CPU/RAM/GPU must not freeze at the value
            # from the last running task.
            if signature:
                rows = read_all(session)
                payload = classify(rows, previous, thread_title_for_session(session, Path(args.session_index)))
                payload["tasks"] = recent_tasks(Path(args.session_root), Path(args.session_index))
                payload["overall_state"], payload["overall_progress"], payload["overall_elapsed_s"] = aggregate_tasks(
                    payload["tasks"], payload["state"], payload["progress"]
                )
                payload["overall_task_count"] = len(payload["tasks"])
                payload["overall_elapsed_s"] = all_tasks_runtime(Path(args.session_root))
                payload.update(fetch_quota())
                running_count = sum(item.get("state") == "running" for item in payload["tasks"])
                waiting_count = sum(item.get("state") == "waiting" for item in payload["tasks"])
                all_waiting_count = pending_approval_count(Path(args.session_root))
                if all_waiting_count:
                    waiting_count = max(waiting_count, all_waiting_count)
                    payload["overall_state"] = "waiting"
                if running_count and not all_waiting_count:
                    payload["state"] = "running"
                    payload["step"] = f"{running_count} 个任务同时运行"
                    payload["message"] = f"{running_count} 个任务正在处理"
                elif waiting_count and payload["state"] not in {"error", "done"}:
                    payload["state"] = "waiting"
                    payload["step"] = f"{waiting_count} 个任务等待确认"
                payload["updated_at"] = iso_now()
                try:
                    post(args.url, payload)
                    previous = payload
                    last_signature = signature
                    print(f"[monitor] {payload['state']} -> {session.name}", flush=True)
                except OSError as exc:
                    print(f"[monitor] bridge unavailable: {exc}", flush=True)
        else:
            try:
                publish_idle(args.url, Path(args.session_root), Path(args.session_index))
                print("[monitor] idle -> no active Codex task", flush=True)
            except OSError as exc:
                print(f"[monitor] bridge unavailable: {exc}", flush=True)
        time.sleep(max(0.5, args.interval))


if __name__ == "__main__":
    main()
