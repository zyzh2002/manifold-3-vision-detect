# Result Output (SQLite Structured Storage) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist board-side YOLO detection results into a SQLite database, deduplicated by a stable tracker identity, and reconstruct offline reports without any video stream.

**Architecture:** Build a new `src/output/` module with two pure-C++ units — `ObjectTracker` (inter-frame IoU greedy matching assigns stable `object_id`, emits ADD/UPDATE/REMOVE events) and SQL generation (event -> row). The SQLite C API is used only in a cross-only wrapper (`SqliteSink`) so the host build (which lacks sqlite3 dev headers) still unit-tests the tracker and SQL. Extend the device-derived sysroot with sqlite3 (dpkg-verified, same flow as Phase 5). Wire the tracker+sink into the existing app pipeline after `DecodeSyntheticSeg`. Add an offline report script that reads the DB.

**Tech Stack:** C++17, CMake (host-debug + manifold3-cross-release), SQLite 3.31.1, PSDK 3.16.0 (`DjiFcSubscription_*`), bash (fake-ssh/scp script tests).

## Global Constraints

- C++17; LLVM clang-format 120; snake_case files/functions/variables; PascalCase types; no Chinese in code/comments.
- PSDK 3.16.0 local headers/samples are the version authority. Manifold 3: JetPack 5.1.3, r35.5.0, CUDA 11.4, TensorRT 8.5.2.
- Host build must keep working; cross build never resolves host x86_64 paths.
- `third_party/psdk/` read-only. `sysroot/` git-ignored. Real credentials only via CMake cache; never commit.
- sqlite3 header is NOT on the host, so no host test may `#include <sqlite3.h>` or call sqlite3 API. Host tests cover `ObjectTracker` and SQL string generation only. `SqliteSink` is cross-compile-only (like `tensorrt_engine.cpp`).
- Device sqlite3 package version: `libsqlite3-dev 3.31.1-4ubuntu0.6` (dpkg-verified extension).
- Report geometry is source-frame pixel coordinates; aircraft GNSS is radians (lon/lat) + meters WGS84 (alt) from `DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED`.

## Reference

- Spec: `.agents/docs/specs/2026-08-04-result-output-design.md`
- Branch: `main`; work on one short-lived branch `feat/result-output`.

---

### Task 1: ObjectTracker (pure C++, host-testable)

**Files:**
- Create: `src/output/object_tracker.h`, `src/output/object_tracker.cpp`, `src/output/CMakeLists.txt`
- Modify: `src/CMakeLists.txt:5` (add `add_subdirectory(output)`)
- Create: `tests/output/test_object_tracker.cpp`, `tests/output/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt:5` (add `add_subdirectory(output)`)

**Interfaces:**
- Consumes: `manifold3::inference::Detection` from `src/inference/inference_types.h` (`species_id`, `age_class_id`, `confidence`, `cx`, `cy`, `w`, `h`; normalized center+size in 0..65535 units).
- Produces `manifold3::output`:
  - `struct TrackedDetection { uint32_t object_id; uint16_t species_id; uint16_t age_class_id; float confidence; uint16_t cx; uint16_t cy; uint16_t w; uint16_t h; bool is_new; bool property_changed; };`
  - `class ObjectTracker` with `std::vector<TrackedDetection> Update(const std::vector<inference::Detection>& dets, uint32_t frame_id)` (one entry per input detection, each with assigned `object_id`, `is_new`, `property_changed`) and `std::vector<TrackedDetection> UpdateAndGetRemoved(uint32_t frame_id)` (removed tracks with their last-known fields — species/age/box/conf — populated; these are the tracks absent >= `kLostFrameThreshold` frames).
  - Constants `kIoUThreshold = 0.4f`, `kLostFrameThreshold = 30`.

- [ ] **Step 1: Write the failing test**

Create `tests/output/test_object_tracker.cpp`:

```cpp
#include <cassert>
#include <cstdint>
#include <vector>

#include "inference/inference_types.h"
#include "output/object_tracker.h"

using manifold3::inference::Detection;
using manifold3::output::ObjectTracker;
using manifold3::output::TrackedDetection;

static Detection MakeDet(uint16_t cx, uint16_t cy, uint16_t w, uint16_t h,
                         uint16_t species, uint16_t age, float conf) {
    Detection d;
    d.species_id = species;
    d.age_class_id = age;
    d.confidence = conf;
    d.cx = cx;
    d.cy = cy;
    d.w = w;
    d.h = h;
    return d;
}

int main() {
    ObjectTracker tracker;

    std::vector<TrackedDetection> out1 =
        tracker.Update({MakeDet(1000, 1000, 200, 200, 0, 1, 0.9f)}, 1);
    assert(out1.size() == 1);
    assert(out1[0].object_id == 1);
    assert(out1[0].is_new);
    assert(!out1[0].property_changed);

    std::vector<TrackedDetection> out2 =
        tracker.Update({MakeDet(1005, 1000, 200, 200, 0, 1, 0.9f)}, 2);
    assert(out2.size() == 1);
    assert(out2[0].object_id == 1);
    assert(!out2[0].is_new);
    assert(!out2[0].property_changed);

    std::vector<TrackedDetection> out3 =
        tracker.Update({MakeDet(1005, 1000, 200, 200, 0, 2, 0.9f)}, 3);
    assert(out3.size() == 1);
    assert(out3[0].object_id == 1);
    assert(out3[0].property_changed);

    std::vector<TrackedDetection> out4 =
        tracker.Update({MakeDet(30000, 30000, 100, 100, 0, 1, 0.8f)}, 4);
    assert(out4.size() == 1);
    assert(out4[0].object_id == 2);
    assert(out4[0].is_new);

    std::vector<TrackedDetection> out5 = tracker.Update(
        {MakeDet(1005, 1000, 200, 200, 0, 2, 0.9f), MakeDet(30005, 30000, 100, 100, 0, 1, 0.8f)}, 5);
    assert(out5.size() == 2);
    assert(out5[0].object_id == 1);
    assert(out5[1].object_id == 2);
    assert(!out5[0].is_new && !out5[1].is_new);

    std::vector<TrackedDetection> removed = tracker.UpdateAndGetRemoved(6);
    assert(removed.empty());  // no track has been missed 30 frames yet

    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --preset host-debug && cmake --build --preset host-debug --target test_object_tracker`
Expected: FAIL — `object_tracker.h` does not exist; include error.

- [ ] **Step 3: Write minimal implementation**

Create `src/output/object_tracker.h`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include "inference/inference_types.h"

namespace manifold3 {
namespace output {

constexpr float kIoUThreshold = 0.4f;
constexpr uint32_t kLostFrameThreshold = 30;

struct TrackedDetection {
    uint32_t object_id;
    uint16_t species_id;
    uint16_t age_class_id;
    float confidence;
    uint16_t cx;
    uint16_t cy;
    uint16_t w;
    uint16_t h;
    bool is_new;
    bool property_changed;
};

class ObjectTracker {
  public:
    explicit ObjectTracker(float iou_threshold = kIoUThreshold,
                           uint32_t lost_frame_threshold = kLostFrameThreshold);

    std::vector<TrackedDetection> Update(const std::vector<inference::Detection> &dets, uint32_t frame_id);
    std::vector<TrackedDetection> UpdateAndGetRemoved(uint32_t frame_id);

  private:
    struct Track {
        uint32_t object_id;
        uint16_t cx, cy, w, h;
        uint16_t species_id, age_class_id;
        float confidence;
        uint32_t last_seen_frame;
        uint32_t consecutive_miss;
    };
    float iou_threshold_;
    uint32_t lost_frame_threshold_;
    uint32_t next_object_id_ = 1;
    std::vector<Track> tracks_;
    float IoU(const Track &t, const inference::Detection &d) const;
};

} // namespace output
} // namespace manifold3
```

Create `src/output/object_tracker.cpp`:

```cpp
#include "output/object_tracker.h"

#include <algorithm>

namespace manifold3 {
namespace output {

ObjectTracker::ObjectTracker(float iou_threshold, uint32_t lost_frame_threshold)
    : iou_threshold_(iou_threshold), lost_frame_threshold_(lost_frame_threshold) {}

float ObjectTracker::IoU(const Track &t, const inference::Detection &d) const {
    const float ax1 = t.cx - t.w / 2.0f, ay1 = t.cy - t.h / 2.0f;
    const float ax2 = t.cx + t.w / 2.0f, ay2 = t.cy + t.h / 2.0f;
    const float bx1 = d.cx - d.w / 2.0f, by1 = d.cy - d.h / 2.0f;
    const float bx2 = d.cx + d.w / 2.0f, by2 = d.cy + d.h / 2.0f;
    const float ix1 = std::max(ax1, bx1), iy1 = std::max(ay1, by1);
    const float ix2 = std::min(ax2, bx2), iy2 = std::min(ay2, by2);
    const float inter = (ix2 > ix1 && iy2 > iy1) ? (ix2 - ix1) * (iy2 - iy1) : 0.0f;
    const float aarea = (ax2 - ax1) * (ay2 - ay1);
    const float barea = (bx2 - bx1) * (by2 - by1);
    const float u = aarea + barea - inter;
    return u > 0.0f ? inter / u : 0.0f;
}

std::vector<TrackedDetection> ObjectTracker::Update(const std::vector<inference::Detection> &dets,
                                                    uint32_t frame_id) {
    std::vector<TrackedDetection> out;
    if (dets.empty()) {
        return out;
    }
    std::vector<bool> matched(tracks_.size(), false);
    for (const inference::Detection &d : dets) {
        int best_idx = -1;
        float best_iou = iou_threshold_;
        for (size_t i = 0; i < tracks_.size(); ++i) {
            if (matched[i]) {
                continue;
            }
            const float iou = IoU(tracks_[i], d);
            if (iou > best_iou) {
                best_iou = iou;
                best_idx = static_cast<int>(i);
            }
        }
        TrackedDetection td;
        if (best_idx >= 0) {
            Track &t = tracks_[static_cast<size_t>(best_idx)];
            matched[static_cast<size_t>(best_idx)] = true;
            td.object_id = t.object_id;
            td.is_new = false;
            td.property_changed = (t.age_class_id != d.age_class_id ||
                                   t.species_id != d.species_id);
            td.species_id = d.species_id;
            td.age_class_id = d.age_class_id;
            td.confidence = d.confidence;
            td.cx = d.cx;
            td.cy = d.cy;
            td.w = d.w;
            td.h = d.h;
            t.cx = d.cx;
            t.cy = d.cy;
            t.w = d.w;
            t.h = d.h;
            t.species_id = d.species_id;
            t.age_class_id = d.age_class_id;
            t.confidence = d.confidence;
            t.last_seen_frame = frame_id;
            t.consecutive_miss = 0;
        } else {
            td.object_id = next_object_id_++;
            td.is_new = true;
            td.property_changed = false;
            td.species_id = d.species_id;
            td.age_class_id = d.age_class_id;
            td.confidence = d.confidence;
            td.cx = d.cx;
            td.cy = d.cy;
            td.w = d.w;
            td.h = d.h;
            Track t;
            t.object_id = td.object_id;
            t.cx = d.cx;
            t.cy = d.cy;
            t.w = d.w;
            t.h = d.h;
            t.species_id = d.species_id;
            t.age_class_id = d.age_class_id;
            t.confidence = d.confidence;
            t.last_seen_frame = frame_id;
            t.consecutive_miss = 0;
            tracks_.push_back(t);
        }
        out.push_back(td);
    }
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (!matched[i]) {
            ++tracks_[i].consecutive_miss;
        }
    }
    return out;
}

std::vector<TrackedDetection> ObjectTracker::UpdateAndGetRemoved(uint32_t frame_id) {
    std::vector<TrackedDetection> removed;
    std::vector<Track> kept;
    for (const Track &t : tracks_) {
        if (t.consecutive_miss >= lost_frame_threshold_) {
            TrackedDetection td;
            td.object_id = t.object_id;
            td.species_id = t.species_id;
            td.age_class_id = t.age_class_id;
            td.confidence = t.confidence;
            td.cx = t.cx;
            td.cy = t.cy;
            td.w = t.w;
            td.h = t.h;
            td.is_new = false;
            td.property_changed = false;
            removed.push_back(td);
        } else {
            kept.push_back(t);
        }
    }
    tracks_ = std::move(kept);
    return removed;
}

} // namespace output
} // namespace manifold3
```

- [ ] **Step 4: Add CMake for the new module**

Create `src/output/CMakeLists.txt`:

```cmake
add_library(output STATIC
    object_tracker.cpp
)
target_include_directories(output PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
```

Modify `src/CMakeLists.txt` to add `add_subdirectory(output)` after the `inference` line.

Create `tests/output/CMakeLists.txt`:

```cmake
add_executable(test_object_tracker test_object_tracker.cpp)
target_link_libraries(test_object_tracker PRIVATE output)
add_test(NAME object_tracker COMMAND test_object_tracker)
```

Modify `tests/CMakeLists.txt` to add `add_subdirectory(output)` after the `inference` line.

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug`
Expected: PASS (`object_tracker` test present and green).

- [ ] **Step 6: Commit**

```bash
git add src/output tests/output src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add ObjectTracker for stable detection identity"
```

---

### Task 2: SqliteRow SQL generation (pure C++, host-testable)

**Files:**
- Create: `src/output/sqlite_row.h`, `src/output/sqlite_row.cpp`
- Modify: `src/output/CMakeLists.txt` (add `sqlite_row.cpp`)
- Create: `tests/output/test_sqlite_row.cpp`
- Modify: `tests/output/CMakeLists.txt` (add test)

**Interfaces:**
- Consumes: `manifold3::output::TrackedDetection` (Task 1).
- Produces `manifold3::output`:
  - `struct GpsPosition { double lon; double lat; double alt; double ts; uint32_t frame_id; };`
  - `std::string BuildInsertRow(const TrackedDetection& d, const GpsPosition& gps, const char* event);` — returns a single SQLite INSERT statement string for one row, with values already escaped (floats formatted, integers as-is).
  - `std::string CreateSchemaSql();` — returns the `CREATE TABLE detections (...)` plus `CREATE INDEX` statements.

- [ ] **Step 1: Write the failing test**

Create `tests/output/test_sqlite_row.cpp`:

```cpp
#include <cassert>
#include <string>

#include "output/object_tracker.h"
#include "output/sqlite_row.h"

using manifold3::output::BuildInsertRow;
using manifold3::output::CreateSchemaSql;
using manifold3::output::GpsPosition;
using manifold3::output::TrackedDetection;

int main() {
    const std::string schema = CreateSchemaSql();
    assert(schema.find("CREATE TABLE detections") != std::string::npos);
    assert(schema.find("CREATE INDEX idx_object") != std::string::npos);
    assert(schema.find("CREATE INDEX idx_frame") != std::string::npos);

    TrackedDetection d;
    d.object_id = 7;
    d.species_id = 1;
    d.age_class_id = 2;
    d.confidence = 0.92f;
    d.cx = 210;
    d.cy = 340;
    d.w = 880;
    d.h = 520;
    d.is_new = true;
    d.property_changed = false;

    GpsPosition gps;
    gps.lon = 1.234567;
    gps.lat = -0.987654;
    gps.alt = 150.5;
    gps.ts = 1023.5;
    gps.frame_id = 30120;

    const std::string sql = BuildInsertRow(d, gps, "ADD");
    assert(sql.find("INSERT INTO detections") != std::string::npos);
    assert(sql.find("7") != std::string::npos);          // object_id
    assert(sql.find("1") != std::string::npos);          // species_id
    assert(sql.find("2") != std::string::npos);          // age_class_id
    assert(sql.find("0.92") != std::string::npos);       // confidence
    assert(sql.find("210,340,880,520") != std::string::npos);  // box_px
    assert(sql.find("1.234567") != std::string::npos);   // lon
    assert(sql.find("-0.987654") != std::string::npos);  // lat
    assert(sql.find("150.5") != std::string::npos);      // alt
    assert(sql.find("1023.5") != std::string::npos);     // ts
    assert(sql.find("30120") != std::string::npos);      // frame_id
    assert(sql.find("'ADD'") != std::string::npos);      // event quoted
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --preset host-debug && cmake --build --preset host-debug --target test_sqlite_row`
Expected: FAIL — `sqlite_row.h` does not exist; include error.

- [ ] **Step 3: Write minimal implementation**

Create `src/output/sqlite_row.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>

#include "output/object_tracker.h"

namespace manifold3 {
namespace output {

struct GpsPosition {
    double lon;
    double lat;
    double alt;
    double ts;
    uint32_t frame_id;
};

std::string CreateSchemaSql();
std::string BuildInsertRow(const TrackedDetection &d, const GpsPosition &gps, const char *event);

} // namespace output
} // namespace manifold3
```

Create `src/output/sqlite_row.cpp`:

```cpp
#include "output/sqlite_row.h"

#include <cstdio>

namespace manifold3 {
namespace output {

std::string CreateSchemaSql() {
    return "CREATE TABLE IF NOT EXISTS detections ("
           "id INTEGER PRIMARY KEY,"
           "frame_id INTEGER NOT NULL,"
           "ts REAL NOT NULL,"
           "object_id INTEGER NOT NULL,"
           "class TEXT NOT NULL,"
           "age TEXT NOT NULL,"
           "conf REAL NOT NULL,"
           "box_px TEXT NOT NULL,"
           "lon REAL NOT NULL,"
           "lat REAL NOT NULL,"
           "alt REAL NOT NULL,"
           "event TEXT NOT NULL);"
           "CREATE INDEX IF NOT EXISTS idx_object ON detections(object_id);"
           "CREATE INDEX IF NOT EXISTS idx_frame ON detections(frame_id);";
}

std::string BuildInsertRow(const TrackedDetection &d, const GpsPosition &gps, const char *event) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "INSERT INTO detections (frame_id,ts,object_id,class,age,conf,box_px,lon,lat,alt,event) "
                  "VALUES (%u,%.3f,%u,%d,%d,%.3f,'%u,%u,%u,%u',%.6f,%.6f,%.3f,'%s');",
                  gps.frame_id, gps.ts, d.object_id, d.species_id, d.age_class_id, d.confidence,
                  d.cx, d.cy, d.w, d.h, gps.lon, gps.lat, gps.alt, event);
    return std::string(buf);
}

} // namespace output
} // namespace manifold3
```

- [ ] **Step 4: Wire into CMake**

Modify `src/output/CMakeLists.txt` to add `sqlite_row.cpp` to the `output` library sources.

Modify `tests/output/CMakeLists.txt` to add:

```cmake
add_executable(test_sqlite_row test_sqlite_row.cpp)
target_link_libraries(test_sqlite_row PRIVATE output)
add_test(NAME sqlite_row COMMAND test_sqlite_row)
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug`
Expected: PASS (`sqlite_row` test present and green).

- [ ] **Step 6: Commit**

```bash
git add src/output tests/output
git commit -m "feat: add SQLite row SQL generation for detection events"
```

---

### Task 3: Extend sysroot with sqlite3 (device-derived, dpkg-verified)

**Files:**
- Modify: `scripts/extend_sysroot_from_device.sh` (add sqlite3 package + copy)
- Modify: `scripts/check_inference_sysroot.sh` (add sqlite3 verify)
- Modify: `tests/scripts/fixtures/make_fake_sysroot.sh` (add fake sqlite3 files)
- Modify: `tests/scripts/fixtures/expected_packages.txt` (add `libsqlite3-dev 3.31.1-4ubuntu0.6`)
- Modify: `tests/scripts/test_extend_sysroot_from_device.sh` (update expected counts)
- Modify: `tests/scripts/test_check_inference_sysroot.sh` (add sqlite3 checks)

**Interfaces:**
- Consumes: the existing `extend_sysroot_from_device.sh` mechanics (dpkg version check, staging, symlink restore, staged verify, install).
- Produces: sqlite3 header `usr/include/sqlite3.h` and runtime+dev libs under `usr/lib/aarch64-linux-gnu/` in the sysroot, verified by `check_inference_sysroot.sh`.

- [ ] **Step 1: Add sqlite3 to the extension script**

In `scripts/extend_sysroot_from_device.sh`:
- Add `"libsqlite3-0 3.31.1-4ubuntu0.6"` and `"libsqlite3-dev 3.31.1-4ubuntu0.6"` to `EXPECTED_PACKAGES`.
- Add a `SYMLINKS` entry: `["usr/lib/aarch64-linux-gnu/libsqlite3.so.0.8.6"]="libsqlite3.so.0:libsqlite3.so.0.8.6 libsqlite3.so:libsqlite3.so.0.8.6"`.
- In the staging copy section, copy `dji@${TARGET_IP}:/usr/include/sqlite3.h` into `${STAGING}/usr/include/` and `dji@${TARGET_IP}:/usr/lib/aarch64-linux-gnu/libsqlite3.so*` into `${STAGING}/usr/lib/aarch64-linux-gnu/`.
- In the install section, add the sqlite3 header to `HDR_DIR` (note: header lives at `usr/include/sqlite3.h`, not `aarch64-linux-gnu/`) and the libraries to `LIB_DIR`.

- [ ] **Step 2: Add sqlite3 to the checker**

In `scripts/check_inference_sysroot.sh`:
- Add header check: `"${SYSROOT}/usr/include/sqlite3.h"` must be a regular non-empty file.
- Add to `REAL_LIBS`: `"${LIB}/libsqlite3.so.0.8.6:libsqlite3.so.0"`.
- Add to `LINKS`: `"${LIB}/libsqlite3.so:libsqlite3.so.0.8.6"` and `"${LIB}/libsqlite3.so.0:libsqlite3.so.0.8.6"`.

- [ ] **Step 3: Update fake fixtures**

In `tests/scripts/fixtures/make_fake_sysroot.sh`:
- Add `printf '/* fake sqlite3.h */\n' >"${DEST}/usr/include/sqlite3.h"` (mkdir `usr/include`).
- Add `make_so "${LIB}/libsqlite3.so.0.8.6" libsqlite3.so.0`, `ln -s libsqlite3.so.0.8.6 .../libsqlite3.so.0`, `ln -s libsqlite3.so.0.8.6 .../libsqlite3.so`.

In `tests/scripts/fixtures/expected_packages.txt`, append:
```
libsqlite3-0 3.31.1-4ubuntu0.6
libsqlite3-dev 3.31.1-4ubuntu0.6
```

- [ ] **Step 4: Update script tests**

In `tests/scripts/test_extend_sysroot_from_device.sh`:
- `test_happy_path`: change expected dpkg count from 14 to 16, and scp count from 4 to 5 (the new sqlite3 header+lib copy). Update the `snapshot_managed` find to include `-o -name 'libsqlite3*' -o -name 'sqlite3.h'`.

In `tests/scripts/test_check_inference_sysroot.sh`, add sqlite3 file presence checks matching the new checker expectations.

- [ ] **Step 5: Run script tests**

Run: `bash tests/scripts/test_extend_sysroot_from_device.sh && bash tests/scripts/test_check_inference_sysroot.sh`
Expected: ALL PASS (both suites).

- [ ] **Step 6: Commit**

```bash
git add scripts tests/scripts
git commit -m "feat: extend sysroot with sqlite3 (dpkg-verified)"
```

---

### Task 4: SqliteSink (cross-only sqlite3 wrapper)

**Files:**
- Create: `src/output/sqlite_sink.h`, `src/output/sqlite_sink.cpp`
- Modify: `src/output/CMakeLists.txt` (add cross-only `sqlite_sink.cpp` + link)

**Interfaces:**
- Consumes: `BuildInsertRow`, `CreateSchemaSql`, `GpsPosition` (Task 2); `TrackedDetection` (Task 1).
- Produces `manifold3::output::SqliteSink`:
  - `bool Open(const char* path);` — opens (or creates) the DB, runs `CreateSchemaSql()`.
  - `bool Append(const TrackedDetection& d, const GpsPosition& gps, const char* event);` — `sqlite3_exec` the `BuildInsertRow` SQL.
  - `void Close();` — closes the DB.
  - No `sqlite3.h` include in the header (pimpl: `void* db_`), so the header stays host-compilable.

- [ ] **Step 1: Write the header (host-compilable)**

Create `src/output/sqlite_sink.h`:

```cpp
#pragma once

#include <cstdint>

#include "output/object_tracker.h"
#include "output/sqlite_row.h"

namespace manifold3 {
namespace output {

class SqliteSink {
  public:
    SqliteSink();
    ~SqliteSink();
    bool Open(const char *path);
    bool Append(const TrackedDetection &d, const GpsPosition &gps, const char *event);
    void Close();

  private:
    void *db_;  // sqlite3*; opaque so the header has no sqlite3 dependency
};

} // namespace output
} // namespace manifold3
```

- [ ] **Step 2: Write the cross-only implementation**

Create `src/output/sqlite_sink.cpp`:

```cpp
#include "output/sqlite_sink.h"

#include <sqlite3.h>

namespace manifold3 {
namespace output {

SqliteSink::SqliteSink() : db_(nullptr) {}
SqliteSink::~SqliteSink() { Close(); }

bool SqliteSink::Open(const char *path) {
    if (sqlite3_open(path, reinterpret_cast<sqlite3 **>(&db_)) != SQLITE_OK) {
        db_ = nullptr;
        return false;
    }
    const std::string sql = CreateSchemaSql();
    char *err = nullptr;
    if (sqlite3_exec(static_cast<sqlite3 *>(db_), sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        sqlite3_free(err);
        sqlite3_close(static_cast<sqlite3 *>(db_));
        db_ = nullptr;
        return false;
    }
    return true;
}

bool SqliteSink::Append(const TrackedDetection &d, const GpsPosition &gps, const char *event) {
    const std::string sql = BuildInsertRow(d, gps, event);
    char *err = nullptr;
    if (sqlite3_exec(static_cast<sqlite3 *>(db_), sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        sqlite3_free(err);
        return false;
    }
    return true;
}

void SqliteSink::Close() {
    if (db_ != nullptr) {
        sqlite3_close(static_cast<sqlite3 *>(db_));
        db_ = nullptr;
    }
}

} // namespace output
} // namespace manifold3
```

- [ ] **Step 3: Wire into CMake (cross-only)**

Modify `src/output/CMakeLists.txt`:

```cmake
add_library(output STATIC
    object_tracker.cpp
    sqlite_row.cpp
)
target_include_directories(output PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
if(CMAKE_CROSSCOMPILING)
    target_sources(output PRIVATE sqlite_sink.cpp)
    target_include_directories(output PRIVATE
        ${CMAKE_SYSROOT}/usr/include
    )
    target_link_libraries(output PUBLIC sqlite3)
endif()
```

- [ ] **Step 4: Cross-build to verify it compiles**

Run: `cmake --preset manifold3-cross-release && cmake --build --preset manifold3-cross-release`
Expected: BUILD SUCCESS (the `output` static lib compiles, including `sqlite_sink.cpp`).

- [ ] **Step 5: Host build still passes**

Run: `cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug`
Expected: PASS (host `output` still builds `object_tracker.cpp` + `sqlite_row.cpp` only; no sqlite3 needed).

- [ ] **Step 6: Commit**

```bash
git add src/output
git commit -m "feat: add SqliteSink sqlite3 wrapper (cross-only)"
```

---

### Task 5: GNSS helper + app integration

**Files:**
- Create: `src/output/gps_reader.h`, `src/output/gps_reader.cpp` (cross-only)
- Modify: `src/app/main.cpp` (wire tracker + sink + gps into the loop)
- Modify: `src/app/CMakeLists.txt` (link `output`)

**Interfaces:**
- Consumes: `DjiFcSubscription_Init`, `DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED, ...)`, `DjiFcSubscription_GetLatestValueOfTopic(...)` from PSDK 3.16.0 (`dji_fc_subscription.h`); `ObjectTracker`, `SqliteSink`, `GpsPosition` (Tasks 1-4).
- Produces `manifold3::output::GpsReader`:
  - `bool Init();` — `DjiFcSubscription_Init()` + subscribe `POSITION_FUSED`.
  - `bool Read(GpsPosition* out);` — `GetLatestValueOfTopic` into `T_DjiFcSubscriptionPositionFused`, fills `lon/lat/alt` (radians+meters) and `ts` from `std::chrono::steady_clock`.
  - `void Deinit();` — `DjiFcSubscription_DeInit()`.

- [ ] **Step 1: Write the GNSS reader (cross-only)**

Create `src/output/gps_reader.h`:

```cpp
#pragma once

#include "output/sqlite_row.h"

namespace manifold3 {
namespace output {

class GpsReader {
  public:
    bool Init();
    bool Read(GpsPosition *out);
    void Deinit();
};

} // namespace output
} // namespace manifold3
```

Create `src/output/gps_reader.cpp`:

```cpp
#include "output/gps_reader.h"

#include <chrono>

#include "dji_fc_subscription.h"

namespace manifold3 {
namespace output {

bool GpsReader::Init() {
    if (DjiFcSubscription_Init() != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        return false;
    }
    if (DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED, 0) !=
        DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        return false;
    }
    return true;
}

bool GpsReader::Read(GpsPosition *out) {
    T_DjiFcSubscriptionPositionFused pos;
    if (DjiFcSubscription_GetLatestValueOfTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
                                                reinterpret_cast<uint8_t *>(&pos), sizeof(pos), nullptr) !=
        DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        return false;
    }
    out->lon = pos.longitude;
    out->lat = pos.latitude;
    out->alt = pos.altitude;
    out->ts = std::chrono::duration<double>(
                  std::chrono::steady_clock::now().time_since_epoch()).count();
    return true;
}

void GpsReader::Deinit() {
    DjiFcSubscription_DeInit();
}

} // namespace output
} // namespace manifold3
```

- [ ] **Step 2: Wire into CMake (cross-only)**

In `src/output/CMakeLists.txt`, add `gps_reader.cpp` to the `if(CMAKE_CROSSCOMPILING)` block (alongside `sqlite_sink.cpp`), and add the PSDK include dir if not already present.

- [ ] **Step 3: Integrate into the app loop**

Modify `src/app/main.cpp`:
- Include `output/object_tracker.h`, `output/sqlite_sink.h`, `output/gps_reader.h`.
- After capture init, construct `ObjectTracker tracker;` and `SqliteSink sink;` and `GpsReader gps;`.
- `sink.Open("/home/dji/vision-detect/flight.db")` (path configurable via argv[2] like the engine path).
- `gps.Init()`.
- In the loop, after `DecodeSyntheticSeg` produces `dets`:
  - `GpsPosition gpspos; if (gps.Read(&gpspos)) { gpspos.frame_id = frame.frame_id; }`
  - `std::vector<TrackedDetection> tracked = tracker.Update(dets, frame.frame_id);`
- For each `tracked` entry: write `ADD` when `td.is_new`, `UPDATE` when `td.property_changed`, and skip the row (no write) when the matched detection is unchanged — only ADD/UPDATE/REMOVE events are written, per the spec. Concretely: `if (td.is_new) sink.Append(td, gpspos, "ADD"); else if (td.property_changed) sink.Append(td, gpspos, "UPDATE");`
  - `for (const TrackedDetection &td : tracker.UpdateAndGetRemoved(frame.frame_id)) { sink.Append(td, gpspos, "REMOVE"); }` — REMOVE rows carry the removed track's last-known species/age/box and the current GNSS.
 - On shutdown: `sink.Close(); gps.Deinit();`.

- [ ] **Step 4: Cross-build**

Run: `cmake --preset manifold3-cross-release && cmake --build --preset manifold3-cross-release`
Expected: BUILD SUCCESS.

- [ ] **Step 5: ElF and dependency checks (target)**

Run the cross ELF checks (see `tests/toolchain/`), then `scripts/deploy.sh <ip> run` and verify the `flight.db` file is created with expected rows. Stop `Smart3DExplore` first (`dji_app_ctl stop Smart3DExplore`).

- [ ] **Step 6: Commit**

```bash
git add src/app src/output
git commit -m "feat: wire detection results into SQLite with GNSS position"
```

---

### Task 6: Offline report script

**Files:**
- Create: `scripts/export_report.py`

**Interfaces:**
- Consumes: the SQLite `detections` table produced by the app (Task 5).
- Produces: `-` stdout CSV (per-class/per-age distribution, spatial distribution, object lifecycle).

- [ ] **Step 1: Write the script**

Create `scripts/export_report.py`:

```python
#!/usr/bin/env python3
"""Export a summary report from the detection SQLite database.

Usage: export_report.py <flight.db> [--csv]
Reads the `detections` table and prints per-class/per-age counts, a spatial
summary (min/max/mean lon/lat/alt), and the number of unique tracked objects.
"""

import argparse
import sqlite3
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("db", help="path to the SQLite database")
    args = parser.parse_args()

    con = sqlite3.connect(args.db)
    cur = con.cursor()

    print("== per-class / per-age ==")
    for row in cur.execute(
        "SELECT class, age, COUNT(*) FROM detections GROUP BY class, age ORDER BY class, age"
    ):
        print(f"{row[0]}\t{row[1]}\t{row[2]}")

    print("== spatial (ADD events) ==")
    for row in cur.execute(
        "SELECT MIN(lon), MAX(lon), MIN(lat), MAX(lat), MIN(alt), MAX(alt), "
        "COUNT(DISTINCT object_id) FROM detections WHERE event='ADD'"
    ):
        print("\t".join(str(v) for v in row))

    print("== tracked objects ==")
    print(cur.execute("SELECT COUNT(DISTINCT object_id) FROM detections").fetchone()[0])

    con.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Test the script against a fixture DB**

Create a small temporary DB with `sqlite3` (or the app's output) and run:

```bash
python3 scripts/export_report.py /tmp/test.db
```

Verify it prints the per-class/per-age table, spatial summary, and object count. If the host lacks the `sqlite3` CLI, create the DB via Python's `sqlite3` module in a one-off snippet.

- [ ] **Step 3: Commit**

```bash
git add scripts/export_report.py
git commit -m "feat: add offline report export script for detection DB"
```

---

### Task 7: Final verification

- [ ] **Step 1: Full host test suite**

Run: `cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug`
Expected: all tests pass (including new `object_tracker`, `sqlite_row`).

- [ ] **Step 2: Full cross build + ELF checks**

Run: `cmake --preset manifold3-cross-release && cmake --build --preset manifold3-cross-release`
Then run the toolchain ELF verifier (`tests/toolchain/verify_elf.cmake` via ctest) and, if the device is connected, `scripts/deploy.sh <ip> run` and confirm a `flight.db` is produced with rows.

- [ ] **Step 3: Target regression**

On the device: stop `Smart3DExplore`, run the app, confirm `flight.db` rows appear, and `scripts/export_report.py flight.db` prints a sensible report. Restore `Smart3DExplore`.

- [ ] **Step 4: clang-format**

Run `clang-format -i` on all new/modified `.cpp`/`.h` files. Confirm no diff beyond formatting.

- [ ] **Step 5: Self-review against spec**

Confirm every spec section has a task: SQLite schema (T2/T4), one-row-per-detection (T2/T4), dedup via tracker (T1), GNSS per frame (T5), event-driven write (T1/T5), offline report (T6), sysroot extension (T3). No spec gap.

- [ ] **Step 6: Integrate branch**

Rebase `feat/result-output` onto latest `main`, ensure no merge commits, then request user approval before merging/pushing.
