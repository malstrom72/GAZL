/*
	BSD 2-Clause License

	Copyright (c) 2005-2026, Magnus Lidström

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright notice, this
	list of conditions and the following disclaimer.

	2. Redistributions in binary form must reproduce the above copyright notice,
	this list of conditions and the following disclaimer in the documentation
	and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
	# Symbiosis ABI

	A minimal, strict C ABI for audio plug-ins and hosts. Designed for deterministic behavior, stable binary layout, and one consistent, stringent contract a plug-in codes against once, across every adapter format.

	## Design notes

	- Symbiosis is an interface specification, not an engine model.
	- Fixed core ABI with no optional fields in base structs.
	- Design-by-contract: unless explicitly stated otherwise, all inputs are required and must be valid.

	## Priorities and non-goals

	These resolve the compromises the rest of the header only implies. When a rule seems to cost something, it is buying one of these:

	- The product is a single, consistent, stringent contract the plug-in codes against once, plus honesty about what each host can actually do. It is explicitly NOT identical run-time behavior across hosts.
	- Consistency is of the *interface and its semantics*, never of *host capability*. Where hosts genuinely differ, the contract degrades predictably to what the host provides. Symbiosis never fabricates a capability the host lacks, and never makes a poor host cosplay a rich one.
	- Symbiosis is not a thread-safety layer and not a host emulator. Any ordering, timing, or scheduling guarantee is a property of a specific call in a specific documented mode, never a general service the ABI provides. Thread-coherence, when it happens, is a consequence of such a rule, not its purpose.
	- Single authority: where a value or event can reach the plug-in by more than one route, exactly one route is authoritative at any moment, and the contract states which. Degraded or redundant routes must never compete with the authoritative one as a second source of truth. (The parameter `updateParameter`/timed-event split is the primary instance; see below.)

	## General rules

	- Parameter values are normalized [0.0, 1.0]. See SymbiosisParameterScale comments for mapping rules.
	- Parameters reach the plug-in through exactly one authoritative delivery discipline (single-authority rule): where the format supports timed parameter events and the plug-in enabled them, those events are authoritative and `updateParameter` arrives only as serialized block-boundary state commits; otherwise `updateParameter` is the sole authority and may arrive at any time. The two disciplines never coexist as competing sources of truth, and Symbiosis does not synthesize timed events on formats that lack them (that would fake a capability the host does not have).
	- All strings are UTF-8, zero-terminated.
	- `maxLength` parameters for string output is always > 0 and don't count the null terminator (meaning minimum size buffers are `maxLength + 1`).
	- Called functions must always append a null terminator at the end of string output even when truncating.
	- Pointers passed into calls are never optional unless explicitly documented (for example "may be null").
	- Threading rules in this header are strict API contracts, not hints.
	- Violations of preconditions (e.g. pointer validity, value ranges, ordering, and required thread) are programmer errors: behavior is undefined, assert in debug builds.

	## Parameter identity and layout

	- Parameter and bus layout are immutable after shipping.
	- All parameters are public and potentially automatable; there is no concept of private/internal parameters in the core ABI.
	- Parameter numbers are stable identifiers: append-only; never reorder, insert in the middle, or remove (deprecate and keep numbers reserved).
	- Some wrapped formats and hosts bind automation to parameter index, so index stability is required for session compatibility.
	- SymbiosisParameterInfo::sortOrder is a presentation hint only and must not be used as parameter identity.

	## Bus formats

	- Plug-ins declare, per bus, the set of bus formats they support via masks (a bus may list several).
	- Selection is strict. For each bus the host / adapter selects exactly one of that bus's declared formats, and it must select a format whose channel topology the host can actually provide. The adapter never *agrees to* a wider or narrower format than the host offers in order to make things fit (e.g. never commits a stereo bus to a mono-only source, and never folds stereo down to mono at the format level). If the host can provide none of a bus's declared formats, the adapter must not present that configuration to the plug-in at all.
	- The plug-in only ever receives one of its own declared formats; widths it did not declare are never synthesised for it. A plug-in that accepts several widths declares them all (choosing among them is the host's job); any looser adaptation between widths it accepts is the plug-in's own concern, never the adapter's.
	- Delivery is best-effort. renderAudio() always receives exactly `inputChannelCount` non-null channel pointers for the agreed format. The host controls per-block buffer delivery and cannot be renegotiated mid-stream, so it may hand the adapter fewer usable buffers than the agreed format for a connected bus (null or short channel arrays). The adapter then aliases the channels the host did provide up to the agreed count (channel `i` -> host channel `i % hostChannelCount`), preserving signal rather than dropping it; a fully unconnected bus (0 host channels) is filled from a single shared silence buffer. This buffer-pointer aliasing is a delivery-time fallback only and never licenses *selecting* a format the host cannot provide.
	- Updates can only happen when audio is stopped.

	## Versioning rules

	- `structVersion` and `interfaceVersion` identify the layout version of a struct or vtable and must match what the caller expects for that call.
	- The factory/plug-in must not use, expect, or return any interface/struct version greater than `SymbiosisLoaderInfo::maxSymbiosisVersion`.
	- All core v1 interfaces and structs in this header have version = 1 and are frozen.
	- Extension interfaces should provide their own versioning.

	## Ownership and lifetime rules

	- The caller that creates or obtains an instance (`SymbiosisFactory` or `SymbiosisPlugIn`) owns that instance and must call its destroy() exactly once.
	- Interface pointers are valid for the lifetime of their owning instance.
	- queryExtension() returns either null or a pointer to an interface/struct owned by that instance; returned pointers are valid for the lifetime of that instance.
	- Any memory returned to the host must be freed by a corresponding ABI function provided by the same instance; the host must never free it directly.
	- Unless explicitly stated otherwise, pointer fields inside a struct are owned by the instance that contains them and remain valid for that instance's lifetime.
	- Unless explicitly stated otherwise, pointers passed into functions are borrowed and only required to be valid for the duration of that call.

	## Binary layout rules and calling convention

	- All structs are packed with 8-byte alignment.
	- All functions use the SYMBIOSIS_CALL calling convention (cdecl on Windows, default on other platforms).
	- All integer types and 4-char codes are native endian.
	- The only exported function is symbiosisCreateFactory; all other functions are accessed through the returned factory and plug-in interfaces.

	## Future

	- Note expression events: per-note parameter-like events (noteId + expression id + normalizedValue).
	- SysEx events: variable-length MIDI SysEx payloads with explicit length and byte payload.
	- Patch management: enumerate patches, load/store patches, and expose stable patch identifiers.
	- Transport and timeline info: time signature, bar/beat position, loop range, and play state changes.
	- CPU saving / sleep mode: allow the plug-in to signal that it can stop processing until the next relevant input (audio becoming non-silent, parameter/MIDI/event input, transport change, etc).
	- Note names: optional mapping of MIDI note numbers to display names (useful for drum instruments). Suggested minimal API: getNoteName(plugIn, UInt32 midiNoteNumber, UInt32 maxLength, UTF8Z* outText). Hosts may cache results and refresh after updateDisplay().
	- A "bypass" parameter type that maps to VST3's bypass parameter, so that GUI can offer a linked bypass button in the hosts that allow it.
*/

#ifndef SymbiosisABI_h
#define SymbiosisABI_h

#include <stdint.h>

#if defined(_WIN32) || defined(_WIN64)
	#define SYMBIOSIS_CALL __cdecl
	#define SYMBIOSIS_EXPORT __declspec(dllexport)
#else
	#define SYMBIOSIS_CALL
	#if defined(__GNUC__) && __GNUC__ >= 4
		#define SYMBIOSIS_EXPORT __attribute__((visibility("default")))
	#else
		#define SYMBIOSIS_EXPORT
	#endif
#endif

#if defined(_MSC_VER)
	__pragma(pack(push, 8))
#else
	_Pragma("pack(push, 8)")
#endif

typedef uint8_t UByte8;
typedef uint16_t UInt16;
typedef int32_t Int32;
typedef uint32_t UInt32;
typedef int64_t Int64;
typedef uint64_t UInt64;
typedef float Float32;
typedef double Double64;
typedef uint8_t Bool8;

typedef char UTF8Z;

enum SymbiosisParameterScale {
	SYMBIOSIS_PARAMETER_SCALE_LINEAR = 1,	// `minimum` + [0..1] * (`maximum` - `minimum`) (supports inverted ranges where minimum > maximum)
	SYMBIOSIS_PARAMETER_SCALE_STEPPED = 2,	// floor([0..1] * (`maximum` + 0.5); step count = `maximum` + 1; use `displayTexts` when provided
	SYMBIOSIS_PARAMETER_SCALE_BOOLEAN = 3,	// false if < 0.5, true otherwise; `minimum`/`maximum` must be 0/1; use `displayTexts` when provided
	SYMBIOSIS_PARAMETER_SCALE_CUSTOM = 4	// see `convertParameterValueToText` and `convertTextToParameterValue`
};

enum SymbiosisEventType {
	SYMBIOSIS_EVENT_TYPE_MIDI = 1,			// 3-byte MIDI
	SYMBIOSIS_EVENT_TYPE_PARAMETER = 2
};

enum SymbiosisBusFormat {
	SYMBIOSIS_BUS_FORMAT_MONO_MASK = 0x01, 				// 1 ch: M
	SYMBIOSIS_BUS_FORMAT_STEREO_MASK = 0x02, 			// 2 ch: L, R
	SYMBIOSIS_BUS_FORMAT_LCR_MASK = 0x04, 				// 3 ch: L, C, R
	SYMBIOSIS_BUS_FORMAT_QUAD_MASK = 0x08, 				// 4 ch: L, R, Ls, Rs
	SYMBIOSIS_BUS_FORMAT_5_0_MASK = 0x10, 				// 5 ch: L, R, C, Ls, Rs
	SYMBIOSIS_BUS_FORMAT_5_1_MASK = 0x20, 				// 6 ch: L, R, C, LFE, Ls, Rs
	SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK = 0x40, 			// 6 ch: L, R, C, Ls, Rs, Cs
	SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK = 0x80, 			// 7 ch: L, R, C, LFE, Ls, Rs, Cs
	SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK = 0x100, 		// 7 ch: L, R, C, Ls, Rs, Lc, Rc
	SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK = 0x200, 		// 8 ch: L, R, C, LFE, Ls, Rs, Lc, Rc
	SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK = 0x400, 		// 8 ch: L, R, C, LFE, Ls, Rs, Lrs, Rrs
	SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK = 0x40000000,	// any channel count except 0; host chooses
	SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK = 0x80000000	// same-index input/output bus must use the same format; set on both input and output
};

enum SymbiosisEventCapabilities {
	SYMBIOSIS_WANTS_MIDI_INPUT_MASK = 0x01,
	SYMBIOSIS_WANTS_PARAMETER_EVENTS_MASK = 0x02, 	// host/adapter may deliver parameter updates as SYMBIOSIS_EVENT_TYPE_PARAMETER events in inputEvents
	SYMBIOSIS_CREATES_MIDI_OUTPUT_MASK = 0x10,
	SYMBIOSIS_USES_MULTI_CHANNEL_MASK = 0x100, 		// if unset, message channel is ignored (expect channel 1)
	SYMBIOSIS_USES_PITCH_BEND_MASK = 0x200,			// MIDI message status 0xE0
	SYMBIOSIS_USES_CHANNEL_PRESSURE_MASK = 0x400, 	// MIDI message status 0xD0
	SYMBIOSIS_USES_POLY_PRESSURE_MASK = 0x800, 		// MIDI message status 0xA0 (polyphonic key pressure)
	SYMBIOSIS_USES_PROGRAM_CHANGE_MASK = 0x1000,	// MIDI message status 0xC0
	SYMBIOSIS_USES_PC_PER_CHANNEL_MASK = 0x2000,	// only when SYMBIOSIS_USES_PROGRAM_CHANGE_MASK is set; sends program change per channel instead of channel 1 only
};

enum SymbiosisPlugInType {
	SYMBIOSIS_PLUGIN_TYPE_EFFECT = 1,
	SYMBIOSIS_PLUGIN_TYPE_MIDI_EFFECT = 2,
	SYMBIOSIS_PLUGIN_TYPE_INSTRUMENT = 3,
	SYMBIOSIS_PLUGIN_TYPE_MIDI_PROCESSOR = 4
};

enum SymbiosisPlugInCategory {
	SYMBIOSIS_PLUGIN_CATEGORY_SYNTH_MASK = 0x01,			SYMBIOSIS_PLUGIN_CATEGORY_SAMPLER_MASK = 0x02,
	SYMBIOSIS_PLUGIN_CATEGORY_DRUM_MASK = 0x04,				SYMBIOSIS_PLUGIN_CATEGORY_PIANO_MASK = 0x08,
	SYMBIOSIS_PLUGIN_CATEGORY_GUITAR_MASK = 0x10,			SYMBIOSIS_PLUGIN_CATEGORY_VOCALS_MASK = 0x20,
	SYMBIOSIS_PLUGIN_CATEGORY_UTILITY_MASK = 0x40,			SYMBIOSIS_PLUGIN_CATEGORY_ANALYZER_MASK = 0x80,
	SYMBIOSIS_PLUGIN_CATEGORY_CHANNEL_STRIP_MASK = 0x100,	SYMBIOSIS_PLUGIN_CATEGORY_DELAY_MASK = 0x200,
	SYMBIOSIS_PLUGIN_CATEGORY_REVERB_MASK = 0x400,			SYMBIOSIS_PLUGIN_CATEGORY_EQ_MASK = 0x800,
	SYMBIOSIS_PLUGIN_CATEGORY_DYNAMICS_MASK = 0x1000,		SYMBIOSIS_PLUGIN_CATEGORY_PITCH_MASK = 0x2000,
	SYMBIOSIS_PLUGIN_CATEGORY_DISTORTION_MASK = 0x4000,		SYMBIOSIS_PLUGIN_CATEGORY_MODULATION_MASK = 0x8000,
	SYMBIOSIS_PLUGIN_CATEGORY_FILTER_MASK = 0x10000,		SYMBIOSIS_PLUGIN_CATEGORY_SPATIAL_MASK = 0x20000,
	SYMBIOSIS_PLUGIN_CATEGORY_RESTORATION_MASK = 0x40000
};

enum SymbiosisStateScope {
	SYMBIOSIS_STATE_SCOPE_FULL = 1,
	SYMBIOSIS_STATE_SCOPE_PROGRAM = 2
};

enum SymbiosisStateSourceFormat {
	SYMBIOSIS_STATE_SOURCE_FORMAT_SYMB = 0x53594D42,	// 'SYMB'
	SYMBIOSIS_STATE_SOURCE_FORMAT_VST2 = 0x56535432,	// 'VST2'
	SYMBIOSIS_STATE_SOURCE_FORMAT_VST3 = 0x56535433,	// 'VST3'
	SYMBIOSIS_STATE_SOURCE_FORMAT_AU = 0x41552020,		// 'AU  '
	SYMBIOSIS_STATE_SOURCE_FORMAT_AUV3 = 0x41555633,	// 'AUV3'
	SYMBIOSIS_STATE_SOURCE_FORMAT_AAX = 0x41415820		// 'AAX '
};

struct SymbiosisParameterInfo {
	UInt32 structVersion;              				// = 1
	const UTF8Z* displayName;						// display only; keep short (ideally under 20 chars)
	const UTF8Z* displayUnit;          				// may be null; include leading space if needed between value and unit
	UInt32 sortOrder;                   			// host ordering hint
	UInt32 scale;                        			// SymbiosisParameterScale
	Float32 defaultValue;							// [0..1]
	Float32 minimum;                   				// metadata range endpoint (may be greater than maximum for inverted controls; see rules)
	Float32 maximum;                   				// metadata range endpoint (may be less than minimum for inverted controls; see rules)
	const UTF8Z* const* displayTexts;				// STEPPED/BOOLEAN labels; null => host defaults; otherwise exactly Int32(maximum + 1) entries
};

struct SymbiosisAudioBusInfo {
	UInt32 structVersion;							// = 1
	const UTF8Z* displayName;
	UInt32 supportedBusFormats;         			// bitmask SYMBIOSIS_BUS_FORMAT_*_MASK, at least one bit must be set
};

struct SymbiosisLoaderInfo {
	UInt32 structVersion;                			// = 1
	UInt32 maxSymbiosisVersion; 					// highest supported Symbiosis ABI version
	const UTF8Z* applicationName;					// may be null if unknown
	const UTF8Z* applicationVendor;					// may be null if unknown
	UInt32 applicationVersionHex;         			// 0 if unknown; otherwise `(major << 16) | (minor << 8) | patch`
	const UTF8Z* adapterFormat;           			// null => direct Symbiosis; else one of: "VST2", "VST3", "AU", "AUv3", "AAX" (case-sensitive, not empty)
};

struct SymbiosisPlugInInfo {
	UInt32 structVersion;                			// = 1 and set by the plugin
	UInt32 plugInId;                     			// 4-char code; native-endian; stable
	const UTF8Z* displayName;
	const UTF8Z* displayVendor;
	UInt32 versionHex;                   			// aa.bb.cc as 0xAABBCC
	UInt32 plugInType;								// SymbiosisPlugInType
	UInt32 plugInCategories;						// SymbiosisPlugInCategory bitmask; try to keep it 4 bits set maximum
	UInt32 vendorId; 		           	 			// 4-char code; native-endian; stable
	const UTF8Z* vendorUrl;
	Bool8 hasCustomUIView;							// false => host/adapter provides generic UI; true => host/adapter must call getUIViewSize() and openUIView()
	Bool8 handlesBypass;							// false => plug-in has NO bypass surface: adapters advertise no host bypass, setBypass() is never called and `SymbiosisRenderInputArgs::isBypassed` stays false (host performs its own hard bypass); true => plug-in fully implements the bypass contract (see setBypass() and `isBypassed`)
	UInt32 programCount;							// minimum is 1
	UInt32 parameterCount;							// all parameters in Symbiosis are public and automatable
	const SymbiosisParameterInfo* parameters;     	// * parameterCount; null if parameterCount == 0
	UInt32 audioInputBusCount;
	const SymbiosisAudioBusInfo* audioInputBuses; 	// * audioInputBusCount; null if audioInputBusCount == 0
	UInt32 audioOutputBusCount;
	const SymbiosisAudioBusInfo* audioOutputBuses; 	// * audioOutputBusCount; null if audioOutputBusCount == 0
	UInt32 eventCapabilities;						// SymbiosisEventCapabilities bitmask
	Bool8 requiredCCNumbers[128];					// if SYMBIOSIS_WANTS_MIDI_INPUT_MASK is set, host may send CC where entry[index] is true; index = MIDI CC number
	UInt32 maxInputEventCountPerBlock;				// max input events per renderAudio(); must be > 0 if SYMBIOSIS_WANTS_MIDI_INPUT_MASK or SYMBIOSIS_WANTS_PARAMETER_EVENTS_MASK is set; <= 4096 for sanity
	UInt32 maxOutputEventCountPerBlock;				// max output events per renderAudio(); must be > 0 if SYMBIOSIS_CREATES_MIDI_OUTPUT_MASK is set; <= 4096 for sanity
};

struct SymbiosisMidiEventData {
	UByte8 status;									// only events declared in eventCapabilities are guaranteed
	UByte8 data1;
	UByte8 data2;
};

struct SymbiosisParameterEventData {
	UInt32 parameterNumber;
	Float32 normalizedValue;
};

struct SymbiosisEvent {
	UInt32 offset;                        			// 0..bufferSize-1 (offset 0 corresponds to SymbiosisRenderInputArgs::samplePosition)
	UInt32 type;									// SymbiosisEventType; only types declared in plug-in's eventCapabilities are valid
	const void* data;								// payload: SymbiosisMidiEventData or SymbiosisParameterEventData; input valid for call; output valid until next renderAudio()
};

struct SymbiosisConfigureAudioInputArgs {
	UInt32 structVersion; 							// = 1
	Float32 sampleRate;
	UInt32 maxBufferSize; 							// 0 = unknown
	UInt32 inputChannelCount; 						// sum of input bus channels
	UInt32 outputChannelCount; 						// sum of output bus channels
	const UInt32* inputBusFormats;      			// selected SYMBIOSIS_BUS_FORMAT_*_MASK values * audioInputBusCount; each must match a supported format mask; null if audioInputBusCount == 0
	const UInt32* inputBusChannelCounts; 			// * audioInputBusCount (all > 0); null if audioInputBusCount == 0
	const UInt32* outputBusFormats;     			// selected SYMBIOSIS_BUS_FORMAT_*_MASK values * audioOutputBusCount; each must match a supported format mask; null if audioOutputBusCount == 0
	const UInt32* outputBusChannelCounts; 			// * audioOutputBusCount (all > 0); null if audioOutputBusCount == 0
};

struct SymbiosisConfigureAudioOutputArgs {
	UInt32 structVersion;                			// = 1 (set by the host)
	UInt32 latencySamples;
	Int32 tailSamples;         						// -1 = infinite; 0 = none
};

struct SymbiosisRenderInputArgs {
	UInt32 structVersion;							// = 1
	UInt32 bufferSize;								// number of frames to process in this block; > 0; <= `maxBufferSize` (if not 0)
	const Bool8* inputBusConnected;					// * audioInputBusCount
	const Bool8* inputBusSilent;					// * audioInputBusCount; 1 => entire bus is silent; optimization hint; null if audioInputBusCount == 0
	const Float32* const* inputChannels; 			// * inputChannelCount; canonical channel order; may alias input/output; null if inputChannelCount == 0
	UInt32 inputEventCount;							// number of events in inputEvents; will be <= maxInputEventCountPerBlock
	const SymbiosisEvent* inputEvents;				// * inputEventCount; null if count == 0; sorted by offset (arrival order for equal offsets); payload pointers valid for the call
	const Bool8* outputBusConnected;				// * audioOutputBusCount
	Bool8 isBypassed;								// true => still process events/update state, but pass audio through (or silence for instrument); per-block mirror of the sticky setBypass() state; always false when `handlesBypass` is false
	Bool8 isTransportRunning;
	Double64 tempo;
	Double64 ppqPosition;
	Bool8 isTransportLooping;
	Double64 loopStartPPQPosition;
	Double64 loopEndPPQPosition;
	Int64 samplePosition;                   		// absolute position in samples of offset 0 for this renderAudio() call
};

struct SymbiosisRenderOutputArgs {
	UInt32 structVersion;                			// = 1 (set by the host)
	Bool8* outputBusSilent;							// * audioOutputBusCount; host-owned; plug-in writes elements; 1 => entire bus silent; optimization hint; null if audioOutputBusCount == 0
	Float32* const* outputChannels; 				// * outputChannelCount; host-owned; plug-in writes data; canonical channel order; may alias input/output; null if outputChannelCount == 0
	UInt32 outputEventCount;						// defaults to 0; must be <= maxOutputEventCountPerBlock; if > 0, outputEvents must be valid until next call
	const SymbiosisEvent* outputEvents;				// plug-in-owned; valid until next renderAudio(); sorted by offset (arrival order for equal offsets)
};

typedef struct SymbiosisHost SymbiosisHost;
typedef struct SymbiosisPlugIn SymbiosisPlugIn;
typedef struct SymbiosisFactory SymbiosisFactory;

// Host interface passed to plug-in instances for callbacks; standard callbacks are UI-thread only.
struct SymbiosisHostInterface {
	UInt32 interfaceVersion;               																				// = 1; extension interfaces may have other versions
	void (SYMBIOSIS_CALL *updateDisplay)(SymbiosisHost* host);															// request host to update all UI (e.g. after GUI loads a new patch); host may call back into plug-in to get updated program/parameter info; may imply that host suspends all automation playback
	void (SYMBIOSIS_CALL *beginEdit)(SymbiosisHost* host, UInt32 parameterNumber);										// user starts editing a parameter (e.g. host can begin writing automation); do not call more than once for a given `parameterNumber` before a corresponding endEdit()
	void (SYMBIOSIS_CALL *writeParameter)(SymbiosisHost* host, UInt32 parameterNumber, Float32 normalizedValue);		// always echoed back to updateParameter() (directly, asynchronously, or by a later process block), so the processor can rely on it alone; beginEdit() optional but recommended while editing
	void (SYMBIOSIS_CALL *endEdit)(SymbiosisHost* host, UInt32 parameterNumber);										// user stops editing a parameter (e.g. host can end writing automation); do not call more than once for a given `parameterNumber` after a corresponding beginEdit()
	Bool8 (SYMBIOSIS_CALL *requestResize)(SymbiosisHost* host, UInt32 width, UInt32 height);							// false => resize unavailable (typically ask user to close/reopen editor)
	const void* (SYMBIOSIS_CALL *queryExtension)(SymbiosisHost* host, UInt32 vendorId, UInt32 interfaceId);         	// null => unsupported; pointer is host-owned and valid for host instance lifetime
};

struct SymbiosisPlugInInterface {
	UInt32 interfaceVersion;               																				// = 1; extension interfaces may have other versions

	// Parameter access and program selection. Calls may overlap with each other and with UI/audio calls. Use your own synchronization; avoid blocking or allocation in these callbacks.
	void (SYMBIOSIS_CALL *changeProgram)(SymbiosisPlugIn* plugIn, UInt32 programNumber);								// may be ignored when `programCount` == 1
	void (SYMBIOSIS_CALL *updateParameter)(SymbiosisPlugIn* plugIn, UInt32 parameterNumber, Float32 normalizedValue);	// with enabled and supported parameter events (SYMBIOSIS_EVENT_TYPE_PARAMETER), this is serialized at render block boundaries; otherwise it may be called at any time
	Float32 (SYMBIOSIS_CALL *getParameter)(SymbiosisPlugIn* plugIn, UInt32 parameterNumber);							// read current normalized value [0..1]; may be called at any time
	void (SYMBIOSIS_CALL *setBypass)(SymbiosisPlugIn* plugIn, Bool8 bypassed);											// sticky host bypass; called on every change (even while disabled or not rendering); only when `handlesBypass`; mirrored per block by `isBypassed`

	// Audio functions. Calls are serialized (non-overlapping) within this group, but may overlap with parameter/program and UI calls.
	void (SYMBIOSIS_CALL *configureAudio)(SymbiosisPlugIn* plugIn, const SymbiosisConfigureAudioInputArgs* inArgs
			, SymbiosisConfigureAudioOutputArgs* outArgs);																// configure/reconfigure audio processing for current sample-rate/block-size/bus layout; may allocate/reallocate resources; call only while audio is disabled
	void (SYMBIOSIS_CALL *enableAudio)(SymbiosisPlugIn* plugIn);														// enter enabled state for rendering; no allocation, blocking, or system I/O
	void (SYMBIOSIS_CALL *renderAudio)(SymbiosisPlugIn* plugIn, const SymbiosisRenderInputArgs* inArgs
				, SymbiosisRenderOutputArgs* outArgs);																	// main real-time processing callback; no blocking, allocation, or system I/O
	void (SYMBIOSIS_CALL *disableAudio)(SymbiosisPlugIn* plugIn);														// leave enabled state; no allocation, blocking, or system I/O; after disableAudio(), no renderAudio() until next enableAudio()

	// UI functions. Calls are serialized by host UI context, but may overlap with audio/parameter calls. Allocation is allowed here.
	void (SYMBIOSIS_CALL *setProgramName)(SymbiosisPlugIn* plugIn, UInt32 programNumber, const UTF8Z* text);
	void (SYMBIOSIS_CALL *getProgramName)(SymbiosisPlugIn* plugIn, UInt32 programNumber, UInt32 maxLength
			, UTF8Z* outText);
	Bool8 (SYMBIOSIS_CALL *convertParameterValueToText)(SymbiosisPlugIn* plugIn, UInt32 parameterNumber
			, Float32 normalizedValue, UInt32 maxLength, UTF8Z* outText);												// display text conversion; false => host default (linear fallback for `SYMBIOSIS_PARAMETER_SCALE_CUSTOM`)
	Bool8 (SYMBIOSIS_CALL *convertTextToParameterValue)(SymbiosisPlugIn* plugIn, UInt32 parameterNumber
			, const UTF8Z* text, Float32* normalizedValue); 															// false => use default conversion (linear for `SYMBIOSIS_PARAMETER_SCALE_CUSTOM`)
	Bool8 (SYMBIOSIS_CALL *loadState)(SymbiosisPlugIn* plugIn, UInt32 dataSize, const UByte8* data); 					// false => failure (details via getLastErrorText()); plug-in defines blob format incl. byte order/encoding
	Bool8 (SYMBIOSIS_CALL *createSaveState)(SymbiosisPlugIn* plugIn, UInt32* dataSize, UByte8** data); 					// false => failure (details via getLastErrorText()); success sets `*dataSize`/`*data`; if `*dataSize == 0`, `*data` may be null
	Bool8 (SYMBIOSIS_CALL *loadProgramState)(SymbiosisPlugIn* plugIn, UInt32 programNumber, UInt32 dataSize
			, const UByte8* data);																						// load one program into `programNumber`; plug-in defines blob format incl. byte order/encoding
	Bool8 (SYMBIOSIS_CALL *createProgramSaveState)(SymbiosisPlugIn* plugIn, UInt32 programNumber, UInt32* dataSize
			, UByte8** data);																							// export one program from `programNumber`; success sets `*dataSize`/`*data`; if `*dataSize == 0`, `*data` may be null
	void (SYMBIOSIS_CALL *destroySaveState)(SymbiosisPlugIn* plugIn, UByte8* data); 									// free memory from createSaveState()/createProgramSaveState(); `data` may be null
	Bool8 (SYMBIOSIS_CALL *getUIViewSize)(SymbiosisPlugIn* plugIn, UInt32* outWidth, UInt32* outHeight);				// only if `hasCustomUIView`; false => failure (details via getLastErrorText())
	Bool8 (SYMBIOSIS_CALL *openUIView)(SymbiosisPlugIn* plugIn, void* nativeGUIElement); 								// only if `hasCustomUIView`; Windows: HWND, macOS: NSView*; false => failure (details via getLastErrorText())
	void (SYMBIOSIS_CALL *closeUIView)(SymbiosisPlugIn* plugIn);														// called exactly once for each successful openUIView()
	const void* (SYMBIOSIS_CALL *queryExtension)(SymbiosisPlugIn* plugIn, UInt32 vendorId, UInt32 interfaceId);    		// null => unsupported; pointer is plug-in-owned and valid for plug-in instance lifetime
	void (SYMBIOSIS_CALL *getLastErrorText)(SymbiosisPlugIn* plugIn, UInt32 maxLength, UTF8Z* outText);					// call only after an error-reporting function returns false; empty string => unknown error
	
	void (SYMBIOSIS_CALL *destroy)(SymbiosisPlugIn* plugIn);
};

// PROVISIONAL / UNAPPROVED (2026-07-04): legacy state-import extension added by Codex,
// not reviewed, referenced by no adapter. Do not treat as stable ABI. See task #28 /
// docs/SymbiosisTODO.md "Legacy state-import ABI". Candidate for removal.
enum {
	SYMBIOSIS_EXTENSION_VENDOR_ID = 0x53594D42, // 'SYMB'
	SYMBIOSIS_EXTENSION_LEGACY_STATE_IMPORT_INTERFACE_ID = 0x4C53494D, // 'LSIM' (v1)
	SYMBIOSIS_EXTENSION_LEGACY_STATE_IMPORT_V2_INTERFACE_ID = 0x4C534932 // 'LSI2'
};

struct SymbiosisLegacyStateImportInterface {
	UInt32 interfaceVersion; // = 1
	Bool8 (SYMBIOSIS_CALL *loadLegacyState)(SymbiosisPlugIn* plugIn, const UTF8Z* formatName, UInt32 dataSize
			, const UByte8* data);
};

struct SymbiosisLegacyStateImportProbeArgs {
	UInt32 structVersion; // = 1
	UInt32 stateScope; // SymbiosisStateScope
	UInt32 programNumber; // required when stateScope == SYMBIOSIS_STATE_SCOPE_PROGRAM; ignored otherwise
	UInt32 sourceFormat; // SymbiosisStateSourceFormat or adapter-defined 4-char code
	const UTF8Z* legacyFormatName; // required for legacy payload routing; UTF-8 identifier
};

struct SymbiosisLegacyStateImportArgs {
	UInt32 structVersion; // = 1
	UInt32 stateScope; // SymbiosisStateScope
	UInt32 programNumber; // required when stateScope == SYMBIOSIS_STATE_SCOPE_PROGRAM; ignored otherwise
	UInt32 sourceFormat; // SymbiosisStateSourceFormat or adapter-defined 4-char code
	const UTF8Z* legacyFormatName; // required for legacy payload routing; UTF-8 identifier
	UInt32 dataSize; // bytes in data
	const UByte8* data; // required when dataSize > 0
};

struct SymbiosisLegacyStateImportV2Interface {
	UInt32 interfaceVersion; // = 2
	Bool8 (SYMBIOSIS_CALL *canLoadLegacyState)(SymbiosisPlugIn* plugIn, const SymbiosisLegacyStateImportProbeArgs* inArgs);
	Bool8 (SYMBIOSIS_CALL *loadLegacyState)(SymbiosisPlugIn* plugIn, const SymbiosisLegacyStateImportArgs* inArgs);
};
// PROVISIONAL / UNAPPROVED ends here

struct SymbiosisFactoryInterface {
	UInt32 interfaceVersion;               																				// = 1; extension interfaces may have other versions
	UInt32 (SYMBIOSIS_CALL *getPlugInCount)(SymbiosisFactory* factory);													// number of available plug-ins (0 is allowed); stable until factory is destroyed
	const SymbiosisPlugInInfo* (SYMBIOSIS_CALL *getPlugInInfo)(SymbiosisFactory* factory, UInt32 index);				// returned pointer must be valid until factory is destroyed; index < getPlugInCount(); used during scanning, be efficient and never fail
	SymbiosisPlugIn* (SYMBIOSIS_CALL *createPlugIn)(SymbiosisFactory* factory, UInt32 index, SymbiosisHost* host
			, const SymbiosisHostInterface* hostInterface, const SymbiosisPlugInInterface** outPlugInInterface);		// set `outPlugInInterface`; return instance or null on failure (details via getLastErrorText()); host and hostInterface must remain valid until plug-in destroy()
	const void* (SYMBIOSIS_CALL *queryExtension)(SymbiosisFactory* factory, UInt32 vendorId, UInt32 interfaceId);       // null => unsupported; pointer is factory-owned and valid for factory instance lifetime
	void (SYMBIOSIS_CALL *getLastErrorText)(SymbiosisFactory* factory, UInt32 maxLength, UTF8Z* outText);				// call only after an error-reporting function returns false; empty string => unknown error
	void (SYMBIOSIS_CALL *destroy)(SymbiosisFactory* factory);															// host must destroy all plug-ins created by this factory before destroying the factory
};

#ifdef __cplusplus
extern "C" {
#endif

SYMBIOSIS_EXPORT SymbiosisFactory* SYMBIOSIS_CALL symbiosisCreateFactory(const SymbiosisLoaderInfo* loaderInfo
		, const SymbiosisFactoryInterface** outFactoryInterface, UInt32 errorTextMaxLength, UTF8Z* outErrorText);     	// set `outFactoryInterface`; return instance or null on failure (write short error to `outErrorText`); `loaderInfo` is valid until factory is destroyed; multiple factories may be created

#ifdef __cplusplus
}
#endif

#if defined(_MSC_VER)
	__pragma(pack(pop))
#else
	_Pragma("pack(pop)")
#endif

#endif // SymbiosisABI_h
