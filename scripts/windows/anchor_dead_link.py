#!/usr/bin/env python3
"""A real dead link for the anchor_dead_link scenario (testing only).

Starts the local Anchor server (wrangler dev), runs the scenario against it, and when the game has connected,
suspends the server's process (workerd) so it stays connected but never answers again, not even to a close
frame: what a friend's 11c log showed (a 10 s freeze at "[anchor] disconnected"). Then BLUEWAKE_TEST_ANCHOR_CYCLE
in the scenario disconnects, reconnects (the server is dead, so it stalls) and disconnects again; each must hold
the frame under 5 ms.

    python scripts/windows/anchor_dead_link.py --exe <exe> [--out <build/windows>] [--port 8913]

The server comes from server/anchor of this checkout, or --server-dir (a checkout with node_modules). Its state
goes to a temporary folder. Exit code 0 when the scenario passed and every "held the frame" line is under 5 ms.
"""
from __future__ import annotations

import argparse
import ctypes
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def suspend_tree(root_pid: int, suspend: bool = True) -> int:
    """NtSuspendProcess (or NtResumeProcess) on the descendants named workerd (a child of wrangler's node)."""
    out = subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         "Get-CimInstance Win32_Process | ForEach-Object { \"$($_.ProcessId) $($_.ParentProcessId) $($_.Name)\" }"],
        capture_output=True, text=True).stdout
    kids: dict[int, list[tuple[int, str]]] = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0].isdigit() and parts[1].isdigit():
            kids.setdefault(int(parts[1]), []).append((int(parts[0]), parts[2]))
    todo, found = [root_pid], []
    while todo:
        p = todo.pop()
        for pid, name in kids.get(p, []):
            found.append((pid, name))
            todo.append(pid)
    ntdll = ctypes.WinDLL("ntdll")
    k32 = ctypes.WinDLL("kernel32")
    k32.OpenProcess.restype = ctypes.c_void_p
    count = 0
    for pid, name in found:
        if "workerd" not in name.lower():
            continue
        h = k32.OpenProcess(0x0800, False, pid)  # PROCESS_SUSPEND_RESUME
        if h:
            (ntdll.NtSuspendProcess if suspend else ntdll.NtResumeProcess)(ctypes.c_void_p(h))
            count += 1
            print(f"{'suspended' if suspend else 'resumed'} {name} {pid}", flush=True)
    return count


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--out", default=str(ROOT / "build/windows"))
    ap.add_argument("--port", type=int, default=8913)
    ap.add_argument("--server-dir", default=str(ROOT / "server/anchor"))
    ap.add_argument("--scenario", default="anchor_dead_link")
    a = ap.parse_args()

    state = Path(tempfile.mkdtemp(prefix="bodstate_dead_"))
    npx = shutil.which("npx.cmd") or shutil.which("npx") or "npx"
    wr_log = open(state / "wrangler.log", "w")
    wr = subprocess.Popen([npx, "wrangler", "dev", "--port", str(a.port), "--local", "--persist-to", str(state / "p")],
                          cwd=a.server_dir, stdout=wr_log, stderr=subprocess.STDOUT)
    try:
        for _ in range(120):
            time.sleep(1)
            if "Ready on" in (state / "wrangler.log").read_text(errors="ignore"):
                break
        else:
            print("wrangler did not become ready", file=sys.stderr)
            return 2
        env = dict(os.environ)
        env.setdefault("BLUEWAKE_SCENARIO_LOCK", str(Path(os.environ.get("LOCALAPPDATA", ".")) / "bluewake-scenario-quick.lock"))
        cmd = [sys.executable, str(ROOT / "scripts/windows/scenario.py"), a.scenario, "--out", a.out, "--exe", a.exe,
               "--cvar", f"gNetwork.Anchor.Host=127.0.0.1:{a.port}"]
        run_log = Path(a.out) / "scenarios" / a.scenario / "1280x960" / "run.log"
        run_log.unlink(missing_ok=True)  # an older run's lines must not count
        sc = subprocess.Popen(cmd, env=env)
        # The server is dead for each disconnect and alive for the connect between them (a connect needs an answer):
        # suspended after the first connection, resumed at "connect again", suspended after the second connection.
        suspended = False
        stage = 0
        while sc.poll() is None:
            time.sleep(0.1)
            if not run_log.exists():
                continue
            text = run_log.read_text(errors="ignore")
            connected = text.count("[anchor] connected")
            if stage == 0 and connected >= 1:
                suspended = suspend_tree(wr.pid, True) > 0
                stage = 1
            elif stage == 1 and "test: connect again" in text:
                suspend_tree(wr.pid, False)
                stage = 2
            elif stage == 2 and connected >= 2:
                suspend_tree(wr.pid, True)
                stage = 3
        code = sc.returncode
        text = run_log.read_text(errors="ignore") if run_log.exists() else ""
        held = re.findall(r"\[anchor\] test: disconnect (\d+) held the frame ([0-9.]+) ms", text)
        for line in text.splitlines():
            if "[anchor]" in line:
                print(line)
        ok = suspended and len(held) >= 2 and all(float(ms) < 5.0 for _, ms in held)
        print(f"suspended={suspended} held={held} scenario_exit={code} -> {'PASS' if ok else 'FAIL'}")
        return 0 if ok and code == 0 else 1
    finally:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(wr.pid)], capture_output=True)
        shutil.rmtree(state, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
