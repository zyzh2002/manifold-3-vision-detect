---
name: psdk-deepwiki-research
description: Use when researching DJI Payload SDK APIs, platform behavior, samples, architecture, or implementation constraints through the DeepWiki MCP server.
---

# PSDK DeepWiki Research

Research `dji-sdk/Payload-SDK` from primary documentation context before using generated answers.

## Required Workflow

1. Decide whether to perform the research directly or delegate it to a subagent. If delegating, ensure the subagent can access the DeepWiki MCP tools and require it to follow this workflow. Do not delegate when MCP availability is unknown.

2. Read the wiki structure first:

   Use the available DeepWiki tool for reading the wiki structure of `dji-sdk/Payload-SDK`. Tool names may vary between MCP server mappings; select the tool by capability rather than by an exact name.

3. Pull and validate the complete wiki through the cache client:

   Use the local client as the authoritative full-content acquisition path. It initializes a
   standard MCP Streamable HTTP session, discovers the DeepWiki tools, fetches both structure
   and contents, compares their page sets, and replaces the cache only after validation:

   ```
   python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py pull --force
   ```

   This direct client uses the same DeepWiki MCP service. It is not a browser, web fetch, search
   engine, or substitute documentation source. If the pull or validation fails, keep the old
   cache only as stale data and stop the current research workflow.

4. Verify the cache and read every relevant chapter completely:

   Inspect provenance and the validated chapter list:

   ```
   python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py info --json
   python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py ls --json
   ```

   Use search only to identify relevant chapters, then read each selected chapter from beginning
   to end:

   ```
   python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py search "ApiName" --json
   python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py get "Exact Chapter Name"
   ```

   If `get` output is truncated by the host, use the chapter path from `ls --json` and read that
   file in contiguous line ranges until all indexed lines are covered. Record the exact chapter
   names read. Cache creation and search matches are not evidence that a chapter was read.

5. If the selected agent cannot read the structure through the host MCP tool, cannot pull and
   validate the complete wiki through the direct MCP client, or cannot read every relevant cached
   chapter completely, stop the workflow and report the limitation. Do not use a browser, web
   fetch, search engine, DeepWiki CLI, or another documentation transport as a substitute.

6. After successfully reading the DeepWiki content, cross-check conclusions against repository headers and samples under `third_party/psdk/`. For version-specific facts, local PSDK 3.16.0 headers and samples take precedence over upstream DeepWiki content. Prefer exact declarations and sample call sequences for API signatures, enum values, ordering constraints, callbacks, and platform support. Report unresolved differences as version discrepancies instead of combining them into a guessed conclusion.

7. Use `ask_question` only after the full wiki contents and local primary sources have been read, and only to resolve a specific remaining ambiguity. Treat `ask_question` output as a secondary interpretation, not as the sole source of a factual claim.

   Use the available DeepWiki tool that provides focused question answering for `dji-sdk/Payload-SDK`.

   Note: Multi-repository `ask_question` is currently unreliable; use single-repo calls only.

## Common Mistakes

- Calling `ask_question` immediately after `read_wiki_structure`.
- Delegating without confirming that the subagent can access DeepWiki through MCP.
- Calling the host full-wiki tool and treating a truncated presentation as complete evidence.
- Running `pull` without `--force` and assuming an existing cache was refreshed.
- Treating cache creation, `search` results, or a content hash as proof that chapters were read.
- Reusing an old cache without validating it against a successful current pull.
- Falling back to browser or web-fetch tools when DeepWiki MCP access is unavailable.
- Treating an `ask_question` response as primary documentation.
- Reading only search snippets or a truncated wiki response.
- Guessing behavior that is absent from both the wiki and local SDK sources.
- Generalizing a sample's aircraft-specific limitation to every supported platform.

For cache metadata and error semantics, see `references/cache-format.md`.