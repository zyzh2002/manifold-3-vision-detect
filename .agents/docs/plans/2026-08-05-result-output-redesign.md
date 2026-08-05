# Result Output Design Remediation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the unsafe SQLite result-output design and executable plan with a correct, implementation-gated design covering session identity, source-frame geometry, tracker aging, PSDK GNSS lifecycle, transactional writes, and object-level reports.

**Architecture:** This work changes documentation only. Code implementation waits until Phase 5B provides real-model source-frame detections and frozen label semantics. The future system processes one source-frame ID per tracker call, persists one transactional event batch per analyzed frame, scopes object IDs by session, and stores raw PSDK GNSS sampling status without claiming unavailable health information.

**Tech Stack:** C++17 design, SQLite 3.31.1 schema, Python standard-library `sqlite3` reporting, DJI PSDK 3.16.0 Flight Controller Data Subscription, CMake/CTest commands.

## Global Constraints

- Work on short-lived branch `docs/result-output-redesign`; direct commits to `main` require explicit user authorization.
- Do not create `src/output/` in this remediation.
- Phase 5B must first produce source-frame pixel boxes and frozen species/age labels. Current synthetic `Detection` uses normalized `uint16_t` center coordinates and is not a valid persistence input.
- PSDK evidence: current DeepWiki chapter `Flight Controller Data Subscription`, cross-checked against local PSDK 3.16.0 headers and samples; local files win.
- `DjiFcSubscription_SubscribeTopic()` takes `(topic, frequency, callback)`. Frequency is nonzero; the local sample uses `DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ` and `NULL` for `POSITION_FUSED`.
- A successful `POSITION_FUSED` read is a sample success, not a lat/lon health guarantee. Persist `visibleSatelliteNumber` and keep the distinction explicit.
- Human docs under `docs/` are Chinese. Agent specs/plans under `.agents/docs/` are English.

## Verified PSDK Evidence

- DeepWiki chapter read in full: `Flight Controller Data Subscription`.
- Signature/frequency rules: `third_party/psdk/psdk_lib/include/dji_fc_subscription.h:1435-1455`.
- Frequency enum: `third_party/psdk/psdk_lib/include/dji_typedef.h:259-267`.
- Position health warning: `third_party/psdk/psdk_lib/include/dji_fc_subscription.h:191-204`.
- Position struct: `third_party/psdk/psdk_lib/include/dji_fc_subscription.h:1014-1019`.
- Subscription sample: `third_party/psdk/samples/sample_c/module_sample/flight_control/test_flight_control.c:208-210`.
- Polling sample: `third_party/psdk/samples/sample_c/module_sample/flight_control/test_flight_control.c:1525-1545`.
- Manifold 3 lifecycle ordering: application starts before module samples in `third_party/psdk/samples/sample_c/platform/linux/manifold3/application/main.c`.

---

### Task 1: Correct the Persistence Schema and Phase Gate

**Files:**
- Modify: `.agents/docs/specs/2026-08-04-result-output-design.md`

**Interfaces:**
- Consumes: future `SourceDetection` with source pixels and nonempty labels.
- Produces: session-scoped schema with explicit GNSS nullability and object identity.

- [ ] **Step 1: Prepare the documentation branch**

```bash
git status --short --branch
git switch -c docs/result-output-redesign
```

Expected: clean branch. If dirty or existing with unknown work, inspect and stop rather than resetting.

- [ ] **Step 2: Add an implementation gate**

```markdown
## Implementation Gate

Implementation is blocked until Phase 5B produces a real-model postprocessor whose output contains source-frame pixel boxes and frozen species/age labels. The current synthetic `Detection` stores normalized center coordinates and MUST NOT be persisted as pixel geometry.
```

- [ ] **Step 3: Replace the schema**

```sql
PRAGMA foreign_keys = ON;

CREATE TABLE sessions (
    session_id TEXT PRIMARY KEY,
    boot_id TEXT NOT NULL,
    started_monotonic_us INTEGER NOT NULL,
    ended_monotonic_us INTEGER,
    model_version TEXT NOT NULL,
    source_width_px INTEGER NOT NULL CHECK (source_width_px > 0),
    source_height_px INTEGER NOT NULL CHECK (source_height_px > 0)
);

CREATE TABLE detection_events (
    id INTEGER PRIMARY KEY,
    session_id TEXT NOT NULL REFERENCES sessions(session_id),
    event_seq INTEGER NOT NULL,
    frame_id INTEGER NOT NULL,
    frame_monotonic_us INTEGER NOT NULL,
    object_id INTEGER NOT NULL,
    event TEXT NOT NULL CHECK (event IN ('ADD', 'UPDATE', 'REMOVE')),
    species_id INTEGER NOT NULL,
    species_label TEXT NOT NULL CHECK (length(species_label) > 0),
    age_class_id INTEGER NOT NULL,
    age_label TEXT NOT NULL CHECK (length(age_label) > 0),
    confidence REAL NOT NULL CHECK (confidence >= 0.0 AND confidence <= 1.0),
    x_px INTEGER NOT NULL CHECK (x_px >= 0),
    y_px INTEGER NOT NULL CHECK (y_px >= 0),
    width_px INTEGER NOT NULL CHECK (width_px > 0),
    height_px INTEGER NOT NULL CHECK (height_px > 0),
    gnss_sample_ok INTEGER NOT NULL CHECK (gnss_sample_ok IN (0, 1)),
    lon_rad REAL,
    lat_rad REAL,
    alt_m REAL,
    visible_satellites INTEGER,
    CHECK (
        (gnss_sample_ok = 0 AND lon_rad IS NULL AND lat_rad IS NULL AND
         alt_m IS NULL AND visible_satellites IS NULL) OR
        (gnss_sample_ok = 1 AND lon_rad IS NOT NULL AND lat_rad IS NOT NULL AND
         alt_m IS NOT NULL AND visible_satellites IS NOT NULL)
    ),
    UNIQUE (session_id, event_seq)
);

CREATE INDEX idx_events_object
    ON detection_events(session_id, object_id, event_seq);
CREATE INDEX idx_events_frame
    ON detection_events(session_id, frame_id, event_seq);
```

- [ ] **Step 4: Define schema invariants**

```text
One app run creates one UUID session_id and one sessions row.
object_id is unique only inside session_id.
boot_id is copied from /proc/sys/kernel/random/boot_id.
Monotonic timestamps order events only within the session/boot; no cross-reboot time comparison is claimed.
IDs and labels are both stored so the DB remains interpretable without model.yaml.
gnss_sample_ok=0 requires all GNSS value columns NULL.
gnss_sample_ok=1 stores the raw latest POSITION_FUSED sample and satellite count; it does not assert health.
```

- [ ] **Step 5: Define the Phase 5B source interface**

```cpp
struct PixelBox {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct SourceDetection {
    uint16_t species_id;
    uint16_t age_class_id;
    std::string species_label;
    std::string age_label;
    float confidence;
    PixelBox box;
};
```

Phase 5B owns inverse-letterbox conversion, clipping, and center-to-top-left conversion. Tracker/SQLite never reinterpret normalized tensors.

The future sink must reject boxes that fail these overflow-safe source bounds:

```text
x_px < source_width_px
y_px < source_height_px
width_px <= source_width_px - x_px
height_px <= source_height_px - y_px
```

Do not validate with `x + width` or `y + height`, which can overflow.

- [ ] **Step 6: Commit**

```bash
git add .agents/docs/specs/2026-08-04-result-output-design.md
git commit -m "docs: correct the SQLite result output schema"
```

---

### Task 2: Retire the Unsafe 2026-08-04 Implementation Plan

**Files:**
- Modify: `.agents/docs/plans/2026-08-04-result-output.md`
- Modify: `.agents/docs/plan.md`

**Interfaces:**
- Produces: an unambiguous superseded marker; invalid snippets cannot be executed as active instructions.

- [ ] **Step 1: Replace the old plan body**

```markdown
# Result Output Implementation Plan (Superseded)

This plan was superseded on 2026-08-05 after review found unsafe tracker state handling, invalid PSDK 3.16.0 calls, missing session identity, incorrect box geometry, and inconsistent report aggregation.

Do not execute code snippets from git history for this file.

Implementation prerequisites:

1. Phase 5B exposes source-frame `SourceDetection` values with frozen labels and geometry.
2. `.agents/docs/specs/2026-08-04-result-output-design.md` is approved in its corrected form.
3. Target sqlite package/version and sysroot requirements are inspected again.
4. A new implementation plan is written from the approved spec.

Review findings and remediation steps are recorded in `.agents/docs/plans/2026-08-05-result-output-redesign.md`.
```

- [ ] **Step 2: Add the blocker to `.agents/docs/plan.md`**

```markdown
- [ ] Implement SQLite structured output only after Phase 5B freezes source-frame geometry and model label semantics. The 2026-08-04 implementation plan is superseded; use the corrected spec and 2026-08-05 remediation plan.
```

- [ ] **Step 3: Verify the retired file contains no invalid recipe**

```bash
grep -n "SubscribeTopic.*0\|matched\[i\]\|ctest --preset host-debug" \
    .agents/docs/plans/2026-08-04-result-output.md
```

Expected: exit 1, no matches.

- [ ] **Step 4: Commit**

```bash
git add .agents/docs/plans/2026-08-04-result-output.md .agents/docs/plan.md
git commit -m "docs: retire unsafe result output implementation plan"
```

---

### Task 3: Define Safe Tracker Events and Source-Frame Aging

**Files:**
- Modify: `.agents/docs/specs/2026-08-04-result-output-design.md`

**Interfaces:**
- Consumes: each analyzed frame's PSDK `frame_id`, including frames whose detection vector is empty.
- Produces: complete ADD/UPDATE/REMOVE batch for that call; shutdown events use explicit finalization metadata.

- [ ] **Step 1: Define the interface**

```cpp
enum class DetectionEventType { kAdd, kUpdate, kRemove };

struct DetectionEvent {
    DetectionEventType type;
    uint32_t object_id;
    SourceDetection detection;
};

struct TrackerConfig {
    float match_iou_threshold = 0.40f;
    uint32_t lost_source_frame_span = 30;
    float box_update_iou_threshold = 0.90f;
};

class ObjectTracker {
  public:
    explicit ObjectTracker(TrackerConfig config);
    std::vector<DetectionEvent> ProcessFrame(
        const std::vector<SourceDetection> &detections,
        uint32_t frame_id);
    std::vector<DetectionEvent> Finish();
};
```

- [ ] **Step 2: Define one aging unit consistently**

`lost_source_frame_span` is a PSDK source-frame ID span, not a count of analyzed calls. The latest-wins slot may skip frames. Compute `delta = frame_id - previous_frame_id` as unsigned modular subtraction and accept only `delta < 0x80000000`. `delta == 0` is a duplicate and `delta >= 0x80000000` is stale/out-of-order; reject that call without mutating tracker state. A normal `UINT32_MAX -> 0` wrap has delta 1. Apply the same half-range rule to `frame_id - last_seen_frame`; REMOVE occurs when that span reaches 30.

- [ ] **Step 3: Define matching pseudocode**

```text
1. Snapshot existing_count before matching.
2. Allocate matched_existing[existing_count].
3. Match only against indices [0, existing_count); new tracks never index matched_existing.
4. Update matched tracks and emit UPDATE only on species, age, confidence-band, or box-IoU change.
5. For unmatched existing tracks, compute source frame span and emit/erase REMOVE at threshold.
6. Add unmatched detections as tracks and emit ADD; do not age them on creation.
7. Return all events from this ProcessFrame call.
```

Confidence bands:

```text
[0.25, 0.50), [0.50, 0.75), [0.75, 1.00]
```

- [ ] **Step 4: Define shutdown event context**

`Finish()` returns only REMOVE event payloads. The application constructs one shutdown `FrameEventBatch` using:

```text
frame_id = last successfully analyzed frame_id (0 if no frame was analyzed)
frame_monotonic_us = shutdown steady_clock timestamp
gnss = GnssSample{} (sample_ok=false; SQL GNSS fields are NULL; no shutdown re-sampling)
events = tracker.Finish()
```

- [ ] **Step 5: Define mandatory future tests**

```text
first/new detections do not overflow matched_existing
empty vector advances aging through frame_id span
latest-wins frame gaps advance source-frame aging
UINT32_MAX wrap is handled
duplicate and stale/out-of-order frame IDs are rejected without state mutation
REMOVE occurs exactly at configured span
new track not aged on creation
Finish removes all active tracks
species/age/confidence-band/box changes emit UPDATE
unchanged match emits no event
```

- [ ] **Step 6: Commit**

```bash
git add .agents/docs/specs/2026-08-04-result-output-design.md
git commit -m "docs: define safe tracker event semantics"
```

---

### Task 4: Define a Stateful and Safe PSDK GNSS Reader

**Files:**
- Modify: `.agents/docs/specs/2026-08-04-result-output-design.md`

**Interfaces:**
- Initializes only after `PsdkLifecycle::Start()` succeeds.
- `Read()` is safe when unavailable and returns a zero-initialized failed sample.
- `Deinit()` is idempotent and state-aware.

- [ ] **Step 1: Define object state**

```cpp
class GpsReader {
  public:
    bool Init();
    GnssSample Read();
    bool Deinit();

  private:
    bool initialized_ = false;
    bool subscribed_ = false;
};
```

- [ ] **Step 2: Define exact initialization after application start**

```cpp
bool GpsReader::Init() {
    if (initialized_) {
        return subscribed_;
    }
    if (DjiFcSubscription_Init() != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        return false;
    }
    initialized_ = true;
    const T_DjiReturnCode rc = DjiFcSubscription_SubscribeTopic(
        DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED,
        DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ,
        nullptr);
    if (rc != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        const T_DjiReturnCode deinit_rc = DjiFcSubscription_DeInit();
        if (deinit_rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            initialized_ = false;
        } else {
            LogGpsError("subscription deinit after subscribe failure", deinit_rc);
        }
        return false;
    }
    subscribed_ = true;
    return true;
}
```

Application order: `lifecycle.Initialize()` -> capture init -> `lifecycle.Start()` -> `GpsReader::Init()` -> capture start. GNSS init failure logs a prominent warning and persistence continues with failed samples.

- [ ] **Step 3: Define a safe sample and polling behavior**

```cpp
struct GnssSample {
    bool sample_ok = false;
    double longitude_rad = 0.0;
    double latitude_rad = 0.0;
    float altitude_m = 0.0f;
    uint16_t visible_satellites = 0;
    T_DjiDataTimestamp aircraft_timestamp{};
};
```

`Read()` returns default `GnssSample{}` immediately when `!subscribed_`. Otherwise it zero-initializes `T_DjiFcSubscriptionPositionFused` and `T_DjiDataTimestamp`, calls `GetLatestValueOfTopic`, and fills fields only on success. `sample_ok` must never be called `valid` or `healthy`.

- [ ] **Step 4: Define idempotent shutdown**

```cpp
bool GpsReader::Deinit() {
    bool ok = true;
    if (subscribed_) {
        const T_DjiReturnCode rc =
            DjiFcSubscription_UnSubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED);
        if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            subscribed_ = false;
        } else {
            LogGpsError("unsubscribe POSITION_FUSED", rc);
            ok = false;
        }
    }
    if (initialized_ && !subscribed_) {
        const T_DjiReturnCode rc = DjiFcSubscription_DeInit();
        if (rc == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            initialized_ = false;
        } else {
            LogGpsError("subscription deinit", rc);
            ok = false;
        }
    }
    return ok;
}
```

Clear each state flag only after its SDK operation succeeds. Failed unsubscribe leaves `subscribed_=true`, so deinit is not attempted. Failed deinit leaves `initialized_=true`. The application performs at most one additional `Deinit()` retry before shutting down the PSDK core; a destructor must not call PSDK cleanup after `PsdkLifecycle::Shutdown()` and must not loop.

- [ ] **Step 5: Define error policy**

```text
DB open/session-row failure: fail startup before capture starts.
GNSS init/read failure: continue and persist gnss_sample_ok=0 with NULL values.
Frame transaction failure: log, stop pipeline, return nonzero; no silent loss.
GNSS unsubscribe/deinit failure: log and continue remaining cleanup.
```

Full shutdown order when the sink is not poisoned:

```text
1. Stop capture and prevent new frames from entering the pipeline.
2. Call tracker.Finish() and commit the shutdown batch.
3. Close SqliteSink and update sessions.ended_monotonic_us.
4. Call GpsReader::Deinit() while the PSDK application is still started.
5. If it returns false, wait 100 ms and call it once more; log any remaining failure and do not retry again.
6. Shut down capture resources.
7. Call PsdkLifecycle::Shutdown().
8. Destroy GpsReader without any further PSDK calls.
```

If a frame transaction poisoned the sink, skip steps 2-3's tracker/event writes, call `CloseAbnormal()` without setting `ended_monotonic_us`, then continue with steps 4-8 and return nonzero.

- [ ] **Step 6: Commit**

```bash
git add .agents/docs/specs/2026-08-04-result-output-design.md
git commit -m "docs: correct PSDK GNSS lifecycle semantics"
```

---

### Task 5: Define Transaction Ownership, Event Sequences, and Reports

**Files:**
- Modify: `.agents/docs/specs/2026-08-04-result-output-design.md`

**Interfaces:**
- Sink owns `next_event_seq_` and assigns contiguous sequence numbers only inside successful transactions.
- Reports count each object once and define spatial/lifecycle output deterministically.

- [ ] **Step 1: Define sink interfaces and sequence ownership**

```cpp
struct FrameEventBatch {
    uint32_t frame_id;
    int64_t frame_monotonic_us;
    GnssSample gnss;
    std::vector<DetectionEvent> events;
};

struct SessionMetadata {
    std::string session_id;
    std::string boot_id;
    int64_t started_monotonic_us;
    std::string model_version;
    uint32_t source_width_px;
    uint32_t source_height_px;
};

class SqliteSink {
  public:
    bool Open(const std::string &path, const SessionMetadata &session);
    bool AppendFrame(const FrameEventBatch &batch);
    bool CloseClean(int64_t ended_monotonic_us);
    void CloseAbnormal();
    bool poisoned() const;

  private:
    uint64_t next_event_seq_ = 1;
    bool poisoned_ = false;
};
```

`session_id` belongs to `SessionMetadata` stored by `Open`; callers do not repeat it in every batch.

`poisoned()` exposes read-only state to the application. `CloseClean()` is permitted only when `!poisoned()`; it updates `ended_monotonic_us` and closes. `CloseAbnormal()` never updates the session end field and always closes the SQLite handle without further event writes.

Session metadata sources and failure policy:

```text
session_id: read one UUID from /proc/sys/kernel/random/uuid; fail startup if unreadable or malformed.
boot_id: read /proc/sys/kernel/random/boot_id; fail startup if unreadable or malformed.
started_monotonic_us: steady_clock at output initialization.
model_version: pinned immutable model tag selected by Phase 5B; fail startup if empty or mutable.
source_width_px/source_height_px: validated capture dimensions; fail startup if zero.
```

- [ ] **Step 2: Define event ordering and rollback rules**

Within one frame, sort events by:

```text
REMOVE first, then UPDATE, then ADD; within each type by object_id ascending.
```

`AppendFrame` computes candidate sequence numbers starting at `next_event_seq_`, starts `BEGIN IMMEDIATE`, binds/steps prepared INSERTs, and commits. Only after successful COMMIT does it advance `next_event_seq_` by event count. On bind/step/commit failure it rolls back and leaves `next_event_seq_` unchanged.

For this product path, no automatic retry is attempted: after any `BEGIN IMMEDIATE`, bind, step, commit, or rollback failure, set `poisoned_=true`. Every later `AppendFrame` returns false without writing. This prevents a shutdown REMOVE batch from being persisted after an earlier ADD/UPDATE batch was lost.

- [ ] **Step 3: Require prepared statements**

String-built INSERT SQL and one `sqlite3_exec` per event are forbidden. Labels, event values, and numeric values are bound parameters. One transaction contains all events for one frame or shutdown batch.

Shutdown behavior depends on sink state:

```text
If no frame transaction failed: call tracker.Finish(), write the shutdown batch, and close the session normally.
If sink.poisoned(): do not persist tracker.Finish() events; call CloseAbnormal(), leave ended_monotonic_us NULL, set the app exit status nonzero, then continue GNSS/capture/PSDK cleanup.
```

- [ ] **Step 4: Define object final-state distribution**

```sql
WITH ranked AS (
    SELECT *, ROW_NUMBER() OVER (
        PARTITION BY session_id, object_id
        ORDER BY event_seq DESC
    ) AS rn
    FROM detection_events
    WHERE session_id = ?
)
SELECT species_label, age_label, COUNT(*)
FROM ranked
WHERE rn = 1
GROUP BY species_label, age_label
ORDER BY species_label, age_label;
```

- [ ] **Step 5: Define spatial and lifecycle reports**

Representative position per object is the latest successful GNSS sample in event order:

```sql
WITH ranked_gnss AS (
    SELECT *, ROW_NUMBER() OVER (
        PARTITION BY session_id, object_id
        ORDER BY event_seq DESC
    ) AS rn
    FROM detection_events
    WHERE session_id = ? AND gnss_sample_ok = 1
)
SELECT object_id, lon_rad, lat_rad, alt_m, visible_satellites
FROM ranked_gnss
WHERE rn = 1
ORDER BY object_id;
```

Lifecycle export returns every event ordered by `event_seq`:

```sql
SELECT event_seq, frame_id, object_id, event,
       species_label, age_label, confidence,
       x_px, y_px, width_px, height_px,
       gnss_sample_ok, lon_rad, lat_rad, alt_m, visible_satellites
FROM detection_events
WHERE session_id = ?
ORDER BY event_seq;
```

- [ ] **Step 6: Define future integration tests**

```text
schema and CHECK constraints
overflow-safe source box bounds
malformed UUID/boot ID and empty/mutable model version reject startup
two sessions both using object_id=1 remain isolated
frame transaction is all-or-nothing
BEGIN IMMEDIATE failure poisons sink before any event write
rollback does not consume event_seq
failed frame poisons sink and suppresses shutdown REMOVE writes
poisoned() becomes true; CloseClean() is rejected; CloseAbnormal() leaves ended_monotonic_us NULL
after CloseAbnormal(), GNSS bounded deinit retry, capture shutdown, and PSDK shutdown still execute
shutdown batch AppendFrame failure poisons sink, triggers CloseAbnormal(), leaves ended_monotonic_us NULL, and returns nonzero
labels round-trip as bound text
GNSS failed/success column combinations obey CHECK
shutdown batch uses last frame ID, shutdown timestamp, NULL GNSS
PSDK cleanup failure preserves state for the one bounded retry before core shutdown
final-state distribution counts one object once
spatial query uses latest successful sample
lifecycle rows are strictly event_seq ordered
```

- [ ] **Step 7: Commit**

```bash
git add .agents/docs/specs/2026-08-04-result-output-design.md
git commit -m "docs: define transactional result persistence"
```

---

### Task 6: Correct Human-Facing Status

**Files:**
- Modify: `docs/architecture.md`
- Modify: `docs/roadmap.md`
- Modify: `docs/project-status.md`

**Interfaces:**
- Communicates the selected design and implementation blocker accurately.

- [ ] **Step 1: Add the Phase 5B prerequisite in Chinese**

```text
SQLite 结构化输出方案已选定，但实现必须等待 Phase 5B 冻结真实模型的源帧像素框、类别/年龄标签和模型版本契约。当前合成检测结果使用归一化中心坐标，不能作为持久化像素框。
```

- [ ] **Step 2: Add corrected session and GNSS semantics in Chinese**

```text
每次应用运行创建独立 session_id；object_id 只在 session 内唯一。
单调时间仅用于同一 session 内排序，不跨重启比较。
POSITION_FUSED 保存 SDK 读取状态和可见卫星数；读取成功不等价于经纬度健康。
```

- [ ] **Step 3: Review all output-status references**

```bash
grep -RIn "result-output\|SQLite\|flight.db" docs README.md
```

Every match must say: corrected design exists, implementation deferred until Phase 5B.

- [ ] **Step 4: Commit**

```bash
git add docs
git commit -m "docs: clarify result output implementation prerequisites"
```

---

### Task 7: Verify the Documentation Remediation

**Files:**
- Verify only.

- [ ] **Step 1: Check only remediated active target files for retired patterns**

```bash
targets=(
    .agents/docs/specs/2026-08-04-result-output-design.md
    .agents/docs/plans/2026-08-04-result-output.md
    .agents/docs/plan.md
    docs/architecture.md
    docs/roadmap.md
    docs/project-status.md
)
if grep -nH \
    -e 'DjiFcSubscription_SubscribeTopic(DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED, 0' \
    -e 'ctest --preset host-debug' \
    -e 'ordering is stable across reboots' \
    "${targets[@]}"; then
    echo "ERROR: retired pattern remains" >&2
    exit 1
fi
```

This intentionally excludes the remediation plan, which quotes old patterns as review history.

- [ ] **Step 2: Reconfirm PSDK facts locally**

```bash
grep -n "DjiFcSubscription_SubscribeTopic" \
    third_party/psdk/psdk_lib/include/dji_fc_subscription.h
grep -n "DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ" \
    third_party/psdk/psdk_lib/include/dji_typedef.h
grep -n "visibleSatelliteNumber" \
    third_party/psdk/psdk_lib/include/dji_fc_subscription.h
```

- [ ] **Step 3: Run repository verification**

```bash
cmake --preset host-debug
cmake --build --preset host-debug
ctest --test-dir build-host --output-on-failure
git diff --check
git status --short
```

- [ ] **Step 4: Request design review**

Reviewer checklist:

```text
implementation remains blocked on Phase 5B source geometry
session_id scopes identities and reports
tracker uses one source-frame-span aging definition
new tracks cannot overflow existing-match state
shutdown batch metadata is defined
GpsReader state prevents reads/deinit after failed init
PSDK calls match local 3.16.0 files
GNSS sample success is not called health
sink owns event_seq and rollback semantics
object, spatial, and lifecycle reports are deterministic
```

- [ ] **Step 5: Write a fresh implementation plan only after the gate clears**

After Phase 5B and target sqlite package inspection, generate a new implementation plan from the approved corrected spec. Never restore code from the superseded plan.
