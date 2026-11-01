# WsprryPi UI third-party notices

WsprryPi-authored UI code is covered by the repository-root `LICENSE.md`.
The following terms apply to the corresponding bundled files under
`data/vendor/` and remain independent of the WsprryPi license.

## MIT-licensed assets

- Bootswatch Zephyr 5.3.8: copyright 2012-2025 Thomas Park.
  Source: <https://github.com/thomaspark/bootswatch/tree/v5.3.8>
- Bootstrap 5.3.8: copyright 2011-2025 The Bootstrap Authors.
  Source: <https://github.com/twbs/bootstrap/tree/v5.3.8>
- Bootstrap Icons 1.13.2: copyright 2019-2024 The Bootstrap Authors.
  Source: <https://github.com/twbs/icons/tree/v1.13.2>
- jQuery 4.0.0: copyright OpenJS Foundation and other contributors.
  Source: <https://github.com/jquery/jquery/tree/4.0.0>
- Font Awesome Free 7.3.1 code: copyright 2026 Fonticons, Inc.
  Source: <https://github.com/FortAwesome/Font-Awesome/tree/7.3.1>
- Popper 2.11.8, included in the Bootstrap bundle: copyright 2019 Federico
  Zivolo. Source: <https://github.com/floating-ui/floating-ui/tree/v2.11.8>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

The minified CSS and JavaScript files retain their embedded attribution
comments.

## SIL Open Font License 1.1 assets

The full terms and copyright notices are in [`licenses/OFL-1.1.txt`](licenses/OFL-1.1.txt).
They cover:

- Font Awesome Free 7.3.1 webfonts, with Reserved Font Name "Font Awesome";
- Barlow Semi Condensed 1.422 font files; and
- Source Sans 3 3.052 font files, with Reserved Font Name "Source".

No Font Awesome SVG or JavaScript icon files are bundled. The Font Awesome CSS
and webfonts retain the project's embedded attribution, license classification,
and trademark notice. Brand icons remain trademarks of their respective owners;
their inclusion does not indicate endorsement by either trademark holder.

## Distribution provenance

Browser packages use their published npm distributions: `jquery@4.0.0`,
`bootstrap@5.3.8`, `bootswatch@5.3.8`, `bootstrap-icons@1.13.2`, and
`@fortawesome/fontawesome-free@7.3.1`. Source-map references are omitted where
maps are not shipped. Bootstrap Icons retains only its WOFF2 font source.
Font Awesome font URLs are adjusted to the bundled `webfonts/` directory.
Zephyr's upstream Google Fonts import is omitted in favor of the local fonts.
The Bootstrap bundle matches the published distribution except for its omitted
source-map reference. Bootstrap 5.3.8's source lockfile pins its included Popper
to 2.11.8; Popper is not loaded as a separate script:
<https://github.com/twbs/bootstrap/blob/v5.3.8/package-lock.json>.

Barlow Semi Condensed weights 500, 600, and 700 come from the upstream 1.422
release's `fonts/ttf/BarlowSemiCondensed-{Medium,SemiBold,Bold}.ttf` files:
<https://github.com/jpt/barlow/tree/1.422/fonts/ttf>.
Source Sans 3 remains at upstream release 3.052:
<https://github.com/adobe-fonts/source-sans/releases/tag/3.052R>.
