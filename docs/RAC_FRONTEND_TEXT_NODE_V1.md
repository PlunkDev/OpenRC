# Original frontend text-node callback

`plan_rac_frontend_new_game_v1` reconstructs control owner `21b298`, used by
the original New Game label and other frontend text nodes. This is not a new
menu resource, substitute Start button, host-font UI or runnable frontend.
It consumes the actual bounded source node/graph/literals, selected original
text bank and relocated rows, three metric tables and texture catalog.

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

Timer `1f98c0`, color interpolation `1fa8a8` and timed color `21c6c0` remain
unexecuted numeric dependencies. Every reached call requires an ordered
observation matching its kind, source call site and all argument bits. Missing,
misbound or unused observations fail. The timer return must obey its actual
MFC1 sign extension. These callees do not mutate the later-read node/palette
memory, but their COP1/VU/ACC effects are not recreated by an observation.

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

Eight permanent synthetic test groups cover normal/early-return paths, selector
families and font precedence, timer transitions, original text fallback and
formatting, scrolling/control ordering and malformed observations/ownership.
Actual callback return1/return2 also drive the genuine enclosing RTT owner,
checking setup/clear/callback/restore order and composite crop selection.
Source-instruction comparisons are recorded separately from hardware captures
and the still-missing normal-flow/fidelity test.

An independent original-instruction trace and audited native comparison pass
**96 cases, 1,926 ordered effects, 353 layout calls and 632 symbolic glyph
draws**, including five early returns. It compares every final node byte,
preamble packet, reached call/write, text selection, layout/line/clip field,
glyph operation and binder result. Numeric callees use explicit observations;
formatting/memory-clear hooks are documented diagnostic boundaries. The other
reached traced source helpers execute their actual instructions. This is not
a claim that floating glyphs have been evaluated or the menu has been drawn.
