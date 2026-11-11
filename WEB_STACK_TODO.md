# Temporary web stack TODO

Last reviewed: 2026-11-11.

This is a temporary follow-up checklist. Remove it after the selected work is
completed or moved into the maintained backlog. It does not authorize upgrades,
installation, service changes, or hardware operation.

## Periodic release check for Dot

- [ ] Check whether Bootswatch has published a Zephyr release matching Bootstrap
  5.3.9 (or a newer matching stable pair). Bootstrap remains at 5.3.8 until a
  matching Zephyr release is available. Do not apply upgrades automatically.
  - Bootstrap: <https://github.com/twbs/bootstrap/releases>
  - Bootswatch tags: <https://github.com/thomaspark/bootswatch/tags>
  - Bootswatch package metadata:
    <https://github.com/thomaspark/bootswatch/blob/master/package.json>
  - Published Zephyr: <https://bootswatch.com/zephyr/>
  - Confirm exact published package versions and Zephyr's embedded Bootstrap
    version; a changed website label or an unpublished commit is insufficient.
  - Report the matching versions, publication evidence, and relevant changes.

## Bootstrap and Zephyr upgrade after separate approval

- [ ] Acquire exact matching distributions and record provenance and licenses.
- [ ] Replace local Bootstrap JS bundle, plain Bootstrap CSS, and Zephyr CSS
  together. Update `WsprryPi-UI/data/header.php`, `site.js.includes.php`, and
  `view_diag_logs.php`; preserve the standalone viewer's plain Bootstrap styling.
- [ ] Preserve local fonts, omitted external font imports, local resource URLs,
  attribution, and intentional source-map handling. Verify bundled Popper's
  resolved version rather than adding a separate script.
- [ ] Update `WsprryPi-UI/README.md`, third-party notices, and the explicit
  Bootstrap version assertion in `tests/dependency_browser_test.js`.
- [ ] Run `npm test` and `npm run test:browser` from `WsprryPi-UI`. Cover literal
  tooltip/popover text, jQuery integration, tabs, dropdowns, modal focus and
  dismissal, saved preferences, preserved drafts, and diagnostic log streams.
- [ ] Use Impeccable to inspect desktop and mobile layouts in both themes.
  Check the main views and standalone diagnostic viewer, including font/icon
  loading and overflow. Review the existing UI manifest and refresh behavior.
- [ ] Review operator documentation; replace screenshots only if materially
  inaccurate. Documentation-repository edits need separate authorization.

## Deferred; do not change now

- [ ] Revisit the development Node baseline and CI environments. The current
  `>=18` declaration permits end-of-life releases; evaluate a supported LTS
  baseline without silently dropping required Debian development environments.
  <https://nodejs.org/en/about/previous-releases>
- [ ] Review distribution-supported Apache updates on each authorized serving
  host. Apache 2.4.71 was upstream's latest release at this review; verify current
  vendor packages and security backports before proposing changes.
  <https://httpd.apache.org/download.cgi>
- [ ] Review distribution-supported PHP security updates using the repository's
  read-only audit and vendor advisories. Verify the actual serving web SAPI and
  loaded workers separately from CLI/package metadata. Review newer advisories
  as well as the October fixes; do not assume configuration mitigations replace
  patches. No package update, service restart, or configuration change is
  authorized by this checklist.
  - Local guidance: `scripts/README.md`
  - Upstream advisories: <https://www.php.net/archive/2026.php>

The `ws` 8.22.2 development-dependency update is a separate local change. It is
not included in the documentation-only commit that introduces this checklist.
Other reviewed browser assets remain unchanged; recheck their releases when
performing the later upgrade. Dot scheduling is managed separately by the user.
