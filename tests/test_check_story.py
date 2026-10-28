import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location("check_story", pathlib.Path(__file__).parents[1] / "tools/check_story.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CheckStoryTest(unittest.TestCase):
    def test_compatibility_unknown_unavailable_and_stale_fail(self):
        report = {"schema": 1, "kind": "compatibility", "pid": 10, "process_start_ticks": "100",
                  "status": "approved", "runtime_modifications_allowed": True,
                  "il2cpp_sha256": "a" * 64, "verified_il2cpp_sha256": "a" * 64}
        self.assertFalse(module.validate_compatibility(report, 10, "100"))
        self.assertTrue(module.validate_compatibility(report, 11, "100"))
        self.assertTrue(module.validate_compatibility(report, 10, "101"))
        for status in ("unknown_native_build", "fingerprint_unavailable"):
            changed = dict(report, status=status, runtime_modifications_allowed=False)
            self.assertTrue(module.validate_compatibility(changed, 10, "100"))
        self.assertTrue(module.validate_compatibility(dict(report, il2cpp_sha256="b" * 64), 10, "100"))

    def test_main_probe_requires_identity_and_selftest(self):
        report = {"schema": 1, "kind": "main_thread_probe", "request_id": "x", "status": "passed",
                  "delegate_abi_selftest": True, "context_matches": True, "unity_main_id": 1,
                  "managed_callback_id": 1, "native_sender_tid": 100, "native_callback_tid": 200}
        self.assertFalse(module.validate_main_probe(report, "x"))
        self.assertTrue(module.validate_main_probe(report, "old"))
        for field, value in (("delegate_abi_selftest", False), ("context_matches", False),
                             ("managed_callback_id", 2), ("native_callback_tid", 100)):
            changed = dict(report, **{field: value})
            self.assertTrue(module.validate_main_probe(changed, "x"))

    def test_refresh_requires_an_executed_action(self):
        report = {"schema": 1, "kind": "story_refresh", "request_id": "x", "status": "passed",
                  "delegate_abi_selftest": True, "context_matches": True, "unity_main_id": 1,
                  "managed_callback_id": 1, "native_sender_tid": 100, "native_callback_tid": 200,
                  "action_attempted": True, "action_applied": False}
        self.assertTrue(module.validate_main_probe(report, "x", "story_refresh"))
        report["action_applied"] = True
        self.assertFalse(module.validate_main_probe(report, "x", "story_refresh"))
        report["action_attempted"] = False
        self.assertTrue(module.validate_main_probe(report, "x", "story_refresh"))

    def test_stale_report_is_rejected(self):
        self.assertTrue(module.validate_snapshot({"schema": 1, "request_id": "old"}, "new"))

    def test_no_story_is_not_translation_success(self):
        report = {"schema": 1, "request_id": "x", "status": "no_story"}
        self.assertFalse(module.validate_snapshot(report, "x"))
        self.assertTrue(module.validate_snapshot(report, "x", require_story=True))

    def test_data_and_display_are_separate(self):
        report = {"schema": 1, "request_id": "x", "status": "observed", "data_matches_resource": True,
                  "snapshot_unchanged": True, "current_data": {"content": "中文"},
                  "display_fields": {"tmp_serif": "English"}}
        self.assertFalse(module.validate_snapshot(report, "x", require_data=True))
        self.assertTrue(module.validate_snapshot(report, "x", require_body_cache=True))
        report["display_fields"]["tmp_serif"] = "中文"
        self.assertFalse(module.validate_snapshot(report, "x", require_data=True, require_body_cache=True))
        report["snapshot_unchanged"] = False
        self.assertTrue(module.validate_snapshot(report, "x", require_body_cache=True))

    def test_place_cache_and_empty_place_are_not_success(self):
        report = {"schema": 1, "request_id": "x", "status": "observed", "snapshot_unchanged": True,
                  "current_data": {"place": "黑森林"},
                  "controller_texts": {"_uiAnimator": {"tmp_mapText": "A Dark Forest"}}}
        self.assertTrue(module.validate_snapshot(report, "x", require_place_cache=True))
        report["controller_texts"]["_uiAnimator"]["tmp_mapText"] = "黑森林"
        self.assertFalse(module.validate_snapshot(report, "x", require_place_cache=True))
        report["current_data"]["place"] = ""
        self.assertTrue(module.validate_snapshot(report, "x", require_place_cache=True))


if __name__ == "__main__":
    unittest.main()
