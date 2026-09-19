"""Temporary negative control for issue 36; removed after the measured run."""
import unittest


class CiFailureControlTests(unittest.TestCase):
    def test_intentional_failure_reaches_required_aggregate(self):
        self.fail("CI failure propagation control for issue 36")
