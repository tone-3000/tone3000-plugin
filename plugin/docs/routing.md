# Standalone Input / Output routing

Open the account menu in the **TONE3000 standalone app**, then choose **Input / Output**. The window has separate pages for **TONE3000 routing** and **Kemper hardware**. The first routes software audio to interface channels; the second sends documented MIDI commands to the PROFILER. Applying software routes does not change Kemper physical output-source selectors.

Opening the window, connecting MIDI and restoring remembered values do not start routing or send parameter changes.

## Connect and enable routing

1. Open **Audio device settings** and choose the interface, driver, rate and buffer. This action stops active routing before opening settings. Finish or cancel a raw capture first.
2. Refresh the channel list and inspect the actual input/output device names.
3. Map the input aliases, select the alias feeding the processor, and assign an optional stereo-right input and a separate raw DI input.
4. Select each logical destination, its source and its physical output channels. Assign stage markers when using Stack, Mod, Wet FX or Monitor Cabinet Off.
5. Press **Bind & enable routing**. This explicitly binds the staged mappings to the selected interface. The complete plan is validated before starting; an invalid plan does not open a partial set of routes.

**Turn routing off** restores normal standalone playback. Closing the routing window hides its controls and leaves active routes running. On a new launch, remembered values are loaded with routing off so a replacement interface never starts playing automatically.

## Inputs

Instrument, Alternative, Return Input Reamp and Digital Input Reamp are **software aliases**, each mapped to an actual interface input. Selecting an alias does not change the Kemper's internal Input Source or enable its USB/S/PDIF reamping.

The processor feed has Input Gain and an optional stereo-right channel. Software Reamp Sensitivity adds gain for return/digital reamp aliases; it is separate from the hardware Reamp Sens control.

The **DI** source is tapped from its selected raw input before processor gain, fold-down, gate, pitch shift and the tone chain. Destination volume, pad and EQ can still affect a DI output if enabled. Software cannot create an analog bypass or reproduce Kemper DI Analog/Low/High electrical behavior.

Return and Aux inputs are separate mappings. For a mono feed, assign left and leave right unassigned. Mono destinations retain a single available channel's level; a stereo destination leaves an unassigned right channel silent. The Kemper hardware Aux Mono switch is a different control.

## Destinations and sources

Main, Monitor, Direct, Phones, S/PDIF and USB are six **logical software destinations**. Each maps to physical outputs of the selected interface. Their names do not create connectors or reprogram the Kemper's USB record-source assignment.

An enabled mono route needs a left/mono channel. Stereo needs two distinct channels. DI/processed split sources always need two channels, even with Stereo unchecked. Physical output channels cannot be shared between logical destinations. Unused channels stay silent.

For example, on an interface with three or more outputs, Main can carry Master Stereo on outputs 1+2 while Direct carries DI on output 3. This does not configure the Kemper's physical Direct output.

| Source | Signal sent |
| --- | --- |
| Off | No signal from this destination, including its Aux mix. |
| DI | The selected raw digital DI input. |
| Stack | Mono signal at the explicitly assigned Stack endpoint. |
| Mod Left / Right / Mono / Stereo | The corresponding channel, mono mix or pair at the assigned Mod endpoint. |
| Master Left / Right / Mono / Stereo | The corresponding channel, mono mix or pair from the complete processed output. |
| DLY/REV Wet | Actual wet contributions from the designated serial effect section, without adding its dry share. |
| DI / Stack | Left: DI. Right: Stack mono. |
| DI / Mod Left or Mono | Left: DI. Right: the selected Mod signal. |
| DI / Master Left or Mono | Left: DI. Right: the selected Master signal. |
| Return Inputs | The separately mapped return pair. |

Stereo can be folded to mono where appropriate. Mono mixes average the available channels. DI/processed splits preserve their two different signals and cannot accidentally fold into one output.

### Assign the stage markers

TONE3000 contains general NAM and IR blocks, rather than fixed Kemper AMP, CABINET, MOD, DLY and REV slots. Choose the actual block for each role:

- **Stack ends after block**: the last block included in Stack.
- **Mod ends after block**: the last block included in Mod.
- **Wet effects start at block**: the first block of the designated serial effects section.
- **Separate cabinet block**: the cabinet to bypass in the Monitor cabinet-free renderer.

Markers use block UUIDs, not positions or guessed file types. Each rendered lane has its own assignment. Both menus list blocks from both lanes because a branch lane can select a shared trunk-prefix block. The selected block must be on that lane's effective path. Missing, removed or unreachable markers produce diagnostics and unavailable/silent taps; another stage is not substituted.

Stack, Mod and Wet FX taps are before the global stereo image, DC blocker, global tone stack and final master gain. Master includes the complete processed output. Destination processing is applied afterwards.

Wet FX retains the real wet terms of the designated blocks, including their mix/output gains. It is not Master minus an estimated DI. Assign the intended delay/reverb section; the label does not identify or separate effects baked into a NAM.

### Monitor Cabinet Off

Cabinet Off belongs to **Monitor**. Select a separable cabinet for every rendered lane. A shared-prefix cabinet may serve both lanes; a cabinet on an unrelated lane is not a valid assignment.

The cabinet-free path independently renders the complete chain with the selected cabinet blocks bypassed. Branch anchors, amps and downstream effects remain in that renderer. It is not a pre-cab tap that drops subsequent delay or reverb. Global and per-block controls follow the main renderer, while processing state remains independent.

A combined amp+cab or full-rig NAM cannot have its baked-in cabinet removed. An unavailable, unready or mismatched cabinet-free renderer stays silent instead of falling back to cabinet-on audio. Structural/rate changes rebuild affected rendering resources. Copying a block or selecting the edited lane does not reset DSP or reopen the interface.

## Levels, links, EQ and Aux

Each destination has independent volume, a software Master Volume link, a -12 dB software pad, stereo selection, four-band EQ and low/high cuts. The software pad is attenuation, not the Kemper analog Main output pad. The output EQ is TONE3000's own voicing, not a reproduction of Kemper proprietary processing.

Linked outputs combine destination volume with software Master Volume; unlinked outputs ignore that master adjustment. Filter value 0 means off. Enabled low/high cuts must be ordered correctly. Meters show the routed signal and indicate clipping.

Aux mix is a linear 0–1 amount added independently to enabled Main, Monitor and Phones routes before destination processing. It does not implicitly feed Direct, S/PDIF or USB. Off destinations stay off.

**Hear Yourself** in standalone System Settings controls audible routed audio. Check it when meters move but outputs are silent. Raw capture reads inputs directly and does not record the monitoring signal.

## Capture and device ownership

Routing and raw capture share an exclusive standalone I/O lease. Starting capture pauses active routing and opens only measurement channels. Completion, cancellation or stopping a level check restores the previous routing owner and device setup. If routing was turned off while capture held the lease, it is not restarted afterwards.

Device/channel/rate changes and test tones are guarded while capture or routing owns I/O. The routing window's Audio device settings action stops routing first. Capture records raw inputs without TONE3000 chain EQ, gate, normalization or monitor mix. See [the capture workflow](capture.md) for WAV export and website training.

If the device disconnects, its clock/channel map changes, or its saved setup cannot be restored, routing stops and reports the problem. Re-select the intended interface, refresh, inspect all mappings and bind again.

## Kemper hardware controls

The hardware page exposes **33 controls** from manufacturer MIDI Parameter Documentation 13. Choose Head/Rack, Stage or Player and the applicable Player level, then select the named MIDI output and global channel.

Connecting sends nothing. Initial values are requested values, not a hardware snapshot. Tick **Send** beside intended changes, or edit a control to stage it, then press **Apply selected MIDI values**. Only selected, available values are sent. No rig Store command is issued.

Documented groups include Input Noise Gate/Clean Sens/Distortion Sens, output volumes, Monitor Cab Off, Main/Monitor EQ and analog cuts, Aux mixes/Mono, Space, Kone, Pure Cabinet and the verified Head/Rack Input Source choices. Capability notes distinguish absent ports, upgrades and OS differences. Stage has no Alternative Input and its MIDI Input Source enumeration was not verified, so the Head/Rack enum is not sent to Stage or Player.

Continuous fields use **raw positions 0–16383**. The accessed table does not publish verified physical-unit conversions for these controls, so the UI does not invent dB, Hz or percent values. Toggles and choices use documented literal values. Where a raw option's destination or firmware-specific label is unverified, check the choice on the Kemper itself. The accessed manual and FAQ differ on Player EQ upgrade availability; the relevant controls include that note.

### Optional actual readback

Select the MIDI input carrying Kemper replies and opt in to **Listen for actual parameter replies**. Listening sends no request. Choose a parameter and press **Request selected parameter** for an explicit SysEx read.

An **Actual reply** display comes from a validated received packet and includes its age. Opening a MIDI port or sending bytes is not a hardware acknowledgement. Arriving replies do not replace a staged edit. MIDI13's header table and worked examples use different product values; the request-header selector exposes this distinction instead of silently assuming compatibility.

### Manual hardware settings

The UI lists **23 manual settings** with exact Rig Manager/hardware destinations. These have no accessed, verified command mapping and are not sent automatically:

| Area | Manual settings |
| --- | --- |
| Input | Reamp Sens. |
| Main | Output Source, analog PAD -12 dB, Output Linked. |
| Monitor | Output Source, Output Linked, Stereo. |
| Direct | Output Source, Output Linked. |
| Headphones | Output Linked. |
| S/PDIF | Output Source, Frequency, Output Linked. |
| USB | USB Reamping, Direct Monitor, Rec. Source, Reamp Source, DAW Volume, model-dependent USB 3/4 Source. |
| Space / other | Space Routing when its raw option is unverified, Dynamic/Constant Latency, physical Ground Lift. |
| Player Main | Main Out Stereo and its required hardware adapter. |

USB channel counts, reamp sends and monitoring requirements vary by model. The accessed 14.2 manuals describe fixed 44.1 kHz USB audio. Processing/capture performs the appropriate rate conversion; software output mapping does not enable hardware USB Reamping. Follow the connected model's manual.

## Saved state and validation boundary

Workstation routing lives in standalone-routing.json beside the app's existing settings: ~/Library/Application Support/TONE3000 on macOS, %APPDATA%\TONE3000 on Windows, and the TONE3000 directory in the XDG configuration location on Linux. Physical device/channel assignments and routing markers are outside portable tone presets. Values are remembered with routing off on launch.

This software router and its documented controls do not claim identical Kemper algorithms or electrical behavior. **Real Kemper audio-loopback quality and Windows audio/MIDI runtime behavior remain unverified.** A successful build or synthetic test does not establish those hardware results; report hardware verification separately when testing or contributing a release.

## Manufacturer references

- [PROFILER Main Manual 14.2](https://www.kemper-amps.com/download/692/Main-Manual-14.2): Input, Output and USB.
- [PROFILER Player Main Manual 14.2](https://www.kemper-amps.com/download/693/Main-Manual-for-PROFILER-Player-14.2): model-specific ports, sources and USB workflow.
- [Manufacturer MIDI Parameter Documentation 13](https://cdck-file-uploads-europe1.s3.dualstack.eu-west-1.amazonaws.com/flex017/uploads/electraone/original/2X/5/554852d1a4f562c1745150d36dcf83ce4402fe88.pdf): accessed command addresses and SysEx format. This version boundary does not prove that newer firmware has no additional protocol.
- [Official technical specifications](https://www.kemper-amps.com/profiler/technical-specifications) and [Kemper FAQ](https://www.kemper-amps.com/faqs): connector, model and upgrade differences.
