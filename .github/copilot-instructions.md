# Copilot Instructions for OscaWeb

## Build & Test

```powershell
.\build.ps1              # Build (requires oscan in PATH)
.\build.ps1 -Run         # Build and run
.\build.ps1 -V           # Verbose (shows oscan/clang output)
.\build.ps1 -Test        # Run all tests
.\build.ps1 -Clean       # Remove build/

# Run a single test
oscan tests/test_url.osc --run
oscan tests/test_html.osc --run
```

## Language: Oscan

This project is written in [Oscan](https://github.com/lucabol/Oscan), a minimalist language compiling to freestanding C99. Refer to the Oscan repo's `.github/instructions/oscan.instructions.md` for the full language reference. Critical reminders:

- `fn` = pure (no I/O), `fn!` = impure. Pure functions cannot call impure ones.
- Use `and`/`or`/`not` instead of `&&`/`||`/`!`.
- All variables require explicit type annotations: `let x: i32 = 5;`
- Semicolons after **every** statement including block-statements: `if {} else {};` `while {};` `for {};`
- Last expression without `;` is the return value.
- String comparison: `str_eq(a, b)`, not `==`.
- String indexing: use `str_to_chars(s)` to get `[i32]`, then index that array.
- No self-referential structs. Use flat arrays with integer indices (see DOM design below).
- Oscan's anti-shadowing rule: a variable name cannot be reused in the same or nested scope, even in different `for` loops within the same function.

## Architecture

`browser.osc` is the main entry point and imports all other modules via `use`. The data flow is:

```
User input → url.osc (parse URL) → http.osc (fetch via TCP/TLS)
  → html.osc (tokenize + build DOM) → js.osc (run inline <script>s)
  → css.osc (parse <style>/inline, match, cascade)
  → browser.osc (render + handle input)
```

### Flat DOM (html.osc)

Oscan doesn't support self-referential structs (`children: [HtmlNode]` fails). The DOM uses a **flat array** with `first_child`/`next_sibling` integer indices:

```oscan
struct HtmlNode {
    tag: str, text: str, attrs: map,
    first_child: i32,    // index into nodes array, -1 = none
    next_sibling: i32,   // index into nodes array, -1 = none
    is_text: bool,
}
```

`html_parse(nodes, source)` takes a mutable `[HtmlNode]` array and returns the root index. To walk children: `let mut child: i32 = node.first_child; while child != -1 { ... child = nodes[child].next_sibling; };`

### Rendering (browser.osc)

The renderer walks the flat DOM tree recursively via `render_node()`. It uses `[i32]` single-element arrays as mutable position pointers (`x_ptr`, `y_ptr`) since Oscan has value semantics for structs.

`render_node` takes additional style parameters (`strikethrough`, `force_underline`, `mark_bg`, `ol_counter`, `styles`) that propagate through the tree. When adding new tag support, extend the if/else chain in the "Determine child properties" section and pass new values to children.

### CSS (css.osc)

`css.osc` provides a minimal cascade engine. After `html_parse` (and after `js_run_scripts` so JS-added nodes/classes participate), the browser:

1. Walks the DOM collecting `<style>` text content via `css_collect_style_texts`.
2. Parses each stylesheet with `css_parse(rules, source, order)` into `[CssRule]`.
3. Calls `css_compute_all(nodes, root, rules, styles)` to produce a `[ComputedStyle]` array parallel to `dom_nodes`.

Supported selectors: `tag`, `.class`, `#id`, `*`, compounds (`h1.title`), and comma-separated selector lists. Combinators, pseudo-classes, attribute selectors, and `@media` queries are **not** matched — rules that contain them are parsed and skipped so they never "accidentally" match too broadly.

Supported properties: `color`, `background-color`/`background`, `font-weight`, `font-style`, `text-decoration`, `display: none`. Inline `style=""` beats stylesheet rules; `!important` beats non-`!important`. `color`, `font-weight`, `font-style`, `text-decoration` inherit.

`render_node` consults `styles[idx]` to: return early on `display_none`; override `child_color` with `cs.color` (falling back to `BOLD_COLOR` / `EM_COLOR` for font-weight / font-style when `color` is unset); set `child_uline`, `child_strike`, `child_markbg` from the computed style. Tag defaults still apply when no CSS matches.

**Supported tags with specific rendering:**
- **Text styling:** `b`/`strong`, `em`/`i`/`cite`, `del`/`s` (strikethrough), `u`/`ins` (underline), `mark` (yellow background), `code`/`pre`
- **Structure:** `h1`–`h6`, `p`, `div`, `blockquote` (indented with accent bar), `hr`, `br`
- **Lists:** `ul` (bullets), `ol` (numbered), `li`, `dl`/`dt`/`dd`
- **Links & images:** `a` (clickable + underlined), `img` (fetched, decoded, cached, scaled; SVG via `svg_load`, raster via `img_load`)
- **Tables:** `table`/`tr`/`td`/`th` (column-aligned with borders; handles `thead`/`tbody`/`tfoot` wrappers)
- **Semantic blocks:** `section`, `article`, `nav`, `header`, `footer`, `main`, `figure`/`figcaption`
- **Skipped:** `head`, `script`, `style`, `meta`, `link`, `title`, `noscript`

Key data collected during rendering:
- `page_links: [LinkInfo]` — clickable link hit areas (one per `<a>` element, not per word)
- `word_map: [WordInfo]` — position of every rendered word (for text selection)

### Image pipeline

Images are fetched via HTTP, decoded with `img_load()` for raster formats or `svg_load()` for SVG, and the pixel header is stripped before caching. SVG is detected by `.svg` file extension or `image/svg+xml` content type and rasterized at up to 800px wide. Cached pixel arrays are drawn with `gfx_blit()` at native resolution. Images wider than 1000px are downscaled via nearest-neighbor at cache time.

## Key Conventions

### No `arena { }` blocks in the main loop

Oscan's `arena { }` frees child-arena allocations on exit. Any `push()` to a long-lived array inside an arena block can corrupt data when the array's backing buffer reallocates onto the child arena. All `push()` calls to persistent state arrays (`url_buf`, `dom_nodes`, `search_buf`, etc.) must happen **outside** arena blocks.

### Mutable array clearing

Never reassign arrays inside loops: `arr = [];` allocates a new array that may be freed. Instead clear in-place: `while len(arr) > 0 { pop(arr); };`

### Keyboard input handling outside rendering

Keyboard input that modifies persistent arrays (`url_buf`, `search_buf`, `follow_input`) is processed **before** the render section, outside any arena scope, to avoid the realloc-on-child-arena bug.

### Ctrl+key codes

`canvas_key()` reports Ctrl+letter as key codes 1–26 (Ctrl+A=1, Ctrl+C=3, Ctrl+V=22).

### Test pattern

Tests use `assert_eq_str`/`assert_eq_i32`/`assert_true` helpers that print `PASS:`/`FAIL:` lines. Test files import modules with relative `use "../module.osc"`.
