---
name: psdk-deepwiki-research
description: Use when researching DJI Payload SDK APIs, platform behavior, samples, architecture, or implementation constraints through the DeepWiki MCP server.
---

# PSDK DeepWiki Research

Research `dji-sdk/Payload-SDK` from primary documentation context before using generated answers.

## Required Workflow

1. Quick symbol lookup via the local API index:

   Before opening DeepWiki or searching headers, use the auto-generated local API index to
   locate the right header file and symbol name fast:

   ```
   # Grep for a function/struct/enum name
   grep "SymbolName" .agents/skills/psdk-deepwiki-research/references/api-index.md

   # Or read the full index for module discovery
   ```

   The index contains 5 sections:
   - Quick Reference by Domain — find the right header by functional area
   - Header-by-Header API Map — per-header function and type listings (core headers detailed, rest summarized)
   - PSDK Initialization Sequence — standard call order
   - Platform Porting Checklist — Manifold 3 HAL/OSAL requirements
   - Flat Symbol Index — alphabetical symbol → header mapping (grep-friendly)

   **The index is a navigation aid, not primary evidence.** Always verify symbols,
   signatures, and values against the actual header under `third_party/psdk/psdk_lib/include/`
   before using them in code.

To regenerate after a PSDK version change:
    ```
    python3 .agents/skills/psdk-deepwiki-research/scripts/generate-api-index.py
    ```

    Maintenance guide: `references/api-index-maintenance.md`

2. Perform this workflow in the main conversation. Do **not** delegate unless a subagent explicitly documents both (a) DeepWiki MCP access and (b) the ability to run `wiki-cache.py` and read the resulting cache files. In practice, most subagent types in this project lack (a) and (b); default to no delegation.

3. Read the wiki structure first:

   Use the available DeepWiki tool for reading the wiki structure of `dji-sdk/Payload-SDK`. Tool names may vary between MCP server mappings; select the tool by capability rather than by an exact name.

4. Pull and validate the complete wiki through the cache client:

   Use the local client as the authoritative full-content acquisition path. It initializes a
   standard MCP Streamable HTTP session, discovers the DeepWiki tools, fetches both structure
   and contents, compares their page sets, and replaces the cache only after validation:

   ```
   python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py pull --force
   ```

   `--force` is mandatory. A non-forced `pull` that finds an existing cache exits `2`
   **without** contacting the MCP server, which means freshness has not been validated.
   Never interpret exit code 2 as success. See `references/cache-format.md` for exit
   codes.

   This direct client uses the same DeepWiki MCP service. It is not a browser, web fetch, search
   engine, or substitute documentation source. If the pull or validation fails, keep the old
   cache only as stale data and stop the current research workflow.

5. Verify the cache and read every relevant chapter completely:

   a. Inspect provenance and the validated chapter list:

   ```
   python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py info --json
   python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py ls --json
   ```

   b. **Always** obtain the exact chapter name from `ls --json` before calling `get`.
   Do not construct chapter names from slugs, URLs, or memory — names may contain
   parentheses, punctuation, or CJK characters that require exact matching.

   c. Use `search` only to locate candidate chapters, then feed the corresponding
   `name` field from `ls --json` into `get`:

   ```
   python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py search "ApiName" --json
   python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py get "Exact Chapter Name From ls --json"
   ```

   d. If `get` output is truncated by the host, use the chapter file path from
   `ls --json` and read that file in contiguous line ranges until all indexed lines
   are covered.

   e. Record the exact chapter names read. Cache creation and search matches are not
   evidence that a chapter was read.

6. If the selected agent cannot read the structure through the host MCP tool, cannot pull and
   validate the complete wiki through the direct MCP client, or cannot read every relevant cached
   chapter completely, stop the workflow and report the limitation. Do not use a browser, web
   fetch, search engine, DeepWiki CLI, or another documentation transport as a substitute.

7. After successfully reading the DeepWiki content, cross-check conclusions against repository headers and samples under `third_party/psdk/`. For version-specific facts, local PSDK 3.16.0 headers and samples take precedence over upstream DeepWiki content. Prefer exact declarations and sample call sequences for API signatures, enum values, ordering constraints, callbacks, and platform support. Report unresolved differences as version discrepancies instead of combining them into a guessed conclusion.

8. Use `ask_question` only after the full wiki contents and local primary sources have been read, and only to resolve a specific remaining ambiguity. Treat `ask_question` output as a secondary interpretation, not as the sole source of a factual claim.

   Use the available DeepWiki tool that provides focused question answering for `dji-sdk/Payload-SDK`.

   Note: Multi-repository `ask_question` is currently unreliable; use single-repo calls only.

## Common Mistakes

- Calling `ask_question` immediately after `read_wiki_structure`.
- Delegating without confirming that the subagent can access DeepWiki through MCP.
- Calling the host full-wiki tool and treating a truncated presentation as complete evidence.
- Running `pull` without `--force` and assuming an existing cache was refreshed.
- Interpreting `wiki-cache.py pull` exit code 2 (cache exists, no fetch) as a successful validation.
- Passing a slug or URL fragment to `get` instead of the exact `name` from `ls --json`.
- Treating cache creation, `search` results, or a content hash as proof that chapters were read.
- Reusing an old cache without validating it against a successful current pull.
- Falling back to browser or web-fetch tools when DeepWiki MCP access is unavailable.
- Treating an `ask_question` response as primary documentation.
- Reading only search snippets or a truncated wiki response.
- Guessing behavior that is absent from both the wiki and local SDK sources.
- Generalizing a sample's aircraft-specific limitation to every supported platform.

For cache metadata and error semantics, see `references/cache-format.md`.