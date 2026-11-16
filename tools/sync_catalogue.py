#!/usr/bin/env python3
"""Fetch and normalize The Firmware Hub catalogue under its published terms.

Catalogue reads are limited to one per 24 hours.
Package bytes are deliberately not fetched here: consumers must resolve a
maker's GitHub release asset, then verify its SHA-256 against the catalogue.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

CATALOGUE_URL = "https://thefirmwarehub.com/catalog.json"
CATALOGUE_LICENSE = "CC-BY-4.0"
MAX_CATALOGUE_BYTES = 8 * 1024 * 1024
MAX_GITHUB_RESPONSE_BYTES = 1024 * 1024
MAX_SOURCE_ENTRIES = 1000
PAGE_SIZE = 96
MAX_TEXT = 4096
MAX_HUB_INSTALLS = 2**31 - 1
DAILY_SECONDS = 24 * 60 * 60
CACHE_SCHEMA = 1
DEFAULT_USER_AGENT = "FM1-Flip/0.1 (https://thefirmwarehub.com; catalogue client)"
HUB_CARD_PATH = re.compile(r"fw/cards/[A-Za-z0-9_.-]{1,180}\.(?:webp|png|jpe?g)", re.IGNORECASE)


class CatalogueError(ValueError):
    """The feed or a maker release response is invalid or unsupported."""


def _bounded_text(value, field, limit=MAX_TEXT, optional=True):
    if value is None and optional:
        return None
    if not isinstance(value, str) or len(value) > limit:
        raise CatalogueError(f"invalid {field}")
    return value


def _validate_catalogue(document):
    if not isinstance(document, dict):
        raise CatalogueError("catalogue root must be an object")
    license_info = document.get("license")
    if not isinstance(license_info, dict) or license_info.get("license") != CATALOGUE_LICENSE:
        raise CatalogueError("missing or unsupported catalogue license")
    if not isinstance(document.get("updated"), str):
        raise CatalogueError("missing catalogue updated timestamp")
    entries = document.get("firmwares")
    if not isinstance(entries, list) or len(entries) > MAX_SOURCE_ENTRIES:
        raise CatalogueError("firmwares must be a bounded array")
    seen = set()
    normalized = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise CatalogueError("firmware entry must be an object")
        ident = _bounded_text(entry.get("id"), "id", 128, optional=False)
        if not ident or ident in seen:
            raise CatalogueError("missing or duplicate firmware id")
        seen.add(ident)
        name = _bounded_text(entry.get("name"), "name", 256, optional=False)
        repo = _bounded_text(entry.get("repo"), "repo", 256)
        if repo is not None and not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
            raise CatalogueError(f"invalid repository for {ident}")
        sha256 = entry.get("sha256")
        if sha256 is not None and (not isinstance(sha256, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", sha256)):
            raise CatalogueError(f"invalid sha256 for {ident}")
        size = entry.get("size")
        if size is not None and (not isinstance(size, int) or isinstance(size, bool) or size < 0):
            raise CatalogueError(f"invalid size for {ident}")
        release = entry.get("release")
        tag = entry.get("tag")
        maker_release_url = None
        if release is not None and repo is not None:
            try:
                maker_release_url = validate_release_url(release, repo, tag)
            except CatalogueError:
                # Link-only and legacy records may point at a maker's own
                # installer. They remain visible, but never become package URLs.
                maker_release_url = None
        pkg_present = bool(entry.get("pkg")) and bool(sha256) and size is not None and maker_release_url is not None
        kind = entry.get("kind")
        tags = [kind] if isinstance(kind, str) else [item for item in kind if isinstance(item, str)] if isinstance(kind, list) else []
        card_path = entry.get("card")
        artwork_url = _hub_card_url(card_path)
        installs = entry.get("installs")
        hub_installs = installs if (isinstance(installs, int) and not isinstance(installs, bool)
                                    and 0 <= installs <= MAX_HUB_INSTALLS) else None
        normalized.append({
            "id": ident,
            "name": name,
            "author": _bounded_text(entry.get("author"), "author", 256),
            "description": _bounded_text(entry.get("what"), "what"),
            "tags": tags,
            "target": "FM-1",
            "hubUrl": "https://thefirmwarehub.com/",
            "version": _bounded_text(entry.get("version"), "version", 128),
            "date": _bounded_text(entry.get("date"), "date", 64),
            "license": _bounded_text(entry.get("lic"), "lic", 128),
            "repo": repo,
            "sourceUrl": f"https://github.com/{repo}" if repo else None,
            "releaseUrl": maker_release_url,
            "packageUrl": None,
            "tag": _bounded_text(tag, "tag", 256),
            "asset_pattern": _bounded_text(entry.get("assetPkg"), "assetPkg", 256),
            "identity": _bounded_text(entry.get("product"), "product", 128),
            "identity_pattern": _bounded_text(entry.get("ident"), "ident", 256),
            "size": size,
            "sha256": sha256.lower() if sha256 else None,
            "chip": entry.get("chip") if entry.get("chip") in ("old", "new") else None,
            "beta": bool(entry.get("beta", False)),
            "external_only": not pkg_present,
            # Preserve helpful metadata but never construct a download URL from Hub `pkg`.
            "projectUrl": _optional_https(entry.get("site")),
            # `card` is a Hub-hosted copy of maker artwork, not part of CC BY's compilation licence.
            "artworkUrl": artwork_url,
            "artworkKind": "hub-card" if artwork_url else None,
            # The Hub labels this count "installs via the hub"; it does not document its counting event.
            "hubInstalls": hub_installs,
        })
    return normalized


def _hub_card_url(value):
    """Resolve only a safe relative card-image path supplied by the Hub feed."""
    if (not isinstance(value, str) or len(value) > 220 or not HUB_CARD_PATH.fullmatch(value)
            or ".." in value or "//" in value):
        return None
    return f"https://thefirmwarehub.com/{value}"


def _optional_https(value):
    if value is None:
        return None
    if not isinstance(value, str):
        return None
    parsed = urllib.parse.urlsplit(value)
    return value if parsed.scheme == "https" and parsed.hostname and not parsed.username and not parsed.password else None


def validate_release_url(url, repo, tag=None):
    """Accept only a GitHub release page for this exact repository/tag."""
    if not isinstance(url, str):
        raise CatalogueError("release URL must be a string")
    parsed = urllib.parse.urlsplit(url)
    expected = f"/{repo}/releases/tag/"
    if (parsed.scheme != "https" or parsed.hostname != "github.com" or parsed.port is not None
            or parsed.username or parsed.password or not parsed.path.startswith(expected)
            or parsed.query or parsed.fragment):
        raise CatalogueError("release URL is outside the GitHub release allowlist")
    url_tag = urllib.parse.unquote(parsed.path[len(expected):])
    if not url_tag or "/" in url_tag or (tag is not None and url_tag != tag):
        raise CatalogueError("release URL tag does not match catalogue")
    return url


def github_asset_url(entry, opener=None, user_agent=DEFAULT_USER_AGENT):
    """Resolve one maker release asset from GitHub's release API, never Hub `pkg`.

    The caller downloads from the returned URL and must verify the source SHA-256.
    """
    repo = entry["repo"]
    release_url = validate_release_url(entry["releaseUrl"], repo, entry.get("tag"))
    tag = urllib.parse.unquote(urllib.parse.urlsplit(release_url).path.rsplit("/", 1)[1])
    api_url = f"https://api.github.com/repos/{repo}/releases/tags/{urllib.parse.quote(tag, safe='')}"
    request = urllib.request.Request(api_url, headers={
        "Accept": "application/vnd.github+json",
        "User-Agent": user_agent,
    })
    client = opener or urllib.request.urlopen
    try:
        response = client(request, timeout=20)
        with response:
            final_url = response.geturl()
            _validate_github_api_url(final_url)
            payload = response.read(MAX_GITHUB_RESPONSE_BYTES + 1)
    except (OSError, urllib.error.URLError) as exc:
        raise CatalogueError(f"GitHub release lookup failed: {exc}") from exc
    if len(payload) > MAX_GITHUB_RESPONSE_BYTES:
        raise CatalogueError("GitHub release response exceeds size limit")
    try:
        release = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CatalogueError("invalid GitHub release response") from exc
    assets = release.get("assets") if isinstance(release, dict) else None
    if not isinstance(assets, list):
        raise CatalogueError("GitHub response has no release assets")
    if release.get("tag_name") != tag or release.get("html_url") != release_url:
        raise CatalogueError("GitHub release response does not match the selected tag")
    pattern = entry.get("asset_pattern")
    try:
        matcher = re.compile(pattern) if pattern else None
    except re.error as exc:
        raise CatalogueError("invalid maker asset filename pattern") from exc
    candidates = []
    for asset in assets:
        if not isinstance(asset, dict):
            continue
        name = asset.get("name")
        url = asset.get("browser_download_url")
        if isinstance(name, str) and name.endswith(".fwsc") and isinstance(url, str):
            _validate_asset_url(url, repo, tag, name)
            if matcher is None or matcher.fullmatch(name):
                candidates.append((name, url))
    if len(candidates) != 1:
        raise CatalogueError(f"expected one matching .fwsc release asset, found {len(candidates)}")
    return candidates[0][1]


def _validate_github_api_url(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or parsed.hostname != "api.github.com" or parsed.username or parsed.password:
        raise CatalogueError("GitHub API redirect left HTTPS allowlist")


def _validate_asset_url(url, repo, tag, name):
    parsed = urllib.parse.urlsplit(url)
    expected = f"/{repo}/releases/download/{urllib.parse.quote(tag, safe='')}"
    if (parsed.scheme != "https" or parsed.hostname != "github.com" or parsed.port is not None
            or parsed.username or parsed.password or parsed.query or parsed.fragment
            or not parsed.path.startswith(expected + "/")):
        raise CatalogueError("release asset URL is outside HTTPS GitHub allowlist")
    actual_name = urllib.parse.unquote(parsed.path.rsplit("/", 1)[1])
    if actual_name != name:
        raise CatalogueError("release asset URL filename mismatch")


def _read_response(response, limit):
    final_url = response.geturl()
    parsed = urllib.parse.urlsplit(final_url)
    if parsed.scheme != "https" or parsed.hostname != "thefirmwarehub.com" or parsed.username or parsed.password:
        raise CatalogueError("catalogue redirect left HTTPS allowlist")
    data = response.read(limit + 1)
    if len(data) > limit:
        raise CatalogueError("catalogue response exceeds size limit")
    return data


def _cache_read(path):
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(value, dict) or value.get("schema") != CACHE_SCHEMA:
            return None
        return value
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, AttributeError):
        return None


def _atomic_text(path, rendered):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(rendered)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def _atomic_json(path, value):
    _atomic_text(path, json.dumps(value, ensure_ascii=False, separators=(",", ":")))


def sync_catalogue(cache_path, *, user_agent=DEFAULT_USER_AGENT,
                   opener=None, now=None):
    """Return (normalized_entries, status) and preserve cache on any bad refresh."""
    now = now or dt.datetime.now(dt.timezone.utc)
    path = Path(cache_path)
    cached = _cache_read(path)
    if cached:
        checked = None
        try:
            checked = dt.datetime.fromisoformat(cached.get("last_attempt_at", cached["fetched_at"]))
        except (KeyError, TypeError, ValueError):
            checked = None
        if checked is not None and (now - checked).total_seconds() < DAILY_SECONDS:
            if isinstance(cached.get("catalogue"), dict):
                return _validate_catalogue(cached["catalogue"]), "cached"
            raise CatalogueError("catalogue refresh was attempted within the last 24 hours; no valid cache exists")
    headers = {"User-Agent": user_agent, "Accept": "application/json"}
    if cached:
        if cached.get("etag"):
            headers["If-None-Match"] = cached["etag"]
        if cached.get("last_modified"):
            headers["If-Modified-Since"] = cached["last_modified"]
    request = urllib.request.Request(CATALOGUE_URL, headers=headers)
    client = opener or urllib.request.urlopen
    try:
        response = client(request, timeout=20)
        with response:
            status_code = getattr(response, "status", 200)
            if status_code == 304 and cached:
                cached["fetched_at"] = now.isoformat()
                cached["last_attempt_at"] = now.isoformat()
                _atomic_json(path, cached)
                return _validate_catalogue(cached["catalogue"]), "not-modified"
            raw = _read_response(response, MAX_CATALOGUE_BYTES)
            catalogue = json.loads(raw.decode("utf-8"))
            entries = _validate_catalogue(catalogue)
            replacement = {
                "schema": CACHE_SCHEMA,
                "source": CATALOGUE_URL,
                "fetched_at": now.isoformat(),
                "last_attempt_at": now.isoformat(),
                "etag": response.headers.get("ETag"),
                "last_modified": response.headers.get("Last-Modified"),
                "catalogue": catalogue,
            }
            _atomic_json(path, replacement)
            return entries, "updated"
    except Exception as exc:
        if cached:
            cached["last_attempt_at"] = now.isoformat()
            try:
                _atomic_json(path, cached)
            except OSError:
                pass
            try:
                return _validate_catalogue(cached["catalogue"]), f"stale-cache: {exc}"
            except CatalogueError:
                pass
        else:
            # Remember a failed initial request too, to uphold the daily limit.
            try:
                _atomic_json(path, {
                    "schema": CACHE_SCHEMA, "source": CATALOGUE_URL,
                    "fetched_at": now.isoformat(), "last_attempt_at": now.isoformat(),
                    "catalogue": None,
                })
            except OSError:
                pass
        if isinstance(exc, CatalogueError):
            raise
        raise CatalogueError(f"catalogue refresh failed: {exc}") from exc


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=Path("catalogue-cache.json"))
    parser.add_argument("--user-agent", default=DEFAULT_USER_AGENT, help="identify this catalogue client")
    parser.add_argument("--output", type=Path, help="write normalized JSON here (stdout by default)")
    parser.add_argument("--page", type=int, default=0, help="zero-based page of at most 96 entries")
    args = parser.parse_args(argv)
    if args.page < 0:
        parser.error("--page must be zero or greater")
    if not args.user_agent.strip() or len(args.user_agent) > 256:
        parser.error("--user-agent must be a nonempty identifying string of at most 256 chars")
    try:
        entries, status = sync_catalogue(args.cache, user_agent=args.user_agent)
        cached = _cache_read(args.cache)
        retrieved_at = cached.get("fetched_at") if cached else None
        page_count = max(1, (len(entries) + PAGE_SIZE - 1) // PAGE_SIZE)
        if args.page >= page_count:
            parser.error(f"--page must be less than {page_count}")
        payload = {"schemaVersion": 1, "sourceUrl": CATALOGUE_URL,
                   "retrievedAt": retrieved_at,
                   "attribution": "The Firmware Hub — https://thefirmwarehub.com",
                   "license": CATALOGUE_LICENSE, "status": status,
                   "page": args.page, "pageSize": PAGE_SIZE,
                   "pageCount": page_count, "totalEntries": len(entries),
                   "truncated": page_count > 1,
                   "entries": entries[args.page * PAGE_SIZE:(args.page + 1) * PAGE_SIZE]}
        rendered = json.dumps(payload, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            _atomic_text(args.output, rendered)
        else:
            sys.stdout.write(rendered)
    except CatalogueError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
