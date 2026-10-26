# NLS Settings

All settings live under the `nax` namespace in your VS Code `settings.json`.

## Inlay Hints

| Setting | Type | Default | Description |
|---|---|---|---|
| `nax.inlayHints.typeHints` | `boolean` | `false` | Show inferred types after variable declarations (e.g. `var x = 42` shows `: i64`). |
| `nax.inlayHints.parameterHints` | `boolean` | `false` | Show parameter names at call sites (e.g. `greet("world")` shows `name:`). |

## Analysis

| Setting | Type | Default | Description |
|---|---|---|---|
| `nax.analysis.debounceMs` | `number` | `50` | Milliseconds to wait after an edit before re-analysing the file. Increase on slower machines to reduce CPU usage. |

## Diagnostics

| Setting | Type | Default | Description |
|---|---|---|---|
| `nls.trace.server` | `"off"` \| `"messages"` \| `"verbose"` | `"off"` | Traces communication between VS Code and the language server. Useful for debugging. |

## Example

```jsonc
// .vscode/settings.json
{
  "nax.inlayHints.typeHints": true,
  "nax.inlayHints.parameterHints": true,
  "nax.analysis.debounceMs": 100
}
```
