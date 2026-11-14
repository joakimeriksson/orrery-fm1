#!/usr/bin/env python3
"""Behavioral checks for the shared-checkout tracker, using only temporary state."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / "tools" / "work.py"


def task(ident, deps=(), resources=()):
    return dict(id=ident, title=ident, status="todo", owner=None, depends_on=list(deps),
                resources=list(resources), summary="", next="", validation=[], history=[])


class TrackerCheck(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "WORK.json"
        self.write([task("API", resources=["api"]), task("SEQ", ["API"], ["seq"]),
                    task("API2", resources=["api"])])

    def write(self, tasks):
        self.path.write_text(json.dumps(dict(version=1, updated_at="initial", tasks=tasks)))

    def read(self):
        return json.loads(self.path.read_text())

    def run_cli(self, *args, ok=True):
        result = subprocess.run([sys.executable, str(TOOL), "--file", str(self.path), *args],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0 if ok else 1, result.stdout + result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        return result

    def test_dependency_review_and_persistence(self):
        result = self.run_cli("status")
        self.assertIn("[READY]", result.stdout)
        self.assertIn("dependency API (todo)", result.stdout)
        self.run_cli("claim", "SEQ", "--owner", "B", ok=False)
        self.run_cli("claim", "API", "--owner", "A")
        self.run_cli("submit", "API", "--owner", "A", ok=False)
        self.run_cli("note", "API", "--owner", "A", "--summary", "API implemented",
                     "--next", "Review contracts", "--validation", "native tests pass")
        self.run_cli("submit", "API", "--owner", "A")
        self.run_cli("accept", "API", "--reviewer", "A", "--validation", "self", ok=False)
        self.run_cli("claim", "SEQ", "--owner", "B", ok=False)
        self.run_cli("accept", "API", "--reviewer", "B", "--validation", "checked contracts")
        self.run_cli("claim", "SEQ", "--owner", "B")
        state = self.read()
        self.assertEqual(state["tasks"][0]["status"], "done")
        self.assertEqual(state["tasks"][1]["owner"], "B")
        self.assertEqual([event["action"] for event in state["tasks"][0]["history"]],
                         ["claim", "note", "submit", "accept"])
        self.assertIn("checked contracts", state["tasks"][0]["validation"])
        note = state["tasks"][0]["history"][1]
        self.assertEqual(note["detail"], dict(summary="API implemented", next="Review contracts",
                                              validation=["native tests pass"]))
        self.assertEqual(note["from"]["summary"], "")
        self.assertEqual(note["to"]["next"], "Review contracts")
        self.assertNotEqual(state["updated_at"], "initial")

    def test_resources_ownership_block_and_release(self):
        self.run_cli("claim", "API", "--owner", "A")
        self.run_cli("claim", "API2", "--owner", "B", ok=False)
        self.run_cli("note", "API", "--owner", "B", "--summary", "wrong",
                     "--next", "wrong", ok=False)
        self.run_cli("release", "API", "--owner", "B", "--reason", "steal", ok=False)
        self.run_cli("block", "API", "--owner", "A", "--reason", "need host fix")
        self.assertIn("need host fix", self.run_cli("status").stdout)
        self.run_cli("claim", "API2", "--owner", "B", ok=False)
        self.run_cli("release", "API", "--owner", "A", "--reason", "handoff")
        self.run_cli("claim", "API", "--owner", "B")

    def test_bad_schema_is_clean_and_unchanged(self):
        variants = [[task("X", ["missing"])], [task("X", ["Y"]), task("Y", ["X"])],
                    [task("X"), task("X")], [dict(task("X"), status="invalid")]]
        for tasks in variants:
            with self.subTest(tasks=tasks):
                self.write(tasks)
                before = self.path.read_bytes()
                self.run_cli("status", ok=False)
                self.assertEqual(self.path.read_bytes(), before)

    def test_review_keeps_resource_reserved(self):
        self.run_cli("claim", "API", "--owner", "A")
        self.run_cli("note", "API", "--owner", "A", "--summary", "ready",
                     "--next", "review", "--validation", "test passed")
        self.run_cli("submit", "API", "--owner", "A")
        self.run_cli("claim", "API2", "--owner", "B", ok=False)
        self.run_cli("accept", "API", "--reviewer", "B", "--validation", "reviewed")
        self.run_cli("claim", "API2", "--owner", "B")

    def test_revision_retains_owner_evidence_and_returns_to_active(self):
        self.run_cli("claim", "API", "--owner", "A")
        self.run_cli("note", "API", "--owner", "A", "--summary", "ready",
                     "--next", "review", "--validation", "first tests")
        self.run_cli("submit", "API", "--owner", "A")
        self.run_cli("revise", "API", "--reviewer", "A", "--reason", "self", ok=False)
        self.run_cli("revise", "API", "--reviewer", "B", "--reason", "fix gate release")
        api = self.read()["tasks"][0]
        self.assertEqual(api["status"], "active")
        self.assertEqual(api["owner"], "A")
        self.assertEqual(api["validation"], ["first tests"])
        self.assertIn("fix gate release", api["next"])
        self.assertEqual(api["history"][-1]["action"], "revise")
        self.run_cli("note", "API", "--owner", "A", "--summary", "fixed release",
                     "--next", "review again", "--validation", "new tests")
        self.run_cli("submit", "API", "--owner", "A")
        self.run_cli("accept", "API", "--reviewer", "B", "--validation", "reviewed fix")

    def test_release_archives_evidence_and_requires_new_validation(self):
        self.run_cli("claim", "API", "--owner", "A")
        self.run_cli("note", "API", "--owner", "A", "--summary", "partial implementation",
                     "--next", "fix edge case", "--validation", "test command: one failure")
        self.run_cli("release", "API", "--owner", "A", "--reason", "handoff")
        api = self.read()["tasks"][0]
        self.assertEqual(api["validation"], [])
        release = api["history"][-1]
        self.assertEqual(release["from"]["validation"], ["test command: one failure"])
        self.assertEqual(release["from"]["next"], "fix edge case")
        self.assertEqual(release["to"]["owner"], None)
        self.run_cli("claim", "API", "--owner", "B")
        self.run_cli("submit", "API", "--owner", "B", ok=False)

    def test_competing_claims_are_serialized(self):
        commands = [[sys.executable, str(TOOL), "--file", str(self.path), "claim", "API", "--owner", owner]
                    for owner in ("A", "B")]
        processes = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                     for command in commands]
        outputs = [process.communicate(timeout=10) for process in processes]
        self.assertEqual(sorted(process.returncode for process in processes), [0, 1], outputs)
        api = self.read()["tasks"][0]
        self.assertIn(api["owner"], {"A", "B"})
        self.assertEqual(api["status"], "active")
        self.assertEqual(len(api["history"]), 1)

    def test_competing_resource_claims_are_serialized(self):
        commands = [[sys.executable, str(TOOL), "--file", str(self.path), "claim", ident, "--owner", owner]
                    for ident, owner in (("API", "A"), ("API2", "B"))]
        processes = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                     for command in commands]
        outputs = [process.communicate(timeout=10) for process in processes]
        self.assertEqual(sorted(process.returncode for process in processes), [0, 1], outputs)
        self.assertEqual(sum(task["status"] == "active" for task in self.read()["tasks"]), 1)


if __name__ == "__main__":
    unittest.main()
