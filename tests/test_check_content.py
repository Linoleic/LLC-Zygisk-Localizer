import copy
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from check_content import validate, select_pid


class ContentReadbackTests(unittest.TestCase):
    def setUp(self):
        self.report = dict(schema=1, kind="content_readback", request_id="abc", table="_keywordList", pid=7,
                           process_start_ticks="42", native_callback_tid=9, status="passed", read_only=True,
                           traversal_complete=True, snapshot_unchanged=True, compared_fields=10, equal_fields=10,
                           mismatched_fields=0, invalid_runtime_rows=0, structure_mismatches=0,
                           missing_local_rows=0, unsupported_source_fields=0, unidentified_runtime_rows=0)
        self.bridge = dict(schema=1, kind="content_readback_bridge", request_id="abc", pid=7, status="passed",
                           native_callback_tid=9, native_sender_tid=8, managed_callback_id=1, unity_main_id=1,
                           context_matches=True, delegate_abi_selftest=True, action_attempted=True, action_applied=True)

    def check(self, report=None, bridge=None, strict=False):
        return validate(report or self.report, bridge or self.bridge, "abc", "_keywordList", 7, "42", strict)

    def test_correlated_readback(self):
        self.assertEqual(self.check(), [])

    def test_forked_process_uses_correlated_core_pid(self):
        self.assertEqual(select_pid([7, 8], {"pid": 7}), 7)
        for report in ({"pid": 9}, {"pid": True}, {}):
            with self.assertRaises(ValueError):
                select_pid([7, 8], report)

    def test_stale_report_identity_and_table(self):
        for name, value in dict(request_id="old", pid=8, process_start_ticks="41", table="_bufList").items():
            report = copy.deepcopy(self.report); report[name] = value
            self.assertTrue(self.check(report=report), name)

    def test_callback_and_thread_guards(self):
        for name, value in dict(pid=8, native_callback_tid=8, managed_callback_id=2,
                                delegate_abi_selftest=False, action_applied=False).items():
            bridge = copy.deepcopy(self.bridge); bridge[name] = value
            self.assertTrue(self.check(bridge=bridge), name)

    def test_mismatch_mutation_and_incomplete_traversal(self):
        for name, value in dict(mismatched_fields=1, invalid_runtime_rows=1, structure_mismatches=1,
                                snapshot_unchanged=False, traversal_complete=False, compared_fields=0,
                                equal_fields=9, read_only=False).items():
            report = copy.deepcopy(self.report); report[name] = value
            self.assertTrue(self.check(report=report), name)

    def test_limited_coverage_is_explicit_and_strict_mode_refuses_it(self):
        for name in ("missing_local_rows", "unsupported_source_fields", "unidentified_runtime_rows"):
            report = copy.deepcopy(self.report); report[name] = 1
            self.assertEqual(self.check(report=report), [])
            self.assertTrue(self.check(report=report, strict=True))

    def test_bad_counts_are_not_success(self):
        for value in (None, -1, True, "0"):
            report = copy.deepcopy(self.report); report["mismatched_fields"] = value
            self.assertTrue(self.check(report=report))

    def test_empty_source_is_not_missing_in_strict_mode(self):
        report = copy.deepcopy(self.report); report["empty_source_rows"] = 2
        self.assertEqual(self.check(report=report, strict=True), [])

    def test_mapping_and_uncertain_sources_fail_strict_mode(self):
        for name in ("source_map_mismatches", "source_uncertain_rows"):
            report = copy.deepcopy(self.report); report[name] = 1
            self.assertEqual(self.check(report=report), [])
            self.assertTrue(self.check(report=report, strict=True))

    def test_source_counts_are_validated(self):
        for name in ("empty_source_rows", "source_map_mismatches", "source_uncertain_rows"):
            for value in (-1, True, None, "0"):
                report = copy.deepcopy(self.report); report[name] = value
                self.assertTrue(self.check(report=report), (name, value))


if __name__ == "__main__":
    unittest.main()
