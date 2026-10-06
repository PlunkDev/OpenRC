# Original frontend text-node callback

`plan_rac_frontend_new_game_v1` reconstructs control owner `21b298`, used by
the original New Game label and other frontend text nodes. This is not a new
menu resource, substitute Start button, host-font UI or runnable frontend.
It consumes the actual bounded source node/graph/literals, selected original
text bank and relocated rows, three metric tables and texture catalog.

The main screen's list buttons use the separate integer callback `21c1b0`,
documented in [RAC_FRONTEND_LIST_V1.md](RAC_FRONTEND_LIST_V1.md). Its actual
integer glyph/quad path is now recovered; it does not replace this heading
callback's floating glyph path.

The callback retains all selector families and their precedence, the original
150-row remapping lookup, timer/selection writes, font selection, missing/null
text behavior, original two-string formatting, special labels, centering,
measurement, shadow/main passes and the scrolling repeat. Formatting reads the
source literal and respects its 64-byte stack window; unsupported formats or
overflow fail instead of truncating or executing arbitrary host formatting.

Both early returns retain ALPHA/TEST packets and preceding node writes, return
1 and do not begin a texture batch. A reached batch returns 2. Its cold binder,
text layout, scissors and floating-glyph program builders are the existing
recovered implementations. Measurement does not draw. Palette/control changes
and repeated layout calls preserve their source ordering; a repeated scrolling
pass is not a copy of an already rendered line. The integer RTT composite is
not substituted for these floating glyphs.

## Explicit execution boundary

Timer `1f98c0` executes its actual CVT.S.W, ADDA(.25,.25), ordered MADD and
TRUNC with the live scale read from `15ee68`. The shared integer reference
now supports non-unit scales and the full signed-word conversion range.
The result retains MFC1 sign extension. For PAL scale `3f555555`, duration
ten at `1602b4` produces eight ticks; 180 produces 150.

`plan_rac_frontend_timed_color_v1` executes `21c6c0`'s signed age clamp,
full-64-bit `-1` color sentinels, timer calls and conditional DIV operands.
The over-duration branch uses factor one. The other branch executes the
original DIV.S/SUB.S sequence and the separately rounded ordered VU
MULAw/MADDx/FTOI0 path, including non-dyadic phases and zero divisors.
MULAw establishes its actual accumulator word and overflow latch.

An executed result cannot be overridden by a diagnostic observation. Every
numeric effect marks `evaluated`; these paths run with no supplied numerical
observations. See [SOURCE_DIV_ACC_REFERENCE_V1.md](SOURCE_DIV_ACC_REFERENCE_V1.md)
for the shared arithmetic qualification and explicit limits on hardware
capture, flags and timing.

The actual `1fa8a8` call at `21b814` now evaluates its constant-half color mix.
It retains the original unpack, ITOF0, MULAw, MADDx, FTOI0 and packing order.
Unsigned byte lanes and their half-unit products/sums fit exactly in this
value domain; the power-of-two factor is the right operand of both products.
Final nonnegative half-integers truncate, including alpha, and the original
packing zeroes high return bits. The bounded evaluator accepts no alternative
factor and emits a computed effect rather than a supplied numeric observation.
This does not qualify general interpolation factors, ACC flags or VU latency.

The callback returns complete final node bytes and ordered writes, lookups,
numeric dependencies, batch/control effects and actual layout/glyph programs.
Immutable source mappings cannot overlap each other or the mutable node.
Strings must terminate within their owner; a relocated bank string must match
its parsed source text. Explicit source/effect/layout/glyph budgets bound all
work. Unconditional reached branch-delay reads still require source ownership.

This executes recovered integer control and subordinate symbolic plans; it
does not evaluate those floating programs, establish GS residency, render menu
pixels, consume input or start a game. Complete inherited render state,
qualified arithmetic, presentation, input, transitions and audio remain
required in the original startup-to-Novalis flow.

## Verification

Ten permanent synthetic test groups cover normal/early-return paths, selector
families and font precedence, timer transitions, original text fallback and
formatting, scrolling/control ordering and malformed observations/ownership.
Actual callback return1/return2 also drive the genuine enclosing RTT owner,
checking setup/clear/callback/restore order and composite crop selection.
The timer tests cover signed endpoints and the actual PAL scale.
589,824 color cases cover all byte pairs at nine exact phases of an eight-tick
duration. Non-dyadic phases and DIV by zero also execute through the integer
reference. Callback paths are exercised with an empty observation stream.
Source-instruction comparisons are recorded separately from hardware captures
and the still-missing normal-flow/fidelity test.

The independent original-instruction trace and audited native comparison pass
**96 cases, 1,879 ordered effects, 345 layout calls and 632 symbolic glyph
draws**, including five early returns. It compares every final node byte,
preamble packet, reached call/write, text selection, layout/line/clip field,
glyph operation and binder result. The trace executes all 91 reached half-mix
calls and additionally compares **65,536 complete source/native color pairs**
with padded 128-bit inputs, covering every per-channel byte pair. Those extra
cases execute 1,179,648 original instructions with exact half-unit arithmetic.
The regenerated trace executes **262 timer calls and 91 complete timed-color
calls**. All eleven formerly observed non-dyadic mixers now execute the
original control graph with independent integer DIV/ACC arithmetic; no
placeholder mixer outputs remain.
Formatting/memory-clear hooks remain
documented diagnostic boundaries. Other reached traced source helpers execute
their actual instructions. This is not a new physical-console capture or a
claim that floating glyphs have been evaluated or the menu has been drawn.

## Recovered input and fresh-reset companions

`rac_frontend_input` now executes these separate source control paths:

- The title input gate at `1eb6f0`, followed by all five `219e60` stores,
  requests menu initialization on source button bits `0x840` in title mode 0.
- Main-menu row `1d49d0` has key 20266, action 4 and target `1d5008`.
  Its focused action entry in `21bb90` preserves cancel/confirm precedence,
  requests the source UI sound and executes all 13 stores of dialog kind 3
  initialization in `1fbc80`. Exact timers use the existing timer evaluator.
- The ready-card dialog arm in `1fd3e8` decrements its counters and returns
  the target to the menu owner for source states 1 or 16. It does not wait
  for the fade counter to reach zero. Busy card state 1 retains the dialog.
- The fresh no-save input arm of `224728` preserves focus, card readiness,
  button-source selection, navigation and cancel gates. Its reset compiles
  all 47 primary and 20 times 11 repeated descriptor copies from the validated
  source template. It restores the three source words saved by `209dc0`,
  writes level 0, executes the three `22f4a0(0)` request stores and writes the
  entry halfword. Save/overwrite and preserving-reset arms remain explicit
  unsupported entries, never silently converted to a fresh game.

These APIs own compiler-side source effects. Original address tokens and
payloads must be lowered into neutral state ownership before runtime use.
The list callback's earlier row timers, later navigation tail, the 14 animated
menu objects and the enclosing 12-frame screen-transition owner are separate
dependencies. The shared heading callback `21b298` above is not the main-menu
list renderer `21c1b0`.

An audited native executable matches all **13 action writes and 280 fresh
reset/request writes**, including every byte of **267 descriptor copies**,
against an ignored original-instruction fixture. Its source run executes
3,681,604 instructions through checksum validation, descriptor matching and
the reset/request control path; platform IO, allocator, memcpy, memcmp and
sound submission are explicit diagnostic hooks. The separate source checks
cover the title's five stores and 24 ready-dialog cases, including signed age
wrap and the actual card-state value 3 written by dialog initialization.
This is source execution evidence, not a physical-console capture or proof
that the interactive menu is already rendered and connected to gameplay.
