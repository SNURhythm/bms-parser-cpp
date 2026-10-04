# Selective malformed long-note demotion

The requested policy keeps beatoraja's decoding/discard rules and preserves
malformed holds that its consumers can still render and judge normally. It
converts only surviving unusable LN endpoints to ordinary notes. It does not
reject charts, resurrect overwritten notes, restore discarded keysounds, or
change ordinary unclosed-LN and orphan-LNOBJ discards.

## Rules

| Surviving graph | Result |
| --- | --- |
| Active reciprocal head and tail | Preserve the LN |
| Missing partner | Normal note |
| Active tail with detached head | Normal note: no independently drawn/startable hold |
| CN/HCN head with detached tail | Normal note: active-tail autoplay/end-miss processing is absent; HCN passing state cannot end normally |
| Classic head with detached tail, positive time/section span, no startup shift | Preserve the LN and its owned tail |
| Classic head with detached tail and nonpositive span or reference startup shift | Normal note |

A demoted note retains its decoded WAV (including silent tails), lane, timing,
and position. Counts follow the resulting graph in Full, Scan and metadata-only
parsing. Unknown LN type defaults to classic, as in beatoraja.

The renderer follows the head's pair directly (`LaneRenderer.java:552–586`).
Classic completion uses the processing pointer/time (`JudgeManager.java:563–577`).
CN/HCN autoplay and tail misses instead need the active tail (`272–285`,
`614–626`), and HCN passage ends when the active tail is visited (`238–246`).
These are source traces from beatoraja
`ad42f56c4658e968f93b24bf23440fe51cb9878e`, not a full graphical gameplay test.

`PlayerResource.loadBMSModel` calls `setStartNoteTime(model, 1000)`. That helper
moves timeline-owned notes but leaves detached partners' stored section/time
unchanged. The first surviving note can therefore trigger an offset that
corrupts a later detached hold. The parser records the first final active note
and the first immutable timeline reaching 1000 ms to reproduce this decision.
Discarded open heads do not affect that decision. This classification does not
apply an extra timing offset to C++ output.

## Complexity and ownership

Slot insertion/replacement/erasure maintains an active flag. Finalization checks
partner presence, reciprocity, type and activity directly: O(1) per endpoint.
A head whose partner remains mutable at a rounded measure boundary waits until
that slot is final. Healthy-candidate classic heads with detached tails are
queued until the startup decision is known. Each queued candidate is finalized
once; GC retains/relocates the queue and includes it in its live-size threshold.
There is no chart-wide normalization scan or search for a partner.

A selected CN/HCN mode can make a previously undefined classic hold unusable.
`TimeLine::DemoteUnusableLongNote(lane, resolvedType)` provides the same O(1)
structural check for mode finalization. The caller supplies the resolved type,
including authored type precedence. It allocates before changing ownership,
copies base note fields, unlinks the old partner, and replaces/deletes the active
LN. Any detached owner remains in `Chart::DetachedNotes` with no dangling link.
Call this before consumers cache note pointers, reload the returned slot, and
recount. Demotion is irreversible; changing mode requires a fresh parse.

## Verification

- Parser and amalgamation suites cover all three authored modes, ordinary and
  scratch lanes, null partners, both detached directions, preserved discards,
  rounded boundaries, startup offsets, and queued candidates surviving GC.
- Mode-finalization checks cover preserved classic holds, CN conversion,
  keysound retention, safe ownership and idempotence.
- The sanitized Java comparison uses 1,040 charts × four explicit RANDOM
  selections (seed 19041, 400 generated charts, 500 collision charts), with zero
  unexpected differences. Raw Java dumps remain unchanged; expected demotion
  is structural, with no fixture-name exceptions, and counted in `results.json`
  (1,596 endpoint demotions across the four selections). A separate real chart
  with 52 affected heads also matches the bundled Java decoder exactly after
  those 52 demotions, with no other differences.
- Python comparison tests cover preserved partners, mode/count differences,
  startup offsets, Java's wrapped count window and unrelated timing differences.
- The actual bundled Java decoder plus `BMSModelUtils` probe demonstrates a
  healthy late classic pair and its collapse when an earlier note triggers
  startup adjustment. It does not launch the full renderer or JudgeManager.
- The existing 256,000-note Scan/metadata heap check remains 1,193,200 bytes
  against its 8 MiB limit.

The application uses the helper in its existing mode-finalization count pass,
invalidates a prepared MainMenu chart before reuse under another mode, and
refreshes cached chart metadata with schema 14. Historical replay payloads and
existing replay identity/mismatch checks are not rewritten.
