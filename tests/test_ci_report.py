"""GitHub turnaround includes queueing and fails to claim incomplete evidence."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from ci_report import summarize, timestamp


class CiReportTests(unittest.TestCase):
    def run_record(self, status="completed"):
        return {"id": 1, "html_url": "https://example.invalid/run/1", "head_sha": "a" * 40,
                "event": "pull_request", "created_at": "2026-09-19T00:00:00Z",
                "status": status, "conclusion": "success" if status == "completed" else None}

    def job(self, name, start, end):
        return {"name": name, "started_at": f"2026-09-19T00:{start}Z",
                "completed_at": f"2026-09-19T00:{end}Z" if end else None,
                "status": "completed" if end else "in_progress", "conclusion": "success" if end else None}

    def test_parallel_work_is_not_summed_into_turnaround(self):
        report = summarize(self.run_record(), [self.job("verify (unit)", "00:10", "08:00"),
                                              self.job("verify (runtime)", "00:20", "09:00"),
                                              self.job("browser-build", "09:10", "09:20")])
        self.assertEqual(report["turnaround_seconds"], 560)
        self.assertEqual([row["queue_seconds"] for row in report["jobs"]], [10, 20, 10])
        self.assertEqual(report["job_execution_seconds"], 1000)
        self.assertTrue(report["within_ten_minutes"])

    def test_queue_delay_can_fail_the_ten_minute_target(self):
        report = summarize(self.run_record(), [self.job("browser-build", "02:00", "10:01")])
        self.assertEqual(report["jobs"][0]["execution_seconds"], 481)
        self.assertEqual(report["turnaround_seconds"], 601)
        self.assertFalse(report["within_ten_minutes"])

    def test_seed_dependency_time_is_not_reported_as_runner_queue(self):
        report = summarize(self.run_record(), [
            self.job("compiler seed (0)", "00:05", "02:00"),
            self.job("compiler seed (1)", "00:06", "02:15"),
            self.job("verify (runtime)", "02:25", "08:00"),
            self.job("verify (unit-0)", "00:10", "04:00"),
            self.job("browser-build", "08:05", "08:15"),
        ])
        self.assertEqual(report["turnaround_seconds"], 495)
        self.assertEqual([row["queue_seconds"] for row in report["jobs"]], [5, 6, 10, 10, 5])
        self.assertEqual([row["dependency_wait_seconds"] for row in report["jobs"]], [0, 0, 135, 0, 480])

    def test_running_snapshot_does_not_claim_final_acceptance(self):
        report = summarize(self.run_record("in_progress"), [self.job("browser-build", "00:10", None)],
                           now=timestamp("2026-09-19T00:03:00Z"))
        self.assertEqual(report["turnaround_seconds"], 180)
        self.assertFalse(report["completed"])
        self.assertIsNone(report["within_ten_minutes"])
        self.assertIsNone(report["jobs"][0]["execution_seconds"])

    def test_empty_evidence_rejected(self):
        with self.assertRaisesRegex(ValueError, "No job timing"):
            summarize(self.run_record(), [])

    def test_fast_failed_run_is_never_accepted(self):
        run = self.run_record()
        run["conclusion"] = "failure"
        job = self.job("browser-build", "00:10", "00:20")
        job["conclusion"] = "failure"
        report = summarize(run, [job])
        self.assertTrue(report["within_ten_minutes"])
        self.assertFalse(report["accepted"])


if __name__ == "__main__":
    unittest.main()
