# OscaWeb

**A minimal, Vim-powered web browser written in [Oscan](https://github.com/lucabol/Oscan)**

OscaWeb is a Single Document Interface (SDI) web browser that prioritizes keyboard-driven navigation inspired by [Vimium](https://github.com/philc/vimium). It supports HTTP and HTTPS (TLS built into Oscan — SChannel on Windows, BearSSL on Linux), renders images inline (PNG, JPEG, BMP, GIF, SVG), and executes inline JavaScript via an embedded [QuickJS-ng](https://github.com/nicotordev/quickjs-ng) engine — all with zero external dependencies beyond the Oscan compiler.

### Key Features

- **Vim-like keyboard navigation** — scroll, follow links, search, and navigate entirely from the keyboard
- **HTTP and HTTPS support** — TLS is built into Oscan (zero external dependencies)
- **JavaScript execution** — inline `<script>` tags and `onclick` handlers via embedded QuickJS-ng
- **Image rendering** — PNG, JPEG, BMP, GIF, and SVG decoded, cached, and displayed inline
- **Rich HTML rendering** — headings, lists, tables, blockquotes, code blocks, and 30+ tags
- **Text selection & copy** — click-and-drag to select text, automatically copied to clipboard
- **In-page search** — `/` to search with match highlighting and `n`/`N` navigation
- **Dark theme** — purpose-built color scheme for comfortable reading
- **Minimal dependencies** — only the Oscan compiler is required to build

## Screenshot

![OscaWeb Browser](Home.jpg)

## Prerequisites

- **[Oscan compiler](https://github.com/lucabol/Oscan)** — required (includes TLS support)
- **PowerShell** — for the build script

## Quick Start

```powershell
# Build and run
.\build.ps1 -Run

# Navigate to a URL on startup
build/browser.exe http://example.com
```

Press `o` once the browser is running to open a URL, or pass one on the command line.

## Keyboard Shortcuts

OscaWeb uses Vimium-inspired keybindings. Press `?` in the browser to toggle the help overlay.

### Navigation

| Key     | Action              |
| ------- | ------------------- |
| `j`     | Scroll down         |
| `k`     | Scroll up           |
| `d`     | Half page down      |
| `u`     | Half page up        |
| `Space` | Full page down      |
| `gg`    | Scroll to top       |
| `G`     | Scroll to bottom    |

### Browsing

| Key   | Action                            |
| ----- | --------------------------------- |
| `f`   | Follow link (hint mode)           |
| `o`   | Open URL (clear address bar)      |
| `O`   | Edit current URL                  |
| `r`   | Reload page                       |
| `p`   | Paste URL from clipboard and go   |
| `yy`  | Copy current URL to clipboard     |
| `H`   | Go back in history                |
| `L`   | Go forward in history             |

### Search

| Key   | Action              |
| ----- | ------------------- |
| `/`   | Search in page      |
| `n`   | Next match          |
| `N`   | Previous match      |

### Modes & Other

| Key      | Action                     |
| -------- | -------------------------- |
| `Esc`    | Return to normal mode      |
| `?`      | Toggle help overlay        |
| `Q`      | Quit browser               |
| `Ctrl+C` | Copy current URL           |
| `Ctrl+V` | Paste in address bar       |

### Address Bar Editing (Insert Mode)

| Key      | Action               |
| -------- | -------------------- |
| `Ctrl+A` | Move cursor to start |
| `Ctrl+E` | Move cursor to end   |
| `←` `→`  | Move cursor          |
| `Enter`  | Navigate to URL      |
| `Esc`    | Cancel editing       |

## HTML Rendering

OscaWeb renders 30+ HTML tags with a dark-themed color scheme:

**Text styling** — `<b>`/`<strong>`, `<em>`/`<i>`/`<cite>`, `<del>`/`<s>` (strikethrough), `<u>`/`<ins>` (underline), `<mark>` (highlight), `<code>`, `<pre>`

**Structure** — `<h1>`–`<h6>`, `<p>`, `<div>`, `<blockquote>` (indented with accent bar), `<hr>`, `<br>`, `<section>`, `<article>`, `<nav>`, `<header>`, `<footer>`, `<main>`, `<figure>`/`<figcaption>`

**Lists** — `<ul>` (bullets), `<ol>` (numbered), `<li>`, `<dl>`/`<dt>`/`<dd>` (definition lists)

**Tables** — `<table>`, `<thead>`/`<tbody>`/`<tfoot>`, `<tr>`, `<td>`/`<th>` with automatic column-width calculation, header separators, and cell truncation

**Links & images** — `<a>` (clickable, underlined, hint-followable), `<img>` (fetched, decoded, cached, and scaled inline)

**Entities** — `&amp;`, `&lt;`, `&gt;`, `&mdash;`, `&ndash;`, `&hellip;`, `&copy;`, `&reg;`, `&trade;`, `&bull;`, `&larr;`, `&rarr;`, and more

## Image Pipeline

Images are fetched via HTTP/HTTPS, decoded, and rendered inline:

- **Formats** — PNG, JPEG, BMP, GIF (raster via `img_load()`), SVG (rasterized via `svg_load()`)
- **Caching** — decoded pixel data cached per page; cleared on navigation
- **Scaling** — images wider than 1000px are downscaled via nearest-neighbor at cache time
- **HTML attributes** — `width`/`height` supported (absolute pixels and percentages)
- **SVG compositing** — rendered over light gray background for icon visibility
- **Fallback** — `[IMG: alt text]` placeholder if decoding fails

## JavaScript Engine

OscaWeb embeds [QuickJS-ng](https://github.com/nicotordev/quickjs-ng) for JavaScript execution via a C bridge (`js_bridge.c`):

### What's supported

- **Inline scripts** — `<script>...</script>` blocks executed after page load
- **onclick handlers** — elements with `onclick` attributes are clickable; code evaluated on click
- **DOM dirty tracking** — JS modifications to the DOM trigger automatic re-render

### DOM API

```javascript
// Document methods
document.getElementById("myId")        // → Element or null
document.getElementsByTagName("div")   // → Element[]

// Element properties
element.tagName       // getter
element.textContent   // getter/setter
element.children      // getter → child Element[]
element.id            // getter

// Element methods
element.getAttribute("href")
element.setAttribute("class", "active")

// Console
console.log("hello")
console.warn("warning")
console.error("error")
```

> **Note:** External scripts (`<script src="...">`) are not loaded. Only inline script content is executed.

## Mouse Interaction

- **Click links** — click any link to navigate
- **Click onclick elements** — triggers JavaScript handler
- **Text selection** — click and drag to select text; released selection is automatically copied to clipboard
- **Address bar** — click to focus, click to reposition cursor within URL

## Link Hint Mode

Press `f` to enter Follow mode. Each link gets a hint label:

- **Single-letter** hints for pages with few links (`a`, `s`, `d`, `f`, ...)
- **Double-letter** hints for pages with many links (`aa`, `as`, `ad`, ...)
- Type the hint letters to navigate to the corresponding link
- If no labels match your typed prefix, Follow mode exits automatically

## Status Bar

The bottom bar shows at a glance:

- **Mode** — `NORMAL`, `INSERT`, `FOLLOW`, or `SEARCH`
- **Scroll position** — `Top` or percentage (e.g., `42%`)
- **Link count** — number of links on the current page
- **Search results** — `[2/5]` match counter when searching

## Architecture

```
┌────────────┐     ┌──────────┐     ┌──────────┐
│ browser.osc│────▶│ http.osc │────▶│ url.osc  │
│ (UI, keys, │     │ (HTTP +  │     │ (parser) │
│  rendering)│     │  TLS)    │     └──────────┘
└────────────┘     └──────────┘
      │
      ├───────────▶┌──────────┐
      │            │ js.osc   │
      │            │ (JS FFI) │
      │            └──────────┘
      │                 │
      │            ┌──────────┐
      │            │js_bridge.c│
      │            │(QuickJS) │
      │            └──────────┘
      ▼
┌────────────┐     ┌──────────┐
│ html.osc   │     │libs/     │
│ (tokenizer │     │ ui.osc   │
│  + DOM)    │     │ (widgets)│
└────────────┘     └──────────┘
```

| Module           | Description |
| ---------------- | ----------- |
| `browser.osc`    | Main application — rendering engine, browser chrome, Vim keybindings, image pipeline, and page navigation |
| `url.osc`        | URL parsing (scheme, host, port, path) and relative URL resolution |
| `html.osc`       | State-machine-based HTML tokenizer and flat DOM tree builder |
| `http.osc`       | HTTP/HTTPS client using Oscan's built-in TLS (`tls_connect`, `tls_send`, `tls_recv`) |
| `js.osc`         | JavaScript engine FFI — walks the DOM to execute inline `<script>` tags |
| `js_bridge.c`    | C bridge exposing QuickJS-ng engine lifecycle, console, and DOM bindings to Oscan |
| `libs/ui.osc`    | Reusable UI widget library (panel, label, separator, button, checkbox, slider, textbox) |

## Networking

### HTTP/HTTPS

- **HTTP/1.0** with automatic `User-Agent: OscaWeb/0.1` header
- **TLS built-in** — SChannel on Windows, BearSSL on Linux (zero external dependencies)
- **Redirects** — automatic follow of 301/302/307/308 (up to 5 hops)
- **Default ports** — 80 for HTTP, 443 for HTTPS

### URL Handling

- Scheme detection (`http://`, `https://`, default `http`)
- Relative URL resolution (`../`, `./`, absolute paths, protocol-relative `//`)
- Fragment stripping, query string preservation
- Auto-prepends `http://` when no scheme is entered

## Testing

```powershell
# Run all tests
.\build.ps1 -Test

# Run individual test suites
oscan tests/test_url.osc --run
oscan tests/test_html.osc --run
```

## File Structure

```
oscanweb/
├── browser.osc          # Main application (rendering, chrome, vim keys, navigation)
├── url.osc              # URL parsing and resolution
├── http.osc             # HTTP/HTTPS client (built-in TLS)
├── html.osc             # HTML tokenizer and DOM builder
├── js.osc               # JavaScript engine FFI (QuickJS-ng)
├── js_bridge.c          # C bridge for QuickJS-ng DOM bindings
├── build.ps1            # Build script
├── README.md            # This file
├── requirements.md      # Original requirements
├── libs/
│   ├── ui.osc           # UI widget library (panel, button, checkbox, slider, textbox)
│   └── quickjs/         # QuickJS-ng engine source (quickjs.c, quickjs.h)
└── tests/
    ├── test_url.osc     # URL parser tests
    ├── test_html.osc    # HTML parser tests
    ├── test_render.osc  # Rendering tests
    ├── test_js.osc      # JavaScript engine tests
    ├── test_hints.osc   # Link hint label tests
    └── run_tests.ps1    # Test runner
```

## Limitations

- **No CSS layout** — structural rendering only (no box model, no flexbox)
- **No external script loading** — `<script src="...">` tags are ignored
- **No cookies or session management**
- **No form submission** — `<form>`, `<input>`, `<textarea>` not rendered
- **Fixed viewport** — 1024×768 window with 8×8 monospace bitmap font
- **Single-threaded** — synchronous page fetching

## Design Philosophy

- **SDI (Single Document Interface)** — one window, no tabs, maximum simplicity
- **Keyboard-first** — Vim-inspired navigation means your hands never leave the home row
- **Oscan showcase** — demonstrates the language's ability to build real applications with C interop
- **Zero external dependencies** — only the Oscan compiler is needed; TLS, image decoding, and JS are all built in

## Built With

- **[Oscan](https://github.com/lucabol/Oscan)** — Minimalist language designed for LLM code generation, compiling to C99
- **[QuickJS-ng](https://github.com/nicotordev/quickjs-ng)** — Lightweight JavaScript engine (embedded via C bridge)
- **[Vimium](https://github.com/philc/vimium)** — Inspiration for the keyboard shortcut scheme

## License

This project is licensed under the MIT License.
