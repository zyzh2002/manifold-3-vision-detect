# DeepWiki Cache Contract

## Acquisition

`wiki-cache.py pull --force` is the full-content acquisition path. It performs:

1. MCP `initialize` and `notifications/initialized`.
2. Tool discovery with `tools/list`.
3. Structure and full-content tool calls for one repository.
4. Page-set comparison between structure and contents.
5. Fence-aware Markdown page parsing.
6. Atomic cache replacement after all validation succeeds.

The client supports JSON and Streamable HTTP SSE responses. It selects the JSON-RPC response that
matches the request ID, preserves MCP session and protocol headers, limits each response to 32 MiB,
and treats tool-level `isError` results as failures.

## Index

`.cache/index.json` includes:

- `format_version`
- `source`, `endpoint`, and `repo`
- `protocol_version` and `server_info`
- `cached_at`
- `content_sha256` and `structure_sha256`
- `content_size_bytes`
- `expected_pages`
- `chapter_count`
- Per-chapter ordinal, exact name, relative file path, line count, start line, and SHA-256

Chapter filenames contain an ordinal, slug, and content hash. Names that sanitize to the same slug
therefore remain distinct.

## Evidence Rules

- A successful pull proves that the cached page set matched the current structure response.
- A content hash identifies cache provenance; it does not prove that an agent read the content.
- `search --json` locates candidate chapters; it does not replace full chapter reading.
- A chapter is complete only after its indexed line range has been read without gaps.
- If a pull fails, the previous cache remains intact but is stale and cannot support the current
  research claim.

## Exit Behavior

- Pull success: exit `0`.
- Cache exists and `--force` was omitted: exit `2`; no network request is made.
- Protocol, validation, parsing, ambiguous lookup, or filesystem failure: exit `1`.
- Failed parsing or writing never replaces the previous valid cache.

## Storage

- Cache lives at `.agents/skills/psdk-deepwiki-research/.cache/`.
- Directory permissions are private (`0700`); files are agent-local artifacts.
- The cache directory is not part of the source tree and is excluded from git via the
  repository's top-level `.gitignore`.
- The cache may be deleted at any time; the next `pull --force` will rebuild it.
