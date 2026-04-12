# OscaWeb

**A minimal, Vim-powered web browser written in [Oscan](https://github.com/lucabol/Oscan)**

OscaWeb is a Single Document Interface (SDI) web browser that prioritizes keyboard-driven navigation inspired by [Vimium](https://github.com/philc/vimium). It supports both HTTP and HTTPS (TLS built into Oscan — SChannel on Windows, BearSSL on Linux) and focuses on reading the web efficiently — no JavaScript execution, no CSS layout engine, just content.

### Key Features

- **Vim-like keyboard navigation** — scroll, follow links, search, and navigate entirely from the keyboard
- **HTTP and HTTPS support** — TLS is built into Oscan (zero external dependencies)
- **Image rendering** — PNG, JPEG, BMP, GIF decoded and displayed inline
- **Minimal dependencies** — only the Oscan compiler is required
- **Written entirely in Oscan** — no C code needed

## Demo

```
┌─────────────────────────────────────────────────┐
│ [http://example.com                            ]│
├─────────────────────────────────────────────────┤
│                                                 │
│  Example Domain                                 │
│                                                 │
│  This domain is for use in illustrative         │
│  examples in documents.                         │
│                                                 │
│  More information...                            │
│                                                 │
├─────────────────────────────────────────────────┤
│ NORMAL | http://example.com | 42% | Links: 1   │
└─────────────────────────────────────────────────┘
```

## Prerequisites

- **[Oscan compiler](https://github.com/lucabol/Oscan)** — required (includes TLS support)
- **PowerShell** — for the build script

## Quick Start

```powershell
# Build
.\build.ps1 -Run

# Navigate to a URL on startup
build/browser.exe http://example.com
```

Press `o` once the browser is running to open a URL, or pass one on the command line.

## Keyboard Shortcuts

OscaWeb uses Vimium-inspired keybindings. Press `?` in the browser to toggle the help overlay.

### Navigation

| Key   | Action              |
| ----- | ------------------- |
| `j`   | Scroll down         |
| `k`   | Scroll up           |
| `d`   | Half page down      |
| `u`   | Half page up        |
| `gg`  | Scroll to top       |
| `G`   | Scroll to bottom    |

### Browsing

| Key   | Action              |
| ----- | ------------------- |
| `f`   | Follow link (hint mode) |
| `o`   | Open URL            |
| `O`   | Edit current URL    |
| `r`   | Reload page         |
| `H`   | Go back             |
| `L`   | Go forward          |

### Search

| Key   | Action              |
| ----- | ------------------- |
| `/`   | Search in page      |
| `n`   | Next match          |
| `N`   | Previous match      |

### Modes

| Key   | Action              |
| ----- | ------------------- |
| `i`   | Enter insert mode   |
| `Esc` | Return to normal mode |
| `?`   | Toggle help overlay |

## Architecture

```
┌────────────┐     ┌──────────┐     ┌──────────┐
│ browser.osc│────▶│ http.osc │────▶│ url.osc  │
│ (UI, keys, │     │ (HTTP +  │     │ (parser) │
│  rendering)│     │  TLS)    │     └──────────┘
└────────────┘     └──────────┘
      │
      ▼
┌────────────┐     ┌──────────┐
│ html.osc   │     │libs/     │
│ (tokenizer │     │ ui.osc   │
│  + DOM)    │     │ (widgets)│
└────────────┘     └──────────┘
```

| Module           | Description |
| ---------------- | ----------- |
| `browser.osc`    | Main application — rendering engine, browser chrome, Vim keybindings, image display, and page navigation |
| `url.osc`        | URL parsing (scheme, host, port, path) and relative URL resolution |
| `html.osc`       | State-machine-based HTML tokenizer and DOM tree builder |
| `http.osc`       | HTTP/HTTPS client using Oscan's built-in TLS (`tls_connect`, `tls_send`, `tls_recv`) |
| `libs/ui.osc`    | Reusable UI widget library (buttons, textbox for address bar, etc.) |

## HTTPS / TLS Support

TLS is built into the Oscan language — no external libraries or C code needed:

- **Windows**: SChannel (zero external dependencies, uses the OS built-in TLS)
- **Linux**: BearSSL (compiled into the Oscan runtime)

HTTPS just works out of the box with `.\build.ps1`.

## Testing

```powershell
# Run all tests
.\build.ps1 -Test

# Run individual test suites
oscan tests/test_url.osc --run
oscan tests/test_html.osc --run
```

### File Structure

```
oscanweb/
├── browser.osc          # Main application (rendering, chrome, vim keys, navigation)
├── url.osc              # URL parsing and resolution
├── http.osc             # HTTP/HTTPS client (built-in TLS)
├── html.osc             # HTML tokenizer and DOM builder
├── build.ps1            # Build script
├── README.md            # This file
├── requirements.md      # Original requirements
├── libs/
│   └── ui.osc           # UI widget library (buttons, textbox, etc.)
└── tests/
    ├── test_url.osc     # URL parser tests
    ├── test_html.osc    # HTML parser tests
    └── run_tests.ps1    # Test runner
```

## Limitations

- **No JavaScript execution** — pages are rendered as static HTML
- **No CSS layout** — basic structural rendering only
- **8×8 monospace bitmap font** — single fixed-width font
- **No image rendering** — images display as `[IMG: alt text]` placeholders
- **No cookies or session management**
- **No form submission**
- **Single-threaded** — synchronous page fetching

## Design Philosophy

- **SDI (Single Document Interface)** — one window, no tabs, maximum simplicity
- **Keyboard-first** — Vim-inspired navigation means your hands never leave the home row
- **Oscan showcase** — demonstrates the language's ability to build real applications, including C interop
- **Minimal dependencies** — HTTP-only mode needs nothing but the Oscan compiler; HTTPS adds only OpenSSL

## Built With

- **[Oscan](https://github.com/lucabol/Oscan)** — Minimalist language designed for LLM code generation, compiling to C99
- **[Vimium](https://github.com/philc/vimium)** — Inspiration for the keyboard shortcut scheme

## License

This project is licensed under the MIT License.
