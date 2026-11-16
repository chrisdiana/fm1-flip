# Firmware Hub catalogue client

`tools/sync_catalogue.py` reads the public JSON catalogue at
`https://thefirmwarehub.com/catalog.json`. It sends a descriptive User-Agent,
stores the raw validated feed with retrieval time and validators, and performs
at most one refresh per 24 hours. There is no bypass switch. The response is
bounded to 8 MiB and 1,000 source records; normalized output is paginated at
96 entries and includes `pageCount`, `totalEntries`, and `truncated` so no
records are silently omitted.
The previous valid cache remains available if a refresh fails or the schema
is invalid. ETag and Last-Modified are used when supplied.

Run it from the `fm1-switcher` directory:

```sh
python3 tools/sync_catalogue.py \
  --cache .cache/catalogue-cache.json \
  --output fixtures/catalogue.json \
  --user-agent 'FM1-Flip/0.1 (https://example.invalid/about)'
```

Pass `--page N` to export a later page from the same cached daily fetch.

The output uses schema version 1 and includes `sourceUrl`, `retrievedAt` (the
last successful source fetch time, unchanged when serving a stale cache),
attribution, the CC BY 4.0 licence identifier, and normalized entries. It
includes both installable listings and external-installer-only listings.
Missing values remain null. `chip: "old"` must block installation on FM-1
units with the newer flash chip. The Hub's listing is not evidence that the
firmware is compatible with a particular device or source firmware.

Each normalized entry has `artworkUrl`, `artworkKind`, and `hubInstalls`.
`artworkUrl` resolves the feed's relative `card` path to an HTTPS image URL on
the Hub; `artworkKind` is `"hub-card"` when that URL is present, otherwise
both fields are null. The Hub says these card pictures come from each maker's
repository. They remain the maker's work and are not licensed under CC BY 4.0:
keep the maker name and repository link with the image. Do not treat the Hub's
copy as permission to redistribute it elsewhere.

`hubInstalls` is a bounded nonnegative integer copied from the feed's
`installs` value, or null when absent or invalid. The Hub labels the number
“installs via the hub”; its public guidance does not specify exactly which
event increments it. It is suitable as a Hub-reported popularity signal, not
as a count of unique people or confirmed successful installs. Sort larger
values first and put null values last when using it as the default popularity
order. GitHub repository stars are not included as installs.

## Catalogue and package URLs

The Hub's `pkg` value points to a Hub mirror and must never be used as a
package download URL by this client. The catalogue's `release` is ordinarily
a GitHub release page, not a direct asset URL. For a selected package,
`github_asset_url()` queries the corresponding HTTPS GitHub release API,
matches a single `.fwsc` asset (and `assetPkg` pattern where supplied), and
only accepts a browser download URL under the exact repository and tag's
`github.com/{owner}/{repo}/releases/download/{tag}/...` route. It is a resolver;
the caller must still download with normal TLS verification, enforce the
catalogue's `size` bound, and compare the complete SHA-256 before making a
package selectable. Never infer an asset URL from a title or Hub mirror path.

The Hub's access terms are published in its [catalogue guide](https://thefirmwarehub.com/kb/#catalog):

* Attribute “Firmware list: The Firmware Hub – https://thefirmwarehub.com”,
  link to the Hub where possible, and identify the CC BY 4.0 licence and any
  changes.
* The compilation (selection, checks, identities, chips, loaders and versions)
  is CC BY 4.0. Maker descriptions and images remain theirs; name each maker
  and link their repository if reusing those fields.
* Read `catalog.json` at most once a day and identify the client in its
  User-Agent. Do not scrape the web pages.
* Download firmware packages from maker releases, not the Hub. Verify their
  SHA-256 against the catalogue, reject `chip: "old"` for a newer-chip FM-1,
  and remove entries that disappear from the feed.

The tool intentionally does not fetch package bytes or turn Hub `pkg` into a
URL. Package download, `.fwsc` parsing, target checks, and flash compatibility
remain separate steps.
