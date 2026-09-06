"""Validate the documented skin package contract and all shipped asset references."""
from pathlib import Path
import re
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ManifestTests(unittest.TestCase):
    def test_shipped_skins(self):
        manifests = list((ROOT / "skins").glob("*/skin.toml"))
        self.assertTrue(manifests, "No skin packages found")
        for path in manifests:
            with self.subTest(skin=path.parent.name):
                data = tomllib.loads(path.read_text(encoding="utf-8"))
                self.assertEqual(data["schema_version"], 1)
                self.assertEqual(data["id"], path.parent.name)
                self.assertRegex(data["id"], r"^[a-z0-9._-]{1,64}$")
                for key in ("name", "version", "author"):
                    self.assertTrue(data[key].strip(), key)
                for key in ("toolbar_stylesheet", "preview"):
                    if key in data:
                        asset = (path.parent / data[key]).resolve()
                        self.assertTrue(asset.is_relative_to(path.parent.resolve()), key)
                        self.assertTrue(asset.is_file(), f"Missing {key}: {asset}")
                for theme in data["supports"]["themes"]:
                    self.assertIn(theme, data["candidate"])
                self.assertLessEqual((path.parent / data["preview"]).stat().st_size, 500 * 1024)


if __name__ == "__main__":
    unittest.main()
