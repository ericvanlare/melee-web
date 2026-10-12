"""Focused production call-site guard for source-observed Results input."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FullRouteResultsInputTests(unittest.TestCase):
    def test_both_results_keys_use_bounded_source_pad_observation_and_keep_failure_evidence(self):
        source = (ROOT / "scripts" / "a3_local_room_full_route.mjs").read_text()
        start = source.index("const resultsRetention = {};")
        end = source.index("cssRows = await waitBoth('original CSS returned from Results'", start)
        route = source[start:end]

        self.assertIn("const p1ResultsPress = async (key, options = {}) => resultsPress('alpha', key, options);", route)
        self.assertIn("const p2ResultsPress = async (key, options = {}) => resultsPress('beta', key, options);", route)
        self.assertIn("pressSecond: () => p2ResultsPress('End')", route)
        self.assertIn("expected: {button: 0x1000}", route)
        self.assertIn("releaseMs: options.releaseMs ?? 150", route)
        self.assertIn("observePress: true", route)
        self.assertIn("evidenceSink", route)
        self.assertIn("latest_actual_results_trace", route)
        self.assertIn("failure_snapshot", route)
        self.assertIn("resultsAttempt.status = 'failed'", route)

        # The callback still observes the existing source suffix and 500 ms cap.
        self.assertIn("Math.min(500, Math.floor(remaining() - releaseMs - 1))", source)
        self.assertIn("observeDesiredSourcePad", source)

if __name__ == "__main__":
    unittest.main()
