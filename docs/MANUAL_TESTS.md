# OscaWeb — Manual Test Plan

After a build, these short smoke tests exercise the higher-impact features
end-to-end. All can be run against a local Python server
(`python -m http.server 8000`) or against the real Internet.

## 1. HTTP/1.1 + chunked transfer-encoding

```powershell
.\build\browser.exe https://www.wikipedia.org
```

**Expect:** page renders without garbage characters at the end or a hang.
Wikipedia streams responses as `Transfer-Encoding: chunked` without
`Content-Length`.

## 2. External `<script src>`

Create `tests/scratch_script.html`:

```html
<p id="target">old</p>
<script src="scratch_external.js"></script>
```

and `tests/scratch_external.js`:

```js
document.getElementById("target").textContent = "loaded externally";
```

Serve with `python -m http.server 8000` in `tests/`, then:

```powershell
.\build\browser.exe http://localhost:8000/scratch_script.html
```

**Expect:** page text reads `loaded externally`.

## 3. Checkbox / radio / select via `gf`

Save as `tests/scratch_form.html`:

```html
<form method="get" action="/scratch_form.html">
  <input name="agree" type="checkbox"> agree
  <input name="tier" type="radio" value="pro"> pro
  <input name="tier" type="radio" value="free"> free
  <select name="country">
    <option value="us">US</option>
    <option value="jp">JP</option>
  </select>
  <button>Submit</button>
</form>
```

`gf`, at each prompt type `y`, then `pro`, then `jp`, Enter.

**Expect:** the URL becomes `...?agree=on&tier=pro&country=jp` after submit.
The status-bar prompt shows `[y/n]` next to the first field and `[select]`
next to the dropdown.

## 4. CSS attribute + child selectors

`tests/scratch_css.html`:

```html
<style>
  a[href^="https://"] { color: #00ff00; }
  a[href$=".pdf"]     { color: #ffff00; }
  div > p             { color: #00ffff; }
  div p               { color: #ff00ff; }
</style>
<div><p>child</p><section><p>grandchild</p></section></div>
<a href="https://example.com/file.pdf">both</a>
```

**Expect:** "child" paragraph renders cyan (`div > p`), "grandchild"
paragraph renders magenta (`div p` only), and "both" link renders yellow
(`$=".pdf"` beats `^="https://"` because it is later in source order at
equal specificity).

## 5. JS DOM APIs (`querySelector`, `classList`)

`tests/scratch_js_dom.html`:

```html
<ul id="list">
  <li class="item">one</li>
  <li class="item selected">two</li>
  <li class="item">three</li>
</ul>
<script>
  var sel = document.querySelectorAll("#list .item");
  for (var i = 0; i < sel.length; i++)
    sel[i].classList.add("seen");
  document.querySelector(".selected").textContent = "picked";
</script>
```

**Expect:** second `<li>` shows `picked`; internally all three `<li>`
elements now carry `class="item seen"` (or `item selected seen`).

## 6. Persistent HTTP cache

Fresh launch:

```powershell
.\build\browser.exe https://en.wikipedia.org/wiki/Oscar_Wilde
```

Note the page load time. Quit with `Q`, then relaunch the same command.

**Expect:** the page appears instantly (cached). The file
`%APPDATA%\oscaweb_cache.txt` exists and contains `U:`/`C:`/`S:`/`B:`
lines. Press `r` to force a refetch.
