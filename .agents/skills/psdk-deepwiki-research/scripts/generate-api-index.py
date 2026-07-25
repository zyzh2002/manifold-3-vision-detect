#!/usr/bin/env python3
"""Generate a PSDK API index document from local header files.

The index is a navigation aid for AI agents researching PSDK APIs. It is not
primary evidence -- agents must still cross-check against the actual headers
under third_party/psdk/psdk_lib/include/.

Usage:
    python3 generate-api-index.py [--output PATH]

Outputs Markdown to references/api-index.md by default.

Maintenance guide: references/api-index-maintenance.md
"""

import argparse
import os
import re
import sys
import tempfile
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

SKILL_DIR = Path(__file__).resolve().parent.parent
SCRIPT_DIR = Path(__file__).resolve().parent


def _find_repo_root():
    """Walk up from script directory to find the git repo root."""
    current = SCRIPT_DIR
    for _ in range(10):
        if (current / ".git").exists() or (current / "AGENTS.md").exists():
            return current
        current = current.parent
    return SCRIPT_DIR


REPO_ROOT = _find_repo_root()
PSDK_INCLUDE_DIR = REPO_ROOT / "third_party" / "psdk" / "psdk_lib" / "include"
DEFAULT_OUTPUT = SKILL_DIR / "references" / "api-index.md"
DEFAULT_OUTPUT_MODE = 0o644

# ---------------------------------------------------------------------------
# Static data (manually curated domain knowledge)
# ---------------------------------------------------------------------------

# Headers that get full per-function detail in Section 2.1.
# All others get a one-line summary table in Section 2.2.
CORE_HEADERS = [
    "dji_core.h",
    "dji_platform.h",
    "dji_liveview.h",
    "dji_camera_manager.h",
    "dji_payload_camera.h",
    "dji_typedef.h",
    "dji_error.h",
    "dji_logger.h",
    "dji_version.h",
    "dji_fc_subscription.h",
    "dji_gimbal_manager.h",
    "dji_aircraft_info.h",
    "dji_time_sync.h",
]

# Functional grouping for Section 1 (Quick Reference).
DOMAIN_GROUPS = [
    ("Core Lifecycle", [
        "dji_core.h",
        "dji_typedef.h",
        "dji_error.h",
        "dji_logger.h",
        "dji_version.h",
    ]),
    ("Platform HAL / OSAL", [
        "dji_platform.h",
    ]),
    ("Live Video & AI Metadata", [
        "dji_liveview.h",
    ]),
    ("Camera Control", [
        "dji_camera_manager.h",
        "dji_payload_camera.h",
    ]),
    ("Flight Controller", [
        "dji_flight_controller.h",
        "dji_fc_subscription.h",
        "dji_aircraft_info.h",
    ]),
    ("Gimbal", [
        "dji_gimbal_manager.h",
        "dji_gimbal.h",
        "dji_xport.h",
    ]),
    ("Data Channels", [
        "dji_high_speed_data_channel.h",
        "dji_low_speed_data_channel.h",
        "dji_mop_channel.h",
    ]),
    ("Perception & Positioning", [
        "dji_perception.h",
        "dji_positioning.h",
        "dji_network_rtk.h",
        "dji_time_sync.h",
    ]),
    ("Power & Battery", [
        "dji_power_management.h",
        "dji_tethered_battery.h",
    ]),
    ("Waypoint Missions", [
        "dji_waypoint_v2.h",
        "dji_waypoint_v2_type.h",
        "dji_waypoint_v3.h",
    ]),
    ("Widget / UI", [
        "dji_widget.h",
        "dji_widget_manager.h",
    ]),
    ("HMS", [
        "dji_hms.h",
        "dji_hms_manager.h",
        "dji_hms_customization.h",
        "dji_hms_info_table.h",
    ]),
    ("Other Modules", [
        "dji_cloud_api_by_websockt.h",
        "dji_fts.h",
        "dji_interest_point.h",
        "dji_open_ar.h",
        "dji_psdk_hoist_controller.h",
        "dji_upgrade.h",
    ]),
]

# Human-written summaries for every header.  These are the primary brief text
# shown in the index; the @brief extraction from headers is only a fallback for
# any header not listed here.
HEADER_SUMMARIES = {
    "dji_core.h": "PSDK core lifecycle: init, deinit, application start, product identity (alias/fw version/serial).",
    "dji_typedef.h": "Core type definitions: return codes, mount positions, aircraft/camera types, gimbal modes, vectors, quaternions, firmware version.",
    "dji_error.h": "Error code system: module-based 64-bit error codes across all PSDK subsystems.",
    "dji_logger.h": "Logging subsystem: console registration, log levels, USER_LOG_* convenience macros.",
    "dji_version.h": "PSDK version macros only. No types or functions.",
    "dji_platform.h": "Platform abstraction layer: OSAL (task/mutex/semaphore/time/memory), HAL (UART/USB Bulk/network/I2C), filesystem, socket handler registration.",
    "dji_liveview.h": "Live video streaming: H264 stream, decoded NV12 image stream (Manifold 3 only), AI metadata, H264 encoder, bounding box push to Pilot.",
    "dji_camera_manager.h": "Aircraft camera control: shoot/record, exposure, focus, zoom, metering, media download, SD card, thermal, laser ranging, stream source.",
    "dji_payload_camera.h": "Third-party payload camera emulation: register handlers for common ops, exposure, focus, zoom, media download/playback, video stream push.",
    "dji_fc_subscription.h": "Flight controller data subscription: 50+ topics (attitude, velocity, position, GPS, RTK, gimbal, battery) at up to 400Hz.",
    "dji_flight_controller.h": "Flight control: takeoff/landing, go-home, joystick control, obstacle avoidance, motors, lights, FTS, RC lost action.",
    "dji_gimbal_manager.h": "High-level aircraft gimbal control: mode, rotate, reset, speed, smooth factor, pitch range extension.",
    "dji_gimbal.h": "Low-level payload-side gimbal emulation: init, register system state/attitude/calibration/rotate handlers.",
    "dji_xport.h": "XPort gimbal control: system state, attitude callbacks, mode, rotate, reset, limit angles, speed factor.",
    "dji_aircraft_info.h": "Aircraft system info queries: base info (type, adapter, mount position), mobile app info, connection status, version.",
    "dji_high_speed_data_channel.h": "High-speed data channel over network: bandwidth control, remote address, send stream, stream state.",
    "dji_low_speed_data_channel.h": "Low-speed command channel: init, send, get state, register receive callback.",
    "dji_mop_channel.h": "MOP channel: socket-like reliable/unreliable data between payload and MSDK/OSDK (create, bind, connect, send, recv).",
    "dji_perception.h": "Perception/obstacle data: stereo camera images, depth info, LiDAR point cloud, intrinsics/extrinsics.",
    "dji_positioning.h": "Precise positioning: sync position by event timestamp, RTCM data callback.",
    "dji_network_rtk.h": "Network RTK service: start/stop with NTRIP config, state callback (login/transfer/reconnect/broken).",
    "dji_time_sync.h": "Time synchronization between payload and aircraft: PPS trigger callback, local-to-aircraft time transfer.",
    "dji_power_management.h": "Power management: high power apply (v1/v2), apply pin callback, power-off notification, Manifold 3 high-voltage output.",
    "dji_tethered_battery.h": "Tethered battery system: push tether line status (total/used length) to Pilot2.",
    "dji_widget.h": "Widget init, UI config registration (dir/binary), handler list, speaker, floating window messages.",
    "dji_widget_manager.h": "Widget manager: init, register widget list, set/get values, speaker/sound/light, file download.",
    "dji_hms.h": "HMS aggregate header (includes manager + customization headers).",
    "dji_hms_manager.h": "HMS manager: init/deinit, register callback for HMS info pushed at 1Hz.",
    "dji_hms_customization.h": "HMS customization: init/deinit, inject/eliminate error codes, register text configs, enhanced alarm control.",
    "dji_hms_info_table.h": "HMS info table data structures and error code reference.",
    "dji_waypoint_v2.h": "Waypoint V2 mission: init/deinit, upload, start/stop/pause/resume, cruise speed, event/state callbacks.",
    "dji_waypoint_v2_type.h": "Waypoint V2 type definitions: settings, waypoint actions/triggers/actuators, yaw modes.",
    "dji_waypoint_v3.h": "Waypoint V3 (wayline) mission: upload WPMZ file, mission action, state/event callbacks.",
    "dji_cloud_api_by_websockt.h": "Cloud API via Pilot2 websocket: single function to send data into cloud channel (Manifold 3 only).",
    "dji_fts.h": "Flight Termination System: PWM trigger selection and status query (M400 / M4 series).",
    "dji_interest_point.h": "Interest point (POI orbit) mission: start/stop, speed, mission state callback.",
    "dji_open_ar.h": "Open AR annotations: text, markers, space coordinates overlaid on Pilot video feed.",
    "dji_psdk_hoist_controller.h": "PSDK hoist (winch) controller: cargo release/receive/stop, hook control, tether length, status.",
    "dji_upgrade.h": "Firmware upgrade: init, local upgrade, register handler, push state (FTP or DCFTP transfer).",
}

# Manually curated cross-references from headers to sample code.
# Paths are relative to samples/sample_c/.
SAMPLE_CROSS_REF = {
    "dji_core.h": ["platform/linux/manifold3/application/main.c"],
    "dji_platform.h": [
        "platform/linux/manifold3/hal/hal_usb_bulk.c",
        "platform/linux/common/osal/osal.c",
        "platform/linux/common/osal/osal_fs.c",
        "platform/linux/common/osal/osal_socket.c",
    ],
    "dji_liveview.h": ["module_sample/liveview/test_liveview.c"],
    "dji_camera_manager.h": ["module_sample/camera_manager/test_camera_manager.c"],
    "dji_payload_camera.h": ["module_sample/camera_emu/test_payload_cam_emu_base.c"],
    "dji_fc_subscription.h": ["module_sample/fc_subscription/test_fc_subscription.c"],
    "dji_flight_controller.h": ["module_sample/flight_control/test_flight_control.c"],
    "dji_gimbal_manager.h": ["module_sample/gimbal_manager/test_gimbal_manager.c"],
    "dji_gimbal.h": ["module_sample/gimbal_emu/test_payload_gimbal_emu.c"],
    "dji_xport.h": ["module_sample/xport/test_payload_xport.c"],
    "dji_perception.h": ["module_sample/perception/test_perception.c"],
    "dji_power_management.h": ["module_sample/power_management/test_power_management.c"],
    "dji_widget.h": ["module_sample/widget/test_widget.c"],
    "dji_widget_manager.h": ["module_sample/widget/test_widget.c"],
    "dji_mop_channel.h": ["module_sample/mop_channel/test_mop_channel.c"],
    "dji_high_speed_data_channel.h": ["module_sample/data_transmission/test_data_transmission.c"],
    "dji_low_speed_data_channel.h": ["module_sample/data_transmission/test_data_transmission.c"],
    "dji_waypoint_v2.h": ["module_sample/waypoint_v2/test_waypoint_v2.c"],
    "dji_waypoint_v3.h": ["module_sample/waypoint_v3/test_waypoint_v3.c"],
    "dji_hms.h": ["module_sample/hms/test_hms.c"],
    "dji_upgrade.h": ["module_sample/upgrade/test_upgrade.c"],
    "dji_time_sync.h": ["module_sample/time_sync/test_time_sync.c"],
    "dji_positioning.h": ["module_sample/positioning/test_positioning.c"],
    "dji_network_rtk.h": ["module_sample/positioning/test_positioning.c"],
    "dji_interest_point.h": ["module_sample/interest_point/test_interest_point.c"],
    "dji_aircraft_info.h": [
        "module_sample/camera_manager/test_camera_manager.c",
        "module_sample/gimbal_manager/test_gimbal_manager.c",
    ],
}

# ---------------------------------------------------------------------------
# Header parsing
# ---------------------------------------------------------------------------


def strip_comments_and_preproc(text):
    """Remove C block comments and preprocessor directives, keeping line structure."""
    result = []
    in_comment = False
    in_preprocessor = False
    for line in text.splitlines():
        stripped = line.strip()

        if in_preprocessor:
            in_preprocessor = line.rstrip().endswith("\\")
            result.append("")
            continue

        if in_comment:
            if "*/" in stripped:
                in_comment = False
                line = line[line.index("*/") + 2:]
            else:
                result.append("")
                continue

        if not in_comment:
            while "/*" in line:
                start = line.index("/*")
                end_idx = line.find("*/", start + 2)
                if end_idx != -1:
                    line = line[:start] + " " + line[end_idx + 2:]
                else:
                    line = line[:start]
                    in_comment = True
                    break

            if not in_comment and stripped.startswith("#"):
                in_preprocessor = line.rstrip().endswith("\\")
                result.append("")
                continue

            if not in_comment and "//" in line:
                line = line[:line.index("//")]

        result.append(line)
    return "\n".join(result)


def extract_functions(clean_text):
    """Extract public function declarations.

    Matches patterns like:
        T_DjiReturnCode DjiCore_Init(const T_DjiUserInfo *userInfo);
        void DjiLogger_UserLogOutput(E_DjiLoggerConsoleLogLevel level, const char *fmt, ...);
    """
    functions = []
    pattern = re.compile(
        r"^[ \t]*(?!typedef\b)"
        r"([A-Za-z_][A-Za-z0-9_ *]*(?:\n[ \t]*)?)"
        r"(?<![A-Za-z0-9_])(Dji[A-Za-z0-9_]+)\s*\("
        r"([^;{}]*?)\)"
        r"\s*;",
        re.MULTILINE | re.DOTALL,
    )
    for match in pattern.finditer(clean_text):
        ret_type = match.group(1).strip()
        name = match.group(2)
        params = re.sub(r"\s+", " ", match.group(3).strip())
        functions.append({
            "name": name,
            "return_type": ret_type,
            "params": params,
        })
    return functions


def _find_matching_delimiter(text, start, opening, closing):
    """Return the matching delimiter index for text[start]."""
    if start >= len(text) or text[start] != opening:
        raise ValueError(f"expected '{opening}' at offset {start}")

    depth = 0
    for index in range(start, len(text)):
        char = text[index]
        if char == opening:
            depth += 1
        elif char == closing:
            depth -= 1
            if depth == 0:
                return index
    raise ValueError(f"unclosed '{opening}' at offset {start}")


def _skip_attribute(clean_text, cursor):
    """Skip an optional GCC __attribute__((...)) suffix."""
    attribute_match = re.match(r"__attribute__\s*", clean_text[cursor:])
    if not attribute_match:
        return cursor

    cursor += attribute_match.end()
    if cursor >= len(clean_text) or clean_text[cursor] != "(":
        return cursor
    return _find_matching_delimiter(clean_text, cursor, "(", ")") + 1


def _extract_typedef_aggregates(clean_text, kind):
    """Extract typedef struct/union/enum definitions with balanced braces."""
    definitions = []
    pattern = re.compile(
        rf"\btypedef\s+{kind}\b(?:\s+[A-Za-z_]\w*)?\s*\{{"
    )
    for match in pattern.finditer(clean_text):
        open_brace = match.end() - 1
        close_brace = _find_matching_delimiter(clean_text, open_brace, "{", "}")
        cursor = close_brace + 1
        while cursor < len(clean_text) and clean_text[cursor].isspace():
            cursor += 1
        cursor = _skip_attribute(clean_text, cursor)
        while cursor < len(clean_text) and clean_text[cursor].isspace():
            cursor += 1

        aliases_match = re.match(
            r"([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s*;",
            clean_text[cursor:],
        )
        if not aliases_match:
            continue
        body = clean_text[open_brace + 1:close_brace].strip()
        for name in re.findall(r"[A-Za-z_]\w*", aliases_match.group(1)):
            definitions.append({"name": name, "body": body})
    return definitions


def extract_structs(clean_text):
    """Extract typedef struct definitions."""
    return [
        {
            "name": definition["name"],
            "field_count": _count_aggregate_fields(definition["body"]),
        }
        for definition in _extract_typedef_aggregates(clean_text, "struct")
    ]


def extract_unions(clean_text):
    """Extract typedef union definitions."""
    return [
        {
            "name": definition["name"],
            "field_count": _count_aggregate_fields(definition["body"]),
        }
        for definition in _extract_typedef_aggregates(clean_text, "union")
    ]


def _count_aggregate_fields(body):
    """Count top-level field declarations in a struct or union body.

    Uses a brace-depth tracker so nested anonymous structs/unions are not
    double-counted, and ignores comment-only fragments.
    """
    count = 0
    depth = 0
    current = ""
    for ch in body:
        if ch == "{":
            depth += 1
            current += ch
        elif ch == "}":
            depth -= 1
            current += ch
        elif ch == ";" and depth == 0:
            decl = current.strip()
            if decl and not decl.startswith("/*") and decl != "{":
                count += 1
            current = ""
        else:
            current += ch
    return count


def _extract_function_pointer_fields(body):
    """Extract top-level function pointer fields from an aggregate body."""
    fields = []
    for declaration in _split_top_level(body, ";"):
        declaration = re.sub(r"\s+", " ", declaration.strip())
        if not declaration:
            continue
        match = re.fullmatch(
            r"(.+?)\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\((.*)\)",
            declaration,
        )
        if not match:
            continue
        fields.append({
            "name": match.group(2),
            "return_type": match.group(1).strip(),
            "params": match.group(3).strip(),
        })
    return fields


def _split_top_level(text, separator):
    """Split text only where separator occurs outside (), [], and {}."""
    parts = []
    current = []
    depths = {"(": 0, "[": 0, "{": 0}
    pairs = {")": "(", "]": "[", "}": "{"}
    for char in text:
        if char in depths:
            depths[char] += 1
        elif char in pairs:
            opening = pairs[char]
            depths[opening] = max(0, depths[opening] - 1)

        if char == separator and all(depth == 0 for depth in depths.values()):
            parts.append("".join(current))
            current = []
        else:
            current.append(char)
    parts.append("".join(current))
    return parts


def extract_enums(clean_text):
    """Extract typedef enums and named non-typedef enums."""
    enums = [
        {
            "name": definition["name"],
            "value_count": len([
                item for item in _split_top_level(definition["body"], ",")
                if item.strip()
            ]),
        }
        for definition in _extract_typedef_aggregates(clean_text, "enum")
    ]

    named_pattern = re.compile(r"^[ \t]*enum\s+([A-Za-z_]\w*)\s*\{", re.MULTILINE)
    for match in named_pattern.finditer(clean_text):
        open_brace = match.end() - 1
        close_brace = _find_matching_delimiter(clean_text, open_brace, "{", "}")
        cursor = close_brace + 1
        while cursor < len(clean_text) and clean_text[cursor].isspace():
            cursor += 1
        if cursor >= len(clean_text) or clean_text[cursor] != ";":
            continue
        body = clean_text[open_brace + 1:close_brace]
        enums.append({
            "name": match.group(1),
            "value_count": len([
                item for item in _split_top_level(body, ",") if item.strip()
            ]),
        })
    return enums


def extract_callback_typedefs(clean_text):
    """Extract all public function pointer typedef names."""
    callbacks = []
    pattern = re.compile(
        r"^[ \t]*typedef[ \t]+[^;{}\n]+?\(\s*\*\s*"
        r"([A-Za-z_]\w*)\s*\)"
        r"\s*\(",
        re.MULTILINE,
    )
    for match in pattern.finditer(clean_text):
        callbacks.append(match.group(1))
    return callbacks


def extract_type_aliases(clean_text):
    """Extract simple typedef aliases (not struct/enum/callback)."""
    aliases = []
    pattern = re.compile(
        r"^[ \t]*typedef[ \t]+(?!struct\b|union\b|enum\b)"
        r"([^;\n]*?)(?:[ \t]+|(?<=\*))([A-Za-z_]\w*)[ \t]*;",
        re.MULTILINE,
    )
    for match in pattern.finditer(clean_text):
        if "(*" in match.group(1).replace(" ", ""):
            continue
        aliases.append({
            "name": match.group(2),
            "source": match.group(1).strip(),
        })
    return aliases


def extract_brief_from_header(raw_text):
    """Extract the @brief description from the file header comment.

    This is a low-quality fallback used only when HEADER_SUMMARIES does not
    cover a header.  DJI's @brief text is often a generic template.
    """
    match = re.search(r"@brief\s+(.+?)(?:\n\s*\*\s*@|\n\s*\*\*\*\*)", raw_text, re.DOTALL)
    if not match:
        return ""
    brief = re.sub(r"\s*\n\s*\*\s*", " ", match.group(1)).strip()
    return re.sub(r"\s+", " ", brief)


def parse_header(file_path):
    """Parse a PSDK header file and return structured info."""
    raw = file_path.read_text(encoding="utf-8")
    clean = strip_comments_and_preproc(raw)
    name = file_path.name

    structs = extract_structs(clean)
    aggregate_definitions = {
        definition["name"]: definition["body"]
        for definition in _extract_typedef_aggregates(clean, "struct")
    }
    return {
        "name": name,
        "path": f"third_party/psdk/psdk_lib/include/{name}",
        "lines": len(raw.splitlines()),
        "brief": HEADER_SUMMARIES.get(name, extract_brief_from_header(raw)),
        "functions": extract_functions(clean),
        "structs": structs,
        "function_pointer_fields": {
            name: _extract_function_pointer_fields(body)
            for name, body in aggregate_definitions.items()
        },
        "unions": extract_unions(clean),
        "enums": extract_enums(clean),
        "callbacks": extract_callback_typedefs(clean),
        "type_aliases": extract_type_aliases(clean),
        "sample_refs": SAMPLE_CROSS_REF.get(name, []),
    }


def read_psdk_version(version_header):
    """Read PSDK version components from dji_version.h."""
    text = version_header.read_text(encoding="utf-8")
    components = {}
    for key in ("MAJOR", "MINOR", "MODIFY", "BETA", "BUILD"):
        match = re.search(rf"^\s*#define\s+DJI_VERSION_{key}\s+(\d+)\b", text, re.MULTILINE)
        if not match:
            raise ValueError(f"missing DJI_VERSION_{key} in {version_header}")
        components[key.lower()] = int(match.group(1))
    components["display"] = (
        f"{components['major']}.{components['minor']}.{components['modify']}"
    )
    return components


def validate_static_metadata(headers):
    """Validate curated mappings against the current local PSDK headers."""
    header_names = set(headers)
    domain_headers = [name for _, group in DOMAIN_GROUPS for name in group]

    checks = {
        "header summaries missing": header_names - set(HEADER_SUMMARIES),
        "header summaries stale": set(HEADER_SUMMARIES) - header_names,
        "domain groups missing": header_names - set(domain_headers),
        "domain groups stale": set(domain_headers) - header_names,
        "core headers stale": set(CORE_HEADERS) - header_names,
        "sample mappings stale": set(SAMPLE_CROSS_REF) - header_names,
    }
    errors = [f"{label}: {sorted(names)}" for label, names in checks.items() if names]

    duplicates = sorted({name for name in domain_headers if domain_headers.count(name) > 1})
    if duplicates:
        errors.append(f"domain group duplicates: {duplicates}")

    sample_root = REPO_ROOT / "third_party" / "psdk" / "samples" / "sample_c"
    missing_samples = sorted(
        str(sample_root / relative_path)
        for paths in SAMPLE_CROSS_REF.values()
        for relative_path in paths
        if not (sample_root / relative_path).is_file()
    )
    if missing_samples:
        errors.append(f"sample references missing: {missing_samples}")

    if errors:
        raise ValueError("invalid static metadata: " + "; ".join(errors))


# ---------------------------------------------------------------------------
# Markdown generation
# ---------------------------------------------------------------------------


def generate_doc_header(headers, totals, version):
    """Generate the document title, stats, and table of contents."""
    lines = [
        f"# PSDK {version['display']} API Index",
        "",
        "> Auto-generated from `third_party/psdk/psdk_lib/include/` headers.",
        ">",
        f"> {len(headers)} headers · {totals['functions']} functions · "
        f"{totals['structs']} structs · {totals['unions']} "
        f"{'union' if totals['unions'] == 1 else 'unions'} · "
        f"{totals['enums']} enums · "
        f"{totals['callbacks']} callback typedefs",
        ">",
        "> **This is a navigation aid, not primary evidence.** Always verify "
        "symbols and signatures against the actual header files before using "
        "them in code.",
        ">",
        "> Generated by `scripts/generate-api-index.py`. "
        "Maintenance guide: `references/api-index-maintenance.md`.",
        "",
        "**Contents:**",
        "",
        "1. [Quick Reference by Domain](#1-quick-reference-by-domain) - find the right header fast",
        "2. [Header-by-Header API Map](#2-header-by-header-api-map) - per-header function/type listings",
        "3. [PSDK Initialization Sequence](#3-psdk-initialization-sequence) - standard call order",
        "4. [Platform Porting Checklist](#4-platform-porting-checklist-manifold-3) - HAL/OSAL requirements",
        "5. [Flat Symbol Index](#5-flat-symbol-index) - grep-friendly alphabetical symbol lookup",
        "",
        "---",
        "",
    ]
    return "\n".join(lines)


def generate_quick_reference(headers):
    lines = ["## 1. Quick Reference by Domain", ""]
    for domain, header_names in DOMAIN_GROUPS:
        lines.append(f"### {domain}")
        lines.append("")
        for hname in header_names:
            h = headers.get(hname)
            if not h:
                continue
            brief = h["brief"]
            if len(brief) > 100:
                brief = brief[:97] + "..."
            lines.append(f"- **{hname}** ({len(h['functions'])} functions) - {brief}")
        lines.append("")
    return "\n".join(lines)


def generate_header_section(header_info):
    """Generate a detailed per-header section for core headers."""
    h = header_info
    lines = [
        f"### {h['name']}",
        "",
        f"- **Path:** `{h['path']}`",
        f"- **Lines:** {h['lines']}",
    ]
    if h["brief"]:
        lines.append(f"- **Brief:** {h['brief']}")
    lines.append("")

    if h["functions"]:
        func_names = [f"`{f['name']}`" for f in h["functions"]]
        lines.extend(["**Functions:**", "", ", ".join(func_names), ""])

    key_types = []
    for s in h["structs"]:
        key_types.append(f"`{s['name']}` (struct, {s['field_count']} fields)")
    for union in h["unions"]:
        key_types.append(f"`{union['name']}` (union, {union['field_count']} fields)")
    for e in h["enums"]:
        key_types.append(f"`{e['name']}` (enum, {e['value_count']} values)")
    for cb in h["callbacks"]:
        key_types.append(f"`{cb}` (callback typedef)")
    for ta in h["type_aliases"]:
        key_types.append(f"`{ta['name']}` = `{ta['source']}`")

    if key_types:
        lines.extend(["**Key Types:**", ""])
        lines.extend(f"- {kt}" for kt in key_types)
        lines.append("")

    if h["sample_refs"]:
        lines.extend(["**Sample References:**", ""])
        lines.extend(
            f"- `third_party/psdk/samples/sample_c/{ref}`"
            for ref in h["sample_refs"]
        )
        lines.append("")

    return "\n".join(lines)


def generate_header_map(headers):
    lines = [
        "## 2. Header-by-Header API Map",
        "",
        "> Note: This is a navigation index, not primary documentation. "
        "Always verify against the actual header file before using any declaration.",
        "",
        "### 2.1 Core Headers (Vision Detect / Liveview Relevant)",
        "",
    ]

    for hname in CORE_HEADERS:
        if hname in headers:
            lines.append(generate_header_section(headers[hname]))

    lines.extend([
        "### 2.2 Additional Headers",
        "",
        "| Header | Funcs | Structs | Unions | Enums | Brief |",
        "|--------|------:|--------:|-------:|------:|-------|",
    ])

    non_core = sorted(set(headers) - set(CORE_HEADERS))
    for hname in non_core:
        h = headers[hname]
        brief = h["brief"] or ""
        if len(brief) > 90:
            brief = brief[:87] + "..."
        brief = brief.replace("|", "\\|")
        lines.append(
            f"| `{h['name']}` | {len(h['functions'])} | {len(h['structs'])} | "
            f"{len(h['unions'])} | {len(h['enums'])} | {brief} |"
        )
    lines.append("")
    return "\n".join(lines)


def generate_init_sequence():
    """Generate the PSDK initialization call-order table.

    This is manually curated domain knowledge that cannot be extracted from
    headers alone.
    """
    steps = [
        ("Register OSAL handler", "DjiPlatform_RegOsalHandler()", "dji_platform.h",
         "Provide task, mutex, semaphore, memory, time functions."),
        ("Register logger consoles", "DjiLogger_AddConsole()", "dji_logger.h",
         "Register print and/or file log output."),
        ("Register HAL USB Bulk handler", "DjiPlatform_RegHalUsbBulkHandler()", "dji_platform.h",
         "Manifold 3 uses USB Bulk device mode for transport."),
        ("Register socket handler", "DjiPlatform_RegSocketHandler()", "dji_platform.h",
         "Required for camera stream view and data channels."),
        ("Register filesystem handler", "DjiPlatform_RegFileSystemHandler()", "dji_platform.h",
         "Required for media files and logging."),
        ("Initialize PSDK core", "DjiCore_Init(&userInfo)", "dji_core.h",
         "Blocking call; establishes connection with aircraft. Requires all handlers registered."),
        ("Query aircraft info", "DjiAircraftInfo_GetBaseInfo() / GetAircraftVersion()", "dji_aircraft_info.h",
         "Determine aircraft type, series, mount position, firmware version."),
        ("Set product identity", "DjiCore_SetAlias() / SetFirmwareVersion() / SetSerialNumber()", "dji_core.h",
         "Configure display name, payload version, serial number."),
        ("Initialize/register selected modules", "e.g. DjiLiveview_Init(), DjiCameraManager_Init()", "per-module",
         "The core header requires module init and registration before ApplicationStart."),
        ("Start application", "DjiCore_ApplicationStart()", "dji_core.h",
         "Final lifecycle transition. Aircraft negotiation completes here."),
        ("Start streams/subscriptions", "e.g. DjiLiveview_StartImageStream()", "per-module",
         "Activate data flow for the required features."),
    ]

    lines = [
        "## 3. PSDK Initialization Sequence",
        "",
        "Conservative call order derived from the PSDK 3.16.0 core header. All platform",
        "`Reg*Handler` calls must happen before `DjiCore_Init()`. Initialize and register",
        "selected functional modules before `DjiCore_ApplicationStart()`. The Manifold 3 sample",
        "starts some optional sample services after ApplicationStart; treat that as sample-specific",
        "behavior, not a general module lifecycle rule.",
        "",
        "| Step | Function | Header | Notes |",
        "|------|----------|--------|-------|",
    ]
    for i, (label, func, header, notes) in enumerate(steps, 1):
        lines.append(f"| {i}. {label} | `{func}` | `{header}` | {notes} |")
    lines.extend([
        "",
        "Shutdown proceeds in reverse order: stop streams -> deinit modules -> "
        "`DjiCore_DeInit()`.",
        "",
    ])
    return "\n".join(lines)


def generate_platform_checklist(platform_header):
    """Generate the Manifold 3 platform porting checklist.

    This is manually curated domain knowledge.
    """
    handlers = {
        item["name"]: item
        for item in platform_header["structs"]
        if item["name"].endswith("Handler")
    }
    fields = platform_header["function_pointer_fields"]
    required_handlers = [
        ("T_DjiOsalHandler", "DjiPlatform_RegOsalHandler", "platform/linux/common/osal/osal.c"),
        ("T_DjiHalUsbBulkHandler", "DjiPlatform_RegHalUsbBulkHandler", "platform/linux/manifold3/hal/hal_usb_bulk.c"),
        ("T_DjiFileSystemHandler", "DjiPlatform_RegFileSystemHandler", "platform/linux/common/osal/osal_fs.c"),
        ("T_DjiSocketHandler", "DjiPlatform_RegSocketHandler", "platform/linux/common/osal/osal_socket.c"),
    ]
    optional_handlers = [
        ("T_DjiHalUartHandler", "DjiPlatform_RegHalUartHandler"),
        ("T_DjiHalNetworkHandler", "DjiPlatform_RegHalNetworkHandler"),
        ("T_DjiHalI2cHandler", "DjiPlatform_RegHalI2cHandler"),
    ]

    lines = [
        "## 4. Platform Porting Checklist (Manifold 3)",
        "",
        "Handlers that must be implemented and registered via `DjiPlatform_Reg*Handler()`",
        "before calling `DjiCore_Init()`.",
        "",
        "### 4.1 Required Handlers",
        "",
        "| Handler Struct | Registration Function | Funcs | Reference |",
        "|----------------|----------------------|-------|-----------|",
    ]
    for handler, register_function, reference in required_handlers:
        lines.append(
            f"| `{handler}` | `{register_function}()` | "
            f"{handlers[handler]['field_count']} | `{reference}` |"
        )
    lines.extend([
        "",
        "### 4.2 Optional Handlers (not used by default Manifold 3 configuration)",
        "",
        "| Handler Struct | Registration Function | Funcs |",
        "|----------------|----------------------|-------|",
    ])
    for handler, register_function in optional_handlers:
        lines.append(
            f"| `{handler}` | `{register_function}()` | "
            f"{handlers[handler]['field_count']} |"
        )

    for section_number, handler in enumerate(
        ("T_DjiOsalHandler", "T_DjiHalUsbBulkHandler"), start=3
    ):
        lines.extend(["", f"### 4.{section_number} {handler} Function Pointers", ""])
        for field in fields[handler]:
            params = field["params"] or "void"
            lines.append(
                f"- `{field['name']}({params}) -> {field['return_type']}`"
            )

    lines.extend([
        "",
        "Manifold 3 uses **USB Bulk device mode** (FunctionFS at `/dev/usb-ffs/`) with three configured channels.",
        "",
    ])
    return "\n".join(lines)


def generate_symbol_index(headers):
    """Generate the flat alphabetical symbol-to-header index."""
    symbols = []
    for hname, h in headers.items():
        for func in h["functions"]:
            symbols.append((func["name"], "fn", hname))
        for s in h["structs"]:
            symbols.append((s["name"], "struct", hname))
        for union in h["unions"]:
            symbols.append((union["name"], "union", hname))
        for e in h["enums"]:
            symbols.append((e["name"], "enum", hname))
        for cb in h["callbacks"]:
            symbols.append((cb, "cb", hname))
        for ta in h["type_aliases"]:
            symbols.append((ta["name"], "type", hname))

    symbols.sort(key=lambda x: x[0].lower())

    lines = [
        "## 5. Flat Symbol Index",
        "",
        "Alphabetical index of public functions and declared type names. Use `grep` to search this section.",
        "Kind abbreviations: fn=function, struct=struct, union=union, enum=enum, cb=callback typedef, type=type alias.",
        "",
    ]
    current_letter = ""
    for name, kind, header in symbols:
        letter = name[0].upper()
        if letter != current_letter:
            current_letter = letter
            lines.extend([f"**{current_letter}**", ""])
        lines.append(f"- `{name}` ({kind}) - `{header}`")
    lines.append("")
    return "\n".join(lines)


def write_text_atomic(output_path, content):
    """Atomically replace output_path with UTF-8 content."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_mode = (
        output_path.stat().st_mode & 0o777
        if output_path.exists()
        else DEFAULT_OUTPUT_MODE
    )
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=output_path.parent,
            prefix=f".{output_path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary_file:
            temporary_file.write(content)
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
            temporary_path = Path(temporary_file.name)
        temporary_path.chmod(output_mode)
        os.replace(temporary_path, output_path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description="Generate PSDK API index from local headers")
    parser.add_argument(
        "--output",
        default=str(DEFAULT_OUTPUT),
        help=f"Output file path (default: {DEFAULT_OUTPUT})",
    )
    args = parser.parse_args()

    if not PSDK_INCLUDE_DIR.is_dir():
        print(f"Error: PSDK include directory not found: {PSDK_INCLUDE_DIR}", file=sys.stderr)
        sys.exit(1)

    header_files = sorted(PSDK_INCLUDE_DIR.glob("dji_*.h"))
    if not header_files:
        print(f"Error: No dji_*.h files found in {PSDK_INCLUDE_DIR}", file=sys.stderr)
        sys.exit(1)

    headers = {fp.name: parse_header(fp) for fp in header_files}
    validate_static_metadata(headers)
    version = read_psdk_version(PSDK_INCLUDE_DIR / "dji_version.h")

    totals = {
        "functions": sum(len(h["functions"]) for h in headers.values()),
        "structs": sum(len(h["structs"]) for h in headers.values()),
        "unions": sum(len(h["unions"]) for h in headers.values()),
        "enums": sum(len(h["enums"]) for h in headers.values()),
        "callbacks": sum(len(h["callbacks"]) for h in headers.values()),
    }

    doc_parts = [
        generate_doc_header(headers, totals, version),
        generate_quick_reference(headers),
        "---\n",
        generate_header_map(headers),
        "---\n",
        generate_init_sequence(),
        "---\n",
        generate_platform_checklist(headers["dji_platform.h"]),
        "---\n",
        generate_symbol_index(headers),
    ]

    output_path = Path(args.output)
    document = "\n".join(doc_parts)
    write_text_atomic(output_path, document)

    total_lines = len(document.splitlines())
    print(f"Generated API index: {output_path}")
    print(f"  Headers: {len(headers)}")
    print(f"  Functions: {totals['functions']}")
    print(f"  Structs: {totals['structs']}")
    print(f"  Unions: {totals['unions']}")
    print(f"  Enums: {totals['enums']}")
    print(f"  Callbacks: {totals['callbacks']}")
    print(f"  Total lines: {total_lines}")


def entrypoint():
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    entrypoint()
