"""Validate the documented skin package contract and all shipped asset references."""
from pathlib import Path
import os
import re
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
# MSIME_SKINS_DIR points the same checks at another skin collection, e.g. the repository's skins/.
SKINS_DIR = Path(os.environ.get("MSIME_SKINS_DIR", ROOT / "skins")).resolve()


class ManifestTests(unittest.TestCase):
    def test_shipped_skins(self):
        manifests = list(SKINS_DIR.glob("*/skin.toml"))
        self.assertTrue(manifests, "No skin packages found")
        for path in manifests:
            with self.subTest(skin=path.parent.name):
                data = tomllib.loads(path.read_text(encoding="utf-8"))
                self.assertEqual(data["schema_version"], 1)
                self.assertEqual(data["id"], path.parent.name)
                self.assertRegex(data["id"], r"^[a-z0-9._-]{1,64}$")
                # Mirrors the Server catalog: name, version and base are required, author is not.
                for key in ("name", "version", "base"):
                    self.assertTrue(data[key].strip(), key)
                self.assertIn(data["base"], ("fluent", "wechat", "graphite", "willow_green", "autumn_osmanthus"))
                window = data.get("candidate_window", {})
                assets = {key: data.get(key) for key in ("toolbar_stylesheet", "preview")}
                for table in ("decoration", "background"):
                    if table in window:
                        assets[f"candidate_window.{table}.image"] = window[table]["image"]
                if "decoration" in window:
                    for key in ("top_inset_dip", "width_dip"):
                        self.assertGreater(window["decoration"][key], 0, key)
                for key, rel in assets.items():
                    if rel is None:
                        continue
                    asset = (path.parent / rel).resolve()
                    self.assertTrue(asset.is_relative_to(path.parent.resolve()), key)
                    self.assertTrue(asset.is_file(), f"Missing {key}: {asset}")
                    if asset.suffix.lower() == ".png":
                        self.assertLessEqual(asset.stat().st_size, 500 * 1024, key)
                for scope in ("candidate_window", "toolbar"):
                    if "corner_radius_dip" in data.get(scope, {}):
                        self.assertTrue(0 <= data[scope]["corner_radius_dip"] <= 32, scope)
                if "background" in window:
                    self.assertTrue(0 <= window["background"].get("opacity", 1) <= 1)
                for theme in data["supports"]["themes"]:
                    self.assertIn(theme, data["candidate"])
                    if "toolbar" in data:
                        self.assertIn(theme, data["toolbar"])


if __name__ == "__main__":
    unittest.main()
