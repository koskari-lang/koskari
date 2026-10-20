# Handoff: Koskari Landing Page (koskari.org)

## Overview
This is the marketing landing page for **Koskari**, a stackless Lisp dialect that runs alongside Python (real continuations, ambient async, and an indentation layout mode that escapes the deep-parenthesis problem). The page is a single, static, build-optional marketing page intended to be served at **koskari.org**. It links out to a **separate** Sphinx-generated documentation site at **docs.koskari.org** — this landing page does NOT contain or generate any docs itself.

## About the Design Files
The files in this bundle are **design references created in HTML** — a prototype showing the intended look, layout, and behavior. They are not meant to be shipped verbatim. The `.dc.html` files use a proprietary component runtime (`support.js`) that is specific to the design tool they were authored in; **do not ship that runtime**. Recreate the design as plain static HTML/CSS (this page needs almost no JS), or inside whatever static setup you prefer (a single `index.html`, Astro, Eleventy, Vite, etc.). A fully self-contained, runtime-free rendering is also included (`Koskari Landing (standalone).html`) that you can open in a browser to see the exact intended result and lift markup from.

Since koskari.org is a plain static page, the simplest correct implementation is a single hand-written `index.html` + one CSS file + the SVG icon. No framework is required.

## Fidelity
**High-fidelity.** Final colors, typography, spacing, and interactions are all specified below and visible in the standalone HTML. Recreate it pixel-faithfully. The one deliberately "live" detail is the hero logo's leaf flutter animation (see Interactions).

## Layout / Structure
Single column, centered, `max-width: 1200px`, background `#F7F3EA`. Body font `Hanken Grotesk`. Vertical section order:

1. **Nav bar** — flex row, space-between, padding `22px 56px`, bottom border `1px solid #E9E0CF`.
   - Left: apple mark (34×34) + wordmark "Koskari" (Bricolage Grotesque 700, 21px) + version pill "v0.1.0" (JetBrains Mono 11px, bg `#EFE7D6`, text `#9C8F79`, radius 99px).
   - Right: text links Docs / Examples / Download / GitHub (15px, weight 500, color `#534b40`, hover `#A9761A`). Docs → `https://docs.koskari.org/`, Examples → `#examples`, Download → `#download`, GitHub → `https://github.com/koskari/koskari`.

2. **Hero** — CSS grid `1.18fr .82fr`, gap 40px, padding `72px 56px 60px`.
   - Left column: eyebrow pill ("a lisp that runs alongside python", JetBrains Mono 12px, text `#A9761A`, bg `#F2E8CF`, border `1px solid #E7D6A6`, radius 99px). Headline (Bricolage Grotesque 700, 62px, line-height 1.02, letter-spacing -.025em): "Lisp, the fun parts." then a `<br>` then "Python, all of it." with the second line colored `#A9761A`. Sub-paragraph 18.5px, color `#5C544A`, max-width 30em. Two buttons (see components). Install strip: dark `#1c1812` rounded bar containing `$ pip install koskari` (mono 14px, `$` in green `#7C8B40`, text `#E7DFD0`) with a right-aligned "0.9.0 BETA · SOON" tag (mono 10.5px, `#8A8070`).
   - Right column: a 262×262 circle (radial-gradient `#F4ECD6`→`#F7F3EA`, inset ring `#EBE0C9`) containing the apple mark at 190×190 **with the leaf flutter enabled**. Below it, centered caption in Greek "καλλιστι — to the prettiest" (mono 12px, `#9C8F79`, "to the prettiest" in `#A9761A`). This is a Discordian "Kallisti" wink — keep the Greek.

3. **Features** — eyebrow "Why it's fun" (mono 12px uppercase, letter-spacing .14em, `#A9761A`). 2×2 grid, gap 18px. Each card: white bg, border `1px solid #ECE3D2`, radius 13px, padding 26px, containing a 42×42 rounded icon tile (bg `#F4EAD0`, text `#A9761A`, mono glyph), an h3 (Bricolage 600, 20px), and a 15px paragraph (`#5f5747`). The four cards:
   - ⇄ (U+21C4) **Lives with Python** — "Import any module, pass objects both ways. Not a transpiler — a real stackless interpreter running side by side with CPython."
   - ( ) **Beyond the parens** — "A novel indentation layout lets you write Lisp's power without drowning in brackets. Drop back into S-expressions whenever you like."
   - ↻ (U+21BB) **Continuations, really** — "Stackless to the core: first-class continuations, coroutines, and control flow you can pick up and hold in your hand."
   - ∿ (U+223F) **Ambiently async** — "Concurrency without the colored functions. Everything can await, nothing has to. The runtime just gets out of your way."

4. **Example** (`id="examples"`) — eyebrow "See it for yourself", h2 "A point, and its neighbors" (Bricolage 700, 34px), intro paragraph. Then a "code window" card: bg `#FBF8F1`, border `#ECE3D2`, radius 13px, shadow `0 10px 30px rgba(60,44,16,.07)`. Window title bar (bg `#F5EFE2`, bottom border `#EFE7D6`) has three traffic-light dots (`#E0865A`, `#E6B84C`, `#9FB36A`) and filename "point.ko" (mono 12.5px). Body padding `22px 24px` holds the syntax-highlighted code sample (see Code Sample section).

5. **Documentation** — eyebrow "Documentation", h2 "Find your level". Vertical list (gap 10px) of 5 link rows. Each row: flex, white bg, border `#ECE3D2`, radius 12px, padding `20px 24px`, hover bg `#FBF7EC` + border `#E1C77E` + arrow slides right 4px & turns `#A9761A`. Row structure: number "01"–"05" (mono 13px, `#C0A050`, 700, width 28px) · title (Bricolage 600, 19px, width 230px) · description (15px, `#6a6354`, flex:1) · arrow "→" (mono, `#B8AD98`). Rows and their hrefs:
   - 01 **Getting Started** → `https://docs.koskari.org/getting-started/` — "Install, open the REPL, evaluate your first form. Five minutes, tops."
   - 02 **Tutorial** → `https://docs.koskari.org/tutorial/` — "Build something real, one step at a time — with Python in the mix."
   - 03 **Language Reference** → `https://docs.koskari.org/reference/` — "Every special form and layout rule, spelled out precisely."
   - 04 **API** → `https://docs.koskari.org/api/` — "The runtime, embedding, and the full Python interop surface."
   - 05 **Standard Library** → `https://docs.koskari.org/stdlib/` — "Batteries included — and Python's batteries too, for free."
   - NOTE: doc URL slugs are placeholders matching a flat Sphinx layout. Adjust to your actual Sphinx paths (e.g. if using ReadTheDocs-style versioning, `https://docs.koskari.org/en/latest/getting-started/`).

6. **Download band** (`id="download"`) — full dark card, margin `0 56px 64px`, bg `#1c1812`, radius 16px, padding `44px 48px`, text `#EDE6D7`. Grid `1fr 1fr`. Left: h2 "Get Koskari" (white, Bricolage 700, 32px), paragraph (`#B9B0A0`) noting v0.1.0 from source today / v0.9.0 first public beta on PyPI, with "v0.1.0" and "v0.9.0" emphasized in `#E8B84A`. Then 4 platform pills (macOS / Linux / Windows / CPython 3.11+; mono 11px, `#8A8070`, border `1px solid #3a342a`, radius 99px). Right: two darker command cards (bg `#100d09`, border `#2e2a21`, radius 10px) — one labeled "FROM SOURCE · TODAY" with `git clone github.com/koskari/koskari`, one labeled "PYPI · FROM v0.9.0" with `pip install koskari`. `$` prompt in `#6F6657`, command text `#E7DFD0`.

7. **Footer** — flex row, top border `#E9E0CF`, padding `26px 56px`. Left: apple mark (26×26) + "Koskari" (Bricolage 700, 16px). Right: "MIT licensed · made with parentheses · fnord" (mono 12px, `#9C8F79`; "fnord" in `#C7BBA3` — another Discordian wink, keep it).

## Interactions & Behavior
- **Link hovers**: nav links transition color to `#A9761A` over .15s.
- **Buttons**: translateY(-1px) on hover, .12s ease.
- **Feature cards**: translateY(-3px), shadow `0 14px 34px rgba(60,44,16,.12)`, border `#E1C77E` on hover, .16s.
- **Doc rows**: bg `#FBF7EC`, border `#E1C77E`; nested arrow translateX(4px) + color `#A9761A` on hover.
- **Hero logo leaf flutter**: the leaf on the large hero apple sways continuously. Keyframes named `ko-leaf-flutter`, 3.6s, ease-in-out, infinite, `transform-origin` at the leaf base (2% 98%): rotate oscillates roughly -20deg → -10deg → -24deg → -14deg with slight skewX. IMPORTANT: wrap in `@media (prefers-reduced-motion: no-preference)` so it stays still for users who opt out of motion. Only the hero (large) apple animates; the nav (34px) and footer (26px) marks are static.

## Design Tokens
Colors:
- Paper bg: `#F7F3EA`; card white: `#FFFFFF`; warm card border: `#ECE3D2` / `#E9E0CF`
- Gold accent (primary): `#A9761A`; lighter gold text on dark: `#E8B84A`; gold number: `#C0A050`
- Button gradient: `linear-gradient(#ecc356, #d4a32c)` on text `#2c2206`
- Ink / body text: `#211C16` (headings), `#5C544A` / `#5f5747` / `#6a6354` (body), `#534b40` (nav)
- Dark surfaces: `#1c1812` (bands), `#100d09` (inner command cards), `#17140F`; borders `#2e2a21` / `#3a342a`
- Eyebrow pill bg `#F2E8CF` border `#E7D6A6`; version pill bg `#EFE7D6`; icon tile bg `#F4EAD0`
- Traffic lights: `#E0865A` `#E6B84C` `#9FB36A`
- Code syntax (light theme): keywords `#A9761A` (bold), functions/types `#4F6E8F`, numbers `#9A6B2F`, strings `#7C8B40`, decorators `#B5683C`, params `#8A7E6B`, parens/punct `#B8AD98`, default text `#3A332B`
- Code syntax (dark theme, used only if a dark code block is needed): keywords `#E8B84A`, functions/types `#8FB2D0`, numbers `#E0A45A`, strings `#B9C46E`, decorators `#E0865A`, params `#C3B89F`, parens `#6F6657`, default `#E7DFD0`, bg `#100d09`/`#1c1812`

Typography:
- Display / headings: **Bricolage Grotesque** (500–800), used at 62/34/32/21/20/19/16px
- Body: **Hanken Grotesk** (400–700)
- Mono / code / eyebrows / pills: **JetBrains Mono** (400–700)
- All three load from Google Fonts (see the `<link>` in any of the HTML files).

Radius: pills 99px; cards 12–13px; bands 16px; buttons 8px; inner command cards 10px.
Spacing: section horizontal padding 56px; card padding 26px; nav/footer padding 22–26px vertical.

## Assets
- **`koskari-icon.svg`** — the logo. A gold apple cross-section (radial-gradient skin, pale flesh center) with a stem, a leaf, and a **λ (lambda)** forming the apple's core. Fully self-contained SVG, no font dependency (λ is drawn as vector strokes). Use it as the favicon, the nav/footer mark, and anywhere else. It is a Discordian golden-apple reference.
- In the design prototype the on-page apple marks are rendered by a CSS component (`AppleMark.dc.html`) so the leaf could animate; for production you can either (a) inline that CSS apple for the animated hero and use the SVG for the static nav/footer marks, or (b) use the SVG everywhere and animate just the leaf `<path>` in the hero copy via CSS. Either is fine — the SVG is the source of truth for the shape.
- The code sample is real Koskari source; keep the exact tokens/indentation (see below).

## Code Sample (exact — preserve indentation & the blank line before @dataclass)
```
import-from dataclasses dataclass
import-from itertools product


def around [i reach: 1]:
    (range (- i reach) (+ i reach 1))

@dataclass
class Point []:
    """ A point in 2D space """

    var x :: int 0
    var y :: int 0

    def neighbors [self reach: 1]:
        each [[x y] (product (around self.x reach) (around self.y reach))]:
            (Point x y)

    def move [self x: 0 y: 0]:
        (incr! self.x x)
        (incr! self.y y)

    @property
    def coord [self]:
        #(self.x self.y)

    @coord.setter
    def coord [self value]:
        var [x y] value
        (set! self.x x)
        (set! self.y y)
```
Highlighting map: `import-from`, `def`, `class`, `each`, `var` = keywords (bold gold). Function-call heads and `Point`/`int` = blue. Numbers = brown. The docstring = green. `@dataclass`/`@property`/`@coord.setter` = decorator orange. Keyword args like `reach:`/`x:`/`y:` = muted. Parens/brackets = light punctuation color.

## Suggested repo placement
Since docs come from Sphinx in the project repo and publish to docs.koskari.org, this landing page can live either in the same repo (e.g. `/site/` or `/www/`) or its own small repo. It's fully static — deploy as plain files behind koskari.org. Set the apex/`www` to serve `index.html`; point `docs` subdomain at the Sphinx output separately.

## Files in this bundle
- `Koskari Landing (standalone).html` — self-contained, runtime-free render of the exact page. Open in a browser; lift markup/CSS from here.
- `koskari-icon.svg` — the logo asset.
- `Koskari Site.dc.html`, `AppleMark.dc.html`, `KoskariCode.dc.html` — the design-tool source components (reference only; depend on the proprietary `support.js` runtime — do not ship).
- This `README.md`.

A developer who was not present for the design conversation should be able to rebuild koskari.org from this README plus the standalone HTML alone.
