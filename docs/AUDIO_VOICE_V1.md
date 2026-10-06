# Neutral physical audio voices

`AudioVoicePlayerV1` combines the already admitted `AudioStreamV1`,
`AudioEnvelopeV1` and optional finite `AudioReadAheadV1`. It owns the sample
storage. Per-frame or constant controls supply phase increment and signed
channel gains. The envelope maximum cannot exceed the stream gain denominator.

Before each output frame, input exhaustion and the prepared refill policy are
checked. Only available input advances the envelope. The stream then renders
using the new envelope level, including the final zero frame when release
ends. That final frame advances fractional phase and the output clock. This
ordering is consistent with the inspected software reference
[PCSX2 voice mixer](https://raw.githubusercontent.com/PCSX2/pcsx2/master/pcsx2/SPU2/Mixer.cpp);
it is not a hardware capture. The compiler's independent raw-register envelope
and finite-input reference tests qualify the prepared primitives separately.

Zero phase increment holds input while the envelope continues. Release retains
the current level and restarts its release counter. Explicit stop preserves
the reached clocks and suppresses subsequent PCM. Stopped, exhausted and
released are distinct conditions; none acknowledges device completion.

Rendering validates the entire control span before changing any state or
output. For finite read-ahead, movement is bounded by the smaller of the
refill quantum and watermark plus one. This also protects policies whose
watermark is smaller than their refill quantum: an individually bounded
phase increment must not leap beyond the available buffered input. Rendering
performs no allocation; unwritten tail samples retain their prior values.

`AudioVoiceMixerV1` owns a bounded number of physical voices with unique
tokens. It advances each voice once per PCM frame, sums signed PCM in wide
integers, and saturates only the complete channel sum. Empty output is silent.
All voices must match its admitted sample rate. Finished and forcibly stopped
voices retain their slots until explicit retirement. A reused slot gets a new
token, and stale token operations fail. Release tails continue rendering;
neither release nor physical envelope completion implies logical program
completion. Observation cadence, pending start/release commits and program
callbacks belong to the prepared bank's controller.

Tests cover exact held-input attack and release samples, final zero-frame
phase movement, variable-pitch finite refill endings, chunk independence,
atomic invalid-control rejection, cancellation before saturation, stopped-slot
capacity, release tails, explicit retirement, stale tokens and silent output.
These component tests do not establish complete ambient-bank/source equivalence
or the normal New Game transition.
