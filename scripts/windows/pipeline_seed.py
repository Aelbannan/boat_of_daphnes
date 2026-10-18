#!/usr/bin/env python3
"""Make the app's pipeline cache seed (windows/resources/initial_pipeline_cache.db) from scenario runs.

    python scripts/windows/pipeline_seed.py [--out windows/resources/initial_pipeline_cache.db] [--keep-old]

Every scenario run keeps Aurora's pipeline cache in its data folder (data/cache/pipeline_cache.db: the
seed's pipelines plus every one the run needed). The union of those rows at the newest config version is a
seed that compiles, at start, what the runs' places, menus and states drew, so a first visit draws from its
first frame. Run it after a broad set of runs (the hudq_* audit, the fpsq_* checks) on the current build.
The rows are pipeline state keys (shader and blend configurations), not game data. --keep-old also keeps
the current seed's rows (useful only if they still have the newest version).
"""
from __future__ import annotations

import argparse
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = """
CREATE TABLE aurora_schema(value INTEGER);
CREATE TABLE pipeline_cache (
  type INTEGER NOT NULL,
  hash INTEGER NOT NULL,
  config_version INTEGER NOT NULL,
  config_size INTEGER NOT NULL,
  config BLOB NOT NULL,
  first_frame_used INTEGER NOT NULL,
  PRIMARY KEY (type, hash)
);
"""


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", type=Path, default=ROOT / "windows/resources/initial_pipeline_cache.db")
    p.add_argument("--runs", type=Path, default=ROOT / "build/windows/scenarios")
    p.add_argument("--keep-old", action="store_true")
    args = p.parse_args()
    sources = sorted(args.runs.glob("**/data/cache/pipeline_cache.db"))
    if args.keep_old and args.out.exists():
        sources.append(args.out)
    rows: dict[tuple[int, int], tuple] = {}
    newest = 0
    for src in sources:
        try:
            con = sqlite3.connect(f"file:{src.as_posix()}?mode=ro", uri=True)
            for row in con.execute("select type, hash, config_version, config_size, config, first_frame_used "
                                   "from pipeline_cache"):
                newest = max(newest, row[2])
                key = (row[0], row[1])
                old = rows.get(key)
                if old is None or row[2] > old[2]:
                    rows[key] = row
            con.close()
        except sqlite3.Error as e:
            print(f"skipped {src}: {e}", file=sys.stderr)
    kept = [r for r in rows.values() if r[2] == newest]
    tmp = args.out.with_suffix(".tmp")
    tmp.unlink(missing_ok=True)
    con = sqlite3.connect(tmp)
    con.executescript(SCHEMA)
    con.execute("insert into aurora_schema values (1)")
    # first_frame_used 0: compiled at start, not lazily.
    con.executemany("insert into pipeline_cache values (?, ?, ?, ?, ?, 0)", [r[:5] for r in sorted(kept)])
    con.commit()
    con.close()
    tmp.replace(args.out)
    print(f"{len(kept)} pipelines (config version {newest}) from {len(sources)} caches -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
