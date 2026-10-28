import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location("check_story", pathlib.Path(__file__).parents[1] / "tools/check_story.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CheckStoryTest(unittest.TestCase):
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
