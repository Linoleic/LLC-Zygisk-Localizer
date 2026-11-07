"""Tests for module customization script (module/customize.sh)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CUSTOMIZE_SH = ROOT / "module" / "customize.sh"


class CustomizeTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory(prefix="llc-customize-test-")
        self.root = Path(self.temp_dir.name)
        self.modpath = self.root / "data/adb/modules_update/llc_zygisk"
        self.modpath.mkdir(parents=True, exist_ok=True)
        (self.modpath / "module.prop").write_text("id=llc_zygisk\nversion=v0.6.1\nversionCode=601\n")
        (self.modpath / "libllc_core.so").write_text("dummy-core")
        (self.modpath / ".boot_id").write_text("should-be-cleaned-on-fresh-install")

        # Fake proc boot_id
        proc_dir = self.root / "proc/sys/kernel/random"
        proc_dir.mkdir(parents=True, exist_ok=True)
        (proc_dir / "boot_id").write_text("system-boot-id-12345\n")

    def tearDown(self):
        self.temp_dir.cleanup()

    def run_customize(self, modpath=None):
        if modpath is None:
            modpath = self.modpath

        env = os.environ.copy()
        env["LLC_TEST_ROOT"] = str(self.root)
        env["MODPATH"] = str(modpath)

        if os.name == "nt":
            wsl_root = f"/mnt/{self.root.drive.rstrip(':').lower()}{self.root.as_posix()[2:]}"
            wsl_modpath = f"/mnt/{modpath.drive.rstrip(':').lower()}{modpath.as_posix()[2:]}"
            wsl_script = f"/mnt/{CUSTOMIZE_SH.drive.rstrip(':').lower()}{CUSTOMIZE_SH.as_posix()[2:]}"
            cmd = ["wsl", "bash", "-c", f"LLC_TEST_ROOT='{wsl_root}' MODPATH='{wsl_modpath}' bash '{wsl_script}'"]
        else:
            cmd = ["sh", str(CUSTOMIZE_SH)]

        res = subprocess.run(cmd, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertEqual(res.returncode, 0, f"customize.sh failed: {res.stderr}")
        return res.stdout

    def test_first_time_install_detection(self):
        # When /data/adb/modules/llc_zygisk does not exist:
        output = self.run_customize()

        self.assertIn("未检测到已安装模块，准备首次安装", output)
        self.assertIn("首次安装完成！", output)
        self.assertIn("请重启手机以使模块注入生效", output)
        self.assertIn("WebUI", output)
        # .boot_id MUST NOT exist in $MODPATH
        self.assertFalse((self.modpath / ".boot_id").exists())

    def test_update_install_detection(self):
        # Create existing installed module in /data/adb/modules/llc_zygisk
        prev_moddir = self.root / "data/adb/modules/llc_zygisk"
        prev_moddir.mkdir(parents=True, exist_ok=True)
        (prev_moddir / "module.prop").write_text("id=llc_zygisk\nversion=v0.6.0\nversionCode=600\n")
        (prev_moddir / ".boot_id").write_text("system-boot-id-12345\n")

        output = self.run_customize()

        self.assertIn("检测到已安装版本: v0.6.0", output)
        self.assertIn("模块更新完成！", output)
        self.assertIn("无需重启手机", output)
        self.assertIn("WebUI", output)
        self.assertNotIn("请重启手机以使模块注入生效", output)

        # .boot_id MUST exist and be preserved
        self.assertTrue((self.modpath / ".boot_id").exists())
        self.assertEqual((self.modpath / ".boot_id").read_text().strip(), "system-boot-id-12345")


if __name__ == "__main__":
    unittest.main()
