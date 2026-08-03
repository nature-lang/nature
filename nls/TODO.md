# NLS TODO

## Semantic Coloring

- [ ] **Semantic token modifiers** — Add modifiers: `declaration`, `readonly`, `deprecated`, `static`, `defaultLibrary`
- [ ] **Unused symbol detection + dimming** — Track read counts per symbol in the symbol table; report unused imports/variables/functions/constants/types as `HINT` + `UNNECESSARY` diagnostics so the editor greys them out. Needs symbol table changes.

## Refactoring

- [ ] **Rename** — Rename a symbol across the entire project (with prepare-rename validation)
- [ ] **Import alias resolution** — Fix function aliasing in imports (e.g. `import 'foo.n'.{add as sum}`) so alias + symbol kinds resolve and color correctly

## Advanced

- [ ] **Call hierarchy** — Incoming/outgoing call trees
- [ ] **Formatting** — Format `.n` files
- [ ] **Code lens** — Show reference counts, test run buttons
