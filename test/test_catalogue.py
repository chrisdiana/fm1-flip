import datetime as dt
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "sync_catalogue.py"
SPEC = importlib.util.spec_from_file_location("sync_catalogue", MODULE_PATH)
catalogue = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(catalogue)
FIXTURE = json.loads((ROOT / "test" / "fixtures" / "catalogue.json").read_text())


class FakeResponse:
    def __init__(self, body=b"", status=200, url=catalogue.CATALOGUE_URL, headers=None):
        self.body = body
        self.status = status
        self.url = url
        self.headers = headers or {}

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self, limit=-1):
        return self.body if limit < 0 else self.body[:limit]

    def geturl(self):
        return self.url


class CatalogueTests(unittest.TestCase):
    def test_normalizes_installable_and_external_only_records(self):
        entries = catalogue._validate_catalogue(FIXTURE)
        self.assertEqual(len(entries), 2)
        installable, external = entries
        self.assertEqual(installable["sha256"], "a" * 64)
        self.assertEqual(installable["packageUrl"], None)
        self.assertEqual(installable["releaseUrl"], "https://github.com/example-maker/sample-fw/releases/tag/v1.2.3")
        self.assertEqual(installable["target"], "FM-1")
        self.assertEqual(installable["projectUrl"], "https://example-maker.github.io/sample-fw/")
        self.assertEqual(installable["artworkUrl"],
                         "https://thefirmwarehub.com/fw/cards/sample-fw-a1b2c3.w640.webp")
        self.assertEqual(installable["artworkKind"], "hub-card")
        self.assertEqual(installable["hubInstalls"], 123)
        self.assertFalse(installable["external_only"])
        self.assertTrue(external["external_only"])
        self.assertEqual(external["packageUrl"], None)
        self.assertIsNone(external["artworkUrl"])
        self.assertIsNone(external["artworkKind"])
        self.assertIsNone(external["hubInstalls"])
        self.assertEqual(external["tags"], ["groove", "dev"])

    def test_artwork_requires_safe_hub_card_path(self):
        for path in (
            "https://evil.example/card.webp",
            "//evil.example/card.webp",
            "fw/cards/../../secret.webp",
            "fw/cards/card.svg",
            42,
        ):
            source = json.loads(json.dumps(FIXTURE))
            source["firmwares"][0]["card"] = path
            entry = catalogue._validate_catalogue(source)[0]
            self.assertIsNone(entry["artworkUrl"], path)
            self.assertIsNone(entry["artworkKind"], path)

    def test_hub_install_count_is_bounded_and_unknown_stays_null(self):
        for value, expected in (
            (0, 0),
            (catalogue.MAX_HUB_INSTALLS, catalogue.MAX_HUB_INSTALLS),
            (-1, None),
            (catalogue.MAX_HUB_INSTALLS + 1, None),
            (True, None),
            ("123", None),
            (None, None),
        ):
            source = json.loads(json.dumps(FIXTURE))
            source["firmwares"][0]["installs"] = value
            entry = catalogue._validate_catalogue(source)[0]
            self.assertEqual(entry["hubInstalls"], expected, value)

    def test_chip_old_is_preserved_for_device_compatibility_gate(self):
        source = json.loads(json.dumps(FIXTURE))
        source["firmwares"][0]["chip"] = "old"
        entry = catalogue._validate_catalogue(source)[0]
        self.assertEqual(entry["chip"], "old")

    def test_non_github_installer_link_is_visible_but_never_a_package_url(self):
        source = json.loads(json.dumps(FIXTURE))
        source["firmwares"][0]["release"] = "https://maker.example/install"
        entry = catalogue._validate_catalogue(source)[0]
        self.assertIsNone(entry["releaseUrl"])
        self.assertIsNone(entry["packageUrl"])
        self.assertTrue(entry["external_only"])
        self.assertEqual(entry["projectUrl"], "https://example-maker.github.io/sample-fw/")

    def test_rejects_bad_hash_duplicate_id_and_untrusted_release_url(self):
        for mutate in (
            lambda doc: doc["firmwares"][0].update(sha256="not-a-hash"),
            lambda doc: doc["firmwares"].append(dict(doc["firmwares"][0])),
        ):
            source = json.loads(json.dumps(FIXTURE))
            mutate(source)
            with self.subTest(source=source):
                with self.assertRaises(catalogue.CatalogueError):
                    catalogue._validate_catalogue(source)
        malicious_link = json.loads(json.dumps(FIXTURE))
        malicious_link["firmwares"][0]["release"] = "https://evil.example/download/fw.fwsc"
        entry = catalogue._validate_catalogue(malicious_link)[0]
        self.assertIsNone(entry["releaseUrl"])
        self.assertTrue(entry["external_only"])

    def test_rejects_malformed_or_unknown_schema_and_oversized_response(self):
        with self.assertRaises(catalogue.CatalogueError):
            catalogue._validate_catalogue({"license": {"license": "CC-BY-4.0"}, "updated": "x", "firmwares": "no"})
        bad_license = json.loads(json.dumps(FIXTURE))
        bad_license["license"]["license"] = "MIT"
        with self.assertRaises(catalogue.CatalogueError):
            catalogue._validate_catalogue(bad_license)
        with self.assertRaises(catalogue.CatalogueError):
            catalogue._read_response(FakeResponse(b"x" * 20), 10)

    def test_daily_limit_uses_valid_cache_without_network(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalogue.json"
            now = dt.datetime(2026, 11, 15, tzinfo=dt.timezone.utc)
            catalogue._atomic_json(path, {
                "schema": catalogue.CACHE_SCHEMA,
                "source": catalogue.CATALOGUE_URL,
                "fetched_at": (now - dt.timedelta(hours=2)).isoformat(),
                "catalogue": FIXTURE,
            })
            def should_not_open(_request, **_kwargs):
                raise AssertionError("network should not be used within the daily limit")
            entries, status = catalogue.sync_catalogue(path, opener=should_not_open, now=now)
            self.assertEqual(status, "cached")
            self.assertEqual(len(entries), 2)

    def test_bad_refresh_retains_last_valid_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalogue.json"
            now = dt.datetime(2026, 11, 15, tzinfo=dt.timezone.utc)
            original = {"schema": catalogue.CACHE_SCHEMA, "source": catalogue.CATALOGUE_URL,
                        "fetched_at": (now - dt.timedelta(days=2)).isoformat(), "catalogue": FIXTURE}
            catalogue._atomic_json(path, original)
            def malformed(_request, **_kwargs):
                return FakeResponse(b"{broken json")
            entries, status = catalogue.sync_catalogue(path, opener=malformed, now=now)
            self.assertTrue(status.startswith("stale-cache:"))
            self.assertEqual(len(entries), 2)
            self.assertEqual(json.loads(path.read_text())["catalogue"], FIXTURE)

    def test_failed_initial_fetch_is_rate_limited_for_a_day(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cache.json"
            now = dt.datetime(2026, 11, 15, tzinfo=dt.timezone.utc)
            def fail(_request, **_kwargs):
                raise OSError("offline")
            with self.assertRaises(catalogue.CatalogueError):
                catalogue.sync_catalogue(path, opener=fail, now=now)
            def should_not_open(_request, **_kwargs):
                raise AssertionError("failed attempts are rate limited too")
            with self.assertRaisesRegex(catalogue.CatalogueError, "within the last 24 hours"):
                catalogue.sync_catalogue(path, opener=should_not_open, now=now + dt.timedelta(hours=1))

    def test_catalogue_fetch_sends_identifiable_ua_and_bounds_feed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cache.json"
            seen = {}
            def fetch(request, **_kwargs):
                seen.update(request.header_items())
                return FakeResponse(json.dumps(FIXTURE).encode(), headers={"ETag": '"abc"'})
            entries, status = catalogue.sync_catalogue(path, opener=fetch,
                user_agent="MyFM1App/1.0", now=dt.datetime(2026, 11, 15, tzinfo=dt.timezone.utc))
            self.assertEqual(status, "updated")
            self.assertIn(("User-agent", "MyFM1App/1.0"), seen.items())
            self.assertEqual(json.loads(path.read_text())["etag"], '"abc"')
            self.assertEqual(len(entries), 2)

    def test_normalized_pagination_is_explicit(self):
        source = json.loads(json.dumps(FIXTURE))
        for index in range(100):
            item = dict(source["firmwares"][0])
            item["id"] = f"extra-{index}"
            source["firmwares"].append(item)
        entries = catalogue._validate_catalogue(source)
        page_count = (len(entries) + catalogue.PAGE_SIZE - 1) // catalogue.PAGE_SIZE
        self.assertEqual(page_count, 2)
        self.assertEqual(len(entries[:catalogue.PAGE_SIZE]), 96)
        self.assertEqual(len(entries[catalogue.PAGE_SIZE:]), 6)

    def test_resolves_only_matching_github_release_asset(self):
        entry = catalogue._validate_catalogue(FIXTURE)[0]
        body = {"tag_name": "v1.2.3", "html_url": entry["releaseUrl"], "assets": [
            {"name": "sample-fw-1.2.3.fwsc", "browser_download_url":
             "https://github.com/example-maker/sample-fw/releases/download/v1.2.3/sample-fw-1.2.3.fwsc"},
            {"name": "source.zip", "browser_download_url":
             "https://github.com/example-maker/sample-fw/releases/download/v1.2.3/source.zip"},
        ]}
        def fetch(request, **_kwargs):
            self.assertIn("api.github.com/repos/example-maker/sample-fw/releases/tags/v1.2.3", request.full_url)
            return FakeResponse(json.dumps(body).encode(), url=request.full_url)
        self.assertEqual(catalogue.github_asset_url(entry, opener=fetch), body["assets"][0]["browser_download_url"])

    def test_rejects_non_github_asset_and_ambiguous_assets(self):
        entry = catalogue._validate_catalogue(FIXTURE)[0]
        bad = {"assets": [{"name": "sample-fw-1.2.3.fwsc", "browser_download_url": "https://evil.example/fw.fwsc"}]}
        with self.assertRaises(catalogue.CatalogueError):
            catalogue.github_asset_url(entry, opener=lambda request, **kw:
                FakeResponse(json.dumps(bad).encode(), url=request.full_url))


if __name__ == "__main__":
    unittest.main()
