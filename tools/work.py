#!/usr/bin/env python3
"""Small shared-checkout task tracker. Run `python3 tools/work.py --help`."""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import json
import os
from pathlib import Path
import sys
import tempfile


STATES = {"todo", "active", "review", "done", "blocked"}
DEFAULT_FILE = Path(__file__).resolve().parents[1] / "WORK.json"


def nonempty(value):
    if not isinstance(value, str) or not value.strip():
        raise ValueError("expected a nonempty string")
    return value.strip()


def timestamp():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def validate(data):
    if not isinstance(data, dict) or data.get("version") != 1:
        raise ValueError("work file must have version 1")
    if not isinstance(data.get("updated_at"), str) or not isinstance(data.get("tasks"), list):
        raise ValueError("work file needs updated_at and tasks")
    tasks = {}
    for task in data["tasks"]:
        if not isinstance(task, dict):
            raise ValueError("each task must be an object")
        ident = nonempty(task.get("id"))
        if ident in tasks:
            raise ValueError(f"duplicate task ID: {ident}")
        nonempty(task.get("title"))
        if task.get("status") not in STATES:
            raise ValueError(f"{ident}: invalid status")
        owner = task.get("owner")
        if owner is not None:
            nonempty(owner)
        if task["status"] in {"active", "review", "blocked"} and not owner:
            raise ValueError(f"{ident}: {task['status']} requires an owner")
        if task["status"] == "todo" and owner is not None:
            raise ValueError(f"{ident}: todo must have no owner")
        for field in ("depends_on", "resources", "validation"):
            values = task.get(field)
            if not isinstance(values, list):
                raise ValueError(f"{ident}: {field} must be a list")
            for value in values:
                nonempty(value)
            if len(set(values)) != len(values) and field != "validation":
                raise ValueError(f"{ident}: duplicate {field} entry")
        for field in ("summary", "next"):
            if not isinstance(task.get(field), str):
                raise ValueError(f"{ident}: {field} must be text")
        if not isinstance(task.get("history"), list):
            raise ValueError(f"{ident}: history must be a list")
        tasks[ident] = task
    for ident, task in tasks.items():
        for dep in task["depends_on"]:
            if dep not in tasks:
                raise ValueError(f"{ident}: unknown dependency {dep}")
    visited, visiting = set(), set()

    def visit(ident):
        if ident in visiting:
            raise ValueError(f"dependency cycle at {ident}")
        if ident in visited:
            return
        visiting.add(ident)
        for dep in tasks[ident]["depends_on"]:
            visit(dep)
        visiting.remove(ident)
        visited.add(ident)

    for ident in tasks:
        visit(ident)
    return tasks


@contextmanager
def locked(path):
    # Lock a separate inode: replacing the JSON must not replace the lock itself.
    with path.with_name(".work.lock").open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


def save(path, data):
    fd, name = tempfile.mkstemp(prefix=".work-", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as stream:
            json.dump(data, stream, indent=2, ensure_ascii=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def blockers(task, tasks, owner=None):
    reasons = [f"dependency {dep} ({tasks[dep]['status']})"
               for dep in task["depends_on"] if tasks[dep]["status"] != "done"]
    for other in tasks.values():
        if other["id"] == task["id"] or other["status"] not in {"active", "review", "blocked"}:
            continue
        if owner is not None and other["owner"] == owner:
            continue
        overlap = set(task["resources"]) & set(other["resources"])
        if overlap:
            reasons.append(f"resource {','.join(sorted(overlap))} held by {other['id']} ({other['owner']})")
    return reasons


def print_status(tasks):
    for state in ("active", "review", "blocked", "todo", "done"):
        group = [task for task in tasks.values() if task["status"] == state]
        print(f"{state.upper()} ({len(group)})")
        for task in group:
            reasons = blockers(task, tasks) if state == "todo" else []
            readiness = " [READY]" if state == "todo" and not reasons else ""
            print(f"  {task['id']}: {task['title']} — {task['owner'] or 'unclaimed'}{readiness}")
            if reasons:
                print("    waiting: " + "; ".join(reasons))
            if task["summary"]:
                print("    summary: " + task["summary"])
            if task["next"]:
                print("    next: " + task["next"])
            for evidence in task["validation"]:
                print("    validation: " + evidence)


def mutate(args, data, tasks):
    if args.id not in tasks:
        raise ValueError(f"unknown task: {args.id}")
    task = tasks[args.id]
    def snapshot():
        return {key: list(task[key]) if key == "validation" else task[key]
                for key in ("status", "owner", "summary", "next", "validation")}

    before = snapshot()
    action = args.command
    actor = args.reviewer if action in {"accept", "revise"} else args.owner
    detail = ""
    if action == "claim":
        if task["status"] != "todo":
            raise ValueError(f"{args.id}: must be todo to claim (currently {task['status']}, owner {task['owner']})")
        reasons = blockers(task, tasks, actor)
        if reasons:
            raise ValueError("cannot claim: " + "; ".join(reasons))
        task.update(status="active", owner=actor)
    elif action in {"accept", "revise"}:
        if task["status"] != "review" or actor == task["owner"]:
            raise ValueError(f"{action} requires review status and a reviewer different from the owner")
        if action == "accept":
            task["status"] = "done"
            task["validation"].append(args.validation)
            detail = args.validation
        else:
            task.update(status="active", summary="Review changes requested: " + args.reason,
                        next="Address review: " + args.reason)
            detail = args.reason
    else:
        if task["owner"] != actor:
            raise ValueError(f"{args.id}: only owner {task['owner']} may {action}")
        allowed = {"active", "blocked"} if action in {"note", "release"} else {"active"}
        if task["status"] not in allowed:
            raise ValueError(f"{args.id}: cannot {action} from {task['status']}")
        if action == "note":
            task.update(summary=args.summary, next=args.next)
            task["validation"].extend(args.validation or [])
            detail = {"summary": args.summary, "next": args.next,
                      "validation": args.validation or []}
        elif action == "submit":
            if not task["validation"]:
                raise ValueError("submit requires validation evidence; record it with note --validation")
            task["status"] = "review"
        elif action == "block":
            task.update(status="blocked", summary=args.reason)
            detail = args.reason
        elif action == "release":
            task.update(status="todo", owner=None, validation=[])
            detail = args.reason
    now = timestamp()
    task["history"].append({"at": now, "action": action, "actor": actor, "detail": detail,
                            "from": before, "to": snapshot()})
    data["updated_at"] = now


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--file", type=Path, default=DEFAULT_FILE, help="shared work JSON (default: repo WORK.json)")
    commands = result.add_subparsers(dest="command", required=True)
    commands.add_parser("status", help="show current work, reviews, ready tasks and blockers")
    for command in ("claim", "note", "submit", "accept", "revise", "block", "release"):
        sub = commands.add_parser(command)
        sub.add_argument("id")
        sub.add_argument("--reviewer" if command in {"accept", "revise"} else "--owner", required=True, type=nonempty)
        if command == "note":
            sub.add_argument("--summary", required=True, type=nonempty)
            sub.add_argument("--next", required=True, type=nonempty)
            sub.add_argument("--validation", action="append", type=nonempty)
        elif command == "accept":
            sub.add_argument("--validation", required=True, type=nonempty)
        elif command in {"block", "release", "revise"}:
            sub.add_argument("--reason", required=True, type=nonempty)
    return result


def main():
    args = parser().parse_args()
    path = args.file.resolve()
    try:
        with locked(path):
            data = json.loads(path.read_text())
            tasks = validate(data)
            if args.command == "status":
                print_status(tasks)
                return 0
            mutate(args, data, tasks)
            validate(data)
            save(path, data)
            print(f"{args.id}: {tasks[args.id]['status']}")
        return 0
    except (OSError, ValueError, TypeError, RecursionError) as error:
        print(f"work: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
