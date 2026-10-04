# Beatoraja long-note consumer behavior

Inspected local beatoraja revision
`ad42f56c4658e968f93b24bf23440fe51cb9878e`. Its bundled
`lib/jbms-parser.jar` SHA-256 is
`818809595b4bfae97fa507d31232a0114bc3401f47561e89069145f1f3734ae4`.
The repository and jar were not modified.

## Observed behavior

Beatoraja does not normalize away detached LN partners. Its renderer follows
an active head's partner object even if that object no longer occupies a
playable timeline slot. It does not draw an active tail independently.
A null partner causes a null dereference inside the lane renderer. Whether an
exception is swallowed depends on the caller, as traced below; the isolated
probe does not establish that the whole application exits.

| Decoded graph | Renderer visibility | Other consumers |
| --- | --- | --- |
| Active head and tail | Head drives hold drawing | Normal pair processing |
| Active head, detached tail | Head still uses detached tail's time and section | Judge state retains the partner reference |
| Detached head, active tail | Tail is not drawn independently | Tail remains in lane processing/counts; CN/HCN missed-tail handling can still see it |
| Ordinary unclosed channel LN | Decoder removes it | No surviving LN to render |
| Sentinel-position head with null partner | Visibility predicate throws | Actual `SongInformation` construction also throws |

A detached object is retained and valid; a null partner is absent. Treating
both as the same malformed case would not reproduce beatoraja behavior.

## Source trace

Paths and lines refer to the pinned beatoraja revision:

- `src/bms/player/beatoraja/play/LaneRenderer.java:452`: past-timeline retention
  uses the head's pair time without checking pair nullness or active membership.
- `LaneRenderer.java:552`: drawing requires `!ln.isEnd()` and the partner's time
  to be at or beyond the playback time. There is no active-slot membership test.
- `LaneRenderer.java:562–586`: the body extent walks timelines until the
  partner's section and calls `drawLongNote` when the resulting height is positive.
- `LaneRenderer.java:701–725`: LN/CN/HCN drawing uses the head and partner-based
  judgment state. Tails do not have a separate drawing branch.
- `src/bms/player/beatoraja/play/JudgeManager.java:259–270,431–445`: starting an
  LN assigns the partner object to the lane's processing state.
- `JudgeManager.java:573–577`: normal LN completion can use that retained
  partner's time without retrieving it from an active lane slot.
- `JudgeManager.java:617–624`: CN/HCN tail miss processing walks active lane
  notes. Removing a tail merely because it has no visible active head changes
  gameplay behavior, not just rendering.
- `src/bms/player/beatoraja/PlayerResource.java:204–230`: model preparation
  applies start-time adjustment, player rules and default mine sound handling;
  it does not validate or repair LN pairing.
- `src/bms/player/beatoraja/play/BMSPlayerRule.java:66–91`: validation changes
  judge rank and TOTAL, not LN graph structure.
- `src/bms/player/beatoraja/song/SongInformation.java:118–119`: derived chart
  information dereferences the head's pair without a null check.
- `src/bms/player/beatoraja/song/SongData.java:219`: full information creation
  reaches that constructor. Lightweight scan paths can skip information creation,
  so it would be inaccurate to claim every database-loading path rejects the
  chart cleanly.

The renderer contains a commented-out historical missing-end warning, but
that comment is not executable fallback logic.

### Exception boundaries

- `src/bms/player/beatoraja/song/SongInformationAccessor.java:135–145` catches
  `SQLException | RuntimeException` around information construction and insertion.
  A null-pair failure is logged and that derived-information update is skipped.
  `SQLiteSongDatabaseAccessor.java:924–930` inserts the song record before calling
  this updater, so library scanning can retain the chart and continue.
- `src/bms/player/beatoraja/skin/Skin.java:333–359`,
  `drawAllObjectsSafely`, catches `Throwable` separately for each object's
  preparation and drawing, sets `obj.draw = false`, and continues with other
  objects. This aborts the failing object's operation, not just a single note.
  A later preparation can reset its draw flag (`SkinObject.java:598`).
- The only call to that safe method in this checkout is
  `src/bms/player/beatoraja/config/SkinPreview.java:90`.
  Gameplay instead calls `drawAllObjects` from `MainController.java:408`.
  Both branches of that ordinary method (`Skin.java:276–331`) invoke preparation
  and drawing without a catch. `SkinNote.java:61` delegates directly to
  `LaneRenderer.drawLane`.

Thus "beatoraja always rejects/crashes" is too broad, and "gameplay swallows
the bad note and proceeds" is also unsupported by this revision. The database
and preview recovery paths do not repair the decoded LN graph.

## Executable probe

[`ProbeBeatorajaLongNotes.java`](../../test/reference/ProbeBeatorajaLongNotes.java)
uses the actual bundled decoder and compiles the actual beatoraja
`SongInformation` class. It evaluates the renderer's exact visibility predicate
without initializing graphics. This is not a screenshot or a full app launch.
The detached fixtures start at measure 1 so automatic prep time does not alter
the partner position in this probe. The null fixture also passes through the
actual start-time adjustment helper.

From the parser repository, choosing a scratch output directory:

```sh
mkdir -p /tmp/beatoraja-ln-probe
javac -cp ../beatoraja/lib/jbms-parser.jar -d /tmp/beatoraja-ln-probe \
  ../beatoraja/src/bms/player/beatoraja/Validatable.java \
  ../beatoraja/src/bms/player/beatoraja/song/SongInformation.java \
  test/reference/ProbeBeatorajaLongNotes.java
java -cp /tmp/beatoraja-ln-probe:../beatoraja/lib/jbms-parser.jar \
  ProbeBeatorajaLongNotes
```

All eleven probe fixtures passed, including explicit CN/HCN counts:

- Valid pair: one active head, one tail, one hold drawing candidate.
- Detached tail: one active head, no active tail, one drawing candidate.
- Detached head: no active head, one active tail, no drawing candidate.
- Ordinary unclosed LN: no surviving active LN endpoints.
- CN/HCN: paired endpoints both count; an active tail with a detached head
  still counts despite having no independent drawing.
- Null sentinel head: `NullPointerException` from both the visibility predicate
  and `SongInformation`. The latter failure occurs at its line 119.

C++ contract regressions in `test/BeatorajaLongNoteTests.h` cover valid pairs,
both detached directions and ordinary unclosed LNs in all three LN modes.
They check active-slot counts, reciprocal retained partners, the head-driven
rendering condition and matching Full/Scan metadata. Both `make test` and
`make test_amalgamation` passed with these regressions.

## Parser implications

The current parser's retained detached partners are necessary to preserve this
consumer-visible behavior. There is no beatoraja discard policy to copy for
those cases. A simpler contract that deletes conflicting pairs would be an
intentional departure from beatoraja, as discussed in the
[performance investigation](../performance/2026-10-04-parity-cost-investigation.md).

For null partners, there is no single catch-and-continue behavior to translate
into a parser policy. The current parser preserves the decoded graph. Rejecting
the chart or dropping only the unpaired LN would each be an explicit additional
policy, not a reproduction of beatoraja's caller-specific exception handling.
Neither policy has been implemented. The earlier recommendation to reject the
chart should not be read as an observed beatoraja rule.
