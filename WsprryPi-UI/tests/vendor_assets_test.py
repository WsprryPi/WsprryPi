"""Check that vendored stylesheets do not reference missing local font assets."""

import re
import unittest
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1] / "data"


class VendorAssetsTest(unittest.TestCase):
    def test_stylesheet_resources_exist_inside_vendor(self):
        vendor = (ROOT / "vendor").resolve()
        for css in vendor.rglob("*.css"):
            for value in re.findall(r"url\(\s*([^)]*?)\s*\)", css.read_text()):
                value = value.strip("\"'")
                parsed = urlsplit(value)
                if parsed.scheme == "data":
                    continue
                with self.subTest(stylesheet=css.relative_to(ROOT), url=value):
                    self.assertFalse(parsed.scheme or parsed.netloc, "vendor assets must be local")
                    resource = (css.parent / unquote(parsed.path)).resolve()
                    self.assertTrue(resource.is_relative_to(vendor))
                    self.assertTrue(resource.is_file(), f"Missing resource: {resource}")


if __name__ == "__main__":
    unittest.main()
