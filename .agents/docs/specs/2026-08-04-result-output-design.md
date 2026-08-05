# Result Output Design (SQLite Structured Storage)

## Goal

Persist the board-side YOLO detection results of each flight as structured data that
can be reconstructed offline into reports and charts, without requiring the video
stream. Each detection is stored with its identity, classification, pixel box,
aircraft GNSS position, and a monotonic frame timestamp.

## Implementation Gate

Implementation is blocked until Phase 5B produces a real-model postprocessor whose output
contains source-frame pixel boxes and frozen species/age labels. The current synthetic
`Detection` stores normalized center coordinates and MUST NOT be persisted as pixel geometry.

## Decisions (user-confirmed)

- **Output transport**: SQLite 3.31.1, one row per event (ADD / UPDATE / REMOVE), not one row
  per frame.
- **No screenshots**: structured data only; no board-side image encoding or OpenCV.
  The report is rebuilt offline from the database.
- **Video**: not stored in this phase. If a visual replay is ever required, the raw
  H.264 stream from `DjiLiveview_StartH264Stream()` (interface already validated in
  Phase 4) is saved and the boxes are re-drawn offline using the `frame_id`/timestamp
  to synchronize. This is a future, optional extension and is out of scope here.
- **Deduplication**: two layers, applied before writing — in-frame NMS (already in
  Phase 5A decode) and inter-frame IoU greedy matching that assigns a stable
  `object_id` to each tracked crown.
- **Write trigger**: event-driven. A row is written only on ADD / UPDATE / REMOVE,
  not on every frame. UPDATE fires only when species, age, confidence band, or box
  position changes beyond a threshold.
- **GNSS sampling**: read one `POSITION_FUSED` snapshot per analyzed frame and store it with
  its detections. No interpolation.
- **Session identity**: each app run creates a unique `session_id`; `object_id` is unique only
  within a session, so multiple flights can share one database file without collision.

## Corrected Data Model

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

### Schema invariants

- One app run creates one UUID `session_id` and one `sessions` row.
- `object_id` is unique only inside `session_id`; two sessions may both use object_id 1.
- `boot_id` is copied from `/proc/sys/kernel/random/boot_id`.
- Monotonic timestamps order events only within the session/boot; no cross-reboot time
  comparison is claimed.
- Both numeric IDs and text labels are stored, so the DB is interpretable without `model.yaml`.
- `gnss_sample_ok=0` requires all GNSS value columns to be NULL; `gnss_sample_ok=1` stores the
  raw latest `POSITION_FUSED` sample and satellite count. A successful read does not assert
  lat/lon health.

## Source-Frame Detection Interface (Phase 5B)

Phase 5B owns inverse-letterbox conversion, clipping, and center-to-top-left conversion.
`ObjectTracker` and the sink never reinterpret normalized inference tensors.

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

The sink must reject any box that fails these overflow-safe source bounds:

```text
x_px < source_width_px
y_px < source_height_px
width_px <= source_width_px - x_px
height_px <= source_height_px - y_px
```

Do not validate with `x + width` or `y + height`, which can overflow.

## Tracker Semantics

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

`lost_source_frame_span` is a PSDK source-frame ID span, not a count of analyzed calls. The
latest-wins slot may skip frames. Compute `delta = frame_id - previous_frame_id` as unsigned
modular subtraction and accept only `delta < 0x80000000`. `delta == 0` is a duplicate and
`delta >= 0x80000000` is stale/out-of-order; reject that call without mutating tracker state.
A normal `UINT32_MAX -> 0` wrap has delta 1. Apply the same half-range rule to
`frame_id - last_seen_frame`; REMOVE occurs when that span reaches the configured threshold.

Matching pseudocode:

```text
1. Snapshot existing_count before matching.
2. Allocate matched_existing[existing_count].
3. Match only against indices [0, existing_count); new tracks never index matched_existing.
4. Update matched tracks and emit UPDATE only on species, age, confidence-band, or box-IoU change.
5. For unmatched existing tracks, compute source frame span and emit/erase REMOVE at threshold.
6. Add unmatched detections as tracks and emit ADD; do not age them on creation.
7. Return all events from this ProcessFrame call.
```

Confidence bands: `[0.25, 0.50)`, `[0.50, 0.75)`, `[0.75, 1.00]`.

`Finish()` returns only REMOVE event payloads. The application constructs one shutdown
`FrameEventBatch` using:

```text
frame_id = last successfully analyzed frame_id (0 if no frame was analyzed)
frame_monotonic_us = shutdown steady_clock timestamp
gnss = GnssSample{} (sample_ok=false; SQL GNSS fields are NULL; no shutdown re-sampling)
events = tracker.Finish()
```

## GNSS Sampling (PSDK 3.16.0)

- `DjiFcSubscription_SubscribeTopic()` takes `(topic, frequency, callback)`. Frequency must be
  a nonzero `E_DjiDataSubscriptionTopicFreq`; the local sample uses `DJI_DATA_SUBSCRIPTION_TOPIC_50_HZ`
  and `NULL` for `POSITION_FUSED`.
- Initialize the subscription only after `PsdkLifecycle::Start()` succeeds.
- `POSITION_FUSED` exposes longitude/latitude radians, altitude meters WGS84, and
  `visibleSatelliteNumber`. A successful read is a sample success, not a lat/lon health flag.

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

`sample_ok` means only that `GetLatestValueOfTopic` succeeded. It must never be named `valid`
or `healthy`. On read failure, persist `gnss_sample_ok=0` and SQL NULL for all GNSS value
columns; never reuse the previous position silently.

`GpsReader` maintains `initialized_` and `subscribed_` state. Each state flag clears only after
the corresponding SDK operation succeeds. Failed unsubscribe leaves `subscribed_=true`, so
deinit is not attempted. Failed deinit leaves `initialized_=true`. The application performs at
most one additional `Deinit()` retry before shutting down the PSDK core; a destructor must not
call PSDK cleanup after `PsdkLifecycle::Shutdown()` and must not loop.

## Architecture

New module under `src/output/` (implemented after Phase 5B):

- `ObjectTracker` — inter-frame IoU greedy matching; assigns stable `object_id`;
  emits ADD / UPDATE / REMOVE events.
- `SqliteSink` — serializes frame event batches into `detection_events` and commits
  transactionally.

Data flow:

```
camera -> NV12 -> preprocess -> YOLO -> SourceDetection (Phase 5B)
                                          |-> ObjectTracker (dedup/ID/events)
                                          |        `-> SqliteSink -> flight.db
                                          `- (future) SendAiMetaToPilot
```

## Transactional Persistence

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

- Within one frame, sort events: REMOVE first, then UPDATE, then ADD; within each type by
  `object_id` ascending.
- `AppendFrame` computes candidate sequence numbers starting at `next_event_seq_`, starts
  `BEGIN IMMEDIATE`, binds/steps prepared INSERTs, and commits. Only after a successful COMMIT
  does it advance `next_event_seq_` by event count. On any `BEGIN IMMEDIATE`, bind, step, commit,
  or rollback failure, set `poisoned_=true`; every later `AppendFrame` returns false without
  writing.
- String-built INSERT SQL and one `sqlite3_exec` per event are forbidden. Labels, event values,
  and numeric values are bound parameters.
- `poisoned()` exposes read-only state. `CloseClean()` is permitted only when `!poisoned()`;
  it updates `ended_monotonic_us` and closes. `CloseAbnormal()` never updates the session end
  field and always closes the SQLite handle without further event writes.

### Session metadata sources

```text
session_id: read one UUID from /proc/sys/kernel/random/uuid; fail startup if unreadable or malformed.
boot_id: read /proc/sys/kernel/random/boot_id; fail startup if unreadable or malformed.
started_monotonic_us: steady_clock at output initialization.
model_version: pinned immutable model tag selected by Phase 5B; fail startup if empty or mutable.
source_width_px/source_height_px: validated capture dimensions; fail startup if zero.
```

### Lifecycle error policy

```text
DB open/session-row failure: fail startup before capture starts.
GNSS init/read failure: continue and persist gnss_sample_ok=0 with NULL values.
Frame transaction failure: log, stop pipeline, return nonzero; no silent loss.
GNSS unsubscribe/deinit failure: log and continue remaining cleanup.
```

Shutdown order when the sink is not poisoned:

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

If a frame transaction poisoned the sink, skip steps 2-3's tracker/event writes, call
`CloseAbnormal()` without setting `ended_monotonic_us`, then continue with steps 4-8 and return
nonzero.

## Reconciliation (offline report)

The report is rebuilt from the database alone; no video is required.

Final-state object distribution (counts each `(session_id, object_id)` once):

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

Spatial distribution uses each object's latest successful GNSS sample:

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

Object lifecycle export returns every event ordered by `event_seq`:

```sql
SELECT event_seq, frame_id, object_id, event,
       species_label, age_label, confidence,
       x_px, y_px, width_px, height_px,
       gnss_sample_ok, lon_rad, lat_rad, alt_m, visible_satellites
FROM detection_events
WHERE session_id = ?
ORDER BY event_seq;
```

Delivered as an offline script (e.g. `scripts/export_report.py`) that reads the DB and emits
CSV / charts / map overlays.

## Sysroot Extension

`libsqlite3` is a dpkg-managed package on the device (`libsqlite3-dev 3.31.1-4ubuntu0.6`),
so it fits the existing dpkg-version-verified `extend_sysroot_from_device.sh` flow. The
managed set is extended with:

- header: `/usr/include/sqlite3.h`
- runtime + dev libraries under `/usr/lib/aarch64-linux-gnu/`

The `check_inference_sysroot.sh` checker is extended to verify the sqlite header and
library presence, AArch64 ELF, and SONAME.

## Open Items (calibrated on target / Phase 5B, not fixed here)

- IoU match threshold for `ObjectTracker`.
- Lost source-frame span (`N`) before an object is emitted as REMOVE.
- Box-update IoU threshold that triggers an UPDATE.
- Species/age label semantics and class list (Phase 5B open item).

## Out of Scope

- Pilot overlay (`DjiLiveview_SendAiMetaToPilot`) — interface reserved, not implemented.
- Board-side screenshot / image encoding.
- Video stream storage and offline box redraw.