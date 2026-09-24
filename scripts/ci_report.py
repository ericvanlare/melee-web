#!/usr/bin/env python3
"""Report GitHub verification turnaround, queue time and job execution separately."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess

from ci_verify import GROUPS, UNIT_GROUPS


def timestamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def summarize(run, jobs, *, now=None):
    if not jobs:
        raise ValueError("No job timing evidence")
    created = timestamp(run["created_at"])
    rows = []
    # Account for dependency waits separately from eligible-runner queue time.
    # Older runs have no seed jobs and need no inferred seed dependency.
    seeds = [job for job in jobs if job["name"].startswith("compiler seed (")]
    consumers = {f"verify ({group})" for group in GROUPS if group not in UNIT_GROUPS}
    for job in jobs:
        start = timestamp(job["started_at"]) if job.get("started_at") else None
        end = timestamp(job["completed_at"]) if job.get("completed_at") else None
        ready = created
        dependencies = []
        if job["name"] == "browser-build":
            dependencies = [item for item in jobs if item["name"] != "browser-build"]
        elif job["name"] in consumers:
            dependencies = seeds
        if dependencies:
            completed = [timestamp(item["completed_at"]) for item in dependencies if item.get("completed_at")]
            if len(completed) == len(dependencies):
                ready = max(completed)
            else:
                ready = None
        rows.append({
            "name": job["name"], "status": job["status"], "conclusion": job.get("conclusion"),
            "dependency_wait_seconds": (ready - created).total_seconds() if ready else None,
            "queue_seconds": max(0, (start - ready).total_seconds()) if start and ready else None,
            "execution_seconds": (end - start).total_seconds() if start and end else None,
        })
    complete = run["status"] == "completed" and all(job.get("completed_at") for job in jobs)
    end = max(timestamp(job["completed_at"]) for job in jobs) if complete else now
    elapsed = (end - created).total_seconds() if end else None
    return {
        "schema": 1, "run_id": run["id"], "url": run["html_url"],
        "head_sha": run["head_sha"], "event": run["event"],
        "created_at": run["created_at"], "completed": complete,
        "conclusion": run.get("conclusion"), "turnaround_seconds": elapsed,
        "within_ten_minutes": (elapsed <= 600) if complete else None,
        "accepted": complete and run.get("conclusion") == "success" and elapsed <= 600
                    and all(row["conclusion"] == "success" or
                            (row["name"] == "compiler-cache-audit" and row["conclusion"] == "skipped")
                            for row in rows),
        "job_execution_seconds": sum(row["execution_seconds"] or 0 for row in rows),
        "jobs": rows,
    }


def github(path):
    return json.loads(subprocess.check_output(["gh", "api", path], text=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default="ericvanlare/melee-web")
    parser.add_argument("--run", type=int, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--snapshot", action="store_true", help="Allow an explicitly incomplete aggregate-job snapshot")
    parser.add_argument("--summary", type=Path, help="Append a human-readable GitHub job summary")
    args = parser.parse_args()
    run = github(f"repos/{args.repo}/actions/runs/{args.run}")
    jobs = github(f"repos/{args.repo}/actions/runs/{args.run}/jobs?per_page=100")
    if jobs["total_count"] != len(jobs["jobs"]):
        raise SystemExit("Incomplete job inventory; no turnaround claim")
    report = summarize(run, jobs["jobs"], now=datetime.now(timezone.utc) if args.snapshot else None)
    value = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(value)
    print(value, end="")
    if args.summary:
        label = "Final turnaround" if report["completed"] else "Elapsed at aggregate (workflow still running)"
        lines = [f"### Verification timing\n\n{label}: **{report['turnaround_seconds']:.1f} seconds** (target: 600).\n",
                 f"Commit: `{report['head_sha']}`. [Run]({report['url']}).\n",
                 "| Job | Dependency seconds | Queue seconds | Execution seconds | Result |", "| --- | ---: | ---: | ---: | --- |"]
        for row in report["jobs"]:
            dependency = "—" if row["dependency_wait_seconds"] is None else f"{row['dependency_wait_seconds']:.1f}"
            queue = "—" if row["queue_seconds"] is None else f"{row['queue_seconds']:.1f}"
            duration = "—" if row["execution_seconds"] is None else f"{row['execution_seconds']:.1f}"
            lines.append(f"| {row['name']} | {dependency} | {queue} | {duration} | {row['conclusion'] or row['status']} |")
        with args.summary.open("a") as output:
            output.write("\n".join(lines) + "\n")
    if not report["completed"] and not args.snapshot:
        raise SystemExit("Workflow is incomplete; no final timing claim")


if __name__ == "__main__":
    main()
