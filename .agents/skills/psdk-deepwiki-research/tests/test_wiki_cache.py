import importlib.util
import io
import json
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).parents[1] / "scripts" / "wiki-cache.py"
SPEC = importlib.util.spec_from_file_location("wiki_cache", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Unable to load wiki cache module from {SCRIPT_PATH}")
wiki_cache = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(wiki_cache)


class FakeResponse:
    def __init__(self, body, content_type="application/json", headers=None, status=200):
        self._body = body.encode("utf-8")
        self.headers = {"Content-Type": content_type, **(headers or {})}
        self.status = status

    def read(self, size=-1):
        return self._body if size < 0 else self._body[:size]

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return False


class StreamingResponse(FakeResponse):
    def read(self):
        raise AssertionError("SSE responses must not be buffered with read()")

    def __iter__(self):
        return iter(self._body.splitlines(keepends=True))


class WikiCacheTestCase(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.skill_dir = Path(self.temp_dir.name)
        self.cache_dir = self.skill_dir / ".cache"
        self.chapters_dir = self.cache_dir / "chapters"
        self.index_file = self.cache_dir / "index.json"

        self.patches = [
            mock.patch.object(wiki_cache, "SKILL_DIR", self.skill_dir),
            mock.patch.object(wiki_cache, "CACHE_DIR", self.cache_dir),
            mock.patch.object(wiki_cache, "CHAPTERS_DIR", self.chapters_dir),
            mock.patch.object(wiki_cache, "INDEX_FILE", self.index_file),
        ]
        for patch in self.patches:
            patch.start()

    def tearDown(self):
        for patch in reversed(self.patches):
            patch.stop()
        self.temp_dir.cleanup()

    def test_page_markers_inside_code_fences_are_ignored(self):
        markdown = """# Page: Real
# Real
```text
# Page: Fake
```
End
"""

        index = wiki_cache._parse_and_cache(markdown, {"method": "test"})

        self.assertEqual([chapter["name"] for chapter in index["chapters"]], ["Real"])
        chapter_path = self.skill_dir / index["chapters"][0]["file"]
        self.assertIn("# Page: Fake", chapter_path.read_text(encoding="utf-8"))

    def test_fence_with_trailing_text_does_not_close_code_block(self):
        markdown = """# Page: Real
# Real
```text
```not-a-closing-fence
# Page: Fake
```
End
"""

        index = wiki_cache._parse_and_cache(markdown, {"method": "test"})

        self.assertEqual([chapter["name"] for chapter in index["chapters"]], ["Real"])

    def test_sanitized_filename_collisions_do_not_overwrite_chapters(self):
        markdown = "# Page: A/B\nFirst\n# Page: A:B\nSecond\n"

        index = wiki_cache._parse_and_cache(markdown, {"method": "test"})

        files = [chapter["file"] for chapter in index["chapters"]]
        self.assertEqual(len(files), len(set(files)))
        contents = [
            (self.skill_dir / chapter["file"]).read_text(encoding="utf-8").strip()
            for chapter in index["chapters"]
        ]
        self.assertEqual(contents, ["First", "Second"])

    def test_sse_parser_skips_notifications_and_matches_response_id(self):
        stream = """event: message
data: {"jsonrpc":"2.0","method":"notifications/progress","params":{}}

event: message
data: {"jsonrpc":"2.0","id":7,
data: "result":{"ok":true}}

"""

        response = wiki_cache._parse_sse(stream, request_id=7)

        self.assertEqual(response["result"], {"ok": True})

    def test_mcp_client_consumes_sse_as_a_stream(self):
        response = StreamingResponse(
            """event: message
data: {"jsonrpc":"2.0","method":"notifications/progress","params":{}}

event: message
data: {"jsonrpc":"2.0","id":1,"result":{"ok":true}}

""",
            content_type="text/event-stream",
        )
        client = wiki_cache.McpHttpClient(
            "https://example.test/mcp",
            urlopen=lambda request, timeout: response,
        )

        result = client._post("tools/list", {})

        self.assertEqual(result, {"ok": True})

    def test_mcp_client_rejects_oversized_json_response(self):
        response = FakeResponse("x" * 64)
        client = wiki_cache.McpHttpClient(
            "https://example.test/mcp",
            max_response_bytes=32,
            urlopen=lambda request, timeout: response,
        )

        with self.assertRaisesRegex(RuntimeError, "response exceeds"):
            client._post("tools/list", {})

    def test_mcp_client_initializes_and_reuses_session_headers(self):
        requests = []
        responses = [
            FakeResponse(
                json.dumps({
                    "jsonrpc": "2.0",
                    "id": 1,
                    "result": {
                        "protocolVersion": "2025-06-18",
                        "capabilities": {},
                        "serverInfo": {"name": "deepwiki", "version": "1"},
                    },
                }),
                headers={"Mcp-Session-Id": "session-123"},
            ),
            FakeResponse("", status=202),
            FakeResponse(json.dumps({
                "jsonrpc": "2.0",
                "id": 2,
                "result": {"tools": [{
                    "name": "read_wiki_contents",
                    "inputSchema": {
                        "type": "object",
                        "properties": {"repoName": {"type": "string"}},
                        "required": ["repoName"],
                    },
                }]},
            })),
            FakeResponse(json.dumps({
                "jsonrpc": "2.0",
                "id": 3,
                "result": {"content": [{"type": "text", "text": "# Page: A\nA\n"}]},
            })),
        ]

        def fake_urlopen(request, timeout):
            requests.append(request)
            return responses.pop(0)

        client = wiki_cache.McpHttpClient(
            "https://example.test/mcp",
            timeout=10,
            urlopen=fake_urlopen,
        )
        client.initialize()
        tools = client.list_tools()
        result = client.call_tool("read_wiki_contents", {"repoName": "owner/repo"})

        self.assertEqual(tools[0]["name"], "read_wiki_contents")
        self.assertFalse(result.get("isError", False))
        methods = [json.loads(request.data)["method"] for request in requests]
        self.assertEqual(
            methods,
            ["initialize", "notifications/initialized", "tools/list", "tools/call"],
        )
        for request in requests[1:]:
            self.assertEqual(request.headers["Mcp-session-id"], "session-123")
            self.assertEqual(request.headers["Mcp-protocol-version"], "2025-06-18")

    def test_list_tools_follows_pagination(self):
        client = mock.Mock()
        client._post.side_effect = [
            {"tools": [{"name": "first"}], "nextCursor": "next"},
            {"tools": [{"name": "second"}]},
        ]

        tools = wiki_cache.McpHttpClient.list_tools(client)

        self.assertEqual([tool["name"] for tool in tools], ["first", "second"])
        self.assertEqual(
            client._post.call_args_list,
            [mock.call("tools/list", {}), mock.call("tools/list", {"cursor": "next"})],
        )

    def test_session_404_reinitializes_and_retries_request(self):
        client = wiki_cache.McpHttpClient("https://example.test/mcp")
        client.session_id = "expired"
        client.protocol_version = "2025-06-18"
        client._send_request = mock.Mock(side_effect=[
            wiki_cache.SessionExpiredError("expired"),
            {"protocolVersion": "2025-06-18", "capabilities": {}, "serverInfo": {}},
            {},
            {"tools": []},
        ])

        result = client._post("tools/list", {})

        self.assertEqual(result, {"tools": []})
        self.assertEqual(client.session_id, None)
        methods = [call.args[0] for call in client._send_request.call_args_list]
        self.assertEqual(
            methods,
            ["tools/list", "initialize", "notifications/initialized", "tools/list"],
        )

    def test_initialize_rejects_unsupported_protocol_version(self):
        client = wiki_cache.McpHttpClient("https://example.test/mcp")
        client._post = mock.Mock(return_value={
            "protocolVersion": "2099-01-01",
            "capabilities": {},
            "serverInfo": {},
        })

        with self.assertRaisesRegex(RuntimeError, "unsupported MCP protocol"):
            client.initialize()

    def test_request_rejects_http_202_response(self):
        client = wiki_cache.McpHttpClient(
            "https://example.test/mcp",
            urlopen=lambda request, timeout: FakeResponse("", status=202),
        )

        with self.assertRaisesRegex(RuntimeError, "HTTP 202"):
            client._post("tools/list", {})

    def test_close_deletes_active_session(self):
        requests = []

        def fake_urlopen(request, timeout):
            requests.append(request)
            return FakeResponse("", status=204)

        client = wiki_cache.McpHttpClient(
            "https://example.test/mcp", urlopen=fake_urlopen
        )
        client.session_id = "session-123"
        client.protocol_version = "2025-06-18"

        client.close()

        self.assertEqual(requests[0].method, "DELETE")
        self.assertEqual(requests[0].headers["Mcp-session-id"], "session-123")

    def test_failed_cache_write_preserves_previous_cache(self):
        self.cache_dir.mkdir(parents=True)
        sentinel = self.cache_dir / "sentinel"
        sentinel.write_text("old", encoding="utf-8")

        original_write_text = Path.write_text

        def fail_index_write(path, *args, **kwargs):
            if path.name == "index.json":
                raise OSError("disk full")
            return original_write_text(path, *args, **kwargs)

        with mock.patch.object(Path, "write_text", fail_index_write):
            with self.assertRaises(OSError):
                wiki_cache._parse_and_cache("# Page: New\nNew\n", {"method": "test"})

        self.assertEqual(sentinel.read_text(encoding="utf-8"), "old")

    def test_interrupted_cache_swap_restores_previous_cache(self):
        self.cache_dir.mkdir(parents=True)
        sentinel = self.cache_dir / "sentinel"
        sentinel.write_text("old", encoding="utf-8")
        real_replace = wiki_cache.os.replace
        call_count = 0

        def fail_second_replace(source, destination):
            nonlocal call_count
            call_count += 1
            if call_count == 2:
                raise OSError("interrupted")
            return real_replace(source, destination)

        with mock.patch.object(wiki_cache.os, "replace", fail_second_replace):
            with self.assertRaisesRegex(OSError, "interrupted"):
                wiki_cache._parse_and_cache("# Page: New\nNew\n", {"method": "test"})

        self.assertEqual(sentinel.read_text(encoding="utf-8"), "old")
        self.assertFalse(self.cache_dir.with_name(".cache.backup").exists())

    def test_load_index_recovers_backup_left_by_crashed_process(self):
        backup = self.cache_dir.with_name(".cache.backup")
        backup.mkdir(parents=True)
        (backup / "index.json").write_text(json.dumps({
            "format_version": wiki_cache.FORMAT_VERSION,
            "content_sha256": "0" * 64,
            "chapter_count": 0,
            "chapters": [],
        }), encoding="utf-8")

        index = wiki_cache._load_index()

        self.assertEqual(index["chapter_count"], 0)
        self.assertTrue(self.index_file.is_file())
        self.assertFalse(backup.exists())

    def test_ambiguous_fuzzy_chapter_name_is_rejected(self):
        index = {
            "chapters": [
                {"name": "Development Tools", "file": "one", "lines": 1},
                {"name": "Development Tools and Resources", "file": "two", "lines": 1},
            ]
        }

        with self.assertRaisesRegex(ValueError, "ambiguous"):
            wiki_cache._find_chapter("Development", index)

    def test_tool_discovery_supports_renamed_structure_and_contents_tools(self):
        tools = [
            {
                "name": "deepwiki_outline",
                "description": "Read the wiki structure and table of contents",
                "inputSchema": {"properties": {"repoName": {"type": "string"}}},
            },
            {
                "name": "deepwiki_full_documentation",
                "description": "Read the complete full wiki contents",
                "inputSchema": {"properties": {"repoName": {"type": "string"}}},
            },
        ]

        structure = wiki_cache._tool_by_capability(
            tools, "read_wiki_structure", "repoName", capability="structure"
        )
        contents = wiki_cache._tool_by_capability(
            tools, "read_wiki_contents", "repoName", capability="contents"
        )

        self.assertEqual(structure["name"], "deepwiki_outline")
        self.assertEqual(contents["name"], "deepwiki_full_documentation")

    def test_structure_validation_rejects_duplicates_and_order_mismatch(self):
        with self.assertRaisesRegex(ValueError, "duplicate structure pages"):
            wiki_cache._parse_structure_page_names("- 1 A\n  - 1.1 A\n")

        with self.assertRaisesRegex(ValueError, "order"):
            wiki_cache._parse_and_cache(
                "# Page: A\nA\n# Page: B\nB\n",
                {"method": "test"},
                expected_pages=["B", "A"],
            )

    def test_old_cache_index_is_rejected_before_query(self):
        self.cache_dir.mkdir(parents=True)
        self.index_file.write_text(
            json.dumps({"source": "old", "chapters": []}), encoding="utf-8"
        )

        with self.assertRaisesRegex(RuntimeError, "unsupported cache format"):
            wiki_cache._load_index()

    def test_cache_index_rejects_path_escape_and_tampered_content(self):
        self.cache_dir.mkdir(parents=True)
        outside = self.skill_dir / "outside.md"
        outside.write_text("secret", encoding="utf-8")
        self.index_file.write_text(json.dumps({
            "format_version": wiki_cache.FORMAT_VERSION,
            "content_sha256": "0" * 64,
            "chapter_count": 1,
            "chapters": [{
                "ordinal": 1,
                "name": "Escape",
                "file": ".cache/chapters/../../outside.md",
                "lines": 1,
                "sha256": wiki_cache._sha256("secret"),
            }],
        }), encoding="utf-8")

        with self.assertRaisesRegex(RuntimeError, "unsafe chapter path"):
            wiki_cache._load_index()

        chapters = self.cache_dir / "chapters"
        chapters.mkdir()
        chapter_file = chapters / "safe.md"
        chapter_file.write_text("tampered", encoding="utf-8")
        self.index_file.write_text(json.dumps({
            "format_version": wiki_cache.FORMAT_VERSION,
            "content_sha256": "0" * 64,
            "chapter_count": 1,
            "chapters": [{
                "ordinal": 1,
                "name": "Safe",
                "file": ".cache/chapters/safe.md",
                "lines": 1,
                "sha256": wiki_cache._sha256("original"),
            }],
        }), encoding="utf-8")

        with self.assertRaisesRegex(RuntimeError, "hash mismatch"):
            wiki_cache._load_index()

    def test_cache_source_uses_requested_repository(self):
        index = wiki_cache._parse_and_cache(
            "# Page: A\nA\n",
            {"source": "owner/other", "repo": "owner/other", "method": "test"},
        )

        self.assertEqual(index["source"], "owner/other")


if __name__ == "__main__":
    unittest.main()