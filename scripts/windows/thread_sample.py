#!/usr/bin/env python3
"""Sample the host threads of a running boat-of-daphnes (diagnostic; never part of a player's build).

    python scripts/windows/thread_sample.py --exe-name bod-app.exe --pdb-dir DIR [--wait 20] [--secs 15] [--hz 400] [--top 3]

Waits for a process with that image name, lets it run --wait seconds, reads the CPU time of every thread over
--secs, picks the --top busiest, suspends each of them in turn at --hz total to read Rip, and prints for each
thread its CPU share and its top functions (DbgHelp, the PDB found in --pdb-dir). The module has no symbols
(translated chunks show under the nearest export). It suspends a thread for a few microseconds a sample, so it
slows the sampled threads a little: use it for relative profiles only.
"""
import argparse
import collections
import ctypes
from ctypes import wintypes as w
import subprocess
import time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
dbg = ctypes.WinDLL("dbghelp")
TH32CS_SNAPTHREAD = 4
THREAD_ALL = 0x1FFFFF


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", w.DWORD), ("cntUsage", w.DWORD), ("th32ThreadID", w.DWORD),
                ("th32OwnerProcessID", w.DWORD), ("tpBasePri", w.LONG), ("tpDeltaPri", w.LONG), ("dwFlags", w.DWORD)]


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", w.ULONG), ("TypeIndex", w.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                ("Index", w.ULONG), ("Size", w.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", w.ULONG),
                ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", w.ULONG),
                ("Scope", w.ULONG), ("Tag", w.ULONG), ("NameLen", w.ULONG), ("MaxNameLen", w.ULONG),
                ("Name", ctypes.c_char * 512)]


class FILETIME(ctypes.Structure):
    _fields_ = [("lo", w.DWORD), ("hi", w.DWORD)]


def find_pid(name):
    while True:
        out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {name}", "/NH", "/FO", "CSV"],
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.replace('"', "").split(",")
            if len(parts) > 1 and parts[0].lower() == name.lower():
                return int(parts[1])
        time.sleep(1)


def thread_ids(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    e = THREADENTRY32()
    e.dwSize = ctypes.sizeof(e)
    ids = []
    ok = k32.Thread32First(snap, ctypes.byref(e))
    while ok:
        if e.th32OwnerProcessID == pid:
            ids.append(e.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(e))
    k32.CloseHandle(snap)
    return ids


def cpu_ticks(h):
    c, e, k, u = FILETIME(), FILETIME(), FILETIME(), FILETIME()
    k32.GetThreadTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u))
    return ((k.hi << 32) | k.lo) + ((u.hi << 32) | u.lo)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--exe-name", required=True)
    p.add_argument("--pdb-dir", required=True)
    p.add_argument("--wait", type=float, default=20)
    p.add_argument("--secs", type=float, default=15)
    p.add_argument("--hz", type=int, default=400)
    p.add_argument("--top", type=int, default=3)
    a = p.parse_args()
    k32.OpenThread.restype = w.HANDLE
    k32.SuspendThread.argtypes = [w.HANDLE]
    k32.ResumeThread.argtypes = [w.HANDLE]
    k32.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
    k32.GetThreadContext.argtypes = [w.HANDLE, ctypes.c_void_p]
    k32.GetThreadTimes.argtypes = [w.HANDLE] + [ctypes.c_void_p] * 4
    dbg.SymInitialize.argtypes = [w.HANDLE, ctypes.c_char_p, w.BOOL]
    dbg.SymFromAddr.argtypes = [w.HANDLE, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_void_p]
    k32.OpenProcess.restype = w.HANDLE
    pid = find_pid(a.exe_name)
    print("pid", pid, flush=True)
    time.sleep(a.wait)
    proc = k32.OpenProcess(0x1F0FFF, False, pid)
    handles = {}
    for tid in thread_ids(pid):
        h = k32.OpenThread(THREAD_ALL, False, tid)
        if h:
            handles[tid] = h
    t0 = {tid: cpu_ticks(h) for tid, h in handles.items()}
    time.sleep(a.secs / 3)
    t1 = {tid: cpu_ticks(h) for tid, h in handles.items()}
    busiest = sorted(handles, key=lambda t: t1[t] - t0[t], reverse=True)[:a.top]
    span = a.secs / 3
    for t in sorted(handles, key=lambda t: t1[t] - t0[t], reverse=True)[:8]:
        print(f"thread {t}: {100.0 * (t1[t] - t0[t]) / 1e7 / span:.0f}% of a core", flush=True)
    samples = {t: collections.Counter() for t in busiest}
    stacks = {t: collections.defaultdict(list) for t in busiest}
    raw = ctypes.create_string_buffer(1232 + 16)  # CONTEXT is 1232 bytes, 16-byte aligned
    base = (ctypes.addressof(raw) + 15) & ~15
    ctypes.c_uint32.from_address(base + 0x30).value = 0x100001  # ContextFlags: CONTEXT_CONTROL
    interval = 1.0 / a.hz
    end = time.monotonic() + a.secs * 2 / 3
    n = 0
    while time.monotonic() < end:
        t = busiest[n % len(busiest)]
        n += 1
        h = handles[t]
        if k32.SuspendThread(h) != 0xFFFFFFFF:
            if k32.GetThreadContext(h, ctypes.c_void_p(base)):
                rip = ctypes.c_uint64.from_address(base + 0xF8).value
                samples[t][rip] += 1
                if len(stacks[t][rip]) < 40:  # a bounded copy of the stack top, for 'who called memmove'
                    rsp = ctypes.c_uint64.from_address(base + 0x98).value
                    buf = ctypes.create_string_buffer(512)
                    got = ctypes.c_size_t()
                    if k32.ReadProcessMemory(proc, ctypes.c_void_p(rsp), buf, 512, ctypes.byref(got)):
                        stacks[t][rip].append(bytes(buf.raw[:got.value]))
            k32.ResumeThread(h)
        time.sleep(interval)
    dbg.SymSetOptions(0x2 | 0x4 | 0x10000000)  # UNDNAME | DEFERRED_LOADS | ... keep simple
    dbg.SymInitialize(proc, a.pdb_dir.encode(), True)
    cache = {}

    def name(addr):
        if addr not in cache:
            s = SYMBOL_INFO()
            s.SizeOfStruct = 88
            s.MaxNameLen = 512
            disp = ctypes.c_uint64()
            if dbg.SymFromAddr(proc, ctypes.c_uint64(addr), ctypes.byref(disp), ctypes.byref(s)):
                cache[addr] = s.Name.decode(errors="replace")
            else:
                cache[addr] = "?"
        return cache[addr]

    for t in busiest:
        total = sum(samples[t].values())
        byname = collections.Counter()
        for addr, c in samples[t].items():
            byname[name(addr)] += c
        print(f"== thread {t}: {total} samples", flush=True)
        for fn, c in byname.most_common(25):
            print(f"  {100.0 * c / max(1, total):5.1f}%  {fn}")
        # callers of the leaf libc routines: the first return address on the stack top that names another function
        leaf = ("memmove", "memcmp", "memset", "memcpy", "strcmp", "ppc_guest_alias_resolve")
        callers = collections.defaultdict(collections.Counter)
        for addr, blobs in stacks[t].items():
            fn = name(addr)
            if not fn.startswith(leaf):
                continue
            for blob in blobs:
                for i in range(0, len(blob) - 7, 8):
                    q = int.from_bytes(blob[i:i + 8], "little")
                    if q < 0x10000 or q >> 47:
                        continue
                    cand = name(q)
                    if cand != "?" and not cand.startswith(leaf) and not cand.startswith(("Zw", "Rtl", "Nt")):
                        callers[fn][cand] += 1
                        break
        for fn, c in callers.items():
            print(f"  callers of {fn}: " + "; ".join(f"{k} {v}" for k, v in c.most_common(6)))


if __name__ == "__main__":
    main()
