#include "SymbiosisCpp.h"

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioUnitUtilities.h>
#include <CoreMIDI/MIDIServices.h>	// MIDIPacketList/MIDIPacket + MIDIPacketNext (header-only; the packet list is built manually so no CoreMIDI framework link is required)
#include <CoreFoundation/CoreFoundation.h>
#ifdef __OBJC__
#import <AppKit/AppKit.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <objc/message.h>
#import <objc/runtime.h>
#endif
#include <mach/mach_time.h>

#include <exception>
#include <math.h>
#include <memory>
#include <new>
#include <stddef.h>
#include <stdexcept>
#include <string.h>
#include <vector>

extern "C" void SYMBIOSIS_CALL symbiosisAdapterTrace(const UTF8Z* text);
extern "C" SYMBIOSIS_EXPORT void* SymbiosisAUFactory(const AudioComponentDescription* inDesc);

static const AudioUnitPropertyID SYMBIOSIS_COMPONENT_PROPERTY_ID = 3778794;
static const Float64 AU_INFINITE_TAIL_SECONDS = 10000.0;
static const UInt32 AU_STATE_HEADER_SIZE = 12u;
static const UInt32 STATE_MAGIC_SYMB = static_cast<UInt32>('S')
		| (static_cast<UInt32>('y') << 8)
		| (static_cast<UInt32>('m') << 16)
		| (static_cast<UInt32>('b') << 24);
static const UInt32 STATE_ADAPTER_AU = static_cast<UInt32>('A')
		| (static_cast<UInt32>('U') << 8)
		| (static_cast<UInt32>(' ') << 16)
		| (static_cast<UInt32>(' ') << 24);

#ifndef AudioUnit_AudioComponent_h
// Define these for older SDK's that doesn't have them...
typedef OSStatus (*AudioComponentMethod)(void *self,...);
typedef struct AudioComponentPlugInInterface {
	OSStatus (*Open)(void *self, AudioComponentInstance mInstance);
	OSStatus (*Close)(void *self);
	AudioComponentMethod (*Lookup)(SInt16 selector);
	void* reserved;
} AudioComponentPlugInInterface;
#endif

namespace {

using namespace symbiosis;

class AUAdapterInstance;

// Maps a caught exception to an OSStatus for returning across the AU ABI boundary. Parity with the
// old adapter's SY_COMPONENT_CATCH (Symbiosis.mm ~197-217): bad_alloc -> memFullErr (-108), anything
// else -> -32767 (the old generic component error). Never asserts/aborts: a failing call must return
// an error code to the host, not take the host process down or hand it a meaningless -1.
static OSStatus handleAUBoundaryException(const UTF8Z* context) {
	SYMBIOSIS_ASSERT(context != 0);
	try {
		throw;
	} catch (const std::bad_alloc&) {
		traceMessage(context, "Out of memory");
		return -108; // memFullErr
	} catch (const std::exception& exception) {
		traceMessage(context, exception.what());
		return -32767;
	} catch (...) {
		traceMessage(context, "Unknown exception");
		return -32767;
	}
}

#ifdef __OBJC__
static Class cocoaFactoryClass = nil;
static Class cocoaViewClass = nil;
static unsigned int cocoaFactoryInterfaceVersion(id, SEL);
static NSString* cocoaFactoryDescription(id, SEL);
static NSView* cocoaFactoryUIViewForAudioUnit(id, SEL, AudioUnit audioUnit, NSSize preferredSize);
static void cocoaViewDealloc(id self, SEL);
static void initAUCocoaObjectiveCClasses();
#endif

static bool isParameterDiscoveryProperty(AudioUnitPropertyID inID) {
	switch (inID) {
		case kAudioUnitProperty_ParameterList:
		case kAudioUnitProperty_ParameterInfo:
		case kAudioUnitProperty_ParameterValueStrings:
		case kAudioUnitProperty_ParameterStringFromValue:
		case kAudioUnitProperty_ParameterValueFromString:
			return true;
		default:
			break;
	}
	return false;
}

static bool parseVersionHexText(const UTF8Z* versionText, UInt32* outVersionHex) {
	SYMBIOSIS_ASSERT(versionText != 0);
	SYMBIOSIS_ASSERT(outVersionHex != 0);
	UInt32 major = 0;
	UInt32 minor = 0;
	UInt32 patch = 0;
	UInt32* components[3] = { &major, &minor, &patch };
	const UTF8Z* cursor = versionText;
	UInt32 componentIndex = 0;
	while (componentIndex < 3) {
		const UTF8Z* end = cursor;
		while (*end != 0 && *end != '.') {
			++end;
		}
		const size_t length = static_cast<size_t>(end - cursor);
		if (length == 0 || length >= 32u) {
			return false;
		}
		UTF8Z componentText[32];
		memcpy(componentText, cursor, length);
		componentText[length] = 0;
		if (!parseUnsignedIntegerText(componentText, components[componentIndex])) {
			return false;
		}
		++componentIndex;
		if (*end == 0) {
			cursor = end;
			break;
		}
		cursor = end + 1;
	}
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
		++cursor;
	}
	if (*cursor != 0) {
		return false;
	}
	if (major > 0xFFu) major = 0xFFu;
	if (minor > 0xFFu) minor = 0xFFu;
	if (patch > 0xFFu) patch = 0xFFu;
	*outVersionHex = (major << 16) | (minor << 8) | patch;
	return true;
}

static const Float64 DEFAULT_SAMPLE_RATE = 44100.0;
static const UInt32 DEFAULT_MAXIMUM_FRAMES_PER_SLICE = 1024;
static const Float64 INVALID_RENDER_SAMPLE_TIME = -12345678.0;

static bool copyCFStringToUTF8(CFStringRef string, UTF8Z* outText, UInt32 outTextLength) {
	SYMBIOSIS_ASSERT(outText != 0);
	SYMBIOSIS_ASSERT(outTextLength > 0);
	outText[0] = 0;
	if (string == 0) {
		return true;
	}
	return CFStringGetCString(string, outText, outTextLength, kCFStringEncodingUTF8) != 0;
}

static UInt32 chooseDefaultBusFormatMask(UInt32 supportedBusFormats) {
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_STEREO_MASK) != 0u) {
		return SYMBIOSIS_BUS_FORMAT_STEREO_MASK;
	}
	static const UInt32 FORMAT_MASKS[] = {
		SYMBIOSIS_BUS_FORMAT_MONO_MASK,
		SYMBIOSIS_BUS_FORMAT_LCR_MASK,
		SYMBIOSIS_BUS_FORMAT_QUAD_MASK,
		SYMBIOSIS_BUS_FORMAT_5_0_MASK,
		SYMBIOSIS_BUS_FORMAT_5_1_MASK,
		SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK
	};
	for (UInt32 i = 0; i < sizeof(FORMAT_MASKS) / sizeof(FORMAT_MASKS[0]); ++i) {
		if ((supportedBusFormats & FORMAT_MASKS[i]) != 0u) {
			return FORMAT_MASKS[i];
		}
	}
	SYMBIOSIS_ASSERT(0);
	return SYMBIOSIS_BUS_FORMAT_STEREO_MASK;
}

static bool isSupportedBusChannelCount(UInt32 supportedBusFormats, UInt32 channelCount) {
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u) {
		return channelCount >= 1u;
	}
	static const UInt32 FORMAT_MASKS[] = {
		SYMBIOSIS_BUS_FORMAT_MONO_MASK,
		SYMBIOSIS_BUS_FORMAT_STEREO_MASK,
		SYMBIOSIS_BUS_FORMAT_LCR_MASK,
		SYMBIOSIS_BUS_FORMAT_QUAD_MASK,
		SYMBIOSIS_BUS_FORMAT_5_0_MASK,
		SYMBIOSIS_BUS_FORMAT_5_1_MASK,
		SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK
	};
	for (UInt32 i = 0; i < sizeof(FORMAT_MASKS) / sizeof(FORMAT_MASKS[0]); ++i) {
		const UInt32 formatMask = FORMAT_MASKS[i];
		if ((supportedBusFormats & formatMask) != 0u
				&& channelCount == busFormatMaskToChannelCount(formatMask)) {
			return true;
		}
	}
	return false;
}

static AudioStreamBasicDescription makeCanonicalStreamFormat(Float64 sampleRate, UInt32 channelCount) {
	AudioStreamBasicDescription format;
	memset(&format, 0, sizeof (format));
	format.mSampleRate = sampleRate;
	format.mFormatID = kAudioFormatLinearPCM;
	format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
	format.mBytesPerPacket = sizeof (Float32);
	format.mFramesPerPacket = 1;
	format.mBytesPerFrame = sizeof (Float32);
	format.mChannelsPerFrame = channelCount;
	format.mBitsPerChannel = 32;
	return format;
}

static void collectFiniteSupportedChannelCounts(UInt32 supportedBusFormats, std::vector<SInt16>* outCounts) {
	SYMBIOSIS_ASSERT(outCounts != 0);
	static const UInt32 FORMAT_MASKS[] = {
		SYMBIOSIS_BUS_FORMAT_MONO_MASK,
		SYMBIOSIS_BUS_FORMAT_STEREO_MASK,
		SYMBIOSIS_BUS_FORMAT_LCR_MASK,
		SYMBIOSIS_BUS_FORMAT_QUAD_MASK,
		SYMBIOSIS_BUS_FORMAT_5_0_MASK,
		SYMBIOSIS_BUS_FORMAT_5_1_MASK,
		SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK
	};
	for (UInt32 i = 0; i < sizeof(FORMAT_MASKS) / sizeof(FORMAT_MASKS[0]); ++i) {
		const UInt32 formatMask = FORMAT_MASKS[i];
		if ((supportedBusFormats & formatMask) != 0u) {
			const SInt16 count = static_cast<SInt16>(busFormatMaskToChannelCount(formatMask));
			bool exists = false;
			for (UInt32 i = 0; i < outCounts->size(); ++i) {
				if ((*outCounts)[i] == count) {
					exists = true;
					break;
				}
			}
			if (!exists) {
				outCounts->push_back(count);
			}
		}
	}
}

static AudioUnitParameterUnit mapParameterUnit(const SymbiosisParameterInfo& parameterInfo) {
	if (parameterInfo.displayUnit != 0 && parameterInfo.displayUnit[0] != 0) {
		return kAudioUnitParameterUnit_CustomUnit;
	}
	switch (parameterInfo.scale) {
		case SYMBIOSIS_PARAMETER_SCALE_BOOLEAN:
			return kAudioUnitParameterUnit_Boolean;
		case SYMBIOSIS_PARAMETER_SCALE_STEPPED:
			return kAudioUnitParameterUnit_Indexed;
		case SYMBIOSIS_PARAMETER_SCALE_CUSTOM:
			return kAudioUnitParameterUnit_Generic;
		default:
			break;
	}
	return kAudioUnitParameterUnit_Generic;
}

static Float32 clamp01(Float32 value) {
	if (value < 0.0f) {
		return 0.0f;
	}
	if (value > 1.0f) {
		return 1.0f;
	}
	return value;
}

static Float32 normalizedToAudioUnitParameterValue(const SymbiosisParameterInfo& parameterInfo, Float32 normalizedValue) {
	const Float32 normalized = clamp01(normalizedValue);
	const Float32 value = parameterInfo.minimum + normalized * (parameterInfo.maximum - parameterInfo.minimum);
	switch (parameterInfo.scale) {
		case SYMBIOSIS_PARAMETER_SCALE_BOOLEAN:
		case SYMBIOSIS_PARAMETER_SCALE_STEPPED:
			return floorf(value + 0.5f);
		default:
			break;
	}
	return value;
}

static Float32 audioUnitToNormalizedParameterValue(const SymbiosisParameterInfo& parameterInfo, Float32 audioUnitValue) {
	const Float32 range = parameterInfo.maximum - parameterInfo.minimum;
	if (range == 0.0f) {
		return 0.0f;
	}
	return clamp01((audioUnitValue - parameterInfo.minimum) / range);
}

class AUAdapterHost : public Host {
	public:
		explicit AUAdapterHost(AUAdapterInstance* owner) : owner(owner) {}
		void updateDisplay() override;
		void beginEdit(UInt32 parameterNumber) override;
		void writeParameter(UInt32 parameterNumber, Float32 normalizedValue) override;
		void endEdit(UInt32 parameterNumber) override;
		bool requestResize(UInt32 width, UInt32 height) override;
		const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) override;

	protected:
		AUAdapterInstance* owner;
};

class AUAdapterInstance {
	public:
		friend class AUAdapterHost;

		enum AudioLifecycleState {
			AUDIO_LIFECYCLE_CONFIGURED = 0,
			AUDIO_LIFECYCLE_ALLOCATED = 1,
			AUDIO_LIFECYCLE_PROCESSING = 2
		};

		struct InputBusRouting {
			bool hasRenderCallback;
			AURenderCallbackStruct renderCallback;
			bool hasConnection;
			AudioUnitConnection connection;
		};

		struct PropertyListener {
			AudioUnitPropertyID propertyId;
			AudioUnitPropertyListenerProc proc;
			void* refCon;
		};

		struct RenderNotifyListener {
			AURenderCallback proc;
			void* refCon;
		};

		explicit AUAdapterInstance(const AudioComponentDescription& componentDescription)
			: componentInstance(0)
			, host(this)
			, componentDescription(componentDescription)
			, plugInInfo(0)
			, opened(false)
			, initialized(false)
			, wantsMidiInput(false)
			, createsMidiOutput(false)
			, midiOutputCallbackStruct()
			, sampleRate(DEFAULT_SAMPLE_RATE)
			, maximumFramesPerSlice(DEFAULT_MAXIMUM_FRAMES_PER_SLICE)
			, isBypassed(false)
			, latencySamples(0)
			, tailSamples(0)
			, audioLifecycleState(AUDIO_LIFECYCLE_CONFIGURED)
			, configuredInputChannelCount(0)
			, configuredOutputChannelCount(0)
			, presentPresetNumber(0)
			, customUIViewIsOpen(false)
			, customUIViewParent(0)
			, pendingInputEventCount(0)
			, lastRenderSampleTime(INVALID_RENDER_SAMPLE_TIME)
			, lastRenderFrameCount(0)
		{
			memset(&loaderInfo, 0, sizeof (loaderInfo));
			memset(factoryErrorText, 0, sizeof (factoryErrorText));
			memset(hostApplicationName, 0, sizeof (hostApplicationName));
			memset(hostApplicationVendor, 0, sizeof (hostApplicationVendor));
			memset(&hostCallbackInfo, 0, sizeof (hostCallbackInfo));
		}

		void open(AudioComponentInstance newComponentInstance) {
			SYMBIOSIS_ASSERT(!opened);
			SYMBIOSIS_ASSERT(newComponentInstance != 0);
			componentInstance = newComponentInstance;
			initializeLoaderInfo();

			const SymbiosisFactoryInterface* factoryInterface = 0;
			SymbiosisFactory* factoryInstance = symbiosisCreateFactory(
					&loaderInfo,
					&factoryInterface,
					static_cast<UInt32>(sizeof (factoryErrorText) - 1u),
					factoryErrorText);
			if (factoryInstance == 0 || factoryInterface == 0) {
				throw std::runtime_error("symbiosisCreateFactory failed");
			}
			factory.reset(new HostedFactory(factoryInstance, factoryInterface));
			if (factory->getPlugInCount() == 0) {
				throw std::runtime_error("No Symbiosis plug-ins in factory");
			}
			plugInInfo = factory->getPlugInInfo(0);
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			parameterInfos.resize(plugInInfo->parameterCount);
			parameterValueStrings.resize(plugInInfo->parameterCount, 0);
			for (UInt32 parameterIndex = 0; parameterIndex < plugInInfo->parameterCount; ++parameterIndex) {
				const SymbiosisParameterInfo& parameterInfo = plugInInfo->parameters[parameterIndex];
				SYMBIOSIS_ASSERT(parameterInfo.displayName != 0);
				CFStringRef parameterNameString = CFStringCreateWithCString(
						kCFAllocatorDefault,
						parameterInfo.displayName,
						kCFStringEncodingUTF8);
				SYMBIOSIS_ASSERT(parameterNameString != 0);

				AudioUnitParameterInfo& audioUnitParameterInfo = parameterInfos[parameterIndex];
				memset(&audioUnitParameterInfo, 0, sizeof (audioUnitParameterInfo));
				strncpy(audioUnitParameterInfo.name, parameterInfo.displayName, sizeof (audioUnitParameterInfo.name) - 1u);
				audioUnitParameterInfo.unit = mapParameterUnit(parameterInfo);
				if (audioUnitParameterInfo.unit == kAudioUnitParameterUnit_CustomUnit) {
					audioUnitParameterInfo.unitName = CFStringCreateWithCString(
							kCFAllocatorDefault,
							parameterInfo.displayUnit,
							kCFStringEncodingUTF8);
					SYMBIOSIS_ASSERT(audioUnitParameterInfo.unitName != 0);
				}
				audioUnitParameterInfo.minValue = parameterInfo.minimum;
				audioUnitParameterInfo.maxValue = parameterInfo.maximum;
				audioUnitParameterInfo.defaultValue = normalizedToAudioUnitParameterValue(parameterInfo, parameterInfo.defaultValue);
				audioUnitParameterInfo.flags = kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable;
				audioUnitParameterInfo.cfNameString = parameterNameString;
				audioUnitParameterInfo.flags |= kAudioUnitParameterFlag_HasCFNameString;
				if (parameterInfo.scale != SYMBIOSIS_PARAMETER_SCALE_BOOLEAN
						&& parameterInfo.scale != SYMBIOSIS_PARAMETER_SCALE_STEPPED) {
					//audioUnitParameterInfo.flags |= kAudioUnitParameterFlag_IsHighResolution;
				}
				if (parameterInfo.displayTexts != 0) {
					UInt32 textCount = 0;
					if (parameterInfo.scale == SYMBIOSIS_PARAMETER_SCALE_BOOLEAN) {
						textCount = 2u;
					} else if (parameterInfo.scale == SYMBIOSIS_PARAMETER_SCALE_STEPPED) {
						textCount = static_cast<UInt32>(parameterInfo.maximum) + 1u;
					}
					if (textCount > 0u) {
						CFMutableArrayRef textArray = CFArrayCreateMutable(kCFAllocatorDefault, static_cast<CFIndex>(textCount), &kCFTypeArrayCallBacks);
						SYMBIOSIS_ASSERT(textArray != 0);
						for (UInt32 textIndex = 0; textIndex < textCount; ++textIndex) {
							SYMBIOSIS_ASSERT(parameterInfo.displayTexts[textIndex] != 0);
							CFStringRef itemText = CFStringCreateWithCString(kCFAllocatorDefault, parameterInfo.displayTexts[textIndex], kCFStringEncodingUTF8);
							SYMBIOSIS_ASSERT(itemText != 0);
							CFArrayAppendValue(textArray, itemText);
							CFRelease(itemText);
						}
						parameterValueStrings[parameterIndex] = textArray;
					}
				}
				if (parameterValueStrings[parameterIndex] != 0) {
					audioUnitParameterInfo.flags |= kAudioUnitParameterFlag_ValuesHaveStrings;
				}
			}
			wantsMidiInput = ((plugInInfo->eventCapabilities & SYMBIOSIS_WANTS_MIDI_INPUT_MASK) != 0);
			plugIn.reset(factory->createPlugIn(0, &host));
			if (!plugIn) {
				throw std::runtime_error("createPlugIn failed");
			}
			refreshPresetNameForProgram(0);
			initializeBusFormats();
			if (wantsMidiInput) {
				SYMBIOSIS_ASSERT(plugInInfo->maxInputEventCountPerBlock > 0);
				pendingInputEvents.resize(plugInInfo->maxInputEventCountPerBlock);
				pendingInputMidiEvents.resize(plugInInfo->maxInputEventCountPerBlock);
				pendingInputEventCount = 0;
			}
			createsMidiOutput = ((plugInInfo->eventCapabilities & SYMBIOSIS_CREATES_MIDI_OUTPUT_MASK) != 0);
			if (createsMidiOutput) {
				SYMBIOSIS_ASSERT(plugInInfo->maxOutputEventCountPerBlock > 0);
				// Worst case: every output event is its own packet. Size one MIDIPacket slot per event so
				// manual MIDIPacketNext advancement can never run past the buffer (allocated here, not in render).
				const size_t storageSize = sizeof (MIDIPacketList)
						+ static_cast<size_t>(plugInInfo->maxOutputEventCountPerBlock) * sizeof (MIDIPacket);
				midiOutputPacketListStorage.resize(storageSize);
			}
			opened = true;
		}

		void close() {
			SYMBIOSIS_ASSERT(opened);
			if (customUIViewIsOpen) {
				closeCustomUIView();
			}
			if (initialized) {
				const OSStatus status = uninitialize();
				SYMBIOSIS_ASSERT(status == noErr);
				(void)status;
			}
			plugIn.reset();
			factory.reset();
			for (UInt32 parameterIndex = 0; parameterIndex < parameterInfos.size(); ++parameterIndex) {
				AudioUnitParameterInfo& parameterInfo = parameterInfos[parameterIndex];
				if (parameterInfo.unitName != 0) {
					CFRelease(parameterInfo.unitName);
					parameterInfo.unitName = 0;
				}
				if (parameterInfo.cfNameString != 0) {
					CFRelease(parameterInfo.cfNameString);
					parameterInfo.cfNameString = 0;
				}
				if (parameterValueStrings[parameterIndex] != 0) {
					CFRelease(parameterValueStrings[parameterIndex]);
					parameterValueStrings[parameterIndex] = 0;
				}
			}
			parameterInfos.clear();
			parameterValueStrings.clear();
			for (UInt32 i = 0; i < factoryPresetStorage.size(); ++i) {		// EXPERIMENT cleanup
				if (factoryPresetStorage[i].presetName != 0) {
					CFRelease(factoryPresetStorage[i].presetName);
					factoryPresetStorage[i].presetName = 0;
				}
			}
			factoryPresetStorage.clear();
			plugInInfo = 0;
			initialized = false;
			wantsMidiInput = false;
			createsMidiOutput = false;
			midiOutputPacketListStorage.clear();
			memset(&midiOutputCallbackStruct, 0, sizeof (midiOutputCallbackStruct));
			opened = false;
			componentInstance = 0;
			inputBusFormats.clear();
			outputBusFormats.clear();
			inputBusRouting.clear();
			renderNotifyListeners.clear();
			pendingInputEvents.clear();
			pendingInputMidiEvents.clear();
			pendingInputEventCount = 0;
			presentPresetName.clear();
			presentPresetNumber = 0;
			customUIViewIsOpen = false;
			customUIViewParent = 0;
			audioLifecycleState = AUDIO_LIFECYCLE_CONFIGURED;
			lastRenderSampleTime = INVALID_RENDER_SAMPLE_TIME;
			lastRenderFrameCount = 0;
		}

		bool getCustomUIViewSize(UInt32* outWidth, UInt32* outHeight) {
			SYMBIOSIS_ASSERT(outWidth != 0);
			SYMBIOSIS_ASSERT(outHeight != 0);
			SYMBIOSIS_ASSERT(opened);
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(plugInInfo->hasCustomUIView != 0);
			SYMBIOSIS_ASSERT(plugIn.get() != 0);
			const bool ok = plugIn->getUIViewSize(outWidth, outHeight);
			if (!ok) {
				tracePlugInLastError(plugIn.get(), "AU getUIViewSize");
				return false;
			}
			SYMBIOSIS_ASSERT(*outWidth > 0u);
			SYMBIOSIS_ASSERT(*outHeight > 0u);
			return true;
		}

		bool openCustomUIView(void* nativeGUIElement) {
			SYMBIOSIS_ASSERT(opened);
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(plugInInfo->hasCustomUIView != 0);
			SYMBIOSIS_ASSERT(plugIn.get() != 0);
			SYMBIOSIS_ASSERT(nativeGUIElement != 0);
			SYMBIOSIS_ASSERT(!customUIViewIsOpen);
			if (!plugIn->openUIView(nativeGUIElement)) {
				tracePlugInLastError(plugIn.get(), "AU openUIView");
				return false;
			}
			customUIViewIsOpen = true;
			customUIViewParent = nativeGUIElement;
			return true;
		}

		void closeCustomUIView() {
			SYMBIOSIS_ASSERT(customUIViewIsOpen);
			SYMBIOSIS_ASSERT(plugIn.get() != 0);
			plugIn->closeUIView();
			customUIViewIsOpen = false;
#ifdef __OBJC__
			if (customUIViewParent != 0) {
				// Dealloc closes the UI against the same adapter pointer; keep ivar stable.
			}
#endif
			customUIViewParent = 0;
		}

		bool hasOpenCustomUIView() const {
			return customUIViewIsOpen;
		}

		// True only if `shell` is the view the currently open custom UI lives in. Used by the shell's
		// dealloc so a STALE shell (replaced via latest-wins below) never closes the replacement view.
		bool isCustomUIViewShell(const void* shell) const {
			return customUIViewIsOpen && customUIViewParent == shell;
		}

		OSStatus initialize() {
			if (!opened) {
				return kAudioUnitErr_Uninitialized;
			}
			if (initialized) {
				return noErr;
			}
			std::vector<UInt32> inputBusFormatValues;
			std::vector<UInt32> inputBusChannelCounts;
			std::vector<UInt32> outputBusFormatValues;
			std::vector<UInt32> outputBusChannelCounts;
			UInt32 inputChannelCount = 0;
			UInt32 outputChannelCount = 0;
			resolveCurrentAudioConfiguration(&inputBusFormatValues, &inputBusChannelCounts, &outputBusFormatValues
					, &outputBusChannelCounts, &inputChannelCount, &outputChannelCount);
			const OSStatus coupledValidation = validateCoupledBusChannelCounts();
			if (coupledValidation != noErr) {
				return coupledValidation;
			}
			prepareRenderResources(inputBusChannelCounts, outputBusChannelCounts, inputChannelCount, outputChannelCount);

			SymbiosisConfigureAudioInputArgs inArgs;
			memset(&inArgs, 0, sizeof (inArgs));
			inArgs.structVersion = 1;
			inArgs.sampleRate = static_cast<Float32>(sampleRate);
			inArgs.maxBufferSize = maximumFramesPerSlice;
			inArgs.inputChannelCount = inputChannelCount;
			inArgs.outputChannelCount = outputChannelCount;
			inArgs.inputBusFormats = inputBusFormatValues.data();
			inArgs.inputBusChannelCounts = inputBusChannelCounts.data();
			inArgs.outputBusFormats = outputBusFormatValues.data();
			inArgs.outputBusChannelCounts = outputBusChannelCounts.data();
			SymbiosisConfigureAudioOutputArgs outArgs;
			memset(&outArgs, 0, sizeof (outArgs));
			outArgs.structVersion = 1;
			plugIn->configureAudio(&inArgs, &outArgs);
			latencySamples = outArgs.latencySamples;
			tailSamples = outArgs.tailSamples;
			notifyPropertyChanged(kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
			notifyPropertyChanged(kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0);
			audioLifecycleState = AUDIO_LIFECYCLE_ALLOCATED;
			plugIn->enableAudio();
			audioLifecycleState = AUDIO_LIFECYCLE_PROCESSING;
			initialized = true;
			return noErr;
		}

		OSStatus uninitialize() {
			if (!opened) {
				return kAudioUnitErr_Uninitialized;
			}
			if (initialized) {
				SYMBIOSIS_ASSERT(audioLifecycleState == AUDIO_LIFECYCLE_PROCESSING || audioLifecycleState == AUDIO_LIFECYCLE_ALLOCATED);
				if (audioLifecycleState == AUDIO_LIFECYCLE_PROCESSING) {
					plugIn->disableAudio();
					audioLifecycleState = AUDIO_LIFECYCLE_ALLOCATED;
				}
				audioLifecycleState = AUDIO_LIFECYCLE_CONFIGURED;
			}
			clearRenderResources();
			pendingInputEventCount = 0;
			initialized = false;
			lastRenderSampleTime = INVALID_RENDER_SAMPLE_TIME;
			lastRenderFrameCount = 0;
			return noErr;
		}

		OSStatus getPropertyInfo(AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement,
				UInt32* outDataSize, Boolean* outWritable) {
			if (outDataSize != 0) {
				*outDataSize = 0;
			}
			if (outWritable != 0) {
				*outWritable = 0;
			}
			if (!opened) {
				return kAudioUnitErr_Uninitialized;
			}
			PropertyDescriptor descriptor;
			memset(&descriptor, 0, sizeof (descriptor));
			const OSStatus status = getPropertyDescriptor(inID, inScope, inElement, &descriptor);
			if (isParameterDiscoveryProperty(inID)) {
				UTF8Z text[256];
				snprintf(text, sizeof (text), "id=%u scope=%u element=%u status=%d size=%u readable=%u writable=%u",
						static_cast<unsigned int>(inID),
						static_cast<unsigned int>(inScope),
						static_cast<unsigned int>(inElement),
						static_cast<int>(status),
						static_cast<unsigned int>(descriptor.dataSize),
						static_cast<unsigned int>(descriptor.readable ? 1u : 0u),
						static_cast<unsigned int>(descriptor.writable ? 1u : 0u));
				traceMessage("AU getPropertyInfo", text);
			}
			if (status != noErr) {
				return status;
			}
			if (outDataSize != 0) {
				*outDataSize = descriptor.dataSize;
			}
			if (outWritable != 0) {
				*outWritable = descriptor.writable ? 1 : 0;
			}
			return noErr;
		}

		OSStatus getProperty(AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement, void* outData,
				UInt32* ioDataSize) {
			if (!opened) {
				return kAudioUnitErr_Uninitialized;
			}
			if (ioDataSize == 0) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			PropertyDescriptor descriptor;
			memset(&descriptor, 0, sizeof (descriptor));
			OSStatus status = getPropertyDescriptor(inID, inScope, inElement, &descriptor);
			if (isParameterDiscoveryProperty(inID)) {
				UTF8Z text[256];
				snprintf(text, sizeof (text), "id=%u scope=%u element=%u status=%d requestedSize=%u outData=%u",
						static_cast<unsigned int>(inID),
						static_cast<unsigned int>(inScope),
						static_cast<unsigned int>(inElement),
						static_cast<int>(status),
						static_cast<unsigned int>(*ioDataSize),
						static_cast<unsigned int>(outData != 0 ? 1u : 0u));
				traceMessage("AU getProperty", text);
			}
			if (status != noErr) {
				return status;
			}
			if (!descriptor.readable) {
				return kAudioUnitErr_InvalidProperty;
			}
			const UInt32 requestedSize = *ioDataSize;
			*ioDataSize = descriptor.dataSize;
			if (outData == 0) {
				return noErr;
			}
			const bool acceptsLegacySmallBuffer = (inID == kAudioUnitProperty_HostCallbacks
					|| inID == kAudioUnitProperty_CurrentPreset
					|| inID == kAudioUnitProperty_PresentPreset);
			if (!acceptsLegacySmallBuffer && requestedSize < descriptor.dataSize) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			switch (inID) {
				case kAudioUnitProperty_StreamFormat: {
					AudioStreamBasicDescription format;
					memset(&format, 0, sizeof (format));
					status = getBusFormat(inScope, inElement, &format);
					if (status != noErr) {
						return status;
					}
					*reinterpret_cast<AudioStreamBasicDescription*>(outData) = format;
					*ioDataSize = static_cast<UInt32>(sizeof (format));
					return noErr;
				}
				case kAudioUnitProperty_SampleRate:
					*reinterpret_cast<Float64*>(outData) = sampleRate;
					*ioDataSize = static_cast<UInt32>(sizeof (Float64));
					return noErr;
				case kAudioUnitProperty_MaximumFramesPerSlice:
					*reinterpret_cast<UInt32*>(outData) = maximumFramesPerSlice;
					*ioDataSize = static_cast<UInt32>(sizeof (UInt32));
					return noErr;
				case kAudioUnitProperty_Latency: {
					const Float64 latencySeconds = (sampleRate > 0.0
							? static_cast<Float64>(latencySamples) / sampleRate : 0.0);
					*reinterpret_cast<Float64*>(outData) = latencySeconds;
					*ioDataSize = static_cast<UInt32>(sizeof (Float64));
					return noErr;
				}
				case kAudioUnitProperty_TailTime: {
					Float64 tailSeconds = 0.0;
					if (tailSamples < 0) {
						tailSeconds = AU_INFINITE_TAIL_SECONDS;
					} else if (sampleRate > 0.0) {
						tailSeconds = static_cast<Float64>(tailSamples) / sampleRate;
					}
					*reinterpret_cast<Float64*>(outData) = tailSeconds;
					*ioDataSize = static_cast<UInt32>(sizeof (Float64));
					return noErr;
				}
				case kAudioUnitProperty_BypassEffect:
					*reinterpret_cast<UInt32*>(outData) = (isBypassed ? 1u : 0u);
					*ioDataSize = static_cast<UInt32>(sizeof (UInt32));
					return noErr;
				case kAudioUnitProperty_ElementName: {
					const SymbiosisAudioBusInfo* elementBusInfo = (inScope == kAudioUnitScope_Input
							? &plugInInfo->audioInputBuses[inElement] : &plugInInfo->audioOutputBuses[inElement]);
					if (elementBusInfo->displayName == 0 || elementBusInfo->displayName[0] == 0) {
						return kAudioUnitErr_InvalidElement;
					}
					CFStringRef elementName = CFStringCreateWithCString(kCFAllocatorDefault
							, elementBusInfo->displayName, kCFStringEncodingUTF8);
					if (elementName == 0) {
						return kAudioUnitErr_InvalidElement;
					}
					*reinterpret_cast<CFStringRef*>(outData) = elementName;
					*ioDataSize = static_cast<UInt32>(sizeof (CFStringRef));
					return noErr;
				}
				case kAudioUnitProperty_LastRenderError:
					*reinterpret_cast<OSStatus*>(outData) = noErr;	// parity with old adapter (never tracked)
					*ioDataSize = static_cast<UInt32>(sizeof (OSStatus));
					return noErr;
				case kAudioUnitProperty_MIDIOutputCallbackInfo: {
					// One MIDI output port, named after the plug-in. The caller owns (releases) the array.
					const char* portName = (plugInInfo->displayName != 0 && plugInInfo->displayName[0] != 0
							? plugInInfo->displayName : "MIDI Out");
					CFStringRef nameString = CFStringCreateWithCString(kCFAllocatorDefault, portName, kCFStringEncodingUTF8);
					if (nameString == 0) {
						return kAudioUnitErr_InvalidProperty;
					}
					const void* values[1] = { nameString };
					CFArrayRef nameArray = CFArrayCreate(kCFAllocatorDefault, values, 1, &kCFTypeArrayCallBacks);
					CFRelease(nameString);
					if (nameArray == 0) {
						return kAudioUnitErr_InvalidProperty;
					}
					*reinterpret_cast<CFArrayRef*>(outData) = nameArray;
					*ioDataSize = static_cast<UInt32>(sizeof (CFArrayRef));
					return noErr;
				}
				case kAudioUnitProperty_CurrentPreset:
				case kAudioUnitProperty_PresentPreset:
					return getPresentPreset(inID, outData, ioDataSize, requestedSize);
				case kAudioUnitProperty_FactoryPresets: {		// EXPERIMENT
					CFArrayRef presets = buildFactoryPresetsArray();
					if (presets == 0) {
						return kAudioUnitErr_InvalidProperty;
					}
					*reinterpret_cast<CFArrayRef*>(outData) = presets;
					*ioDataSize = static_cast<UInt32>(sizeof (CFArrayRef));
					return noErr;
				}
				case kAudioUnitProperty_ClassInfo:
					return getClassInfo(outData, ioDataSize);
				case kAudioUnitProperty_HostCallbacks: {
					const UInt32 copyBytes = (requestedSize < static_cast<UInt32>(sizeof (HostCallbackInfo))
							? requestedSize : static_cast<UInt32>(sizeof (HostCallbackInfo)));
					if (copyBytes > 0u) {
						memcpy(outData, &hostCallbackInfo, copyBytes);
					}
					*ioDataSize = copyBytes;
					return noErr;
				}
				case kAudioUnitProperty_SupportedNumChannels:
					if (supportedNumChannels.size() > 0) {
						memcpy(outData, &supportedNumChannels[0], supportedNumChannels.size() * sizeof (AUChannelInfo));
					}
					*ioDataSize = static_cast<UInt32>(supportedNumChannels.size() * sizeof (AUChannelInfo));
					return noErr;
				case kAudioUnitProperty_ParameterList:
					if (inScope == kAudioUnitScope_Global) {
						for (UInt32 i = 0; i < plugInInfo->parameterCount; ++i) {
							reinterpret_cast<AudioUnitParameterID*>(outData)[i] = i;
						}
					}
					*ioDataSize = (inScope == kAudioUnitScope_Global
							? plugInInfo->parameterCount * static_cast<UInt32>(sizeof (AudioUnitParameterID)) : 0u);
					return noErr;
				case kAudioUnitProperty_ElementCount:
					if (inScope == kAudioUnitScope_Input) {
						*reinterpret_cast<UInt32*>(outData) = plugInInfo->audioInputBusCount;
					} else if (inScope == kAudioUnitScope_Output) {
						*reinterpret_cast<UInt32*>(outData) = plugInInfo->audioOutputBusCount;
					} else {
						*reinterpret_cast<UInt32*>(outData) = 0;
					}
					*ioDataSize = static_cast<UInt32>(sizeof (UInt32));
					return noErr;
				case kAudioUnitProperty_ParameterInfo: {
					*reinterpret_cast<AudioUnitParameterInfo*>(outData) = parameterInfos[inElement];
					*ioDataSize = static_cast<UInt32>(sizeof (AudioUnitParameterInfo));
					return noErr;
				}
				case kAudioUnitProperty_ParameterValueStrings:
					SYMBIOSIS_ASSERT(parameterValueStrings[inElement] != 0);
					*reinterpret_cast<CFArrayRef*>(outData) = parameterValueStrings[inElement];
					CFRetain(parameterValueStrings[inElement]);
					*ioDataSize = static_cast<UInt32>(sizeof (CFArrayRef));
					return noErr;
				case kAudioUnitProperty_ParameterStringFromValue: {
					AudioUnitParameterStringFromValue* parameterStringFromValue = reinterpret_cast<AudioUnitParameterStringFromValue*>(outData);
					if (parameterStringFromValue->inParamID >= plugInInfo->parameterCount) {
						return kAudioUnitErr_InvalidParameter;
					}
					const UInt32 parameterNumber = parameterStringFromValue->inParamID;
					const SymbiosisParameterInfo& parameterInfo = plugInInfo->parameters[parameterNumber];
					Float32 normalizedValue = plugIn->getParameter(parameterNumber);
					if (parameterStringFromValue->inValue != 0) {
						normalizedValue = audioUnitToNormalizedParameterValue(parameterInfo, *parameterStringFromValue->inValue);
					}
					UTF8Z text[256];
					const bool converted = plugIn->convertParameterValueToText(parameterNumber, normalizedValue, 255, text);
					if (!converted) {
						convertParameterValueToTextDefault(parameterInfo, normalizedValue, 255, text);
					}
					parameterStringFromValue->outString = CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
					SYMBIOSIS_ASSERT(parameterStringFromValue->outString != 0);
					*ioDataSize = static_cast<UInt32>(sizeof (AudioUnitParameterStringFromValue));
					return noErr;
				}
				case kAudioUnitProperty_ParameterValueFromString: {
					AudioUnitParameterValueFromString* parameterValueFromString = reinterpret_cast<AudioUnitParameterValueFromString*>(outData);
					if (parameterValueFromString->inParamID >= plugInInfo->parameterCount) {
						return kAudioUnitErr_InvalidParameter;
					}
					if (parameterValueFromString->inString == 0) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					UTF8Z text[256];
					const bool gotCString = CFStringGetCString(parameterValueFromString->inString, text, 256, kCFStringEncodingUTF8) != 0;
					if (!gotCString) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					const UInt32 parameterNumber = parameterValueFromString->inParamID;
					const SymbiosisParameterInfo& parameterInfo = plugInInfo->parameters[parameterNumber];
					Float32 normalizedValue = 0.0f;
					bool ok = plugIn->convertTextToParameterValue(parameterNumber, text, &normalizedValue);
					if (!ok) {
						ok = convertTextToParameterValueDefault(parameterInfo, text, &normalizedValue);
					}
					if (!ok) {
						return kAudioUnitErr_InvalidProperty;
					}
					parameterValueFromString->outValue = normalizedToAudioUnitParameterValue(parameterInfo, normalizedValue);
					*ioDataSize = static_cast<UInt32>(sizeof (AudioUnitParameterValueFromString));
					return noErr;
				}
				case kAudioUnitProperty_CocoaUI: {
#ifdef __OBJC__
					AudioUnitCocoaViewInfo cocoaInfo;
					memset(&cocoaInfo, 0, sizeof (cocoaInfo));
					initAUCocoaObjectiveCClasses();
					NSBundle* viewBundle = [NSBundle bundleForClass:cocoaFactoryClass];
					NSString* bundlePath = [viewBundle bundlePath];
					cocoaInfo.mCocoaAUViewBundleLocation = (CFURLRef)[[NSURL fileURLWithPath:bundlePath] retain];
					const UTF8Z* className = class_getName(cocoaFactoryClass);
					cocoaInfo.mCocoaAUViewClass[0] = CFStringCreateWithCString(kCFAllocatorDefault, className, kCFStringEncodingUTF8);
					*reinterpret_cast<AudioUnitCocoaViewInfo*>(outData) = cocoaInfo;
					*ioDataSize = static_cast<UInt32>(sizeof (AudioUnitCocoaViewInfo));
					return noErr;
#else
					return kAudioUnitErr_InvalidProperty;
#endif
				}
				case SYMBIOSIS_COMPONENT_PROPERTY_ID:
					*reinterpret_cast<AUAdapterInstance**>(outData) = this;
					*ioDataSize = static_cast<UInt32>(sizeof (AUAdapterInstance*));
					return noErr;
				default:
					break;
			}
			return kAudioUnitErr_InvalidProperty;
		}

		OSStatus setProperty(AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement,
				const void* inData, UInt32 inDataSize) {
			if (!opened) {
				return kAudioUnitErr_Uninitialized;
			}
			if (inData == 0) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			if (initialized
					&& (inID == kAudioUnitProperty_StreamFormat
					|| inID == kAudioUnitProperty_SampleRate
					|| inID == kAudioUnitProperty_MaximumFramesPerSlice)) {
				return kAudioUnitErr_Initialized;
			}
			switch (inID) {
				case kAudioUnitProperty_StreamFormat: {
					if (inDataSize != static_cast<UInt32>(sizeof (AudioStreamBasicDescription))) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					if (inScope != kAudioUnitScope_Input
							&& inScope != kAudioUnitScope_Output
							&& inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					const AudioStreamBasicDescription* format = reinterpret_cast<const AudioStreamBasicDescription*>(inData);
					return setBusFormat(inScope, inElement, *format);
				}
				case kAudioUnitProperty_SampleRate: {
					if (inDataSize != static_cast<UInt32>(sizeof (Float64))) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					if (inScope != kAudioUnitScope_Input
							&& inScope != kAudioUnitScope_Output
							&& inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					if ((inScope == kAudioUnitScope_Input && inElement >= plugInInfo->audioInputBusCount)
							|| (inScope != kAudioUnitScope_Input && inElement >= plugInInfo->audioOutputBusCount)) {
						return kAudioUnitErr_InvalidElement;
					}
					const Float64 value = *reinterpret_cast<const Float64*>(inData);
					if (!(value > 0.0)) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					sampleRate = value;
					for (UInt32 i = 0; i < inputBusFormats.size(); ++i) {
						inputBusFormats[i].mSampleRate = sampleRate;
					}
					for (UInt32 i = 0; i < outputBusFormats.size(); ++i) {
						outputBusFormats[i].mSampleRate = sampleRate;
					}
					notifyPropertyChanged(kAudioUnitProperty_SampleRate, inScope, inElement);
					return noErr;
				}
				case kAudioUnitProperty_MaximumFramesPerSlice: {
					if (inDataSize != static_cast<UInt32>(sizeof (UInt32))) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					const UInt32 value = *reinterpret_cast<const UInt32*>(inData);
					if (value == 0) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					maximumFramesPerSlice = value;
					notifyPropertyChanged(kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0);
					return noErr;
				}
				case kAudioUnitProperty_SetRenderCallback: {
					// Input-scope property, element = input bus. Guard scope and bounds before indexing
					// inputBusRouting (sized to audioInputBusCount) - a stray scope/element would OOB write.
					if (inScope != kAudioUnitScope_Input) {
						return kAudioUnitErr_InvalidScope;
					}
					if (inElement >= inputBusRouting.size()) {
						return kAudioUnitErr_InvalidElement;
					}
					if (inDataSize != static_cast<UInt32>(sizeof (AURenderCallbackStruct))) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					AURenderCallbackStruct callback;
					memset(&callback, 0, sizeof (callback));
					callback = *reinterpret_cast<const AURenderCallbackStruct*>(inData);
					if (callback.inputProc != 0) {
						inputBusRouting[inElement].hasRenderCallback = true;
						inputBusRouting[inElement].renderCallback = callback;
						inputBusRouting[inElement].hasConnection = false;
						memset(&inputBusRouting[inElement].connection, 0, sizeof (inputBusRouting[inElement].connection));
					} else {
						inputBusRouting[inElement].hasRenderCallback = false;
						memset(&inputBusRouting[inElement].renderCallback, 0, sizeof (inputBusRouting[inElement].renderCallback));
					}
					return noErr;
				}
				case kAudioUnitProperty_MakeConnection: {
					// Input-scope property, element = destination input bus. Guard scope and bounds before
					// indexing inputBusRouting (sized to audioInputBusCount) - a stray scope/element would
					// OOB write.
					if (inScope != kAudioUnitScope_Input) {
						return kAudioUnitErr_InvalidScope;
					}
					if (inElement >= inputBusRouting.size()) {
						return kAudioUnitErr_InvalidElement;
					}
					if (inDataSize != static_cast<UInt32>(sizeof (AudioUnitConnection))) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					const AudioUnitConnection connection = *reinterpret_cast<const AudioUnitConnection*>(inData);
					if (connection.destInputNumber != inElement) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					if (connection.sourceAudioUnit != 0) {
						inputBusRouting[inElement].hasConnection = true;
						inputBusRouting[inElement].connection = connection;
						inputBusRouting[inElement].hasRenderCallback = false;
						memset(&inputBusRouting[inElement].renderCallback, 0, sizeof (inputBusRouting[inElement].renderCallback));
					} else {
						inputBusRouting[inElement].hasConnection = false;
						memset(&inputBusRouting[inElement].connection, 0, sizeof (inputBusRouting[inElement].connection));
					}
					return noErr;
				}
				case kAudioUnitProperty_MIDIOutputCallback: {
					if (!createsMidiOutput) {
						return kAudioUnitErr_InvalidProperty;
					}
					if (inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					if (inDataSize != static_cast<UInt32>(sizeof (AUMIDIOutputCallbackStruct))) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					midiOutputCallbackStruct = *reinterpret_cast<const AUMIDIOutputCallbackStruct*>(inData);
					return noErr;
				}
				case kAudioUnitProperty_BypassEffect: {
					if (plugInInfo->handlesBypass == 0) {
						return kAudioUnitErr_InvalidProperty;	// no bypass surface; reject the set too, not just the query
					}
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					if (inDataSize < static_cast<UInt32>(sizeof (UInt32))) {
						return kAudioUnitErr_InvalidPropertyValue;
					}
					isBypassed = (*reinterpret_cast<const UInt32*>(inData) != 0);
					// Deliver out-of-band: a host may bypass and then stop rendering entirely (Ableton Live
					// device-off, verified 2026-07-06), so the render-args mirror alone is not enough. The old
					// adapter forwarded the property immediately too (Symbiosis.mm ~4676).
					plugIn->setBypass(toBool8(isBypassed));
					return noErr;
				}
				case kAudioUnitProperty_CurrentPreset:
				case kAudioUnitProperty_PresentPreset:
					return setPresentPreset(inData, inDataSize);
				case kAudioUnitProperty_ClassInfo:
					return setClassInfo(inData, inDataSize);
				case kAudioUnitProperty_HostCallbacks: {
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					HostCallbackInfo info;
					memset(&info, 0, sizeof (info));
					const UInt32 copyBytes = (inDataSize < static_cast<UInt32>(sizeof (info))
							? inDataSize : static_cast<UInt32>(sizeof (info)));
					if (copyBytes > 0) {
						memcpy(&info, inData, copyBytes);
					}
					hostCallbackInfo = info;
					return noErr;
				}
				default:
					break;
			}
			return kAudioUnitErr_InvalidProperty;
		}

		OSStatus getParameter(AudioUnitParameterID inID, AudioUnitScope inScope, AudioUnitElement inElement, Float32* outValue) {
			(void)inElement;
			if (!opened) {
				traceMessage("AU getParameter", "Uninitialized");
				return kAudioUnitErr_Uninitialized;
			}
			if (inScope != kAudioUnitScope_Global) {
				traceMessage("AU getParameter", "Invalid scope");
				return kAudioUnitErr_InvalidScope;
			}
			if (inID >= plugInInfo->parameterCount) {
				traceMessage("AU getParameter", "Invalid parameter id");
				return kAudioUnitErr_InvalidParameter;
			}
			if (outValue == 0) {
				traceMessage("AU getParameter", "Invalid outValue");
				return kAudioUnitErr_InvalidParameterValue;
			}
			const SymbiosisParameterInfo& parameterInfo = plugInInfo->parameters[inID];
			*outValue = normalizedToAudioUnitParameterValue(parameterInfo, plugIn->getParameter(inID));
			// No success-path trace here: getParameter is a hot path (hosts/UI poll it), and a per-call
			// snprintf with "%f" is both costly and locale-sensitive (the VST2 no-locale rule). The error
			// paths above already trace with cheap static strings.
			return noErr;
		}

		OSStatus setParameter(AudioUnitParameterID inID, AudioUnitScope inScope, AudioUnitElement inElement, Float32 inValue,
				UInt32 inBufferOffsetInFrames) {
			(void)inElement;
			(void)inBufferOffsetInFrames;
			if (!opened) {
				traceMessage("AU setParameter", "Uninitialized");
				return kAudioUnitErr_Uninitialized;
			}
			if (inScope != kAudioUnitScope_Global) {
				traceMessage("AU setParameter", "Invalid scope");
				return kAudioUnitErr_InvalidScope;
			}
			if (inID >= plugInInfo->parameterCount) {
				traceMessage("AU setParameter", "Invalid parameter id");
				return kAudioUnitErr_InvalidParameter;
			}
			const SymbiosisParameterInfo& parameterInfo = plugInInfo->parameters[inID];
			const Float32 normalizedValue = audioUnitToNormalizedParameterValue(parameterInfo, inValue);
			plugIn->updateParameter(inID, normalizedValue);
			// No success-path trace here: setParameter can be driven per automation point and a per-call
			// snprintf with "%f" is both costly and locale-sensitive (the VST2 no-locale rule). The error
			// paths above already trace with cheap static strings.
			return noErr;
		}

		// Build a MIDIPacketList from the plug-in's output events and hand it to the host's installed MIDI
		// output callback. Assembled in place in pre-allocated storage (no audio-thread allocation);
		// MIDIPacket time stamps are sample offsets within the current render buffer, and the callback's
		// AudioTimeStamp supplies the buffer base. Only channel-voice MIDI is emitted (2- or 3-byte).
		void deliverMidiOutputEvents(const SymbiosisRenderOutputArgs& outArgs, const AudioTimeStamp* inTimeStamp) {
			SYMBIOSIS_ASSERT(outArgs.outputEventCount <= plugInInfo->maxOutputEventCountPerBlock);
			SYMBIOSIS_ASSERT(midiOutputPacketListStorage.size() >= sizeof (MIDIPacketList));
			MIDIPacketList* packetList = reinterpret_cast<MIDIPacketList*>(midiOutputPacketListStorage.data());
			packetList->numPackets = 0;
			MIDIPacket* packet = &packetList->packet[0];
			for (UInt32 i = 0; i < outArgs.outputEventCount; ++i) {
				const SymbiosisEvent& outputEvent = outArgs.outputEvents[i];
				SYMBIOSIS_ASSERT(outputEvent.type == SYMBIOSIS_EVENT_TYPE_MIDI);
				SYMBIOSIS_ASSERT(outputEvent.data != 0);
				SYMBIOSIS_ASSERT(i == 0 || outArgs.outputEvents[i - 1].offset <= outputEvent.offset);
				const SymbiosisMidiEventData* midiData = reinterpret_cast<const SymbiosisMidiEventData*>(outputEvent.data);
				const UByte8 status = midiData->status;
				const UByte8 highNibble = static_cast<UByte8>(status & 0xF0u);
				// Program change (0xC0) and channel pressure (0xD0) are 2-byte; the other channel-voice
				// messages (note on/off, poly pressure, CC, pitch bend) are 3-byte.
				const UInt16 messageLength = static_cast<UInt16>((highNibble == 0xC0u || highNibble == 0xD0u) ? 2 : 3);
				packet->timeStamp = static_cast<MIDITimeStamp>(outputEvent.offset);
				packet->length = messageLength;
				packet->data[0] = status;
				packet->data[1] = midiData->data1;
				if (messageLength > 2) {
					packet->data[2] = midiData->data2;
				}
				++packetList->numPackets;
				packet = MIDIPacketNext(packet);
			}
			midiOutputCallbackStruct.midiOutputCallback(midiOutputCallbackStruct.userData, inTimeStamp, 0, packetList);
		}

		OSStatus render(AudioUnitRenderActionFlags* ioActionFlags, const AudioTimeStamp* inTimeStamp,
				UInt32 inOutputBusNumber, UInt32 inNumberFrames, AudioBufferList* ioData) {
			if (!opened || !initialized) {
				return kAudioUnitErr_Uninitialized;
			}
			if (inOutputBusNumber >= outputBusFormats.size()) {
				return kAudioUnitErr_InvalidElement;
			}
			if (ioData == 0) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			if (inNumberFrames == 0) {
				return noErr;
			}
			if (inNumberFrames > maximumFramesPerSlice) {
				return kAudioUnitErr_TooManyFramesToProcess;
			}
			const UInt32 outputStart = outputBusChannelOffsets[inOutputBusNumber];
			const UInt32 outputEnd = outputBusChannelOffsets[inOutputBusNumber + 1];
			const UInt32 outputChannelCountForBus = outputEnd - outputStart;
			if (ioData->mNumberBuffers != outputChannelCountForBus) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			for (UInt32 channel = 0; channel < outputChannelCountForBus; ++channel) {
				AudioBuffer& outputBuffer = ioData->mBuffers[channel];
				if (outputBuffer.mDataByteSize < inNumberFrames * sizeof (Float32)) {
					return kAudioUnitErr_InvalidPropertyValue;
				}
			}
			AudioUnitRenderActionFlags localActionFlags = (ioActionFlags != 0 ? *ioActionFlags : 0u);
			SYMBIOSIS_ASSERT((localActionFlags
					& (kAudioUnitRenderAction_PreRender | kAudioUnitRenderAction_PostRender)) == 0);
			for (UInt32 listenerIndex = 0; listenerIndex < renderNotifyListeners.size(); ++listenerIndex) {
				localActionFlags |= kAudioUnitRenderAction_PreRender;
				const RenderNotifyListener& listener = renderNotifyListeners[listenerIndex];
				const OSStatus status = listener.proc(listener.refCon, &localActionFlags, inTimeStamp
						, inOutputBusNumber, inNumberFrames, ioData);
				if (status != noErr) {
					return status;
				}
			}
			localActionFlags &= ~kAudioUnitRenderAction_PreRender;
			bool shouldRenderBatch = true;
			if (inTimeStamp != 0 && (inTimeStamp->mFlags & kAudioTimeStampSampleTimeValid) != 0) {
				shouldRenderBatch = (inTimeStamp->mSampleTime != lastRenderSampleTime || inNumberFrames != lastRenderFrameCount);
			}
			if (shouldRenderBatch) {
				lastRenderSampleTime = ((inTimeStamp != 0 && (inTimeStamp->mFlags & kAudioTimeStampSampleTimeValid) != 0)
						? inTimeStamp->mSampleTime : INVALID_RENDER_SAMPLE_TIME);
				lastRenderFrameCount = inNumberFrames;

				for (UInt32 channelIndex = 0; channelIndex < configuredInputChannelCount; ++channelIndex) {
					memset(scratchInputChannels[channelIndex], 0, inNumberFrames * sizeof (Float32));
				}
				for (UInt32 channelIndex = 0; channelIndex < configuredOutputChannelCount; ++channelIndex) {
					memset(scratchOutputChannels[channelIndex], 0, inNumberFrames * sizeof (Float32));
					outputChannelPointers[channelIndex] = scratchOutputChannels[channelIndex];
				}

				std::fill(inputBusConnectedFlags.begin(), inputBusConnectedFlags.end(), 0);
				std::fill(inputBusSilentFlags.begin(), inputBusSilentFlags.end(), 1);
				std::fill(outputBusConnectedFlags.begin(), outputBusConnectedFlags.end(), 1);
				std::fill(outputBusSilentFlags.begin(), outputBusSilentFlags.end(), 0);
				for (UInt32 inputBusNumber = 0; inputBusNumber < inputBusRouting.size(); ++inputBusNumber) {
					OSStatus status = collectInputAudioForBus(inputBusNumber, inTimeStamp, inNumberFrames
							, &inputBusConnectedFlags[inputBusNumber], &inputBusSilentFlags[inputBusNumber]);
					if (status != noErr) {
						return status;
					}
				}
			#if !defined(NDEBUG)
				// Parity with the old adapter (Symbiosis.mm ~3926): a bus flagged silent must actually be
				// silent; a signal here means the upstream source misreports its silence flag.
				for (UInt32 busNumber = 0; busNumber < inputBusRouting.size(); ++busNumber) {
					if (inputBusSilentFlags[busNumber] == 0) {
						continue;
					}
					for (UInt32 ch = inputBusChannelOffsets[busNumber];
							ch < inputBusChannelOffsets[busNumber + 1u]; ++ch) {
						for (UInt32 f = 0; f < inNumberFrames; ++f) {
							SYMBIOSIS_ASSERT(inputChannelPointers[ch][f] == 0.0f
									&& "Input flagged silent but contains signal (bug in signal source)");
						}
					}
				}
			#endif

				SymbiosisRenderInputArgs inArgs;
				memset(&inArgs, 0, sizeof (inArgs));
				UInt32 currentInputEventCount = 0;
				SYMBIOSIS_ASSERT(pendingInputEventCount == 0 || pendingInputEvents.size() > 0);
				for (UInt32 i = 0; i < pendingInputEventCount; ++i) {
					if (pendingInputEvents[i].offset < inNumberFrames) {
						++currentInputEventCount;
					}
					if (i > 0 && pendingInputEvents[i - 1].offset > pendingInputEvents[i].offset) {
						const SymbiosisEvent currentEvent = pendingInputEvents[i];
						const SymbiosisMidiEventData currentData = pendingInputMidiEvents[i];
						UInt32 j = i;
						do {
							pendingInputEvents[j] = pendingInputEvents[j - 1];
							pendingInputEvents[j].data = &pendingInputMidiEvents[j];
							pendingInputMidiEvents[j] = pendingInputMidiEvents[j - 1];
							--j;
						} while (j > 0 && pendingInputEvents[j - 1].offset > currentEvent.offset);
						pendingInputEvents[j] = currentEvent;
						pendingInputEvents[j].data = &pendingInputMidiEvents[j];
						pendingInputMidiEvents[j] = currentData;
					}
				}
				inArgs.structVersion = 1;
				inArgs.bufferSize = inNumberFrames;
				inArgs.inputBusConnected = inputBusConnectedFlags.data();
				inArgs.inputBusSilent = inputBusSilentFlags.data();
				inArgs.inputChannels = inputChannelPointers.data();
				inArgs.inputEventCount = currentInputEventCount;
				inArgs.inputEvents = (currentInputEventCount == 0 ? 0 : pendingInputEvents.data());
				inArgs.outputBusConnected = outputBusConnectedFlags.data();
				inArgs.isBypassed = toBool8(isBypassed);
				populateRenderTimeInfo(inTimeStamp, &inArgs);

				SymbiosisRenderOutputArgs outArgs;
				memset(&outArgs, 0, sizeof (outArgs));
				outArgs.structVersion = 1;
				outArgs.outputBusSilent = outputBusSilentFlags.data();
				outArgs.outputChannels = outputChannelPointers.data();
				outArgs.outputEventCount = 0;
				outArgs.outputEvents = 0;
				plugIn->renderAudio(&inArgs, &outArgs);

				if (createsMidiOutput && midiOutputCallbackStruct.midiOutputCallback != 0 && outArgs.outputEventCount > 0) {
					deliverMidiOutputEvents(outArgs, inTimeStamp);
				}

				SYMBIOSIS_ASSERT(currentInputEventCount <= pendingInputEventCount);
				const UInt32 remainingEventCount = pendingInputEventCount - currentInputEventCount;
				for (UInt32 i = 0; i < remainingEventCount; ++i) {
					pendingInputEvents[i] = pendingInputEvents[currentInputEventCount + i];
					pendingInputEvents[i].offset -= inNumberFrames;
					pendingInputEvents[i].data = &pendingInputMidiEvents[i];
					pendingInputMidiEvents[i] = pendingInputMidiEvents[currentInputEventCount + i];
				}
				pendingInputEventCount = remainingEventCount;
			}

			if (outputBusSilentFlags[inOutputBusNumber] != 0) {
				localActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
			} else {
				localActionFlags &= ~kAudioUnitRenderAction_OutputIsSilence;
			}
			for (UInt32 channel = 0; channel < outputChannelCountForBus; ++channel) {
				AudioBuffer& outputBuffer = ioData->mBuffers[channel];
				Float32* output = scratchOutputChannels[outputStart + channel];
				if (outputBuffer.mData == 0) {
					outputBuffer.mData = output;
					outputBuffer.mDataByteSize = inNumberFrames * static_cast<UInt32>(sizeof (Float32));
				} else {
					memcpy(outputBuffer.mData, output, inNumberFrames * sizeof (Float32));
				}
			}
			for (SInt32 listenerIndex = static_cast<SInt32>(renderNotifyListeners.size()) - 1; listenerIndex >= 0; --listenerIndex) {
				localActionFlags |= kAudioUnitRenderAction_PostRender;
				const RenderNotifyListener& listener = renderNotifyListeners[static_cast<UInt32>(listenerIndex)];
				const OSStatus status = listener.proc(listener.refCon, &localActionFlags, inTimeStamp
						, inOutputBusNumber, inNumberFrames, ioData);
				if (status != noErr) {
					return status;
				}
			}
			localActionFlags &= ~kAudioUnitRenderAction_PostRender;
			if (ioActionFlags != 0) {
				*ioActionFlags = localActionFlags;
			}
			return noErr;
		}

		OSStatus reset(AudioUnitScope inScope, AudioUnitElement inElement) {
			if (!opened || !initialized) {
				return kAudioUnitErr_Uninitialized;
			}
			if (inScope != kAudioUnitScope_Global) {
				return kAudioUnitErr_InvalidScope;
			}
			if (inElement != 0) {
				return kAudioUnitErr_InvalidElement;
			}
			SYMBIOSIS_ASSERT(audioLifecycleState == AUDIO_LIFECYCLE_PROCESSING || audioLifecycleState == AUDIO_LIFECYCLE_ALLOCATED);
			if (audioLifecycleState == AUDIO_LIFECYCLE_PROCESSING) {
				plugIn->disableAudio();
				audioLifecycleState = AUDIO_LIFECYCLE_ALLOCATED;
			}
				plugIn->enableAudio();
				audioLifecycleState = AUDIO_LIFECYCLE_PROCESSING;
				pendingInputEventCount = 0;
				lastRenderSampleTime = INVALID_RENDER_SAMPLE_TIME;
				lastRenderFrameCount = 0;
				return noErr;
			}

		OSStatus addPropertyListener(AudioUnitPropertyID inID, AudioUnitPropertyListenerProc inProc, void* inProcRefCon) {
			if (!opened) {
				return kAudioUnitErr_Uninitialized;
			}
			if (inProc == 0) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			PropertyListener listener;
			memset(&listener, 0, sizeof (listener));
			listener.propertyId = inID;
			listener.proc = inProc;
			listener.refCon = inProcRefCon;
			propertyListeners.push_back(listener);
			return noErr;
		}

		OSStatus removePropertyListener(AudioUnitPropertyID inID, AudioUnitPropertyListenerProc inProc) {
			if (!opened) {
				return kAudioUnitErr_Uninitialized;
			}
			std::vector<PropertyListener> retainedListeners;
			retainedListeners.reserve(propertyListeners.size());
			for (UInt32 i = 0; i < propertyListeners.size(); ++i) {
				const PropertyListener& listener = propertyListeners[i];
				if (listener.propertyId != inID || listener.proc != inProc) {
					retainedListeners.push_back(listener);
				}
			}
			propertyListeners.swap(retainedListeners);
			return noErr;
		}

		OSStatus removePropertyListenerWithUserData(AudioUnitPropertyID inID, AudioUnitPropertyListenerProc inProc,
				void* inProcRefCon) {
			if (!opened) {
				return kAudioUnitErr_Uninitialized;
			}
			std::vector<PropertyListener> retainedListeners;
			retainedListeners.reserve(propertyListeners.size());
			for (UInt32 i = 0; i < propertyListeners.size(); ++i) {
				const PropertyListener& listener = propertyListeners[i];
				if (listener.propertyId != inID || listener.proc != inProc || listener.refCon != inProcRefCon) {
					retainedListeners.push_back(listener);
				}
			}
			propertyListeners.swap(retainedListeners);
			return noErr;
		}

		OSStatus addRenderNotify(AURenderCallback inProc, void* inProcRefCon) {
			if (!opened) {
				return kAudioUnitErr_Uninitialized;
			}
			if (inProc == 0) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			RenderNotifyListener listener;
			memset(&listener, 0, sizeof (listener));
			listener.proc = inProc;
			listener.refCon = inProcRefCon;
			renderNotifyListeners.push_back(listener);
			return noErr;
		}

		OSStatus removeRenderNotify(AURenderCallback inProc, void* inProcRefCon) {
			if (!opened) {
				return kAudioUnitErr_Uninitialized;
			}
			std::vector<RenderNotifyListener> retainedListeners;
			retainedListeners.reserve(renderNotifyListeners.size());
			for (UInt32 i = 0; i < renderNotifyListeners.size(); ++i) {
				const RenderNotifyListener& listener = renderNotifyListeners[i];
				if (listener.proc != inProc || listener.refCon != inProcRefCon) {
					retainedListeners.push_back(listener);
				}
			}
			renderNotifyListeners.swap(retainedListeners);
			return noErr;
		}

		OSStatus scheduleParameters(AudioUnitParameterEvent* inParameterEvent, UInt32 inNumParamEvents) {
			if (!opened || !initialized) {
				traceMessage("AU scheduleParameters", "Uninitialized");
				return kAudioUnitErr_Uninitialized;
			}
			if (inNumParamEvents > 0 && inParameterEvent == 0) {
				traceMessage("AU scheduleParameters", "Invalid parameter event pointer");
				return kAudioUnitErr_InvalidPropertyValue;
			}
			for (UInt32 i = 0; i < inNumParamEvents; ++i) {
				AudioUnitParameterEvent& event = inParameterEvent[i];
				if (event.scope != kAudioUnitScope_Global) {
					traceMessage("AU scheduleParameters", "Invalid scope");
					return kAudioUnitErr_InvalidScope;
				}
				if (event.parameter >= plugInInfo->parameterCount) {
					traceMessage("AU scheduleParameters", "Invalid parameter id");
					return kAudioUnitErr_InvalidParameter;
				}
				Float32 normalizedValue = 0.0f;
				const SymbiosisParameterInfo& parameterInfo = plugInInfo->parameters[event.parameter];
				switch (event.eventType) {
					case kParameterEvent_Immediate:
						normalizedValue = audioUnitToNormalizedParameterValue(parameterInfo, event.eventValues.immediate.value);
						break;
					case kParameterEvent_Ramped:
						normalizedValue = audioUnitToNormalizedParameterValue(parameterInfo, event.eventValues.ramp.endValue);
						break;
					default:
						traceMessage("AU scheduleParameters", "Invalid event type");
						return kAudioUnitErr_InvalidParameterValue;
				}
				plugIn->updateParameter(event.parameter, normalizedValue);
			}
			return noErr;
		}

		OSStatus midiEvent(UInt32 inStatus, UInt32 inData1, UInt32 inData2, UInt32 inOffsetSampleFrame) {
			if (!opened || !initialized) {
				return kAudioUnitErr_Uninitialized;
			}
			if (!wantsMidiInput) {
				return badComponentSelector;
			}
			if (pendingInputEventCount >= plugInInfo->maxInputEventCountPerBlock) {
				return noErr;
			}
			SymbiosisEvent& event = pendingInputEvents[pendingInputEventCount];
			event.offset = inOffsetSampleFrame;
			event.type = SYMBIOSIS_EVENT_TYPE_MIDI;
			SymbiosisMidiEventData* eventData = &pendingInputMidiEvents[pendingInputEventCount];
			event.data = eventData;
			eventData->status = static_cast<UByte8>(inStatus);
			eventData->data1 = static_cast<UByte8>(inData1);
			eventData->data2 = static_cast<UByte8>(inData2);
			++pendingInputEventCount;
			return noErr;
		}

	protected:
		struct PropertyDescriptor {
			UInt32 dataSize;
			bool readable;
			bool writable;
		};

		OSStatus getPropertyDescriptor(AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement,
				PropertyDescriptor* outDescriptor) {
			SYMBIOSIS_ASSERT(outDescriptor != 0);
			PropertyDescriptor descriptor;
			descriptor.dataSize = 0;
			descriptor.readable = false;
			descriptor.writable = false;
			switch (inID) {
				case kAudioUnitProperty_StreamFormat:
					if (inScope != kAudioUnitScope_Input
							&& inScope != kAudioUnitScope_Output
							&& inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					if ((inScope == kAudioUnitScope_Input && inElement >= plugInInfo->audioInputBusCount)
							|| (inScope != kAudioUnitScope_Input && inElement >= plugInInfo->audioOutputBusCount)) {
						return kAudioUnitErr_InvalidElement;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (AudioStreamBasicDescription));
					descriptor.readable = true;
					descriptor.writable = (inScope != kAudioUnitScope_Input || !inputBusRouting[inElement].hasConnection);
					break;
				case kAudioUnitProperty_SampleRate:
					if (inScope != kAudioUnitScope_Input
							&& inScope != kAudioUnitScope_Output
							&& inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					if ((inScope == kAudioUnitScope_Input && inElement >= plugInInfo->audioInputBusCount)
							|| (inScope != kAudioUnitScope_Input && inElement >= plugInInfo->audioOutputBusCount)) {
						return kAudioUnitErr_InvalidElement;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (Float64));
					descriptor.readable = true;
					descriptor.writable = true;
					break;
				case kAudioUnitProperty_MaximumFramesPerSlice:
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (UInt32));
					descriptor.readable = true;
					descriptor.writable = true;
					break;
				case kAudioUnitProperty_SetRenderCallback:
					if (inScope != kAudioUnitScope_Input) {
						return kAudioUnitErr_InvalidScope;
					}
					if (inElement >= plugInInfo->audioInputBusCount) {
						return kAudioUnitErr_InvalidElement;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (AURenderCallbackStruct));
					descriptor.readable = false;
					descriptor.writable = true;
					break;
				case kAudioUnitProperty_MakeConnection:
					if (inScope != kAudioUnitScope_Input) {
						return kAudioUnitErr_InvalidScope;
					}
					if (inElement >= plugInInfo->audioInputBusCount) {
						return kAudioUnitErr_InvalidElement;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (AudioUnitConnection));
					descriptor.readable = false;
					descriptor.writable = true;
					break;
				case kAudioUnitProperty_Latency:
				case kAudioUnitProperty_TailTime:
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (Float64));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_BypassEffect:
					if (plugInInfo->handlesBypass == 0) {
						return kAudioUnitErr_InvalidProperty;	// no bypass surface; host performs its own hard bypass
					}
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (UInt32));
					descriptor.readable = true;
					descriptor.writable = true;
					break;
				case kAudioUnitProperty_ElementName: {
					// Parity with the old adapter (Symbiosis.mm ~3656): bus display names for host routing UIs.
					if (inScope != kAudioUnitScope_Input && inScope != kAudioUnitScope_Output) {
						return kAudioUnitErr_InvalidScope;
					}
					const UInt32 elementBusCount = (inScope == kAudioUnitScope_Input
							? plugInInfo->audioInputBusCount : plugInInfo->audioOutputBusCount);
					if (inElement >= elementBusCount) {
						return kAudioUnitErr_InvalidElement;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (CFStringRef));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				}
				case kAudioUnitProperty_LastRenderError:
					// Parity with the old adapter (Symbiosis.mm ~3593): always advertised, reads noErr.
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (OSStatus));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_CurrentPreset:
				case kAudioUnitProperty_PresentPreset:
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (AUPreset));
					descriptor.readable = true;
					descriptor.writable = true;
					break;
				case kAudioUnitProperty_FactoryPresets:		// EXPERIMENT: expose the program list as AU factory presets
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					if (plugInInfo->programCount <= 1u) {
						return kAudioUnitErr_InvalidProperty;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (CFArrayRef));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_ClassInfo:
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (CFPropertyListRef));
					descriptor.readable = true;
					descriptor.writable = true;
					break;
				case kAudioUnitProperty_HostCallbacks:
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (HostCallbackInfo));
					descriptor.readable = true;
					descriptor.writable = true;
					break;
				case kAudioUnitProperty_SupportedNumChannels:
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					if (!buildSupportedNumChannels()) {
						return kAudioUnitErr_InvalidProperty;
					}
					descriptor.dataSize = static_cast<UInt32>(supportedNumChannels.size() * sizeof (AUChannelInfo));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_MIDIOutputCallbackInfo:
					if (!createsMidiOutput) {
						return kAudioUnitErr_InvalidProperty;
					}
					if (inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (CFArrayRef));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_MIDIOutputCallback:
					if (!createsMidiOutput) {
						return kAudioUnitErr_InvalidProperty;
					}
					if (inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (AUMIDIOutputCallbackStruct));
					descriptor.readable = false;
					descriptor.writable = true;
					break;
				case kAudioUnitProperty_ParameterList:
					if (inScope != kAudioUnitScope_Global) {
						descriptor.dataSize = 0;
					} else {
						descriptor.dataSize = plugInInfo->parameterCount * static_cast<UInt32>(sizeof (AudioUnitParameterID));
					}
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_ElementCount:
					descriptor.dataSize = static_cast<UInt32>(sizeof (UInt32));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_ParameterInfo:
					if (inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					if (inElement >= plugInInfo->parameterCount) {
						return kAudioUnitErr_InvalidElement;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (AudioUnitParameterInfo));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_ParameterValueStrings:
					if (inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					if (inElement >= plugInInfo->parameterCount) {
						return kAudioUnitErr_InvalidElement;
					}
					if (parameterValueStrings[inElement] == 0) {
						return kAudioUnitErr_InvalidParameter;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (CFArrayRef));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_ParameterStringFromValue:
					if (inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					if (inElement >= plugInInfo->parameterCount) {
						return kAudioUnitErr_InvalidElement;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (AudioUnitParameterStringFromValue));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_ParameterValueFromString:
					if (inScope != kAudioUnitScope_Global) {
						return kAudioUnitErr_InvalidScope;
					}
					if (inElement >= plugInInfo->parameterCount) {
						return kAudioUnitErr_InvalidElement;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (AudioUnitParameterValueFromString));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case kAudioUnitProperty_CocoaUI:
					if (inScope != kAudioUnitScope_Global || inElement != 0) {
						return (inScope != kAudioUnitScope_Global ? kAudioUnitErr_InvalidScope : kAudioUnitErr_InvalidElement);
					}
					if (plugInInfo->hasCustomUIView == 0) {
						return kAudioUnitErr_InvalidProperty;
					}
					descriptor.dataSize = static_cast<UInt32>(sizeof (AudioUnitCocoaViewInfo));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				case SYMBIOSIS_COMPONENT_PROPERTY_ID:
					descriptor.dataSize = static_cast<UInt32>(sizeof (AUAdapterInstance*));
					descriptor.readable = true;
					descriptor.writable = false;
					break;
				default:
					return kAudioUnitErr_InvalidProperty;
			}
			*outDescriptor = descriptor;
			return noErr;
		}

		void refreshPresetNameForProgram(UInt32 programNumber) {
			SYMBIOSIS_ASSERT(programNumber < plugInInfo->programCount);
			UTF8Z programName[256];
			programName[0] = 0;
			plugIn->getProgramName(programNumber, static_cast<UInt32>(sizeof (programName) - 1u), programName);
			presentPresetNumber = static_cast<SInt32>(programNumber);
			presentPresetName.assign(programName);
		}

		bool buildRawSymbiosisState(std::vector<UByte8>* outState) {
			SYMBIOSIS_ASSERT(outState != 0);
			UInt32 payloadSize = 0;
			UByte8* payload = 0;
			if (!plugIn->createSaveState(&payloadSize, &payload)) {
				tracePlugInLastError(plugIn.get(), "AU createSaveState");
				return false;
			}
			outState->resize(AU_STATE_HEADER_SIZE + payloadSize);
			UByte8* stateBytes = outState->data();
			encodeLE32(&stateBytes[0], STATE_MAGIC_SYMB);
			encodeLE32(&stateBytes[4], STATE_ADAPTER_AU);
			encodeLE32(&stateBytes[8], 1u);
			if (payloadSize > 0u) {
				SYMBIOSIS_ASSERT(payload != 0);
				memcpy(&stateBytes[AU_STATE_HEADER_SIZE], payload, payloadSize);
			}
			plugIn->destroySaveState(payload);
			return true;
		}

		OSStatus loadRawSymbiosisState(UInt32 dataSize, const UByte8* data) {
			if (dataSize > 0u && data == 0) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			if (dataSize < AU_STATE_HEADER_SIZE) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			if (decodeLE32(&data[0]) != STATE_MAGIC_SYMB) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			if (decodeLE32(&data[4]) != STATE_ADAPTER_AU) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			if (decodeLE32(&data[8]) != 1u) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			const UInt32 payloadSize = dataSize - AU_STATE_HEADER_SIZE;
			const UByte8* payload = (payloadSize > 0u ? data + AU_STATE_HEADER_SIZE : 0);
			if (!plugIn->loadState(payloadSize, payload)) {
				tracePlugInLastError(plugIn.get(), "AU loadState");
				return kAudioUnitErr_InvalidPropertyValue;
			}
			presentPresetNumber = -1;
			return noErr;
		}

		// EXPERIMENT: capture the program list as AUPreset names ONCE, lazily, the first time the host
		// queries kAudioUnitProperty_FactoryPresets (see buildFactoryPresetsArray). This must NOT run
		// during open(): calling getProgramName for every program before the plug-in is initialized can
		// fail instantiation on real plug-ins whose program data isn't valid that early. Buffer is
		// maxLength+1 per the ABI (pass sizeof-1). A non-UTF-8 name makes CFStringCreateWithCString
		// return null, so fall back to a synthesized name (a null presetName in an AUPreset crashes the
		// host).
		void prepareFactoryPresetStorage() {
			if (plugInInfo == 0 || plugIn.get() == 0 || plugInInfo->programCount <= 1u) {
				return;
			}
			factoryPresetStorage.resize(plugInInfo->programCount);
			for (UInt32 i = 0; i < plugInInfo->programCount; ++i) {
				UTF8Z programName[256];
				programName[0] = 0;
				plugIn->getProgramName(i, static_cast<UInt32>(sizeof (programName) - 1u), programName);
				CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault,
						programName, kCFStringEncodingUTF8);
				if (name == 0) {
					name = CFStringCreateWithFormat(kCFAllocatorDefault, 0,
							CFSTR("Program %u"), static_cast<unsigned int>(i + 1u));
				}
				factoryPresetStorage[i].presetNumber = static_cast<SInt32>(i);
				factoryPresetStorage[i].presetName = name;
			}
		}

		// EXPERIMENT: assemble a fresh AU factory-preset array. The name cache is built LAZILY here,
		// on the first host query of kAudioUnitProperty_FactoryPresets -- never during open(). Calling
		// getProgramName for every program during open() can fail instantiation on real plug-ins whose
		// program data is not valid that early; by the time the host asks for factory presets the plug-in
		// is initialized. This runs inside the getProperty boundary try/catch, so a failure here degrades
		// gracefully (no factory presets) and never breaks open(). Returns a retained array (+1).
		CFArrayRef buildFactoryPresetsArray() {
			prepareFactoryPresetStorage();
			if (factoryPresetStorage.empty()) {
				return 0;
			}
			CFMutableArrayRef array = CFArrayCreateMutable(kCFAllocatorDefault,
					static_cast<CFIndex>(factoryPresetStorage.size()), 0);
			if (array == 0) {
				return 0;
			}
			for (UInt32 i = 0; i < factoryPresetStorage.size(); ++i) {
				CFArrayAppendValue(array, &factoryPresetStorage[i]);
			}
			return array;
		}

		OSStatus getPresentPreset(AudioUnitPropertyID inID, void* outData, UInt32* ioDataSize, UInt32 requestedSize) {
			SYMBIOSIS_ASSERT(outData != 0);
			SYMBIOSIS_ASSERT(ioDataSize != 0);
			if (requestedSize >= static_cast<UInt32>(sizeof (AUPreset))) {
				AUPreset preset;
				memset(&preset, 0, sizeof (preset));
				preset.presetNumber = presentPresetNumber;
				preset.presetName = CFStringCreateWithCString(kCFAllocatorDefault,
						presentPresetName.c_str(), kCFStringEncodingUTF8);
				*reinterpret_cast<AUPreset*>(outData) = preset;
				*ioDataSize = static_cast<UInt32>(sizeof (AUPreset));
				(void)inID;
				return noErr;
			}
			if (requestedSize == static_cast<UInt32>(sizeof (SInt32))) {
				*reinterpret_cast<SInt32*>(outData) = presentPresetNumber;
				*ioDataSize = static_cast<UInt32>(sizeof (SInt32));
				return noErr;
			}
			return kAudioUnitErr_InvalidPropertyValue;
		}

		OSStatus setPresentPreset(const void* inData, UInt32 inDataSize) {
			SInt32 requestedPresetNumber = -1;
			CFStringRef requestedPresetName = 0;
			AUPreset preset;
			memset(&preset, 0, sizeof (preset));
			if (inDataSize >= static_cast<UInt32>(sizeof (AUPreset))) {
				const AUPreset* inPreset = reinterpret_cast<const AUPreset*>(inData);
				requestedPresetNumber = inPreset->presetNumber;
				requestedPresetName = inPreset->presetName;
			} else if (inDataSize == static_cast<UInt32>(sizeof (SInt32))) {
				requestedPresetNumber = *reinterpret_cast<const SInt32*>(inData);
			} else {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			if (requestedPresetNumber >= 0) {
				const UInt32 programNumber = static_cast<UInt32>(requestedPresetNumber);
				if (programNumber >= plugInInfo->programCount) {
					return kAudioUnitErr_InvalidPropertyValue;
				}
				plugIn->changeProgram(programNumber);
				refreshPresetNameForProgram(programNumber);
			} else {
				if (requestedPresetName == 0) {
					presentPresetNumber = requestedPresetNumber;
					return noErr;
				}
				UTF8Z presetName[256];
				if (!copyCFStringToUTF8(requestedPresetName, presetName, static_cast<UInt32>(sizeof (presetName)))) {
					return kAudioUnitErr_InvalidPropertyValue;
				}
				presentPresetNumber = requestedPresetNumber;
				presentPresetName.assign(presetName);
			}
			notifyPropertyChanged(kAudioUnitProperty_CurrentPreset, kAudioUnitScope_Global, 0);
			notifyPropertyChanged(kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
			return noErr;
		}

		OSStatus getClassInfo(void* outData, UInt32* ioDataSize) {
			SYMBIOSIS_ASSERT(outData != 0);
			SYMBIOSIS_ASSERT(ioDataSize != 0);
			std::vector<UByte8> stateBytes;
			if (!buildRawSymbiosisState(&stateBytes)) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			CFDataRef data = CFDataCreate(kCFAllocatorDefault, stateBytes.data(), static_cast<CFIndex>(stateBytes.size()));
			if (data == 0) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			CFMutableDictionaryRef classInfo = CFDictionaryCreateMutable(kCFAllocatorDefault, 0
					, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
			if (classInfo == 0) {
				CFRelease(data);
				return kAudioUnitErr_InvalidPropertyValue;
			}
			CFDictionarySetValue(classInfo, CFSTR(kAUPresetDataKey), data);
			CFRelease(data);

			CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, presentPresetName.c_str(), kCFStringEncodingUTF8);
			if (name != 0) {
				CFDictionarySetValue(classInfo, CFSTR(kAUPresetNameKey), name);
				CFRelease(name);
			}
			const SInt32 presetNumber = presentPresetNumber;
			CFNumberRef presetNumberValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &presetNumber);
			if (presetNumberValue != 0) {
				CFDictionarySetValue(classInfo, CFSTR(kAUPresetNumberKey), presetNumberValue);
				CFRelease(presetNumberValue);
			}
			const SInt32 componentType = static_cast<SInt32>(componentDescription.componentType);
			CFNumberRef typeValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &componentType);
			if (typeValue != 0) {
				CFDictionarySetValue(classInfo, CFSTR(kAUPresetTypeKey), typeValue);
				CFRelease(typeValue);
			}
			const SInt32 componentSubtype = static_cast<SInt32>(componentDescription.componentSubType);
			CFNumberRef subtypeValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &componentSubtype);
			if (subtypeValue != 0) {
				CFDictionarySetValue(classInfo, CFSTR(kAUPresetSubtypeKey), subtypeValue);
				CFRelease(subtypeValue);
			}
			const SInt32 componentManufacturer = static_cast<SInt32>(componentDescription.componentManufacturer);
			CFNumberRef manufacturerValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type
					, &componentManufacturer);
			if (manufacturerValue != 0) {
				CFDictionarySetValue(classInfo, CFSTR(kAUPresetManufacturerKey), manufacturerValue);
				CFRelease(manufacturerValue);
			}
			const SInt32 presetVersion = 0;
			CFNumberRef presetVersionValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &presetVersion);
			if (presetVersionValue != 0) {
				CFDictionarySetValue(classInfo, CFSTR(kAUPresetVersionKey), presetVersionValue);
				CFRelease(presetVersionValue);
			}

			*reinterpret_cast<CFPropertyListRef*>(outData) = classInfo;
			*ioDataSize = static_cast<UInt32>(sizeof (CFPropertyListRef));
			return noErr;
		}

		OSStatus setClassInfo(const void* inData, UInt32 inDataSize) {
			if (inDataSize != static_cast<UInt32>(sizeof (CFPropertyListRef))) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			CFPropertyListRef propertyList = *reinterpret_cast<const CFPropertyListRef*>(inData);
			if (propertyList == 0 || CFGetTypeID(propertyList) != CFDictionaryGetTypeID()) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			CFDictionaryRef classInfo = reinterpret_cast<CFDictionaryRef>(propertyList);
			CFTypeRef dataValue = CFDictionaryGetValue(classInfo, CFSTR(kAUPresetDataKey));
			if (dataValue == 0 || CFGetTypeID(dataValue) != CFDataGetTypeID()) {
				return kAudioUnitErr_InvalidPropertyValue;
			}
			CFDataRef data = reinterpret_cast<CFDataRef>(dataValue);
			const UInt32 stateSize = static_cast<UInt32>(CFDataGetLength(data));
			const UByte8* stateBytes = CFDataGetBytePtr(data);
			const OSStatus loadStatus = loadRawSymbiosisState(stateSize, stateBytes);
			if (loadStatus != noErr) {
				return loadStatus;
			}
			AudioUnitParameter changedParameter;
			memset(&changedParameter, 0, sizeof (changedParameter));
			changedParameter.mAudioUnit = reinterpret_cast<AudioUnit>(componentInstance);
			changedParameter.mParameterID = kAUParameterListener_AnyParameter;
			changedParameter.mScope = kAudioUnitScope_Global;
			changedParameter.mElement = 0;
			AUParameterListenerNotify(0, 0, &changedParameter);
			notifyPropertyChanged(kAudioUnitProperty_CurrentPreset, kAudioUnitScope_Global, 0);
			notifyPropertyChanged(kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
			return noErr;
		}

		bool buildSupportedNumChannels() {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			supportedNumChannels.clear();
			const UInt32 inputBusCount = plugInInfo->audioInputBusCount;
			const UInt32 outputBusCount = plugInInfo->audioOutputBusCount;
			if (!((inputBusCount == 1u && outputBusCount == 1u)
					|| (inputBusCount == 0u && outputBusCount == 1u)
					|| (inputBusCount == 1u && outputBusCount == 0u))) {
				return false;
			}

			if (inputBusCount == 1u && outputBusCount == 1u) {
				const UInt32 inputMask = plugInInfo->audioInputBuses[0].supportedBusFormats;
				const UInt32 outputMask = plugInInfo->audioOutputBuses[0].supportedBusFormats;
				const bool inputAnyCount = ((inputMask & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u);
				const bool outputAnyCount = ((outputMask & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u);
				const bool ioCoupled = ((inputMask & SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK) != 0u)
						|| ((outputMask & SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK) != 0u);
				if (inputAnyCount && outputAnyCount) {
					if (!ioCoupled) {
						return false;
					}
					AUChannelInfo info;
					memset(&info, 0, sizeof (info));
					info.inChannels = -1;
					info.outChannels = -1;
					supportedNumChannels.push_back(info);
					return true;
				}
				std::vector<SInt16> inputCounts;
				std::vector<SInt16> outputCounts;
				if (!inputAnyCount) {
					collectFiniteSupportedChannelCounts(inputMask, &inputCounts);
				}
				if (!outputAnyCount) {
					collectFiniteSupportedChannelCounts(outputMask, &outputCounts);
				}
				if (inputAnyCount) {
					for (UInt32 i = 0; i < outputCounts.size(); ++i) {
						AUChannelInfo info;
						memset(&info, 0, sizeof (info));
						info.inChannels = -1;
						info.outChannels = outputCounts[i];
						supportedNumChannels.push_back(info);
					}
				} else if (outputAnyCount) {
					for (UInt32 i = 0; i < inputCounts.size(); ++i) {
						AUChannelInfo info;
						memset(&info, 0, sizeof (info));
						info.inChannels = inputCounts[i];
						info.outChannels = -1;
						supportedNumChannels.push_back(info);
					}
				} else {
					for (UInt32 inIndex = 0; inIndex < inputCounts.size(); ++inIndex) {
						for (UInt32 outIndex = 0; outIndex < outputCounts.size(); ++outIndex) {
							AUChannelInfo info;
							memset(&info, 0, sizeof (info));
							info.inChannels = inputCounts[inIndex];
							info.outChannels = outputCounts[outIndex];
							supportedNumChannels.push_back(info);
						}
					}
				}
				return supportedNumChannels.size() > 0;
			}

			if (inputBusCount == 1u && outputBusCount == 0u) {
				const UInt32 inputMask = plugInInfo->audioInputBuses[0].supportedBusFormats;
				if ((inputMask & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u) {
					AUChannelInfo info;
					memset(&info, 0, sizeof (info));
					info.inChannels = -1;
					info.outChannels = 0;
					supportedNumChannels.push_back(info);
				} else {
					std::vector<SInt16> inputCounts;
					collectFiniteSupportedChannelCounts(inputMask, &inputCounts);
					for (UInt32 i = 0; i < inputCounts.size(); ++i) {
						AUChannelInfo info;
						memset(&info, 0, sizeof (info));
						info.inChannels = inputCounts[i];
						info.outChannels = 0;
						supportedNumChannels.push_back(info);
					}
				}
				return supportedNumChannels.size() > 0;
			}

			if (inputBusCount == 0u && outputBusCount == 1u) {
				const UInt32 outputMask = plugInInfo->audioOutputBuses[0].supportedBusFormats;
				if ((outputMask & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u) {
					AUChannelInfo info;
					memset(&info, 0, sizeof (info));
					info.inChannels = 0;
					info.outChannels = -1;
					supportedNumChannels.push_back(info);
				} else {
					std::vector<SInt16> outputCounts;
					collectFiniteSupportedChannelCounts(outputMask, &outputCounts);
					for (UInt32 i = 0; i < outputCounts.size(); ++i) {
						AUChannelInfo info;
						memset(&info, 0, sizeof (info));
						info.inChannels = 0;
						info.outChannels = outputCounts[i];
						supportedNumChannels.push_back(info);
					}
				}
				return supportedNumChannels.size() > 0;
			}
			return false;
		}

		void initializeBusFormats() {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			inputBusFormats.resize(plugInInfo->audioInputBusCount);
			outputBusFormats.resize(plugInInfo->audioOutputBusCount);
			inputBusRouting.resize(plugInInfo->audioInputBusCount);
			for (UInt32 i = 0; i < inputBusRouting.size(); ++i) {
				inputBusRouting[i].hasRenderCallback = false;
				memset(&inputBusRouting[i].renderCallback, 0, sizeof (inputBusRouting[i].renderCallback));
				inputBusRouting[i].hasConnection = false;
				memset(&inputBusRouting[i].connection, 0, sizeof (inputBusRouting[i].connection));
			}
			for (UInt32 i = 0; i < plugInInfo->audioInputBusCount; ++i) {
				const UInt32 supportedBusFormats = plugInInfo->audioInputBuses[i].supportedBusFormats;
				const UInt32 channelCount = ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u
						? 2u : busFormatMaskToChannelCount(chooseDefaultBusFormatMask(supportedBusFormats)));
				inputBusFormats[i] = makeCanonicalStreamFormat(sampleRate, channelCount);
			}
			for (UInt32 i = 0; i < plugInInfo->audioOutputBusCount; ++i) {
				const UInt32 supportedBusFormats = plugInInfo->audioOutputBuses[i].supportedBusFormats;
				const UInt32 channelCount = ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u
						? 2u : busFormatMaskToChannelCount(chooseDefaultBusFormatMask(supportedBusFormats)));
				outputBusFormats[i] = makeCanonicalStreamFormat(sampleRate, channelCount);
			}
		}

		OSStatus getBusFormat(AudioUnitScope scope, AudioUnitElement element, AudioStreamBasicDescription* outFormat) {
			SYMBIOSIS_ASSERT(outFormat != 0);
			if (scope == kAudioUnitScope_Input) {
				if (element >= plugInInfo->audioInputBusCount) {
					return kAudioUnitErr_InvalidElement;
				}
				*outFormat = inputBusFormats[element];
				return noErr;
			}
			if (scope == kAudioUnitScope_Output || scope == kAudioUnitScope_Global) {
				if (element >= plugInInfo->audioOutputBusCount) {
					return kAudioUnitErr_InvalidElement;
				}
				*outFormat = outputBusFormats[element];
				return noErr;
			}
			return kAudioUnitErr_InvalidScope;
		}

		OSStatus setBusFormat(AudioUnitScope scope, AudioUnitElement element, const AudioStreamBasicDescription& format) {
			if (scope != kAudioUnitScope_Input && scope != kAudioUnitScope_Output && scope != kAudioUnitScope_Global) {
				return kAudioUnitErr_InvalidScope;
			}
			if (!(format.mSampleRate > 0.0)
					|| format.mFormatID != kAudioFormatLinearPCM
					|| (format.mFormatFlags & kAudioFormatFlagIsFloat) == 0
					|| (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0
					|| (format.mFormatFlags & kAudioFormatFlagIsBigEndian) != 0
					|| format.mFramesPerPacket != 1
					|| format.mBytesPerPacket != sizeof (Float32)
					|| format.mBytesPerFrame != sizeof (Float32)
					|| format.mBitsPerChannel != 32
					|| format.mChannelsPerFrame == 0) {
				return kAudioUnitErr_InvalidPropertyValue;
			}

			UInt32 supportedBusFormats = 0;
				if (scope == kAudioUnitScope_Input) {
					if (element >= plugInInfo->audioInputBusCount) {
						return kAudioUnitErr_InvalidElement;
					}
					supportedBusFormats = plugInInfo->audioInputBuses[element].supportedBusFormats;
				} else {
					if (element >= plugInInfo->audioOutputBusCount) {
						return kAudioUnitErr_InvalidElement;
					}
					supportedBusFormats = plugInInfo->audioOutputBuses[element].supportedBusFormats;
				}
			const bool isAuxInputElement = (scope == kAudioUnitScope_Input && element > 0);
			if (!isSupportedBusChannelCount(supportedBusFormats, format.mChannelsPerFrame)) {
				if (!isAuxInputElement) {
					return kAudioUnitErr_InvalidPropertyValue;
				}
				// Host wiring leniency for aux/side-chain INPUT elements: Logic sets the track format
				// (for example stereo) on every input element, including a mono-only side-chain, and
				// treats a rejection as fatal for the whole plug-in (generic editor, never initialized,
				// custom view never requested). Accept the host format as WIRING only: the bus format
				// SELECTED for the plug-in is still clamped to a declared format at initialize() and
				// delivery adapts by channel aliasing (see collectInputAudioForBus). The old single-file
				// adapter accepted this the same way (wide side-chain buses plus a Logic accept-and-
				// swallow workaround), which is why it never hit this.
				UTF8Z lenientText[128];
				snprintf(lenientText, sizeof (lenientText),
						"aux input element %u: accepting undeclared host channel count %u as wiring",
						(unsigned)element, (unsigned)format.mChannelsPerFrame);
				traceMessage("AU setBusFormat", lenientText);
			}

			if (scope == kAudioUnitScope_Input) {
				inputBusFormats[element] = format;
			} else {
				outputBusFormats[element] = format;
			}
			if (element < plugInInfo->audioInputBusCount && element < plugInInfo->audioOutputBusCount) {
				const UInt32 inputMask = plugInInfo->audioInputBuses[element].supportedBusFormats;
				const UInt32 outputMask = plugInInfo->audioOutputBuses[element].supportedBusFormats;
				const bool isCoupled = ((inputMask & SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK) != 0u)
						|| ((outputMask & SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK) != 0u);
					if (isCoupled) {
						const UInt32 coupledChannelCount = format.mChannelsPerFrame;
						if (!isSupportedBusChannelCount(inputMask, coupledChannelCount)
								|| !isSupportedBusChannelCount(outputMask, coupledChannelCount)) {
							return kAudioUnitErr_InvalidPropertyValue;
						}
					// Do not coerce the paired scope to match here. Silently rewriting the other scope's
					// channel count hides a host-requested mono/stereo mismatch (auval flags it as accepting
					// an un-supported channel config). Each scope keeps exactly what the host set; the strict
					// input==output coupling is enforced in validateCoupledBusChannelCounts() at initialize(),
					// by which point an effect host has set both scopes. (Sample rate is synced below.)
					}
				}
			sampleRate = format.mSampleRate;
			for (UInt32 i = 0; i < inputBusFormats.size(); ++i) {
				inputBusFormats[i].mSampleRate = sampleRate;
			}
			for (UInt32 i = 0; i < outputBusFormats.size(); ++i) {
				outputBusFormats[i].mSampleRate = sampleRate;
			}
			return noErr;
		}

		OSStatus validateCoupledBusChannelCounts() const {
			const UInt32 pairedBusCount = (plugInInfo->audioInputBusCount < plugInInfo->audioOutputBusCount
					? plugInInfo->audioInputBusCount : plugInInfo->audioOutputBusCount);
			for (UInt32 busIndex = 0; busIndex < pairedBusCount; ++busIndex) {
				const UInt32 inputMask = plugInInfo->audioInputBuses[busIndex].supportedBusFormats;
				const UInt32 outputMask = plugInInfo->audioOutputBuses[busIndex].supportedBusFormats;
				const bool isCoupled = ((inputMask & SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK) != 0u)
						|| ((outputMask & SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK) != 0u);
				if (isCoupled && inputBusFormats[busIndex].mChannelsPerFrame != outputBusFormats[busIndex].mChannelsPerFrame) {
					return kAudioUnitErr_FormatNotSupported;
				}
			}
			return noErr;
		}

			// For an aux input bus whose host wiring count matches no declared format: select the
			// largest declared format that does not exceed the host count (host stereo on a mono-only
			// side-chain selects mono); if the host count is below every declared format, select the
			// smallest declared one (delivery aliasing covers the under-delivery).
			UInt32 selectBusFormatForHostChannelCount(UInt32 supportedBusFormats, UInt32 hostChannelCount) const {
				static const UInt32 ORDERED_MASKS[] = {
					SYMBIOSIS_BUS_FORMAT_MONO_MASK,
					SYMBIOSIS_BUS_FORMAT_STEREO_MASK,
					SYMBIOSIS_BUS_FORMAT_LCR_MASK,
					SYMBIOSIS_BUS_FORMAT_QUAD_MASK,
					SYMBIOSIS_BUS_FORMAT_5_0_MASK,
					SYMBIOSIS_BUS_FORMAT_5_1_MASK,
					SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK,
					SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK,
					SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK,
					SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK,
					SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK
				};
				UInt32 bestAtOrBelow = 0;
				UInt32 smallestDeclared = 0;
				for (UInt32 i = 0; i < sizeof(ORDERED_MASKS) / sizeof(ORDERED_MASKS[0]); ++i) {
					const UInt32 mask = ORDERED_MASKS[i];
					if ((supportedBusFormats & mask) == 0u) {
						continue;
					}
					if (smallestDeclared == 0u) {
						smallestDeclared = mask;
					}
					if (busFormatMaskToChannelCount(mask) <= hostChannelCount) {
						bestAtOrBelow = mask;
					}
				}
				return (bestAtOrBelow != 0u ? bestAtOrBelow : smallestDeclared);
			}

			UInt32 resolveConfiguredBusFormat(UInt32 supportedBusFormats, UInt32 channelCount) {
				if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u) {
					return SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK;
				}
				static const UInt32 FORMAT_MASKS[] = {
					SYMBIOSIS_BUS_FORMAT_MONO_MASK,
					SYMBIOSIS_BUS_FORMAT_STEREO_MASK,
					SYMBIOSIS_BUS_FORMAT_LCR_MASK,
					SYMBIOSIS_BUS_FORMAT_QUAD_MASK,
					SYMBIOSIS_BUS_FORMAT_5_0_MASK,
					SYMBIOSIS_BUS_FORMAT_5_1_MASK,
					SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK,
					SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK,
					SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK,
					SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK,
					SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK
				};
				for (UInt32 i = 0; i < sizeof(FORMAT_MASKS) / sizeof(FORMAT_MASKS[0]); ++i) {
					const UInt32 formatMask = FORMAT_MASKS[i];
					if ((supportedBusFormats & formatMask) != 0u && busFormatMaskToChannelCount(formatMask) == channelCount) {
						return formatMask;
					}
				}
				SYMBIOSIS_ASSERT(0);
				return SYMBIOSIS_BUS_FORMAT_STEREO_MASK;
			}

		void resolveCurrentAudioConfiguration(std::vector<UInt32>* outInputBusFormatValues
				, std::vector<UInt32>* outInputBusChannelCounts, std::vector<UInt32>* outOutputBusFormatValues
				, std::vector<UInt32>* outOutputBusChannelCounts, UInt32* outInputChannelCount, UInt32* outOutputChannelCount) {
			SYMBIOSIS_ASSERT(outInputBusFormatValues != 0);
			SYMBIOSIS_ASSERT(outInputBusChannelCounts != 0);
			SYMBIOSIS_ASSERT(outOutputBusFormatValues != 0);
			SYMBIOSIS_ASSERT(outOutputBusChannelCounts != 0);
			SYMBIOSIS_ASSERT(outInputChannelCount != 0);
			SYMBIOSIS_ASSERT(outOutputChannelCount != 0);
			outInputBusFormatValues->clear();
			outInputBusChannelCounts->clear();
			outOutputBusFormatValues->clear();
			outOutputBusChannelCounts->clear();
			*outInputChannelCount = 0;
			*outOutputChannelCount = 0;
			outInputBusFormatValues->reserve(inputBusFormats.size());
			outInputBusChannelCounts->reserve(inputBusFormats.size());
			outOutputBusFormatValues->reserve(outputBusFormats.size());
			outOutputBusChannelCounts->reserve(outputBusFormats.size());
			inputBusHostChannelCounts.resize(inputBusFormats.size());
			for (UInt32 i = 0; i < inputBusFormats.size(); ++i) {
				const UInt32 hostChannelCount = inputBusFormats[i].mChannelsPerFrame;
				const UInt32 supportedBusFormats = plugInInfo->audioInputBuses[i].supportedBusFormats;
				inputBusHostChannelCounts[i] = hostChannelCount;
				UInt32 selectedFormat;
				UInt32 selectedChannelCount;
				if (i > 0u && (supportedBusFormats & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) == 0u
						&& !isSupportedBusChannelCount(supportedBusFormats, hostChannelCount)) {
					// Aux input bus with undeclared host wiring: strict SELECTION from the declared
					// formats; delivery adapts (see collectInputAudioForBus).
					selectedFormat = selectBusFormatForHostChannelCount(supportedBusFormats, hostChannelCount);
					selectedChannelCount = busFormatMaskToChannelCount(selectedFormat);
				} else {
					selectedFormat = resolveConfiguredBusFormat(supportedBusFormats, hostChannelCount);
					selectedChannelCount = hostChannelCount;
				}
				outInputBusFormatValues->push_back(selectedFormat);
				outInputBusChannelCounts->push_back(selectedChannelCount);
				*outInputChannelCount += selectedChannelCount;
			}
			for (UInt32 i = 0; i < outputBusFormats.size(); ++i) {
				const UInt32 channelCount = outputBusFormats[i].mChannelsPerFrame;
				const UInt32 supportedBusFormats = plugInInfo->audioOutputBuses[i].supportedBusFormats;
				outOutputBusFormatValues->push_back(resolveConfiguredBusFormat(supportedBusFormats, channelCount));
				outOutputBusChannelCounts->push_back(channelCount);
				*outOutputChannelCount += channelCount;
			}
		}

		void prepareRenderResources(const std::vector<UInt32>& inputBusChannelCounts, const std::vector<UInt32>& outputBusChannelCounts
				, UInt32 inputChannelCount, UInt32 outputChannelCount) {
			SYMBIOSIS_ASSERT(maximumFramesPerSlice > 0);
			configuredInputChannelCount = inputChannelCount;
			configuredOutputChannelCount = outputChannelCount;

			inputBusChannelOffsets.resize(inputBusChannelCounts.size() + 1u);
			UInt32 inputOffset = 0;
			for (UInt32 i = 0; i < inputBusChannelCounts.size(); ++i) {
				inputBusChannelOffsets[i] = inputOffset;
				inputOffset += inputBusChannelCounts[i];
			}
			inputBusChannelOffsets[inputBusChannelCounts.size()] = inputOffset;
			SYMBIOSIS_ASSERT(inputOffset == configuredInputChannelCount);

			outputBusChannelOffsets.resize(outputBusChannelCounts.size() + 1u);
			UInt32 outputOffset = 0;
			for (UInt32 i = 0; i < outputBusChannelCounts.size(); ++i) {
				outputBusChannelOffsets[i] = outputOffset;
				outputOffset += outputBusChannelCounts[i];
			}
			outputBusChannelOffsets[outputBusChannelCounts.size()] = outputOffset;
			SYMBIOSIS_ASSERT(outputOffset == configuredOutputChannelCount);

			scratchInputStorage.resize(static_cast<size_t>(configuredInputChannelCount) * maximumFramesPerSlice);
			scratchOutputStorage.resize(static_cast<size_t>(configuredOutputChannelCount) * maximumFramesPerSlice);
			scratchInputChannels.resize(configuredInputChannelCount);
			scratchOutputChannels.resize(configuredOutputChannelCount);
			inputChannelPointers.resize(configuredInputChannelCount);
			outputChannelPointers.resize(configuredOutputChannelCount);
			for (UInt32 channel = 0; channel < configuredInputChannelCount; ++channel) {
				Float32* const channelBuffer = &scratchInputStorage[static_cast<size_t>(channel) * maximumFramesPerSlice];
				scratchInputChannels[channel] = channelBuffer;
				inputChannelPointers[channel] = channelBuffer;
			}
			for (UInt32 channel = 0; channel < configuredOutputChannelCount; ++channel) {
				Float32* const channelBuffer = &scratchOutputStorage[static_cast<size_t>(channel) * maximumFramesPerSlice];
				scratchOutputChannels[channel] = channelBuffer;
				outputChannelPointers[channel] = channelBuffer;
			}

			inputBusConnectedFlags.resize(inputBusChannelCounts.size());
			inputBusSilentFlags.resize(inputBusChannelCounts.size());
			outputBusConnectedFlags.resize(outputBusChannelCounts.size());
			outputBusSilentFlags.resize(outputBusChannelCounts.size());
			scratchInputTrashStorage.resize(maximumFramesPerSlice);
			inputBusAudioBufferListStorage.resize(inputBusChannelCounts.size());
			for (UInt32 bus = 0; bus < inputBusChannelCounts.size(); ++bus) {
				// The pull buffer list is sized to the HOST wiring channel count (what the upstream
				// callback/connection delivers per the format the host set), which can differ from the
				// plug-in's selected count on an aux input bus; see collectInputAudioForBus.
				const UInt32 hostChannelCount = (bus < inputBusHostChannelCounts.size()
						? inputBusHostChannelCounts[bus] : inputBusChannelCounts[bus]);
				const UInt32 bufferCount = (hostChannelCount > inputBusChannelCounts[bus]
						? hostChannelCount : inputBusChannelCounts[bus]);
				const size_t size = offsetof(AudioBufferList, mBuffers) + static_cast<size_t>(bufferCount) * sizeof (AudioBuffer);
				inputBusAudioBufferListStorage[bus].resize(size);
				AudioBufferList* bufferList = reinterpret_cast<AudioBufferList*>(inputBusAudioBufferListStorage[bus].data());
				bufferList->mNumberBuffers = bufferCount;
				for (UInt32 channel = 0; channel < bufferCount; ++channel) {
					bufferList->mBuffers[channel].mNumberChannels = 1;
					bufferList->mBuffers[channel].mDataByteSize = 0;
					bufferList->mBuffers[channel].mData = 0;
				}
			}
		}

		void clearRenderResources() {
			configuredInputChannelCount = 0;
			configuredOutputChannelCount = 0;
			inputBusChannelOffsets.clear();
			outputBusChannelOffsets.clear();
			inputBusAudioBufferListStorage.clear();
			inputBusHostChannelCounts.clear();
			scratchInputTrashStorage.clear();
			scratchInputStorage.clear();
			scratchOutputStorage.clear();
			scratchInputChannels.clear();
			scratchOutputChannels.clear();
			inputChannelPointers.clear();
			outputChannelPointers.clear();
			inputBusConnectedFlags.clear();
			inputBusSilentFlags.clear();
			outputBusConnectedFlags.clear();
			outputBusSilentFlags.clear();
		}

		void notifyPropertyChanged(AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement) {
			SYMBIOSIS_ASSERT(opened);
			const AudioUnit audioUnit = reinterpret_cast<AudioUnit>(componentInstance);
			for (UInt32 i = 0; i < propertyListeners.size(); ++i) {
				const PropertyListener& listener = propertyListeners[i];
				if (listener.propertyId == inID) {
					listener.proc(listener.refCon, audioUnit, inID, inScope, inElement);
				}
			}
			AudioUnitEvent event;
			memset(&event, 0, sizeof (event));
			event.mEventType = kAudioUnitEvent_PropertyChange;
			event.mArgument.mProperty.mAudioUnit = audioUnit;
			event.mArgument.mProperty.mPropertyID = inID;
			event.mArgument.mProperty.mScope = inScope;
			event.mArgument.mProperty.mElement = inElement;
			AUEventListenerNotify(0, 0, &event);
		}

		void notifyParameterEvent(AudioUnitParameterID parameterNumber, AudioUnitEventType eventType) {
			SYMBIOSIS_ASSERT(opened);
			SYMBIOSIS_ASSERT(parameterNumber < plugInInfo->parameterCount);
			AudioUnitEvent event;
			memset(&event, 0, sizeof (event));
			event.mEventType = eventType;
			event.mArgument.mParameter.mAudioUnit = reinterpret_cast<AudioUnit>(componentInstance);
			event.mArgument.mParameter.mParameterID = parameterNumber;
			event.mArgument.mParameter.mScope = kAudioUnitScope_Global;
			event.mArgument.mParameter.mElement = 0;
			if (eventType == kAudioUnitEvent_ParameterValueChange) {
				AUParameterListenerNotify(0, 0, &event.mArgument.mParameter);
			}
			AUEventListenerNotify(0, 0, &event);
		}

		OSStatus collectInputAudioForBus(UInt32 inputBusNumber, const AudioTimeStamp* inTimeStamp, UInt32 inNumberFrames
				, Bool8* outConnected, Bool8* outSilent) {
			SYMBIOSIS_ASSERT(outConnected != 0);
			SYMBIOSIS_ASSERT(outSilent != 0);
			SYMBIOSIS_ASSERT(inputBusNumber < inputBusChannelOffsets.size() - 1u);
			SYMBIOSIS_ASSERT(inputBusNumber < inputBusAudioBufferListStorage.size());
			const UInt32 channelStart = inputBusChannelOffsets[inputBusNumber];
			const UInt32 channelEnd = inputBusChannelOffsets[inputBusNumber + 1u];
			const UInt32 channelCount = channelEnd - channelStart;
			// Pull with the HOST wiring channel count (the format the host set on this element); on an
			// aux input bus this can differ from the plug-in's selected count. Extra host channels land
			// in a shared trash buffer; missing ones are aliased below (channel % hostChannelCount),
			// per the delivery-time fallback contract.
			const UInt32 hostChannelCount = (inputBusNumber < inputBusHostChannelCounts.size()
					? inputBusHostChannelCounts[inputBusNumber] : channelCount);
			AudioBufferList* bufferList = reinterpret_cast<AudioBufferList*>(inputBusAudioBufferListStorage[inputBusNumber].data());
			bufferList->mNumberBuffers = hostChannelCount;
			for (UInt32 channel = 0; channel < hostChannelCount; ++channel) {
				bufferList->mBuffers[channel].mNumberChannels = 1;
				bufferList->mBuffers[channel].mDataByteSize = inNumberFrames * static_cast<UInt32>(sizeof (Float32));
				bufferList->mBuffers[channel].mData = (channel < channelCount
						? scratchInputChannels[channelStart + channel] : scratchInputTrashStorage.data());
			}

			AudioUnitRenderActionFlags inputFlags = 0;
			const InputBusRouting& routing = inputBusRouting[inputBusNumber];
			if (routing.hasRenderCallback) {
				*outConnected = 1;
				const OSStatus status = routing.renderCallback.inputProc(routing.renderCallback.inputProcRefCon, &inputFlags
						, inTimeStamp, inputBusNumber, inNumberFrames, bufferList);
				if (status != noErr) {
					return status;
				}
			} else if (routing.hasConnection) {
				*outConnected = 1;
				const OSStatus status = AudioUnitRender(routing.connection.sourceAudioUnit, &inputFlags, inTimeStamp
						, routing.connection.sourceOutputNumber, inNumberFrames, bufferList);
				if (status != noErr) {
					return status;
				}
			} else {
				*outConnected = 0;
				for (UInt32 channel = 0; channel < channelCount; ++channel) {
					memset(scratchInputChannels[channelStart + channel], 0, inNumberFrames * sizeof (Float32));
				}
				inputFlags = kAudioUnitRenderAction_OutputIsSilence;
			}
			// Point the plug-in's input channels at whatever buffer the source actually delivered into.
			// A render callback or connection may repoint mBuffers[].mData at its own buffer (zero-copy)
			// rather than filling ours -- Logic does exactly this -- so we read it back, as the old
			// single-file adapter did. When the host under-delivers, alias up to the selected count via
			// channel % hostChannelCount. No audio copy; the else (unconnected) branch left mData at our
			// zeroed scratch, so this yields silence there.
			for (UInt32 channel = 0; channel < channelCount; ++channel) {
				const UInt32 sourceChannel = (hostChannelCount > 0u ? (channel % hostChannelCount) : channel);
				inputChannelPointers[channelStart + channel]
						= reinterpret_cast<Float32*>(bufferList->mBuffers[sourceChannel].mData);
			}
			*outSilent = ((inputFlags & kAudioUnitRenderAction_OutputIsSilence) != 0 ? 1 : 0);
			return noErr;
		}

		void onPlugInUpdateDisplay() {
			notifyPropertyChanged(kAudioUnitProperty_CurrentPreset, kAudioUnitScope_Global, 0);
			notifyPropertyChanged(kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
		}

		void onPlugInBeginEdit(UInt32 parameterNumber) {
			notifyParameterEvent(parameterNumber, kAudioUnitEvent_BeginParameterChangeGesture);
		}

		void onPlugInWriteParameter(UInt32 parameterNumber, Float32 normalizedValue) {
			SYMBIOSIS_ASSERT(parameterNumber < plugInInfo->parameterCount);
			// Round-trip contract: AU's listener notification does not carry the value into the plug-in,
			// so deliver the incoming updateParameter ourselves before notifying the host. See
			// docs/Symbiosis_Parameter_Policies.md (3.3).
			plugIn->updateParameter(parameterNumber, normalizedValue);
			notifyParameterEvent(parameterNumber, kAudioUnitEvent_ParameterValueChange);
		}

		void onPlugInEndEdit(UInt32 parameterNumber) {
			notifyParameterEvent(parameterNumber, kAudioUnitEvent_EndParameterChangeGesture);
		}

		bool onPlugInRequestResize(UInt32 width, UInt32 height) {
#ifdef __OBJC__
			SYMBIOSIS_ASSERT(width > 0u);
			SYMBIOSIS_ASSERT(height > 0u);
			SYMBIOSIS_ASSERT(customUIViewParent != 0);
			NSView* parentView = reinterpret_cast<NSView*>(customUIViewParent);
			SYMBIOSIS_ASSERT(parentView != nil);
			NSRect frame = [parentView frame];
			frame.size = NSMakeSize(static_cast<CGFloat>(width), static_cast<CGFloat>(height));
			[parentView setFrame:frame];
			[parentView setNeedsLayout:YES];
			[parentView setNeedsDisplay:YES];
			NSView* superview = [parentView superview];
			if (superview != nil) {
				[superview setNeedsLayout:YES];
				[superview setNeedsDisplay:YES];
			}
			return true;
#else
			(void)width;
			(void)height;
			return false;
#endif
		}

		void populateRenderTimeInfo(const AudioTimeStamp* inTimeStamp, SymbiosisRenderInputArgs* outInArgs) {
			SYMBIOSIS_ASSERT(outInArgs != 0);
			outInArgs->isTransportRunning = 0;
			outInArgs->tempo = 120.0;
			outInArgs->ppqPosition = 0.0;
			outInArgs->isTransportLooping = 0;
			outInArgs->loopStartPPQPosition = 0.0;
			outInArgs->loopEndPPQPosition = 0.0;
			outInArgs->samplePosition = ((inTimeStamp != 0 && (inTimeStamp->mFlags & kAudioTimeStampSampleTimeValid) != 0)
					? static_cast<Int64>(inTimeStamp->mSampleTime) : 0);

			if (hostCallbackInfo.beatAndTempoProc != 0) {
				Float64 currentBeat = 0.0;
				Float64 currentTempo = 120.0;
				if (hostCallbackInfo.beatAndTempoProc(hostCallbackInfo.hostUserData, &currentBeat, &currentTempo) == noErr) {
					outInArgs->ppqPosition = currentBeat;
					outInArgs->tempo = currentTempo;
				}
			}

			if (hostCallbackInfo.transportStateProc2 != 0) {
				Boolean isPlaying = false;
				Boolean isRecording = false;
				Boolean transportStateChanged = false;
				Float64 currentSampleInTimeline = static_cast<Float64>(outInArgs->samplePosition);
				Boolean isCycling = false;
				Float64 cycleStartBeat = 0.0;
				Float64 cycleEndBeat = 0.0;
				(void)isRecording;
				(void)transportStateChanged;
				if (hostCallbackInfo.transportStateProc2(hostCallbackInfo.hostUserData, &isPlaying, &isRecording
						, &transportStateChanged, &currentSampleInTimeline, &isCycling, &cycleStartBeat
						, &cycleEndBeat) == noErr) {
					outInArgs->isTransportRunning = toBool8(isPlaying != false);
					outInArgs->samplePosition = static_cast<Int64>(currentSampleInTimeline);
					outInArgs->isTransportLooping = toBool8(isCycling != false);
					outInArgs->loopStartPPQPosition = cycleStartBeat;
					outInArgs->loopEndPPQPosition = cycleEndBeat;
				}
			} else if (hostCallbackInfo.transportStateProc != 0) {
				Boolean isPlaying = false;
				Boolean transportStateChanged = false;
				Float64 currentSampleInTimeline = static_cast<Float64>(outInArgs->samplePosition);
				Boolean isCycling = false;
				Float64 cycleStartBeat = 0.0;
				Float64 cycleEndBeat = 0.0;
				(void)transportStateChanged;
				if (hostCallbackInfo.transportStateProc(hostCallbackInfo.hostUserData, &isPlaying, &transportStateChanged
						, &currentSampleInTimeline, &isCycling, &cycleStartBeat, &cycleEndBeat) == noErr) {
					outInArgs->isTransportRunning = toBool8(isPlaying != false);
					outInArgs->samplePosition = static_cast<Int64>(currentSampleInTimeline);
					outInArgs->isTransportLooping = toBool8(isCycling != false);
					outInArgs->loopStartPPQPosition = cycleStartBeat;
					outInArgs->loopEndPPQPosition = cycleEndBeat;
				}
			}
		}

		void initializeLoaderInfo() {
			loaderInfo.structVersion = 1;
			loaderInfo.maxSymbiosisVersion = 1;
			loaderInfo.applicationVersionHex = 0;
			loaderInfo.applicationName = 0;
			loaderInfo.applicationVendor = 0;
			loaderInfo.adapterFormat = "AU";

			CFBundleRef mainBundle = CFBundleGetMainBundle();
			if (mainBundle == 0) {
				return;
			}
			CFStringRef bundleName = static_cast<CFStringRef>(CFBundleGetValueForInfoDictionaryKey(mainBundle, CFSTR("CFBundleName")));
			CFStringRef bundleIdentifier = CFBundleGetIdentifier(mainBundle);
			CFStringRef shortVersion = static_cast<CFStringRef>(
					CFBundleGetValueForInfoDictionaryKey(mainBundle, CFSTR("CFBundleShortVersionString")));
			if (bundleName != 0) {
				if (CFStringGetCString(bundleName, hostApplicationName, static_cast<CFIndex>(sizeof (hostApplicationName)),
						kCFStringEncodingUTF8)) {
					loaderInfo.applicationName = hostApplicationName;
				}
			}
			if (bundleIdentifier != 0) {
				if (CFStringGetCString(bundleIdentifier, hostApplicationVendor,
						static_cast<CFIndex>(sizeof (hostApplicationVendor)), kCFStringEncodingUTF8)) {
					loaderInfo.applicationVendor = hostApplicationVendor;
				}
			}
			if (shortVersion != 0) {
				char versionText[64];
				if (CFStringGetCString(shortVersion, versionText, static_cast<CFIndex>(sizeof (versionText)),
						kCFStringEncodingUTF8)) {
					UInt32 versionHex = 0;
					if (parseVersionHexText(versionText, &versionHex)) {
						loaderInfo.applicationVersionHex = versionHex;
					}
				}
			}
		}

		AudioComponentInstance componentInstance;
		AUAdapterHost host;
		AudioComponentDescription componentDescription;
		SymbiosisLoaderInfo loaderInfo;
		UTF8Z factoryErrorText[1024];
		UTF8Z hostApplicationName[256];
		UTF8Z hostApplicationVendor[256];
		std::unique_ptr<HostedFactory> factory;
		std::unique_ptr<HostedPlugIn> plugIn;	// plugIn declared after factory so it is destroyed before
		const SymbiosisPlugInInfo* plugInInfo;
		bool opened;
		bool initialized;
		bool wantsMidiInput;
		bool createsMidiOutput;
		AUMIDIOutputCallbackStruct midiOutputCallbackStruct;	// host-installed MIDI-out sink; midiOutputCallback == 0 until set
		std::vector<UByte8> midiOutputPacketListStorage;		// reused per render block; never allocated on the audio thread
		Float64 sampleRate;
		UInt32 maximumFramesPerSlice;
		bool isBypassed;
		UInt32 latencySamples;
		Int32 tailSamples;
		HostCallbackInfo hostCallbackInfo;
		AudioLifecycleState audioLifecycleState;
		UInt32 configuredInputChannelCount;
		UInt32 configuredOutputChannelCount;
		SInt32 presentPresetNumber;
		std::string presentPresetName;
		std::vector<AUPreset> factoryPresetStorage;		// EXPERIMENT: persistent AUPreset backing; returned NULL-callback arrays point into this and it owns the CFString names
		bool customUIViewIsOpen;
		void* customUIViewParent;
		std::vector<UInt32> inputBusChannelOffsets;
		std::vector<UInt32> outputBusChannelOffsets;
		std::vector<InputBusRouting> inputBusRouting;
		std::vector<std::vector<UByte8>> inputBusAudioBufferListStorage;
		std::vector<UInt32> inputBusHostChannelCounts;
		std::vector<Float32> scratchInputTrashStorage;
		std::vector<Float32> scratchInputStorage;
		std::vector<Float32> scratchOutputStorage;
			std::vector<Float32*> scratchInputChannels;
			std::vector<Float32*> scratchOutputChannels;
			std::vector<const Float32*> inputChannelPointers;
		std::vector<Float32*> outputChannelPointers;
			std::vector<AudioUnitParameterInfo> parameterInfos;
			std::vector<CFArrayRef> parameterValueStrings;
			std::vector<Bool8> inputBusConnectedFlags;
		std::vector<Bool8> inputBusSilentFlags;
		std::vector<Bool8> outputBusConnectedFlags;
		std::vector<Bool8> outputBusSilentFlags;
		std::vector<SymbiosisEvent> pendingInputEvents;
		std::vector<SymbiosisMidiEventData> pendingInputMidiEvents;
		UInt32 pendingInputEventCount;
		Float64 lastRenderSampleTime;
		UInt32 lastRenderFrameCount;
		std::vector<RenderNotifyListener> renderNotifyListeners;
		std::vector<PropertyListener> propertyListeners;
		std::vector<AUChannelInfo> supportedNumChannels;
		std::vector<AudioStreamBasicDescription> inputBusFormats;
		std::vector<AudioStreamBasicDescription> outputBusFormats;
};

#ifdef __OBJC__
static unsigned int cocoaFactoryInterfaceVersion(id, SEL) {
	return 0;
}

static NSString* cocoaFactoryDescription(id, SEL) {
	return @"Symbiosis AU Cocoa UI";
}

static NSView* cocoaFactoryUIViewForAudioUnit(id, SEL, AudioUnit audioUnit, NSSize preferredSize) {
	NSView* view = nil;
	try {
		AUAdapterInstance* adapter = 0;
		UInt32 dataSize = static_cast<UInt32>(sizeof (adapter));
		const OSStatus status = AudioUnitGetProperty(audioUnit, SYMBIOSIS_COMPONENT_PROPERTY_ID, 0, 0, &adapter, &dataSize);
		SYMBIOSIS_ASSERT(status == noErr);
		SYMBIOSIS_ASSERT(dataSize == static_cast<UInt32>(sizeof (adapter)));
		SYMBIOSIS_ASSERT(adapter != 0);
		NSRect frame = NSMakeRect(0.0, 0.0, preferredSize.width, preferredSize.height);
		view = [[cocoaViewClass alloc] initWithFrame:frame];
		if (view == nil) {
			return nil;
		}
		object_setInstanceVariable(view, "symbiosisAdapter", adapter);
		UInt32 width = static_cast<UInt32>(preferredSize.width > 1.0 ? preferredSize.width : 1.0);
		UInt32 height = static_cast<UInt32>(preferredSize.height > 1.0 ? preferredSize.height : 1.0);
		if (adapter->getCustomUIViewSize(&width, &height)) {
			[view setFrame:NSMakeRect(0.0, 0.0, static_cast<CGFloat>(width), static_cast<CGFloat>(height))];
		}
		[view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
		if (adapter->hasOpenCustomUIView()) {
			// Latest-wins, parity with the old adapter (Symbiosis.mm 5074: `if (cocoaView != 0) dropView();`):
			// hosts may request a replacement view before the previous shell deallocs (autorelease timing).
			adapter->closeCustomUIView();
		}
		if (!adapter->openCustomUIView(view)) {
			[view release];
			return nil;
		}
		return [view autorelease];
	} catch (...) {
		if (view != nil) {
			[view release];
			view = nil;
		}
		handleAUBoundaryException("AU cocoaFactoryUIViewForAudioUnit");
		return nil;
	}
}

static void cocoaViewDealloc(id self, SEL) {
	AUAdapterInstance* adapter = 0;
	object_getInstanceVariable(self, "symbiosisAdapter", reinterpret_cast<void**>(&adapter));
	SYMBIOSIS_ASSERT(adapter != 0);
	try {
		if (adapter->isCustomUIViewShell(self)) {
			adapter->closeCustomUIView();
		}
	} catch (...) {
		handleAUBoundaryException("AU cocoaViewDealloc");
	}
	object_setInstanceVariable(self, "symbiosisAdapter", 0);
	objc_super superData = { self, [NSView class] };
	(reinterpret_cast<void (*)(objc_super*, SEL)>(objc_msgSendSuper))(&superData, @selector(dealloc));
}

static void initAUCocoaObjectiveCClasses() {
	if (cocoaFactoryClass != nil) {
		return;
	}

	uint64_t uniqueNumber = mach_absolute_time();
	while (cocoaFactoryClass == nil) {
		UTF8Z className[256];
		snprintf(className, sizeof (className), "SymbiosisAUCocoaViewFactory%llx", uniqueNumber);
		cocoaFactoryClass = objc_allocateClassPair([NSObject class], className, 0);
		++uniqueNumber;
	}

	BOOL ok = class_addProtocol(cocoaFactoryClass, @protocol (AUCocoaUIBase));
	SYMBIOSIS_ASSERT(ok);
	ok = class_addMethod(cocoaFactoryClass, @selector(interfaceVersion), (IMP)cocoaFactoryInterfaceVersion, "I@:");
	SYMBIOSIS_ASSERT(ok);
	ok = class_addMethod(cocoaFactoryClass, @selector(description), (IMP)cocoaFactoryDescription, "@@:");
	SYMBIOSIS_ASSERT(ok);
	{
		UTF8Z types[64];
		snprintf(types, sizeof (types), "@@:%s%s", @encode(AudioUnit), @encode(NSSize));
		ok = class_addMethod(cocoaFactoryClass, @selector(uiViewForAudioUnit:withSize:), (IMP)cocoaFactoryUIViewForAudioUnit, types);
		SYMBIOSIS_ASSERT(ok);
	}
	objc_registerClassPair(cocoaFactoryClass);

	while (cocoaViewClass == nil) {
		UTF8Z className[256];
		snprintf(className, sizeof (className), "SymbiosisAUCocoaView%llx", uniqueNumber);
		cocoaViewClass = objc_allocateClassPair([NSView class], className, 0);
		++uniqueNumber;
	}
	ok = class_addIvar(cocoaViewClass, "symbiosisAdapter", sizeof (AUAdapterInstance*)
			, static_cast<uint8_t>(log2(static_cast<double>(sizeof (AUAdapterInstance*))))
			, @encode(void*));
	SYMBIOSIS_ASSERT(ok);
	ok = class_addMethod(cocoaViewClass, @selector(dealloc), (IMP)cocoaViewDealloc, "v@:");
	SYMBIOSIS_ASSERT(ok);
	objc_registerClassPair(cocoaViewClass);
}
#endif

void AUAdapterHost::updateDisplay() {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->onPlugInUpdateDisplay();
}

void AUAdapterHost::beginEdit(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->onPlugInBeginEdit(parameterNumber);
}

void AUAdapterHost::writeParameter(UInt32 parameterNumber, Float32 normalizedValue) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->onPlugInWriteParameter(parameterNumber, normalizedValue);
}

void AUAdapterHost::endEdit(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->onPlugInEndEdit(parameterNumber);
}

bool AUAdapterHost::requestResize(UInt32 width, UInt32 height) {
	SYMBIOSIS_ASSERT(owner != 0);
	return owner->onPlugInRequestResize(width, height);
}

const void* AUAdapterHost::queryExtension(UInt32 vendorId, UInt32 interfaceId) {
	SYMBIOSIS_ASSERT(owner != 0);
	(void)vendorId;
	(void)interfaceId;
	return 0;
}

struct SymbiosisAudioComponent {
	explicit SymbiosisAudioComponent(const AudioComponentDescription* inDescription)
		: adapter(0)
	{
		memset(&description, 0, sizeof (description));
		if (inDescription != 0) {
			description = *inDescription;
		}
		memset(&plugInInterface, 0, sizeof (plugInInterface));
		plugInInterface.Open = open;
		plugInInterface.Close = close;
		plugInInterface.Lookup = lookup;
	}

	AudioComponentPlugInInterface plugInInterface;
	AUAdapterInstance* adapter;
	AudioComponentDescription description;

	static AUAdapterInstance& accessAdapter(void* self) {
		SymbiosisAudioComponent* component = reinterpret_cast<SymbiosisAudioComponent*>(self);
		SYMBIOSIS_ASSERT(component != 0);
		SYMBIOSIS_ASSERT(component->adapter != 0);
		return *component->adapter;
	}

	static OSStatus open(void* self, AudioComponentInstance instance) {
		try {
			SymbiosisAudioComponent* component = reinterpret_cast<SymbiosisAudioComponent*>(self);
			SYMBIOSIS_ASSERT(component != 0);
			SYMBIOSIS_ASSERT(component->adapter == 0);
			component->adapter = new AUAdapterInstance(component->description);
			component->adapter->open(instance);
			return noErr;
		} catch (...) {
			return handleAUBoundaryException("AU open");
		}
	}

	static OSStatus close(void* self) {
		try {
			SymbiosisAudioComponent* component = reinterpret_cast<SymbiosisAudioComponent*>(self);
			SYMBIOSIS_ASSERT(component != 0);
			if (component->adapter != 0) {
				component->adapter->close();
				delete component->adapter;
				component->adapter = 0;
			}
			return noErr;
		} catch (...) {
			return handleAUBoundaryException("AU close");
		}
	}

	static OSStatus initialize(void* self) {
		try {
			return accessAdapter(self).initialize();
		} catch (...) {
			return handleAUBoundaryException("AU initialize");
		}
	}

	static OSStatus uninitialize(void* self) {
		try {
			return accessAdapter(self).uninitialize();
		} catch (...) {
			return handleAUBoundaryException("AU uninitialize");
		}
	}

	static OSStatus getPropertyInfo(void* self, AudioUnitPropertyID inID, AudioUnitScope inScope,
			AudioUnitElement inElement, UInt32* outDataSize, Boolean* outWritable) {
		try {
			return accessAdapter(self).getPropertyInfo(inID, inScope, inElement, outDataSize, outWritable);
		} catch (...) {
			return handleAUBoundaryException("AU getPropertyInfo");
		}
	}

	static OSStatus getProperty(void* self, AudioUnitPropertyID inID, AudioUnitScope inScope,
			AudioUnitElement inElement, void* outData, UInt32* ioDataSize) {
		try {
			return accessAdapter(self).getProperty(inID, inScope, inElement, outData, ioDataSize);
		} catch (...) {
			return handleAUBoundaryException("AU getProperty");
		}
	}

	static OSStatus setProperty(void* self, AudioUnitPropertyID inID, AudioUnitScope inScope,
			AudioUnitElement inElement, const void* inData, UInt32 inDataSize) {
		try {
			return accessAdapter(self).setProperty(inID, inScope, inElement, inData, inDataSize);
		} catch (...) {
			return handleAUBoundaryException("AU setProperty");
		}
	}

	static OSStatus getParameter(void* self, AudioUnitParameterID inID, AudioUnitScope inScope,
			AudioUnitElement inElement, Float32* outValue) {
		try {
			return accessAdapter(self).getParameter(inID, inScope, inElement, outValue);
		} catch (...) {
			return handleAUBoundaryException("AU getParameter");
		}
	}

	static OSStatus setParameter(void* self, AudioUnitParameterID inID, AudioUnitScope inScope,
			AudioUnitElement inElement, Float32 inValue, UInt32 inBufferOffsetInFrames) {
		try {
			return accessAdapter(self).setParameter(inID, inScope, inElement, inValue, inBufferOffsetInFrames);
		} catch (...) {
			return handleAUBoundaryException("AU setParameter");
		}
	}

	static OSStatus render(void* self, AudioUnitRenderActionFlags* ioActionFlags, const AudioTimeStamp* inTimeStamp,
			UInt32 inOutputBusNumber, UInt32 inNumberFrames, AudioBufferList* ioData) {
		try {
			return accessAdapter(self).render(ioActionFlags, inTimeStamp, inOutputBusNumber, inNumberFrames, ioData);
		} catch (...) {
			return handleAUBoundaryException("AU render");
		}
	}

	static OSStatus reset(void* self, AudioUnitScope inScope, AudioUnitElement inElement) {
		try {
			return accessAdapter(self).reset(inScope, inElement);
		} catch (...) {
			return handleAUBoundaryException("AU reset");
		}
	}

	static OSStatus addPropertyListener(void* self, AudioUnitPropertyID inID, AudioUnitPropertyListenerProc inProc,
			void* inProcRefCon) {
		try {
			return accessAdapter(self).addPropertyListener(inID, inProc, inProcRefCon);
		} catch (...) {
			return handleAUBoundaryException("AU addPropertyListener");
		}
	}

	static OSStatus removePropertyListener(void* self, AudioUnitPropertyID inID, AudioUnitPropertyListenerProc inProc) {
		try {
			return accessAdapter(self).removePropertyListener(inID, inProc);
		} catch (...) {
			return handleAUBoundaryException("AU removePropertyListener");
		}
	}

	static OSStatus removePropertyListenerWithUserData(void* self, AudioUnitPropertyID inID,
			AudioUnitPropertyListenerProc inProc, void* inProcRefCon) {
		try {
			return accessAdapter(self).removePropertyListenerWithUserData(inID, inProc, inProcRefCon);
		} catch (...) {
			return handleAUBoundaryException("AU removePropertyListenerWithUserData");
		}
	}

	static OSStatus addRenderNotify(void* self, AURenderCallback inProc, void* inProcRefCon) {
		try {
			return accessAdapter(self).addRenderNotify(inProc, inProcRefCon);
		} catch (...) {
			return handleAUBoundaryException("AU addRenderNotify");
		}
	}

	static OSStatus removeRenderNotify(void* self, AURenderCallback inProc, void* inProcRefCon) {
		try {
			return accessAdapter(self).removeRenderNotify(inProc, inProcRefCon);
		} catch (...) {
			return handleAUBoundaryException("AU removeRenderNotify");
		}
	}

	static OSStatus scheduleParameters(void* self, AudioUnitParameterEvent* inParameterEvent, UInt32 inNumParamEvents) {
		try {
			return accessAdapter(self).scheduleParameters(inParameterEvent, inNumParamEvents);
		} catch (...) {
			return handleAUBoundaryException("AU scheduleParameters");
		}
	}

	static OSStatus midiEvent(void* self, UInt32 inStatus, UInt32 inData1, UInt32 inData2, UInt32 inOffsetSampleFrame) {
		try {
			return accessAdapter(self).midiEvent(inStatus, inData1, inData2, inOffsetSampleFrame);
		} catch (...) {
			return handleAUBoundaryException("AU midiEvent");
		}
	}

	static AudioComponentMethod lookup(SInt16 selector) {
		switch (selector) {
			case kAudioUnitInitializeSelect: return reinterpret_cast<AudioComponentMethod>(initialize);
			case kAudioUnitUninitializeSelect: return reinterpret_cast<AudioComponentMethod>(uninitialize);
			case kAudioUnitGetPropertyInfoSelect: return reinterpret_cast<AudioComponentMethod>(getPropertyInfo);
			case kAudioUnitGetPropertySelect: return reinterpret_cast<AudioComponentMethod>(getProperty);
			case kAudioUnitSetPropertySelect: return reinterpret_cast<AudioComponentMethod>(setProperty);
			case kAudioUnitAddPropertyListenerSelect: return reinterpret_cast<AudioComponentMethod>(addPropertyListener);
			case kAudioUnitRemovePropertyListenerSelect: return reinterpret_cast<AudioComponentMethod>(removePropertyListener);
			case kAudioUnitRemovePropertyListenerWithUserDataSelect:
				return reinterpret_cast<AudioComponentMethod>(removePropertyListenerWithUserData);
			case kAudioUnitRenderSelect: return reinterpret_cast<AudioComponentMethod>(render);
			case kAudioUnitResetSelect: return reinterpret_cast<AudioComponentMethod>(reset);
			case kAudioUnitGetParameterSelect: return reinterpret_cast<AudioComponentMethod>(getParameter);
			case kAudioUnitSetParameterSelect: return reinterpret_cast<AudioComponentMethod>(setParameter);
			case kAudioUnitAddRenderNotifySelect: return reinterpret_cast<AudioComponentMethod>(addRenderNotify);
			case kAudioUnitRemoveRenderNotifySelect: return reinterpret_cast<AudioComponentMethod>(removeRenderNotify);
			case kAudioUnitScheduleParametersSelect: return reinterpret_cast<AudioComponentMethod>(scheduleParameters);
			case kMusicDeviceMIDIEventSelect: return reinterpret_cast<AudioComponentMethod>(midiEvent);
			default: break;
		}
		return 0;
	}
};

} // namespace

extern "C"
SYMBIOSIS_EXPORT void* SymbiosisAUFactory(const AudioComponentDescription* inDesc) {
	traceMessage("AU factory", "Using modern AudioComponent dispatch");
	return new SymbiosisAudioComponent(inDesc);
}
