# Oscitron → VCV Rack: Feasibility Study

Date: 2026-08-17
Scope: assess turning the Oscitron firmware into a VCV Rack plugin, across five areas requested: functional AVR emulation, accurate AVR emulation, 16 MHz-accurate audio timing, OPA2134 output-stage modelling, and VCV Rack integration/tooling.

## Executive summary

**Recommendation: port the DSP code natively; don't emulate the AVR.**

While working on the bare-metal AVR-GCC port earlier in this session, one thing became clear: this codebase already separates cleanly into two halves —

- **~20 of the 22 libraries are pure DSP/data**, with zero hardware register access: `AteOscEngine`, `AteOscOscillator`, `AteOscPatch`, `AteOscPitch`, `AtmPitch`, `BiquadFilterFM`, `CvInput` (the *value* smoothing, not the ADC read), `Flanger`, `Portamento`, `Pwm`, `QuantizePitch`, `Wavecrusher`, `Wavetable`, `FixedMath`, `SsHelpers`, `RotaryEncoder`, `Switch`, `LedCircular`, `LedRgb`. These compile as ordinary C++ on any platform once `avr/pgmspace.h`'s `PROGMEM`/`pgm_read_*` macros are shimmed to no-ops (`FixedMath.h` is a good example — pure lookup tables, no inline assembly, trivially portable).
- **Only 2 libraries touch AVR hardware directly**: `AteOscHardware.cpp` (ADC, TWI/I2C, GPIO, EEPROM) and `AtmAudio.cpp` (Timer1/Timer2 audio output). Everything hardware-specific is already concentrated in exactly the two places you'd want it to be for a port.

That means "port the algorithm" (your "functional level") is not a simplification that sacrifices accuracy so much as it's *removing a layer the code never needed for the interesting part in the first place*. Instruction-level AVR emulation (your "accurate level") is real, proven technology (see Prior art below) — but it's solving a much harder problem than this port actually requires, and it moves the risk from "does the DSP sound right" (answerable by ear, on your own hardware, quickly) to "did I model the ADC/TWI/timer peripherals correctly" (answerable only by fairly deep AVR peripheral archaeology). My recommendation is to spend the effort budget on faithfully reproducing the specific, characterful non-idealities (below) rather than on reproducing the chip that produces them.

That said, both paths are viable engineering projects. The sections below cover both in full, since you asked for both, plus where they intersect.

---

## 1. Functional-level emulation (native port) — recommended path

**What it is:** compile the existing DSP libraries (unmodified engine logic) as native x86/ARM C++, and replace `AteOscHardware`/`AtmAudio` with a thin adapter that maps VCV Rack's params/inputs/lights onto the same interface the engine already calls (`hardwareCvInputChanged()`, `hardwareSwitchChanged()`, `hardwareGateInputChanged()`, etc. — these callback names are already in `AteOsc.cpp`).

**Effort: low–moderate.** Concretely:

| Task | Why |
|---|---|
| `#define PROGMEM` / `pgm_read_byte(p) (*(p))` / `pgm_read_word(p) (*(p))` shim header | Only AVR-specific dependency in the pure-DSP libraries |
| Replace `AteOscHardware` with a `VcvOscHardware` adapter | Owns the 8 `CvInput` objects (`CV_FILT`, `CV_RES`, `CV_PITCHPOT`, `CV_FILTPOT`, `CV_PITCH`, `CV_PWM`, `CV_FLANGE`, `CV_CAPTURE` — from `AteOscHardware.h:135`), the 2 `RotaryEncoder`+`Switch`+`LedCircular`+`LedRgb` sets (`ENCODERS = 2`), fed from VCV `inputs[]`/`params[]` each `process()` call instead of ADC/TWI |
| Replace `AtmAudio`'s Timer1/Timer2 ISR pair with a `process()`-driven equivalent | See §3 — this is the one place where a literal 1:1 port would be *wrong*; it needs to reproduce the ISR's audible behaviour, not its register writes |
| Patch storage: FRAM/EEPROM via `TWI` → simple file or VCV's own patch-persistence (`dataToJson`/`dataFromJson`) | The physical chip (`FM24C64B`) has no VCV equivalent; this is a straight simplification, not a compromise |
| MCP23017 I²C GPIO expander → direct panel widget state | Same reasoning |

**Fixed-point behaviour**: the engine uses `int`/`char`/fixed-point arithmetic throughout (`FixedMath.h`'s 10-bit sine/tan tables, `Wavetable`'s `int8_t` samples), not floating point. Keep it that way in the port rather than "upgrading" to float — the specific quantization *is* part of the sound, and floating-point would silently change the noise floor and rounding behaviour of every filter/oscillator stage.

**CV scaling: reuse the firmware's own calibration system, don't reverse-engineer the analog front-end.** CV inputs on this module are 0–5V (your note) — external bipolar LFOs/10V envelopes go through a separate module (`UniFive`) first. I read `UniFive8.sch` (the latest of several revisions in that folder): it's a trimmer-calibrated op-amp scaling circuit (`LM4040` 2.5V references, `3352T` trim pots in the feedback path of each stage). Its exact transfer function depends on a physical trim pot position that isn't encoded in the schematic — not something to derive analytically. More importantly, it doesn't need to be: `AteOscHardware.h` already has a per-unit calibration system (`cvCalib_[2][3]`, `EEPROM_FILT_HIGH`/`LOW` constants, and a whole separate `AteOscHardwareCalibrate` library) that the firmware itself uses to map raw ADC counts to musical values. Port *that* logic into the native adapter, seeded with either the real module's calibration constants (readable from its EEPROM) or sensible defaults — this is the actual source of truth for CV scaling, not the analog trim network.

---

## 2. Accurate-level emulation (instruction/peripheral-accurate)

**What it is:** run the actual compiled Oscitron firmware — the `.elf` you already build successfully in this repo (`Debug/SsAteOscitron.elf`) — inside a real AVR CPU simulator, driven cycle-by-cycle from VCV's audio callback, with virtual peripherals standing in for the physical chips.

**Recommended core: [simavr](https://github.com/buserror/simavr).** It's C, native, actively maintained, and — unlike JS-based alternatives like avr8js — built for exactly this "embed a real AVR core inside a native real-time host" use case. Performance is not the blocker: AVR cores at 16 MHz are trivial for simavr to simulate many times faster than real-time on modern hardware, so the constraint is implementation completeness, not CPU budget.

**Prior art confirms this is a proven pattern in VCV Rack specifically**, not a novel idea:
- [RackNES](https://github.com/Kautenja/RackNES) runs a real NES CPU/APU emulator (derived from [SimpleNES](https://github.com/amhndu/SimpleNES) + [Nes_Snd_Emu](https://github.com/blargg/Nes_Snd_Emu)) inside a VCV module.
- [KautenjaDSP's plugins](https://library.vcvrack.com/?brand=KautenjaDSP) emulate the Ricoh 2A03.
- SID-chip modules ([vcvrack-sid](https://github.com/bongozone/vcvrack-sid), [sidex-vcv](https://github.com/wipu/sidex-vcv)) do the same for the C64's SID.

**What has to be built beyond the CPU core:**

| Peripheral | Real usage (from `AteOscHardware.cpp`/`AtmAudio.cpp`) | Emulation need |
|---|---|---|
| Timer1 (CTC, `OCR1A`) | Variable-rate audio-sample ISR — see §3 | simavr has built-in timer peripheral support; needs the ISR's effect (an `OCR2B` write) tapped per firing |
| Timer2 (Fast PWM, `OCR2B`) | 8-bit PWM DAC, fixed ~62.5 kHz carrier (`F_CPU/256`) | Tap `OCR2B` writes directly rather than reconstructing the PWM waveform and filtering it — cheaper and exact |
| ADC | CV/pot reads *and* audio-in capture (`ISR(ADC_vect)`, `BUFFER_CAPTURING` state — this chip also samples an audio input for wavetable capture, not just slow pot scanning) | Feed simulated conversion results from VCV `inputs[]`/`params[]` each time the emulated ADC would trigger |
| TWI/I²C | FRAM patch storage (`FM24C64B`) *and* MCP23017 GPIO expander, sharing one bus | Two virtual I²C peripherals need registering against simavr's TWI hooks |
| EEPROM (`avr/eeprom.h`) | Calibration data | Seed from the `.eep` file this project already builds (`Debug/SsAteOscitron.eep`, `Elf/Oscitron1-0.eep`) |
| GPIO/pin-change interrupts | Switches, rotary encoders | simavr IRQ hooks per pin, driven from VCV widget state |

This is a multi-week effort, not a multi-day one — mostly in the peripheral-modelling table above, not the CPU core itself (that part is solved, off-the-shelf).

**When this is worth it:** if bit-exact reproduction of hardware quirks — timer jitter, ADC noise floor, the exact EEPROM-calibration-dependent CV scaling — is a hard product requirement (e.g. you're marketing it as "the real firmware, virtualized" the way chiptune emulation plugins do). If "sounds and behaves the same" is the bar, §1 gets there for a fraction of the effort.

---

## 3. Throttling to 16 MHz / audio-identical wavetable timing

This is where the real character of the instrument lives, and it applies to *both* paths above — worth understanding regardless of which you pick.

I read `AtmAudio.cpp` directly. The audio path is **not** a fixed sample-rate oscillator — it's built around two timers:

- **Timer2** runs Fast PWM at a fixed ~62.5 kHz carrier (`F_CPU/256`, unprescaled) — this is the actual 8-bit DAC. `OCR2B` gets written with `sample + 128` (signed→unsigned) each time a new wavetable sample is due.
- **Timer1** runs in CTC mode and fires `ISR(TIMER1_COMPA_vect)` at a *variable* rate that **is the pitch** — `setSampleFreq()` computes `OCR1A` directly from the target frequency (`ocr = (F_CPU << i) / sampleFreq_`, `ocr1a = ocr - 1`). Each firing advances the wavetable read position.

The important, non-obvious detail: **`OCR1A` is an 8-bit-ish integer divisor of a 16 MHz clock**, so it can't represent every frequency exactly, and once the divisor would go below the safe minimum (~255 counts, per the code's own comment), the ISR starts **skipping samples in powers of two** (`bufferJump = 1 << i`) rather than raising the ISR rate further. That means at higher pitches the wavetable is read with a stride > 1 — a form of decimation aliasing that's a genuine, reproducible, audible characteristic of this instrument, not an incidental limitation to smooth over.

**Implication for the native port (§1):** don't replace this with a clean floating-point phase accumulator and call it done — that will sound *smoother* than the real hardware, not *the same*. Reproduce the same integer-divisor-plus-stride-doubling arithmetic (it's cheap to port verbatim: `unsigned int ocr = (16000000UL << i) / sampleFreq;` etc.), driven by VCV's `args.sampleTime` accumulating toward the next "virtual ISR" firing, rather than reinventing the oscillator's timing model.

**Implication for the emulated path (§2):** this is automatic and exact, since you're running the literal ISR — the only job left is cycle accounting: step the simavr core `round(16'000'000 × args.sampleTime)` cycles per VCV `process()` call, carrying the fractional remainder across calls so it doesn't drift over a long session. This is the standard technique for embedding cycle-accurate emulators in real-time audio hosts (same pattern chiptune-emulation VST/VCV plugins use).

---

## 4. Emulating the OPA2134 output stage

**Update: I read the actual schematic** (`PS_atmega_proto_09.sch`, the main board — Eagle `.sch` files are plain XML, directly parseable). This revises the recommendation below: the analog stage isn't just inconsequential carrier removal — it meaningfully shapes the audio band — but it's still a linear circuit, not something that needs op-amp non-linearity modelling.

### Traced signal path, with real values

```
ATmega PD3 (OC2B, ~62.5 kHz PWM carrier)
  -> R25 (5k1) -> [C7 (10nF) to GND]      passive pole ~3.1 kHz
  -> R29 (2k)  -> [C8 (10nF) to GND]      passive pole ~8.0 kHz (loaded further by R30 below)
  -> R30 (6k8) -> OPA2134 gate A (-IN), +IN grounded
       feedback: R31 (6k8) || C9 (100pF)   inverting, gain -1, HF roll-off pole ~234 kHz (stability only)
  -> gate A OUT
       R1 (10k) into gate B (-IN) [audio path]
       R2 (10k) into gate B (-IN) [bias path, in series with R5 (24.9k) from a 2.5V reference]
  -> OPA2134 gate B (-IN), +IN grounded
       feedback: R3 (20k) || C6 (220pF)
  -> gate B OUT = -2 x (gate A audio) - 2 x (2.5V-derived bias, ≈ -1.43 V contribution)
  -> R4 (1k) -> C12 (10uF, AC-coupling) -> output jack (TIP_OUT)
       R4+C12 forms a ~15.9 Hz high-pass, i.e. DC block / subsonic filter only
```

Two things worth calling out:

1. **The two cascaded passive RC poles (~3.1 kHz, ~8 kHz) are doing real work in the audible band**, not just killing a 62.5 kHz carrier. Given the oscillator's sample rate is *always* high relative to typical pitches — a 128-sample wavetable read out at (pitch × 128), e.g. ~56 kHz for a 440 Hz note — this filter is actively rolling off the top end of the wavetable's own harmonic content. That's part of "the Oscitron sound," not a side effect to ignore. **Revised recommendation: model this as a real (2–3 pole) low-pass around this frequency range, not a generic placeholder far above the audio band.** Given the interaction between stages (R30 loads the second passive pole via the op-amp's virtual ground), an exact analytical transfer function is fiddly — SPICE-simulating this exact R/C network, or simply measuring the physical module's frequency response directly (sweep tone in, read the output), will be faster and more reliable than hand-deriving it.
2. **Gate B is an inverting summing amplifier**, not a plain buffer: it combines the filtered audio (gain ×2 via R3/R1 = 20k/10k) with a fixed DC bias derived from a 2.5V reference (via R5+R2), most likely to cancel the built-in DC offset from the firmware's `sample + 128` PWM encoding (§3) before the final AC-coupling stage removes whatever DC remains. Functionally this nets out to "AC-coupled audio at the jack," so for the plugin the *net effect* (linear gain + the filter above) is what matters, not reproducing the DC-cancellation arithmetic itself.

**What still doesn't matter:** the OPA2134's own non-linearity/noise. Nothing in this circuit drives it outside its linear region, and its contribution is negligible next to the 8-bit DAC quantization and wavetable-stride aliasing that are the actual character of the instrument. The correction here is about the *passive filter's* cutoff sitting lower/more consequential than assumed, not about the op-amp needing a nonlinear model.

### Practical note on the daughterboard

The rotary encoder LED rings are on a separate board (`Euro_LED_PCB_06`) with no schematic symbols for the LEDs themselves (raw footprints on the board layout) — but `Osci_LED_orientation.png` in that folder shows the physical layout directly: **two rings of 16 LEDs**, one per encoder, arranged around a square encoder cutout. This matches `LedCircular.h`'s own header comment ("Hardware 16 LED array") and its `unsigned int` bitmask state — confirms the widget spec in §5 exactly: 2 instances of a 16-segment circular LED ring widget.

---

## 5. VCV Rack integration

### Interfacing with the audio engine

`Module::process(const ProcessArgs& args)` is called once per audio sample (`args.sampleRate` / `args.sampleTime` give you the current engine rate — VCV's *engine* sample rate is independently configurable from the audio device's rate). Standard API:

- `inputs[X].getVoltage()` / `outputs[X].setVoltage(v)` — Eurorack ±5 V / 10 Vpp convention, matching what Oscitron already outputs as physical hardware.
- `params[X].getValue()` — knob/switch state.
- `lights[X].setBrightness(b)` — panel LEDs.

Given the DSP core steps in fixed AVR-clock-relative time (§3), the natural shape is: accumulate `args.sampleTime` toward the next "virtual timer" event inside `process()`, exactly mirroring how the real ISR is clocked — this is true whether you go the native-port or the emulated-core route.

### UI from your source graphics

VCV's workflow: a layered SVG panel (component positions/types encoded as named nodes) is parsed by the SDK's `helper.py createmodule <slug> <panel.svg>` to auto-generate the parameter/port/light declarations and widget placement boilerplate. Practically: take your existing panel graphics into an SVG with properly named component layers (Illustrator/Inkscape both work; VCV's own panel-authoring docs cover the exact naming convention), then let the helper script do the mechanical wiring.

**One thing that needs custom widget code, not just the helper script:** the panel has 2 rotary encoders (`AteOscHardware.h`'s `ENCODERS = 2`), each paired with a `LedCircular` (a *circular ring* of LEDs — likely a wavetable-position/value indicator, not a stock VCV component) and a `LedRgb` in the switch. VCV's stock widget library doesn't have a circular LED-ring component, so this needs a custom `LightWidget`/`ParamWidget` subclass that draws N lights in a ring and handles encoder-style interaction (mouse drag or scroll → relative value change, matching how the physical encoder behaves, rather than a knob's absolute-angle behavior) — a real but bounded piece of custom UI work, not a blocker.

The 8 CV inputs (`CV_FILT`, `CV_RES`, `CV_PITCHPOT`, `CV_FILTPOT`, `CV_PITCH`, `CV_PWM`, `CV_FLANGE`, `CV_CAPTURE`) map straightforwardly to 8 VCV input jacks.

### IDE / toolchain — correcting the VS2026 assumption

I checked this rather than assume, since it directly affects your setup: **VCV Rack plugins on Windows are built with MinGW-w64 GCC, not MSVC.** The Rack SDK's documented Windows workflow is MSYS2 + the `mingw-w64-x86_64-gcc` toolchain, invoked via the SDK's Makefiles — this is because Rack's own binary and its plugin ABI are GCC-built, and mixing MSVC-compiled plugin code with a GCC-compiled host has real, well-known C++ ABI compatibility problems (name mangling, exception handling, standard library internals all differ between the two compiler families). Plain Visual Studio 2026 with its native MSVC backend is **not** a supported path for producing the final plugin binary.

Practical options, in order of how well-trodden they are:
1. **VS Code + MSYS2/MinGW terminal** — the most commonly documented community setup; VS Code as editor/IntelliSense, actual builds run through the MinGW shell's `make`.
2. **CLion**, which has first-class MinGW toolchain support if you'd prefer a fuller IDE.
3. Visual Studio itself *can* host an external Makefile project pointed at the MinGW toolchain (conceptually similar to what we just did in this repo, pointing Microchip Studio's project system at `avr-gcc` instead of MSVC) — viable if you want to stay in the Visual Studio shell you already know, but it's a less-travelled path than options 1–2, so expect more friction than following the documented workflow.

Either way, this is a genuinely different toolchain than the AVR-GCC one this repo now uses — worth setting up as a separate project/repo rather than folding into `SsAteOscitron.cppproj`.

---

## Suggested path

1. **Native port first** (§1). Gets you a working, playable VCV module fastest, and is where nearly all of the actual risk-reduction work (does the port sound and feel like the hardware) has to happen regardless of which emulation strategy you eventually pick.
2. **Get the timing model right** (§3) as part of that port — this is the single highest-leverage detail for "sounds like the real thing," and it's genuinely cheap to port faithfully rather than approximate.
3. **Treat the OPA2134 stage as a simple filter+gain block** (§4) unless the physical module or schematic reveals a surprise (e.g. deliberate soft clipping) worth preserving.
4. **Revisit full instruction-accurate emulation (§2) only if the native port's fidelity doesn't satisfy you** — it's a real, proven, but substantially larger undertaking, and you'd be reproducing behaviour the native port likely already captures if §3 is done carefully.

## Open questions for you

- Where does the Oscitron-specific Eagle schematic live? (Not found under `Eagle\projects` by name search — possibly filed under a different product name, given the `Ate`/`Atm` naming overlap with Atmegatron-family projects in that folder.)
- Is bit-exact hardware fidelity a hard requirement (→ leans toward §2), or is "sounds and plays the same" sufficient (→ §1)?
- Does the physical module's output stage have any known intentional coloration (soft clipping, deliberate ripple) worth preserving, or is it meant to be clean?
