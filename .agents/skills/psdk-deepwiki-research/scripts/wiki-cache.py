#!/usr/bin/env python3
"""Fetch, cache, and query DeepWiki documentation for dji-sdk/Payload-SDK."""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


SKILL_DIR = Path(__file__).resolve().parent.parent
CACHE_DIR = SKILL_DIR / ".cache"
CHAPTERS_DIR = CACHE_DIR / "chapters"
INDEX_FILE = CACHE_DIR / "index.json"

MCP_DEFAULT_ENDPOINT = "https://mcp.deepwiki.com/mcp"
MCP_DEFAULT_TIMEOUT = 60
MCP_DEFAULT_MAX_RESPONSE_BYTES = 32 * 1024 * 1024
MCP_PROTOCOL_VERSION = "2025-06-18"
FORMAT_VERSION = 2
SUPPORTED_PROTOCOL_VERSIONS = {MCP_PROTOCOL_VERSION}


class SessionExpiredError(RuntimeError):
    pass


class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file_pointer, code, message, headers, new_url):
        return None


def _sha256(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def _slugify(name):
    slug = re.sub(r"[^A-Za-z0-9._()-]+", "-", name).strip("-.")
    return slug or "chapter"


def _line_offsets(text):
    offsets = []
    offset = 0
    for line in text.splitlines(keepends=True):
        offsets.append((offset, line))
        offset += len(line)
    if not text or (text and not text.endswith(("\n", "\r"))):
        if not offsets:
            offsets.append((0, text))
    return offsets


def _scan_page_markers(text):
    markers = []
    fence_char = None
    fence_length = 0

    for line_number, (offset, line) in enumerate(_line_offsets(text), start=1):
        stripped = line.rstrip("\r\n")
        fence_match = re.match(r"^ {0,3}(`{3,}|~{3,})(.*)$", stripped)
        if fence_match:
            marker = fence_match.group(1)
            trailing = fence_match.group(2)
            if fence_char is None:
                fence_char = marker[0]
                fence_length = len(marker)
            elif (
                marker[0] == fence_char
                and len(marker) >= fence_length
                and trailing.strip() == ""
            ):
                fence_char = None
                fence_length = 0
            continue

        if fence_char is not None:
            continue

        page_match = re.match(r"^# Page:\s+(.+?)\s*$", stripped)
        if page_match:
            markers.append({
                "name": page_match.group(1),
                "start": offset,
                "content_start": offset + len(line),
                "line": line_number,
            })

    if fence_char is not None:
        raise ValueError("unclosed Markdown code fence in wiki output")
    if not markers:
        raise ValueError("no '# Page:' markers found in wiki output")

    names = [marker["name"] for marker in markers]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise ValueError(f"duplicate wiki pages: {', '.join(duplicates)}")

    return markers


def _split_chapters(text):
    markers = _scan_page_markers(text)
    chapters = []

    for index, marker in enumerate(markers):
        end = markers[index + 1]["start"] if index + 1 < len(markers) else len(text)
        content = text[marker["content_start"]:end].lstrip("\r\n").rstrip()
        if not content:
            raise ValueError(f"wiki page '{marker['name']}' is empty")
        content += "\n"
        chapters.append({
            "ordinal": index + 1,
            "name": marker["name"],
            "start_line": marker["line"],
            "content": content,
        })

    return chapters


def _parse_structure_page_names(text):
    names = []
    for line in text.splitlines():
        match = re.match(r"^\s*-\s+(?:\d+(?:\.\d+)*\s+)?(.+?)\s*$", line)
        if match:
            names.append(match.group(1))
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise ValueError(f"duplicate structure pages: {', '.join(duplicates)}")
    return names


def _write_cache_tree(cache_dir, text, source_meta, expected_pages=None):
    chapters = _split_chapters(text)
    actual_pages = [chapter["name"] for chapter in chapters]
    if expected_pages is not None:
        missing = [name for name in expected_pages if name not in actual_pages]
        unexpected = [name for name in actual_pages if name not in expected_pages]
        if missing or unexpected:
            raise ValueError(
                "wiki structure mismatch: "
                f"missing={missing or []}, unexpected={unexpected or []}"
            )
        if expected_pages != actual_pages:
            raise ValueError(
                "wiki structure page order does not match full contents: "
                f"expected={expected_pages}, actual={actual_pages}"
            )

    chapters_dir = cache_dir / "chapters"
    chapters_dir.mkdir(parents=True, exist_ok=True)
    chapter_entries = []

    for chapter in chapters:
        digest = _sha256(chapter["content"])
        filename = (
            f"{chapter['ordinal']:03d}-{_slugify(chapter['name'])}-{digest[:8]}.md"
        )
        filepath = chapters_dir / filename
        filepath.write_text(chapter["content"], encoding="utf-8")
        chapter_entries.append({
            "ordinal": chapter["ordinal"],
            "name": chapter["name"],
            "file": str(Path(".cache") / "chapters" / filename),
            "lines": chapter["content"].count("\n"),
            "sha256": digest,
            "start_line": chapter["start_line"],
        })

    index = {
        "format_version": FORMAT_VERSION,
        "source": "dji-sdk/Payload-SDK",
        "cached_at": datetime.now(timezone.utc).isoformat(),
        "content_sha256": _sha256(text),
        "content_size_bytes": len(text.encode("utf-8")),
        "chapter_count": len(chapter_entries),
        "chapters": chapter_entries,
    }
    index.update(source_meta)
    (cache_dir / "index.json").write_text(
        json.dumps(index, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    for chapter in chapter_entries:
        relative = Path(chapter["file"]).relative_to(".cache")
        if not (cache_dir / relative).is_file():
            raise OSError(f"cached chapter missing after write: {relative}")

    return index


def _replace_cache_atomically(staged_cache):
    backup = CACHE_DIR.with_name(f"{CACHE_DIR.name}.backup")
    if backup.exists() and not CACHE_DIR.exists():
        os.replace(backup, CACHE_DIR)
    elif backup.exists():
        shutil.rmtree(backup)

    had_cache = CACHE_DIR.exists()
    try:
        if had_cache:
            os.replace(CACHE_DIR, backup)
        os.replace(staged_cache, CACHE_DIR)
    except Exception:
        if CACHE_DIR.exists():
            shutil.rmtree(CACHE_DIR)
        if backup.exists():
            os.replace(backup, CACHE_DIR)
        raise
    else:
        if backup.exists():
            shutil.rmtree(backup)


def _parse_and_cache(text, source_meta, expected_pages=None):
    parent = CACHE_DIR.parent
    parent.mkdir(parents=True, exist_ok=True)
    staged_cache = Path(tempfile.mkdtemp(prefix=f"{CACHE_DIR.name}.tmp-", dir=parent))
    try:
        index = _write_cache_tree(staged_cache, text, source_meta, expected_pages)
        _replace_cache_atomically(staged_cache)
    except Exception:
        if staged_cache.exists():
            shutil.rmtree(staged_cache)
        raise

    print(f"Cached {index['chapter_count']} chapters to {CACHE_DIR}")
    return index


def _parse_sse(text, request_id):
    data_lines = []
    responses = []

    def finish_event():
        nonlocal data_lines
        if not data_lines:
            return
        payload = "\n".join(data_lines)
        data_lines = []
        try:
            message = json.loads(payload)
        except json.JSONDecodeError:
            return
        if message.get("id") == request_id:
            responses.append(message)

    for line in text.splitlines():
        if line == "":
            finish_event()
            continue
        if line.startswith(":"):
            continue
        field, separator, value = line.partition(":")
        if separator and field == "data":
            data_lines.append(value[1:] if value.startswith(" ") else value)
    finish_event()

    if not responses:
        raise RuntimeError(f"no JSON-RPC response found for request id {request_id}")
    return responses[-1]


def _read_sse_response(response, request_id, max_response_bytes):
    data_lines = []
    bytes_read = 0

    for raw_line in response:
        bytes_read += len(raw_line)
        if bytes_read > max_response_bytes:
            raise RuntimeError(
                f"MCP response exceeds {max_response_bytes} bytes"
            )
        line = raw_line.decode("utf-8").rstrip("\r\n")
        if line == "":
            if not data_lines:
                continue
            payload = "\n".join(data_lines)
            data_lines = []
            try:
                message = json.loads(payload)
            except json.JSONDecodeError:
                continue
            if message.get("id") == request_id:
                return message
            continue
        if line.startswith(":"):
            continue
        field, separator, value = line.partition(":")
        if separator and field == "data":
            data_lines.append(value[1:] if value.startswith(" ") else value)

    if data_lines:
        try:
            message = json.loads("\n".join(data_lines))
        except json.JSONDecodeError:
            message = None
        if message and message.get("id") == request_id:
            return message
    raise RuntimeError(f"no JSON-RPC response found for request id {request_id}")


class McpHttpClient:
    def __init__(
        self,
        endpoint,
        timeout=MCP_DEFAULT_TIMEOUT,
        max_response_bytes=MCP_DEFAULT_MAX_RESPONSE_BYTES,
        urlopen=None,
    ):
        self.endpoint = endpoint
        self.timeout = timeout
        self.max_response_bytes = max_response_bytes
        self.urlopen = urlopen or urllib.request.build_opener(NoRedirectHandler()).open
        self.session_id = None
        self.protocol_version = None
        self._next_id = 1

    def _headers(self):
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        if self.protocol_version:
            headers["MCP-Protocol-Version"] = self.protocol_version
        return headers

    def _send_request(self, method, params=None, notification=False):
        request_id = None if notification else self._next_id
        if request_id is not None:
            self._next_id += 1
        body = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            body["params"] = params
        if request_id is not None:
            body["id"] = request_id

        request = urllib.request.Request(
            self.endpoint,
            data=json.dumps(body).encode("utf-8"),
            headers=self._headers(),
            method="POST",
        )

        try:
            with self.urlopen(request, timeout=self.timeout) as response:
                content_type = response.headers.get("Content-Type", "")
                session_id = response.headers.get("Mcp-Session-Id")
                if session_id:
                    self.session_id = session_id
                status = getattr(response, "status", 200)
                if notification:
                    if status != 202:
                        raise RuntimeError(
                            "MCP notification expected HTTP 202, "
                            f"received HTTP {status}"
                        )
                    message = None
                elif status == 202:
                    raise RuntimeError("MCP request received unexpected HTTP 202")
                elif "text/event-stream" in content_type:
                    message = _read_sse_response(
                        response, request_id, self.max_response_bytes
                    )
                else:
                    raw_bytes = response.read(self.max_response_bytes + 1)
                    if len(raw_bytes) > self.max_response_bytes:
                        raise RuntimeError(
                            f"MCP response exceeds {self.max_response_bytes} bytes"
                        )
                    raw = raw_bytes.decode("utf-8")
                    try:
                        message = json.loads(raw)
                    except json.JSONDecodeError as error:
                        raise RuntimeError(
                            f"MCP response is not valid JSON: {error}"
                        ) from error
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", errors="replace")
            if error.code == 404 and self.session_id:
                raise SessionExpiredError("MCP session expired") from error
            raise RuntimeError(
                f"MCP HTTP error {error.code}: {error.reason}: {detail}"
            ) from error
        except urllib.error.URLError as error:
            raise RuntimeError(f"MCP connection failed: {error.reason}") from error
        except OSError as error:
            raise RuntimeError(f"MCP network error: {error}") from error

        if notification:
            return {}
        if message is None:
            raise RuntimeError("MCP request completed without a JSON-RPC response")
        if message.get("id") != request_id:
            raise RuntimeError(
                f"MCP response id {message.get('id')} does not match request id {request_id}"
            )
        if "error" in message:
            error = message["error"]
            raise RuntimeError(f"MCP error {error.get('code')}: {error.get('message')}")
        return message.get("result", {})

    def _post(self, method, params=None, notification=False):
        try:
            return self._send_request(method, params, notification)
        except SessionExpiredError:
            if method == "initialize" or notification:
                raise
            self.session_id = None
            self.protocol_version = None
            self.initialize()
            return self._send_request(method, params, notification)

    def initialize(self):
        result = self._post("initialize", {
            "protocolVersion": MCP_PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {"name": "psdk-deepwiki-cache", "version": "1.0.0"},
        })
        protocol_version = result.get("protocolVersion", MCP_PROTOCOL_VERSION)
        if protocol_version not in SUPPORTED_PROTOCOL_VERSIONS:
            raise RuntimeError(f"unsupported MCP protocol version: {protocol_version}")
        self.protocol_version = protocol_version
        self._post("notifications/initialized", notification=True)
        return result

    def list_tools(self):
        tools = []
        cursor = None
        while True:
            params = {} if cursor is None else {"cursor": cursor}
            result = self._post("tools/list", params)
            tools.extend(result.get("tools", []))
            cursor = result.get("nextCursor")
            if not cursor:
                return tools

    def call_tool(self, name, arguments):
        result = self._post("tools/call", {"name": name, "arguments": arguments})
        if result.get("isError"):
            messages = [
                item.get("text", "")
                for item in result.get("content", [])
                if item.get("type") == "text"
            ]
            raise RuntimeError("MCP tool error: " + "\n".join(messages))
        return result

    def close(self):
        if not self.session_id:
            return
        request = urllib.request.Request(
            self.endpoint,
            headers=self._headers(),
            method="DELETE",
        )
        try:
            with self.urlopen(request, timeout=self.timeout) as response:
                status = getattr(response, "status", 200)
        except (urllib.error.HTTPError, urllib.error.URLError, OSError):
            return
        if status not in (200, 204, 405):
            return
        self.session_id = None


def _tool_by_capability(
    tools, preferred_name, required_argument, capability=None
):
    capability = capability or (
        "structure" if "structure" in preferred_name else "contents"
    )
    for tool in tools:
        schema = tool.get("inputSchema", {})
        properties = schema.get("properties", {})
        if tool.get("name") == preferred_name and required_argument in properties:
            return tool

    keywords = {
        "structure": ("structure", "outline", "toc", "table of contents"),
        "contents": ("contents", "content", "full", "complete", "documentation"),
    }[capability]
    candidates = []
    for tool in tools:
        schema = tool.get("inputSchema", {})
        properties = schema.get("properties", {})
        searchable = " ".join((
            tool.get("name", ""),
            tool.get("description", ""),
        )).lower()
        score = sum(keyword in searchable for keyword in keywords)
        if required_argument in properties and "wiki" in searchable and score:
            candidates.append((score, tool))
    if candidates:
        highest_score = max(score for score, _ in candidates)
        best = [tool for score, tool in candidates if score == highest_score]
        if len(best) == 1:
            return best[0]
    raise RuntimeError(
        f"unable to identify MCP tool '{preferred_name}'; "
        f"available tools: {[tool.get('name') for tool in tools]}"
    )


def _text_from_tool_result(result):
    text_parts = []
    for item in result.get("content", []):
        if item.get("type") == "text":
            text_parts.append(item.get("text", ""))
        elif item.get("type") == "resource":
            resource = item.get("resource", {})
            if resource.get("text"):
                text_parts.append(resource["text"])
    text = "\n".join(part for part in text_parts if part)
    if not text:
        raise RuntimeError("no text content found in MCP tool result")
    return text


def cmd_pull(args):
    if args.timeout <= 0:
        raise ValueError("timeout must be greater than zero")

    endpoint = args.endpoint or MCP_DEFAULT_ENDPOINT
    if INDEX_FILE.is_file() and not args.force:
        print("Cache already exists. Use --force to refresh.", file=sys.stderr)
        raise SystemExit(2)

    print(f"Fetching wiki for {args.repo} from {endpoint} ...", file=sys.stderr)
    client = McpHttpClient(endpoint, timeout=args.timeout)
    try:
        initialization = client.initialize()
        tools = client.list_tools()
        structure_tool = _tool_by_capability(
            tools, "read_wiki_structure", "repoName"
        )
        contents_tool = _tool_by_capability(
            tools, "read_wiki_contents", "repoName"
        )

        structure_text = _text_from_tool_result(client.call_tool(
            structure_tool["name"], {"repoName": args.repo}
        ))
        wiki_text = _text_from_tool_result(client.call_tool(
            contents_tool["name"], {"repoName": args.repo}
        ))
        expected_pages = _parse_structure_page_names(structure_text)
        if not expected_pages:
            raise RuntimeError("DeepWiki structure did not contain any pages")

        _parse_and_cache(wiki_text, {
            "source": args.repo,
            "method": "pull",
            "endpoint": endpoint,
            "repo": args.repo,
            "protocol_version": client.protocol_version,
            "server_info": initialization.get("serverInfo", {}),
            "structure_sha256": _sha256(structure_text),
            "expected_pages": expected_pages,
        }, expected_pages=expected_pages)
    finally:
        client.close()


def _load_index():
    backup = CACHE_DIR.with_name(f"{CACHE_DIR.name}.backup")
    if not CACHE_DIR.exists() and backup.exists():
        os.replace(backup, CACHE_DIR)
    if not INDEX_FILE.is_file():
        raise RuntimeError("no cache found; run 'wiki-cache.py pull --force'")
    try:
        index = json.loads(INDEX_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid cache index: {error}") from error
    if index.get("format_version") != FORMAT_VERSION:
        raise RuntimeError(
            "unsupported cache format; run 'wiki-cache.py pull --force'"
        )
    required = {"content_sha256", "chapter_count", "chapters"}
    missing = sorted(required - index.keys())
    if missing:
        raise RuntimeError(f"invalid cache index; missing fields: {missing}")
    chapters = index["chapters"]
    if index["chapter_count"] != len(chapters):
        raise RuntimeError("invalid cache index; chapter count mismatch")
    ordinals = [chapter.get("ordinal") for chapter in chapters]
    names = [chapter.get("name") for chapter in chapters]
    paths = [chapter.get("file") for chapter in chapters]
    if len(ordinals) != len(set(ordinals)) or len(names) != len(set(names)):
        raise RuntimeError("invalid cache index; duplicate chapter identity")
    if len(paths) != len(set(paths)):
        raise RuntimeError("invalid cache index; duplicate chapter path")

    chapters_root = CHAPTERS_DIR.resolve()
    for chapter in chapters:
        raw_path = chapter.get("file")
        expected_hash = chapter.get("sha256")
        if not isinstance(raw_path, str) or not isinstance(expected_hash, str):
            raise RuntimeError("invalid cache index; malformed chapter entry")
        chapter_path = (SKILL_DIR / raw_path).resolve()
        try:
            chapter_path.relative_to(chapters_root)
        except ValueError as error:
            raise RuntimeError(f"unsafe chapter path: {raw_path}") from error
        if not chapter_path.is_file():
            raise RuntimeError(f"cached chapter missing: {chapter_path}")
        actual_hash = _sha256(chapter_path.read_text(encoding="utf-8"))
        if actual_hash != expected_hash:
            raise RuntimeError(f"cached chapter hash mismatch: {chapter['name']}")
    return index


def _find_chapter(name, index):
    exact = [chapter for chapter in index["chapters"] if chapter["name"] == name]
    if exact:
        return exact[0]
    matches = [
        chapter for chapter in index["chapters"]
        if name.lower() in chapter["name"].lower()
    ]
    if len(matches) > 1:
        raise ValueError(
            f"ambiguous chapter name '{name}': {[chapter['name'] for chapter in matches]}"
        )
    return matches[0] if matches else None


def cmd_ls(args):
    chapters = _load_index()["chapters"]
    if args.json:
        print(json.dumps(chapters, indent=2, ensure_ascii=False))
        return
    for chapter in chapters:
        print(
            f"{chapter['name']:40s} {chapter['lines']:>5d} lines  {chapter['file']}"
        )


def cmd_get(args):
    chapter = _find_chapter(args.name, _load_index())
    if chapter is None:
        raise ValueError(f"chapter '{args.name}' not found")
    chapter_path = SKILL_DIR / chapter["file"]
    if not chapter_path.is_file():
        raise RuntimeError(f"cached file missing: {chapter_path}")
    print(chapter_path.read_text(encoding="utf-8"), end="")


def cmd_search(args):
    index = _load_index()
    results = []
    for chapter in index["chapters"]:
        chapter_path = SKILL_DIR / chapter["file"]
        if not chapter_path.is_file():
            raise RuntimeError(f"cached file missing: {chapter_path}")
        lines = chapter_path.read_text(encoding="utf-8").splitlines()
        for index_number, line in enumerate(lines):
            if args.term.lower() not in line.lower():
                continue
            start = max(0, index_number - args.context)
            end = min(len(lines), index_number + args.context + 1)
            results.append({
                "chapter": chapter["name"],
                "line": index_number + 1,
                "match": line.strip(),
                "context": "\n".join(lines[start:end]),
            })

    if args.json:
        print(json.dumps({
            "term": args.term,
            "content_sha256": index["content_sha256"],
            "total_matches": len(results),
            "chapters_matched": sorted({result["chapter"] for result in results}),
            "matches": results,
        }, indent=2, ensure_ascii=False))
        return
    for result in results:
        print(f"\n=== {result['chapter']} (line {result['line']}) ===")
        print(result["context"])


def cmd_clear(args):
    if CACHE_DIR.exists():
        shutil.rmtree(CACHE_DIR)
        print(f"Removed {CACHE_DIR}")
    else:
        print("No cache found.")


def cmd_info(args):
    index = _load_index()
    if args.json:
        print(json.dumps(index, indent=2, ensure_ascii=False))
        return
    print(f"Source:        {index['source']}")
    print(f"Cached at:     {index['cached_at']}")
    print(f"Method:        {index.get('method', 'unknown')}")
    print(f"Endpoint:      {index.get('endpoint', 'unknown')}")
    print(f"Repo:          {index.get('repo', 'unknown')}")
    print(f"Protocol:      {index.get('protocol_version', 'unknown')}")
    print(f"Content SHA:   {index['content_sha256']}")
    print(f"Chapters:      {index['chapter_count']}")


def build_parser():
    parser = argparse.ArgumentParser(
        description="Cache and query DeepWiki documentation for dji-sdk/Payload-SDK"
    )
    subcommands = parser.add_subparsers(dest="command", required=True)

    pull = subcommands.add_parser(
        "pull", help="Fetch wiki directly from DeepWiki MCP server and cache"
    )
    pull.add_argument("--repo", default="dji-sdk/Payload-SDK")
    pull.add_argument("--endpoint", default=None)
    pull.add_argument("--timeout", type=int, default=MCP_DEFAULT_TIMEOUT)
    pull.add_argument("--force", action="store_true")
    pull.set_defaults(func=cmd_pull)

    list_command = subcommands.add_parser("ls", help="List cached chapters")
    list_command.add_argument("--json", action="store_true")
    list_command.set_defaults(func=cmd_ls)

    get = subcommands.add_parser("get", help="Output a chapter by name")
    get.add_argument("name")
    get.set_defaults(func=cmd_get)

    search = subcommands.add_parser("search", help="Search cached chapters")
    search.add_argument("term")
    search.add_argument("--json", action="store_true")
    search.add_argument("-C", "--context", type=int, default=2)
    search.set_defaults(func=cmd_search)

    clear = subcommands.add_parser("clear", help="Delete the cache directory")
    clear.set_defaults(func=cmd_clear)

    info = subcommands.add_parser("info", help="Show cache metadata")
    info.add_argument("--json", action="store_true")
    info.set_defaults(func=cmd_info)
    return parser


def main():
    args = build_parser().parse_args()
    try:
        args.func(args)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
