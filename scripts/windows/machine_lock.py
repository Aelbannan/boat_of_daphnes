"""A lock for the whole machine, shared by every checkout and worktree.

    with machine_lock("build"):   # one compile at a time
        ...

Builds and game runs from several worktrees at once made the PC unusable and
their frame-rate numbers worthless; each kind of work takes its turn instead.
The lock is a file made with O_EXCL in %LOCALAPPDATA% holding the owner's pid;
a file left by a process that died is taken over. BLUEWAKE_LOCK_DIR names
another folder (a test of the lock itself).
"""
from __future__ import annotations

import os
import subprocess
import time
from contextlib import contextmanager
from pathlib import Path


def lock_path(name: str) -> Path:
    folder = os.environ.get("BLUEWAKE_LOCK_DIR") or os.environ.get("LOCALAPPDATA") or str(Path.home())
    return Path(folder) / f"bluewake-{name}.lock"


def _alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if os.name != "nt":
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True  # another user's process
        return True
    out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True).stdout
    return str(pid) in out


@contextmanager
def machine_lock(name: str):
    path = lock_path(name)
    path.parent.mkdir(parents=True, exist_ok=True)
    waited = False
    while True:
        try:
            fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            break
        except FileExistsError:
            pass
        try:
            pid = int(path.read_text().strip())
        except (ValueError, OSError):
            pid = 0
        if pid == os.getpid():
            break  # taken already by this process (nested use)
        if not _alive(pid):
            path.unlink(missing_ok=True)  # its owner died holding it
            continue
        if not waited:
            print(f"waiting for the {name} lock (pid {pid}, {path})", flush=True)
            waited = True
        time.sleep(3)
    try:
        yield
    finally:
        try:
            if path.read_text().strip() == str(os.getpid()):
                path.unlink(missing_ok=True)
        except OSError:
            pass
