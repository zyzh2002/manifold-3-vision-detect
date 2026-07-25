import importlib.util
import io
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).parents[1] / "scripts" / "generate-api-index.py"
SPEC = importlib.util.spec_from_file_location("generate_api_index", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Unable to load API index generator from {SCRIPT_PATH}")
generate_api_index = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generate_api_index)


class ApiIndexParserTestCase(unittest.TestCase):
    def test_preprocessor_continuation_lines_are_removed(self):
        source = """
#define FAKE_DECLARATION \\
    T_DjiReturnCode DjiFake_Function(void); \\
    typedef uint32_t T_DjiFakeType;
T_DjiReturnCode DjiReal_Function(void);
"""

        clean = generate_api_index.strip_comments_and_preproc(source)

        self.assertEqual(
            [item["name"] for item in generate_api_index.extract_functions(clean)],
            ["DjiReal_Function"],
        )
        self.assertEqual(generate_api_index.extract_type_aliases(clean), [])

    def test_extracts_packed_struct_without_polluting_following_struct(self):
        source = """
typedef struct {
    uint16_t x;
    uint16_t y;
} __attribute__((packed)) T_DjiPackedPoint;

typedef struct {
    uint32_t id;
} T_DjiPlainInfo;
"""

        structs = generate_api_index.extract_structs(source)

        self.assertEqual(
            structs,
            [
                {"name": "T_DjiPackedPoint", "field_count": 2},
                {"name": "T_DjiPlainInfo", "field_count": 1},
            ],
        )

    def test_counts_only_top_level_enum_commas(self):
        source = """
typedef enum {
    TOPIC_A = MAKE_TOPIC(MODULE_A, 0),
    TOPIC_B = MAKE_TOPIC(MODULE_A, 1),
    TOPIC_COUNT,
} E_DjiTopic;
"""

        enums = generate_api_index.extract_enums(source)

        self.assertEqual(enums, [{"name": "E_DjiTopic", "value_count": 3}])

    def test_extracts_multiple_typedef_names_for_one_enum(self):
        source = """
typedef enum {
    STREAM_A = 0,
    STREAM_B = 1,
} E_DjiStreamSource, E_DjiStreamStorage;
"""

        enums = generate_api_index.extract_enums(source)

        self.assertEqual(
            enums,
            [
                {"name": "E_DjiStreamSource", "value_count": 2},
                {"name": "E_DjiStreamStorage", "value_count": 2},
            ],
        )

    def test_extracts_functions_with_pointer_return_types(self):
        source = """
T_DjiOsalHandler *DjiPlatform_GetOsalHandler(void);
T_DjiReturnCode
DjiFlightController_SetRtkPositionEnableStatus(uint8_t enabled);
T_DjiReturnCode DjiCore_Init(const T_DjiUserInfo *userInfo);
"""

        functions = generate_api_index.extract_functions(source)

        self.assertEqual(
            [function["name"] for function in functions],
            [
                "DjiPlatform_GetOsalHandler",
                "DjiFlightController_SetRtkPositionEnableStatus",
                "DjiCore_Init",
            ],
        )

    def test_function_extraction_ignores_typedefs_and_handler_members(self):
        source = """
typedef T_DjiReturnCode (*DjiDataCallback)(const uint8_t *data);
T_DjiReturnCode (*TaskCreate)(const char *name, void *(*taskFunc)(void *));
T_DjiReturnCode DjiCore_Init(const T_DjiUserInfo *userInfo);
"""

        functions = generate_api_index.extract_functions(source)

        self.assertEqual(
            [function["name"] for function in functions],
            ["DjiCore_Init"],
        )

    def test_extracts_all_function_pointer_typedef_names(self):
        source = """
typedef T_DjiReturnCode (*ConsoleFunc)(const uint8_t *data, uint16_t len);
typedef T_DjiReturnCode (*WaypointV2EventCbFunc)(uint32_t event);
typedef void (*DjiLiveview_ImageCallback)(const uint8_t *data);
"""

        callbacks = generate_api_index.extract_callback_typedefs(source)

        self.assertEqual(
            callbacks,
            ["ConsoleFunc", "WaypointV2EventCbFunc", "DjiLiveview_ImageCallback"],
        )

    def test_extracts_named_enum_union_and_non_dji_aliases(self):
        source = """
typedef double dji_f64_t;
typedef void *T_DjiHandle;
typedef union {
    uint8_t byte;
    uint16_t word;
} U_DjiValue;
enum DjiErrorCode {
    DJI_ERROR_OK = ERROR_CODE(MODULE_SYSTEM, 0),
    DJI_ERROR_FAILED = ERROR_CODE(MODULE_SYSTEM, 1),
};
"""

        aliases = generate_api_index.extract_type_aliases(source)
        unions = generate_api_index.extract_unions(source)
        enums = generate_api_index.extract_enums(source)

        self.assertEqual(
            aliases,
            [
                {"name": "dji_f64_t", "source": "double"},
                {"name": "T_DjiHandle", "source": "void *"},
            ],
        )
        self.assertEqual(unions, [{"name": "U_DjiValue", "field_count": 2}])
        self.assertEqual(enums, [{"name": "DjiErrorCode", "value_count": 2}])


class ApiIndexRepositoryTestCase(unittest.TestCase):
    def test_psdk_version_is_read_from_local_header(self):
        version = generate_api_index.read_psdk_version(
            generate_api_index.PSDK_INCLUDE_DIR / "dji_version.h"
        )

        self.assertEqual(
            version,
            {
                "major": 3,
                "minor": 16,
                "modify": 0,
                "beta": 0,
                "build": 2338,
                "display": "3.16.0",
            },
        )

    def test_static_metadata_covers_every_local_header(self):
        headers = {
            path.name: generate_api_index.parse_header(path)
            for path in generate_api_index.PSDK_INCLUDE_DIR.glob("dji_*.h")
        }

        generate_api_index.validate_static_metadata(headers)

    def test_local_psdk_regression_cases_are_indexed(self):
        liveview = generate_api_index.parse_header(
            generate_api_index.PSDK_INCLUDE_DIR / "dji_liveview.h"
        )
        platform = generate_api_index.parse_header(
            generate_api_index.PSDK_INCLUDE_DIR / "dji_platform.h"
        )
        subscription = generate_api_index.parse_header(
            generate_api_index.PSDK_INCLUDE_DIR / "dji_fc_subscription.h"
        )
        error = generate_api_index.parse_header(
            generate_api_index.PSDK_INCLUDE_DIR / "dji_error.h"
        )

        liveview_structs = {item["name"]: item for item in liveview["structs"]}
        self.assertEqual(liveview_structs["T_DjiLiveViewTarget2dBox"]["field_count"], 5)
        self.assertEqual(liveview_structs["T_DjiLiveviewImageInfo"]["field_count"], 4)

        platform_functions = {item["name"] for item in platform["functions"]}
        self.assertIn("DjiPlatform_GetOsalHandler", platform_functions)
        self.assertIn("DjiPlatform_GetSocketHandler", platform_functions)

        topic_enum = next(
            item for item in subscription["enums"]
            if item["name"] == "E_DjiFcSubscriptionTopic"
        )
        self.assertEqual(topic_enum["value_count"], 57)
        self.assertIn("DjiErrorCode", {item["name"] for item in error["enums"]})

    def test_all_sample_cross_references_exist(self):
        sample_root = (
            generate_api_index.REPO_ROOT
            / "third_party"
            / "psdk"
            / "samples"
            / "sample_c"
        )

        missing = [
            str(sample_root / relative_path)
            for paths in generate_api_index.SAMPLE_CROSS_REF.values()
            for relative_path in paths
            if not (sample_root / relative_path).is_file()
        ]

        self.assertEqual(missing, [])

    def test_initialization_guidance_follows_core_header_contract(self):
        generated = generate_api_index.generate_init_sequence()

        self.assertIn("before `DjiCore_ApplicationStart()`", generated)
        self.assertNotIn("All other module init happens after", generated)
        self.assertIn("Manifold 3 sample", generated)

    def test_platform_checklist_is_derived_from_handler_structs(self):
        platform = generate_api_index.parse_header(
            generate_api_index.PSDK_INCLUDE_DIR / "dji_platform.h"
        )

        generated = generate_api_index.generate_platform_checklist(platform)

        self.assertIn("`T_DjiOsalHandler` | `DjiPlatform_RegOsalHandler()` | 17", generated)
        self.assertIn("`T_DjiHalI2cHandler` | `DjiPlatform_RegHalI2cHandler()` | 4", generated)
        self.assertIn(
            "`UsbBulkWriteData(T_DjiUsbBulkHandle usbBulkHandle, const uint8_t *buf, "
            "uint32_t len, uint32_t *realLen) -> T_DjiReturnCode`",
            generated,
        )
        self.assertNotIn("perception/media, and data", generated)

    def test_atomic_write_preserves_existing_file_if_replace_fails(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "api-index.md"
            output_path.write_text("original\n", encoding="utf-8")

            with mock.patch.object(
                generate_api_index.os,
                "replace",
                side_effect=OSError("replace failed"),
            ):
                with self.assertRaisesRegex(OSError, "replace failed"):
                    generate_api_index.write_text_atomic(output_path, "replacement\n")

            self.assertEqual(output_path.read_text(encoding="utf-8"), "original\n")
            temporary_files = list(Path(temp_dir).glob(".api-index.md.*.tmp"))
            self.assertEqual(temporary_files, [])

    def test_atomic_write_preserves_existing_permissions(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "api-index.md"
            output_path.write_text("original\n", encoding="utf-8")
            output_path.chmod(0o644)

            generate_api_index.write_text_atomic(output_path, "replacement\n")

            self.assertEqual(output_path.read_text(encoding="utf-8"), "replacement\n")
            self.assertEqual(output_path.stat().st_mode & 0o777, 0o644)

    def test_atomic_write_creates_readable_file(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "api-index.md"

            generate_api_index.write_text_atomic(output_path, "content\n")

            self.assertEqual(output_path.stat().st_mode & 0o777, 0o644)

    def test_entrypoint_reports_expected_errors_without_traceback(self):
        stderr = io.StringIO()
        with mock.patch.object(
            generate_api_index,
            "main",
            side_effect=ValueError("invalid metadata"),
        ), mock.patch("sys.stderr", stderr):
            with self.assertRaisesRegex(SystemExit, "1"):
                generate_api_index.entrypoint()

        self.assertEqual(stderr.getvalue(), "Error: invalid metadata\n")


if __name__ == "__main__":
    unittest.main()
