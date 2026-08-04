# Result Output Design (SQLite Structured Storage)

## Goal

Persist the board-side YOLO detection results of each flight as structured data that
can be reconstructed offline into reports and charts, without requiring the video
stream. Each detection is stored with its identity, classification, pixel box,
aircraft GNSS position, and a monotonic frame timestamp.

## Decisions (user-confirmed)

- **Output transport**: SQLite 3.31.1, one row per detection (not one row per frame).
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
  not on every frame. UPDATE fires only when a property changes (age class flip,
  confidence band change, or a box position drift beyond a threshold).
- **GNSS sampling**: read one `TOPIC_POSITION_FUSED` snapshot per analyzed frame and
  store it with its detections. No interpolation.

## Data Model

```sql
CREATE TABLE detections (
    id INTEGER PRIMARY KEY,
    frame_id INTEGER NOT NULL,   -- PSDK frame sequence number
    ts REAL NOT NULL,            -- monotonic clock, seconds
    object_id INTEGER NOT NULL,  -- stable tracker identity (dedup key)
    class TEXT NOT NULL,         -- species class
    age TEXT NOT NULL,           -- age bin
    conf REAL NOT NULL,          -- detection confidence
    box_px TEXT NOT NULL,        -- source-frame pixel box "x,y,w,h"
    lon REAL NOT NULL,           -- aircraft longitude, radians
    lat REAL NOT NULL,           -- aircraft latitude, radians
    alt REAL NOT NULL,           -- aircraft altitude, meters (WGS84 ellipsoid)
    event TEXT NOT NULL          -- ADD | UPDATE | REMOVE
);

CREATE INDEX idx_object ON detections(object_id);
CREATE INDEX idx_frame  ON detections(frame_id);
```

- `ts` uses a monotonic clock plus `frame_id`; wall clock is not used, so offline
  ordering is stable across reboots.
- `box_px` keeps source-frame pixel coordinates (full information, independent of the
  Pilot's normalized coordinate space).
- `lon`/`lat`/`alt` are recorded per frame from `DJI_FC_SUBSCRIPTION_TOPIC_POSITION_FUSED`
  (`T_DjiFcSubscriptionPositionFused`: longitude/latitude in radians, altitude in
  meters WGS84). This is the per-frame aircraft position snapshot.

## Architecture

New module under `src/output/`:

- `ObjectTracker` — inter-frame IoU greedy matching; assigns stable `object_id`;
  emits ADD / UPDATE / REMOVE events.
- `SqliteSink` — serializes events into the `detections` table and commits
  transactionally.

Data flow:

```
camera -> NV12 -> preprocess -> YOLO -> detections (NMS)
                                          |-> ObjectTracker (dedup/ID/events)
                                          |        `-> SqliteSink -> flight.db
                                          `- (future) SendAiMetaToPilot
```

## Reconciliation (offline report)

The report is rebuilt from the database alone; no video is required.

- Per-class / per-age distribution: `SELECT class, age, COUNT(*) FROM detections GROUP BY class, age`.
- Spatial distribution: `GROUP BY object_id` to place each crown at its GNSS
  coordinate, mapped to a scatter plot / map overlay.
- Object lifecycle: `GROUP BY object_id` to reconstruct a target's ADD -> REMOVE path.
- Delivered as an offline script (e.g. `scripts/export_report.py`) that reads the DB
  and emits CSV / charts / map overlays.

## Sysroot Extension

`libsqlite3` is a dpkg-managed package on the device (`libsqlite3-dev 3.31.1-4ubuntu0.6`),
so it fits the existing dpkg-version-verified `extend_sysroot_from_device.sh` flow. The
managed set is extended with:

- header: `/usr/include/sqlite3.h`
- runtime + dev libraries under `/usr/lib/aarch64-linux-gnu/`

The `check_inference_sysroot.sh` checker is extended to verify the sqlite header and
library presence, AArch64 ELF, and SONAME.

## Open Items (calibrated on target, not fixed here)

- IoU match threshold for `ObjectTracker`.
- Lost-frame count (`N`) before an object is emitted as REMOVE.
- Box-drift threshold that triggers an UPDATE.

## Out of Scope

- Pilot overlay (`DjiLiveview_SendAiMetaToPilot`) — interface reserved, not implemented.
- Board-side screenshot / image encoding.
- Video stream storage and offline box redraw.
- Age class list is fixed by the real model training (Phase 5B open item).