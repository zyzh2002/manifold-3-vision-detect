# PSDK API Index Generator — Maintenance Guide

## 1. Overview

`generate-api-index.py` parses PSDK C headers under `third_party/psdk/psdk_lib/include/`
and produces a deterministic Markdown API index at `references/api-index.md`. The index is
the first step of the PSDK DeepWiki research workflow — agents use it to locate the right
header and symbol name before opening DeepWiki or reading headers directly.

The index is a **navigation aid, not primary evidence**. Agents must always verify symbols
and signatures against the actual header files before using them in code.

The generator is:
- **Zero-dependency** — pure Python 3 standard library.
- **Deterministic** — two runs on the same headers produce identical output (SHA-256).
- **Verified** — cross-checked against Clang's AST for PSDK 3.16.0 with 100% accuracy
  (398/398 functions, 225 structs, 1 union, 172 enums, 33 callback typedefs).

## 2. Quick Reference

```bash
# Generate the API index
python3 .agents/skills/psdk-deepwiki-research/scripts/generate-api-index.py

# Run all tests
python3 -m unittest discover -s .agents/skills/psdk-deepwiki-research/tests -v

# Run only API index generator tests
cd .agents/skills/psdk-deepwiki-research/tests && python3 -m unittest test_generate_api_index -v
```

| Item | Location |
|------|----------|
| Generator script | `scripts/generate-api-index.py` |
| Generated index | `references/api-index.md` |
| Unit tests | `tests/test_generate_api_index.py` |
| PSDK headers | `third_party/psdk/psdk_lib/include/` |

**When to regenerate:** after any PSDK submodule update, or after modifying any static data
table in the generator script.

## 3. Architecture

The generator is a four-stage pipeline:

```
Parse headers  →  Validate metadata  →  Generate Markdown  →  Atomic write
```

### 3.1 Parsing (lines ~220–548)

A regex-based C header parser extracts declarations after stripping comments and preprocessor
directives.

| Extractor | Output |
|-----------|--------|
| `extract_functions()` | Public `Dji*` function declarations |
| `extract_structs()` | `typedef struct` definitions with field counts |
| `extract_unions()` | `typedef union` definitions with field counts |
| `extract_enums()` | `typedef enum` and named `enum` definitions with value counts |
| `extract_callback_typedefs()` | Function pointer typedef names |
| `extract_type_aliases()` | Simple `typedef` aliases (not struct/enum/callback) |

Balanced brace matching (`_find_matching_delimiter`) handles nested structs and enums.
`_count_aggregate_fields()` counts top-level fields without double-counting nested
anonymous aggregates.

### 3.2 Validation (lines ~566–596)

`validate_static_metadata()` runs before generation and aborts on any inconsistency:

- Every header has a summary in `HEADER_SUMMARIES`.
- Every header is covered by `DOMAIN_GROUPS`.
- `CORE_HEADERS` entries are still present in the include directory.
- `SAMPLE_CROSS_REF` paths resolve to actual files.
- No header appears in more than one domain group.

### 3.3 Markdown Generation (lines ~599–892)

Five sections written by separate generator functions:

| Section | Function | Content |
|---------|----------|---------|
| Document header | `generate_doc_header()` | Title, stats, table of contents |
| Quick Reference | `generate_quick_reference()` | Headers grouped by functional domain |
| Header Map | `generate_header_map()` | Core headers detailed, rest summarized in a table |
| Init Sequence | `generate_init_sequence()` | Manually curated call-order table |
| Platform Checklist | `generate_platform_checklist()` | Handler structs and function pointer signatures |
| Symbol Index | `generate_symbol_index()` | Alphabetical symbol-to-header mapping |

`generate_init_sequence()` and `generate_platform_checklist()` contain manually curated
domain knowledge that cannot be extracted from headers. The platform checklist handler
counts and function pointer signatures are auto-derived from `dji_platform.h` struct
definitions.

### 3.4 Atomic Write (lines ~895–922)

`write_text_atomic()` writes to a temp file, flushes + fsyncs, then `os.replace()`. If
the target file exists, its permissions are preserved. On failure, the temp file is
cleaned up and the original file is untouched.

### 3.5 Design Decisions

**Why regex instead of a real C parser (libclang, tree-sitter, etc.)?**

- Zero external dependencies — runs on any system with Python 3.
- Two orders of magnitude faster than launching a full C frontend.
- PSDK headers use a conservative, predictable C99 subset. The parser covers every
  declaration pattern that DJI actually uses in 3.16.0.
- Deterministic output is easy to verify (SHA-256 comparison).

**Why not an LLM-based subagent?**

- Symbol extraction accuracy is the primary requirement. Clang AST cross-validation
  proves the regex parser is 100% accurate for PSDK 3.16.0. An LLM cannot match this
  guarantee.
- LLM output is non-deterministic and cannot be systematically verified against a
  compiler baseline.
- The index is a navigation tool — a wrong symbol-to-header mapping wastes agent time.

### 3.6 Known Limitations

- **Conditional compilation is not tracked.** Nearly all PSDK functions are inside
  `#if`/`#ifdef` blocks. The parser strips all preprocessor directives, so the index
  lists every API regardless of the target platform. An agent compiling for x86_64
  will not find `__aarch64__`-gated functions in its build.
- **No semantic analysis.** The parser extracts declarations but does not understand
  Doxygen comments, `@note` semantics, or call ordering constraints.
- **Only DJI's actual C subset is supported.** The parser handles `__attribute__((packed))`,
  macro-based enum values, and multi-line declarations. It does not handle `_Generic`,
  `alignas`, or C11/C23 features that DJI does not currently use.

## 4. Static Data Maintenance

The generator has four manually curated data tables. All are validated by
`validate_static_metadata()` at generation time.

### 4.1 `CORE_HEADERS` (line 50)

Headers that receive a detailed per-function listing in Section 2.1 of the index.
All other headers appear in a summary table in Section 2.2.

**When to add a header:** it is central to the vision-detect application (liveview,
camera, FC subscription, etc.) and agents routinely need to browse its function list.

**When to remove:** the header is no longer relevant to the project's feature set.

### 4.2 `DOMAIN_GROUPS` (line 67)

Functional grouping for Section 1 (Quick Reference). Each header must appear in
exactly one group.

**Grouping principle:** group by what the module controls (e.g., "Camera Control",
"Flight Controller"), not by code structure. The "Other Modules" group is the
catch-all for headers that do not fit existing categories.

**Validation catches:** missing headers, stale headers, and duplicates across groups.

### 4.3 `HEADER_SUMMARIES` (line 138)

One-sentence human-written descriptions. These are the primary brief text shown in
the index. The `@brief` extraction from Doxygen comments is only a fallback.

**Writing guidelines:**
- One sentence, under ~120 characters.
- Focus on what the module does, not how it is implemented.
- Mention key function names or API groups if helpful for search.
- Include platform notes when relevant (e.g., "Manifold 3 only").

**Validation catches:** missing or stale header entries. It does not check content
quality — verify manually after a PSDK major version upgrade.

### 4.4 `SAMPLE_CROSS_REF` (line 182)

Maps headers to PSDK sample code files under `third_party/psdk/samples/sample_c/`.
Paths are relative to `samples/sample_c/`.

**How to find sample files for a new header:**
1. Look under `third_party/psdk/samples/sample_c/module_sample/` for a directory
   named after the module.
2. Each module typically has one `test_<module>.c` file.
3. Some modules share a sample file (e.g., `dji_widget.h` and `dji_widget_manager.h`
   both use `test_widget.c`).

**Validation catches:** missing sample files on disk and stale mappings.

### 4.5 Common Validation Errors

| Error message | Fix |
|---------------|-----|
| `header summaries missing: ['dji_new.h']` | Add an entry to `HEADER_SUMMARIES` |
| `header summaries stale: ['dji_old.h']` | Remove the entry (header was deleted) |
| `domain groups missing: ['dji_new.h']` | Add the header to a `DOMAIN_GROUPS` group |
| `domain groups stale: ['dji_old.h']` | Remove the header from `DOMAIN_GROUPS` |
| `domain group duplicates: ['dji_x.h']` | Remove the duplicate entry |
| `core headers stale: ['dji_old.h']` | Remove from `CORE_HEADERS` |
| `sample mappings stale: ['dji_old.h']` | Remove from `SAMPLE_CROSS_REF` |
| `sample references missing: [...]` | Fix the path or remove the entry |

## 5. PSDK Upgrade Playbook

When the PSDK submodule is updated to a new version:

### Step 1 — Update the submodule

```bash
git submodule update --remote third_party/psdk
```

### Step 2 — Run the generator and read the errors

```bash
python3 .agents/skills/psdk-deepwiki-research/scripts/generate-api-index.py
```

`validate_static_metadata()` will report any structural changes (new/removed headers,
broken sample paths). The generator will not produce output until all errors are fixed.

### Step 3 — Update static data

Follow the error messages to update `HEADER_SUMMARIES`, `DOMAIN_GROUPS`, `CORE_HEADERS`,
and `SAMPLE_CROSS_REF`. See Section 4.5 for common fixes.

For new headers, read the Doxygen `@brief` in the header to write a one-line summary.
Check `third_party/psdk/samples/sample_c/module_sample/` for sample code.

### Step 4 — Run unit tests

```bash
cd .agents/skills/psdk-deepwiki-research/tests && python3 -m unittest test_generate_api_index -v
```

All 18 tests must pass. If any fail, the parser may need adjustment for new C syntax
patterns introduced in the upgraded PSDK.

### Step 5 — Clang AST cross-validation (major version upgrades only)

Verify that the regex parser output matches the real C compiler's understanding:

```bash
# Extract function declarations from a header via Clang AST
clang -Xclang -ast-dump -fsyntax-only \
  -D__linux__ -D__aarch64__ \
  -I third_party/psdk/psdk_lib/include/ \
  third_party/psdk/psdk_lib/include/dji_liveview.h 2>&1 \
  | grep -oP 'FunctionDecl.*?Dji\w+' | sort

# Compare with the generated index
grep -oP '`Dji\w+` \(fn\)' references/api-index.md | sed 's/[`()]//g; s/ fn//' | sort
```

Repeat for all headers with significant API surface. The function count must match
exactly (modulo `__aarch64__`-conditional functions, which Clang will include but
are not in the script's non-conditional output).

### Step 6 — Manual review

- Read the initialization sequence table in `generate_init_sequence()`. Check
  `dji_core.h` for any `@note` changes to the lifecycle contract.
- Review the platform checklist in `generate_platform_checklist()`. Handler counts
  and signatures are auto-derived, but the required/optional classification is
  manual.
- Skim `HEADER_SUMMARIES` descriptions for modules whose functionality changed
  significantly.

### Step 7 — Commit

```bash
git add .agents/skills/psdk-deepwiki-research/references/api-index.md
git add .agents/skills/psdk-deepwiki-research/scripts/generate-api-index.py  # if modified
git commit -m "chore: regenerate PSDK API index for vX.Y.Z"
```

## 6. Testing & Validation

### 6.1 Unit Tests

```bash
# Run all skill tests (18 API index + 21 wiki-cache)
python3 -m unittest discover -s .agents/skills/psdk-deepwiki-research/tests -v

# Run only API index generator tests
cd .agents/skills/psdk-deepwiki-research/tests && python3 -m unittest test_generate_api_index -v
```

**Test coverage (18 tests across 2 classes):**

| Class | Tests | Covers |
|-------|-------|--------|
| `ApiIndexParserTestCase` | 8 | Preprocessor stripping, packed structs, enum comma counting, multi-typedef enums, pointer-return functions, callback typedef extraction, named enums, non-Dji type aliases |
| `ApiIndexRepositoryTestCase` | 10 | Dynamic version reading, static metadata validation against real headers, regression cases (liveview/platform/subscription/error), sample cross-reference existence, init sequence contract, platform checklist derivation, atomic write (preserve on failure, preserve permissions, create mode), error handling |

### 6.2 Deterministic Output

The generator is fully deterministic — two runs produce identical output.

```bash
python3 .agents/skills/psdk-deepwiki-research/scripts/generate-api-index.py
sha256sum .agents/skills/psdk-deepwiki-research/references/api-index.md

python3 .agents/skills/psdk-deepwiki-research/scripts/generate-api-index.py
sha256sum .agents/skills/psdk-deepwiki-research/references/api-index.md
# Must match
```

### 6.3 Clang AST Cross-Validation

For major PSDK version upgrades, cross-check the parser output against Clang's AST.

**Prerequisites:** `clang` must be installed on the system.

**Function count verification per header:**

```bash
# Dump AST and extract FunctionDecl nodes
clang -Xclang -ast-dump -fsyntax-only \
  -D__linux__ -D__aarch64__ \
  -I third_party/psdk/psdk_lib/include/ \
  third_party/psdk/psdk_lib/include/dji_liveview.h 2>&1 \
  | grep 'FunctionDecl' | wc -l

# Count in generated index
grep -c '`Dji' references/api-index.md
```

**Struct field count verification:**

```bash
# Dump RecordDecl nodes and count FieldDecl children
clang -Xclang -ast-dump -fsyntax-only \
  -D__linux__ -D__aarch64__ \
  -I third_party/psdk/psdk_lib/include/ \
  third_party/psdk/psdk_lib/include/dji_platform.h 2>&1 \
  | grep -c 'FieldDecl'
```

**Known acceptable difference:** Clang includes `__aarch64__`-conditional Network RTK
functions (3 in PSDK 3.16.0) that the regex parser also extracts (since it strips all
`#if` guards). The counts should match exactly for PSDK 3.16.0.

### 6.4 When to Run Each Check

| Check | When |
|-------|------|
| Unit tests | Every change to the generator script |
| Deterministic output | After any parser change |
| Clang AST cross-validation | Major PSDK version upgrade; after parser rewrite |
| Static metadata validation | Runs automatically on every generation |