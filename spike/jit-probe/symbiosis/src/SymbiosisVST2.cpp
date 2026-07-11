#include "SymbiosisCpp.h"
#include "SymbiosisTests.h"

// You need an include path to the VST2 SDK root, at least for this specific .cpp file

#include "pluginterfaces/vst2.x/aeffectx.h"

#include <algorithm>
#include <exception>
#include <limits.h>
#include <stddef.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string.h>
#include <vector>

#if defined(__GNUC__)
#define VST_EXPORT extern "C" __attribute__((visibility("default")))
#elif defined(_MSC_VER)
#define VST_EXPORT extern "C" __declspec(dllexport)
#else
#error Unknown compiler.
#endif

// Keep enabled for now: some VST2 hosts/paths have been observed to provide
// incomplete channel-pointer arrays for multibus/sidechain cases.
// References:
// - https://forum.juce.com/t/vst-crashing-in-maschine-2-on-some-multi-bus-configurations/22334
// - https://forum.juce.com/t/wavelab-crashes-vst2-plugins-with-sidechain/26981
#ifndef SYMBIOSIS_VST2_ENABLE_CHANNEL_POINTER_WORKAROUND
#define SYMBIOSIS_VST2_ENABLE_CHANNEL_POINTER_WORKAROUND 1
#endif
#ifndef SYMBIOSIS_VST2_SUPPORT_ACCUMULATING_PROCESS
#define SYMBIOSIS_VST2_SUPPORT_ACCUMULATING_PROCESS 1
#define SYMBIOSIS_VST2_ACCUMULATING_PROCESS_FALLBACK_BLOCK_SIZE 1024u
#endif

using namespace symbiosis;

namespace {

#if defined(__APPLE__)
static const unsigned char MAC_ROMAN_TO_ISO_LATIN[256] = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
	0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
	0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
	0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
	0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
	0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,
	0xc4,0xc5,0xc7,0xc9,0xd1,0xd6,0xdc,0xe1,0xe0,0xe2,0xe4,0xe3,0xe5,0xe7,0xe9,0xe8,
	0xea,0xeb,0xed,0xec,0xee,0xef,0xf1,0xf3,0xf2,0xf4,0xf6,0xf5,0xfa,0xf9,0xfb,0xfc,
	0x3f,0xb0,0xa2,0xa3,0xa7,0x3f,0xb6,0xdf,0xae,0xa9,0x3f,0xb4,0xa8,0x3f,0xc6,0xd8,
	0x3f,0xb1,0x3f,0x3f,0xa5,0xb5,0x3f,0x3f,0x3f,0x3f,0x3f,0xaa,0xba,0x3f,0xe6,0xf8,
	0xbf,0xa1,0xac,0x3f,0x3f,0x3f,0x3f,0xab,0xbb,0x3f,0xa0,0xc0,0xc3,0xd5,0x3f,0x3f,
	0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0xf7,0x3f,0xff,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,
	0x3f,0xb7,0x3f,0x3f,0x3f,0xc2,0xca,0xc1,0xcb,0xc8,0xcd,0xce,0xcf,0xcc,0xd3,0xd4,
	0x3f,0xd2,0xda,0xdb,0xd9,0x3f,0x3f,0x3f,0xaf,0x3f,0x3f,0x3f,0xb8,0x3f,0x3f,0x3f
};
static const unsigned char ISO_LATIN_TO_MAC_ROMAN[256] = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
	0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
	0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
	0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
	0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
	0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,
	0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,
	0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,0x3f,
	0xca,0xc1,0xa2,0xa3,0x3f,0xb4,0x3f,0xa4,0xac,0xa9,0xbb,0xc7,0xc2,0x3f,0xa8,0xf8,
	0xa1,0xb1,0x3f,0x3f,0xab,0xb5,0xa6,0xe1,0xfc,0x3f,0xbc,0xc8,0x3f,0x3f,0x3f,0xc0,
	0xcb,0xe7,0xe5,0xcc,0x80,0x81,0xae,0x82,0xe9,0x83,0xe6,0xe8,0xed,0xea,0xeb,0xec,
	0x3f,0x84,0xf1,0xee,0xef,0xcd,0x85,0x3f,0xaf,0xf4,0xf2,0xf3,0x86,0x3f,0x3f,0xa7,
	0x88,0x87,0x89,0x8b,0x8a,0x8c,0xbe,0x8d,0x8f,0x8e,0x90,0x91,0x93,0x92,0x94,0x95,
	0x3f,0x96,0x98,0x97,0x99,0x9b,0x9a,0xd6,0xbf,0x9d,0x9c,0x9e,0x9f,0x3f,0x3f,0xd8
};
#endif

static unsigned char hostByteToIsoByte(unsigned char value) {
#if defined(__APPLE__)
	return MAC_ROMAN_TO_ISO_LATIN[value];
#else
	return value;
#endif
}

static unsigned char isoByteToHostByte(unsigned char value) {
#if defined(__APPLE__)
	return ISO_LATIN_TO_MAC_ROMAN[value];
#else
	return value;
#endif
}

static void copyVstTextToUTF8(const char* source, UInt32 maxSourceLength, UTF8Z* target, UInt32 targetLength) {
	SYMBIOSIS_ASSERT(source != 0);
	SYMBIOSIS_ASSERT(target != 0);
	SYMBIOSIS_ASSERT(targetLength > 0);
	UInt32 targetOffset = 0;
	UInt32 sourceOffset = 0;
	while (sourceOffset < maxSourceLength && source[sourceOffset] != 0 && targetOffset + 1 < targetLength) {
		const unsigned char isoValue = hostByteToIsoByte(static_cast<unsigned char>(source[sourceOffset]));
		if (isoValue < 0x80u) {
			target[targetOffset] = static_cast<char>(isoValue);
			++targetOffset;
		} else if (targetOffset + 2 < targetLength) {
			target[targetOffset + 0] = static_cast<char>(0xC0u | (isoValue >> 6));
			target[targetOffset + 1] = static_cast<char>(0x80u | (isoValue & 0x3Fu));
			targetOffset += 2;
		} else {
			break;
		}
		++sourceOffset;
	}
	target[targetOffset] = 0;
}

static void copyUTF8ToVstText(const UTF8Z* source, char* target, UInt32 targetLength) {
	SYMBIOSIS_ASSERT(source != 0);
	SYMBIOSIS_ASSERT(target != 0);
	SYMBIOSIS_ASSERT(targetLength > 0);
	UInt32 sourceOffset = 0;
	UInt32 targetOffset = 0;
	while (source[sourceOffset] != 0 && targetOffset + 1 < targetLength) {
		const unsigned char first = static_cast<unsigned char>(source[sourceOffset]);
		unsigned char isoValue = '?';
		if (first < 0x80u) {
			isoValue = first;
			++sourceOffset;
		} else if (first >= 0xC2u && first <= 0xDFu) {
			const unsigned char second = static_cast<unsigned char>(source[sourceOffset + 1]);
			if ((second & 0xC0u) == 0x80u) {
				isoValue = static_cast<unsigned char>(((first & 0x1Fu) << 6) | (second & 0x3Fu));
				++sourceOffset;
				++sourceOffset;
			} else {
				++sourceOffset;
			}
		} else {
			++sourceOffset;
			while ((static_cast<unsigned char>(source[sourceOffset]) & 0xC0u) == 0x80u) {
				++sourceOffset;
			}
		}
		target[targetOffset] = static_cast<char>(isoByteToHostByte(isoValue));
		++targetOffset;
	}
	target[targetOffset] = 0;
}

class VST2AdapterInstance;

class VST2AdapterHost : public Host {
	public:
		explicit VST2AdapterHost(VST2AdapterInstance* owner) : owner(owner) {}
		void updateDisplay() override;
		void beginEdit(UInt32 parameterNumber) override;
		void writeParameter(UInt32 parameterNumber, Float32 normalizedValue) override;
		void endEdit(UInt32 parameterNumber) override;
		bool requestResize(UInt32 width, UInt32 height) override;
		const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) override;

	protected:
		VST2AdapterInstance* owner;
};

class VST2AdapterInstance {
	public:
		static const UInt32 STATE_HEADER_SIZE = 12u;
		static const UInt32 STATE_MAGIC_SYMB = static_cast<UInt32>('S')
				| (static_cast<UInt32>('y') << 8)
				| (static_cast<UInt32>('m') << 16)
				| (static_cast<UInt32>('b') << 24);
		static const UInt32 STATE_ADAPTER_VST2 = static_cast<UInt32>('V')
				| (static_cast<UInt32>('S') << 8)
				| (static_cast<UInt32>('T') << 16)
				| (static_cast<UInt32>('2') << 24);

		static AEffect* createAdapterEffect(audioMasterCallback audioMaster) {
			if (!(*audioMaster)(0, audioMasterVersion, 0, 0, 0, 0)) {
				throw std::runtime_error("audioMasterVersion check failed");
			}
			std::unique_ptr<VST2AdapterInstance> instance(new VST2AdapterInstance(audioMaster));
			instance->init();
			return &instance.release()->effect;
		}

		explicit VST2AdapterInstance(audioMasterCallback audioMaster)
			: audioMaster(audioMaster)
			, host(this)
			, plugInInfo(0)
			, currentAudioState(AUDIO_UNCONFIGURED)
			, currentProgram(0)
			, sampleRate(44100.0f)
			, maxBlockSize(0)
			, vstTailSize(0)
			, isBypassed(false)
			, wantsMidiInput(false)
			, createsMidiOutput(false)
			, inputBusCount(0)
			, outputBusCount(0)
			, inputChannelCount(0)
			, outputChannelCount(0)
			, pendingInputEventCount(0)
			, renderFallbackSamplePosition(0)
			, editorOpen(false)
		{
			memset(&effect, 0, sizeof (effect));
			memset(&editorRect, 0, sizeof (editorRect));
		}

		void init() {
			// Bootstrap AEffect enough that host audioMaster identity callbacks can run before real plug-in info is available.
			effect.magic = kEffectMagic;
			effect.dispatcher = &VST2AdapterInstance::dispatcherThunk;
			effect.setParameter = &VST2AdapterInstance::setParameterThunk;
			effect.getParameter = &VST2AdapterInstance::getParameterThunk;
			effect.processReplacing = &VST2AdapterInstance::processReplacingThunk;
		#if SYMBIOSIS_VST2_SUPPORT_ACCUMULATING_PROCESS
			effect.DECLARE_VST_DEPRECATED(process) = &VST2AdapterInstance::processThunk;
		#endif
			effect.object = this;
			effect.flags = effFlagsCanReplacing;

			UTF8Z errorText[1024];
			errorText[0] = 0;

			memset(&loaderInfo, 0, sizeof (loaderInfo));
			loaderInfo.structVersion = 1;
			loaderInfo.maxSymbiosisVersion = 1;

			hostVendor[0] = 0;
			hostName[0] = 0;
			{
				char vendor[kVstMaxVendorStrLen + 1];
				if (audioMaster(&effect, audioMasterGetVendorString, 0, 0, vendor, 0.0f) != 0 && vendor[0] != 0) {
					copyVstTextToUTF8(vendor, kVstMaxVendorStrLen, hostVendor, static_cast<UInt32>(sizeof (hostVendor)));
				}
				char product[kVstMaxProductStrLen + 1];
				if (audioMaster(&effect, audioMasterGetProductString, 0, 0, product, 0.0f) != 0 && product[0] != 0) {
					copyVstTextToUTF8(product, kVstMaxProductStrLen, hostName, static_cast<UInt32>(sizeof (hostName)));
				}
				loaderInfo.applicationVersionHex
						= static_cast<UInt32>(audioMaster(&effect, audioMasterGetVendorVersion, 0, 0, 0, 0.0f));
			}
			loaderInfo.applicationName = (hostName[0] != 0 ? hostName : 0);
			loaderInfo.applicationVendor = (hostVendor[0] != 0 ? hostVendor : 0);
			loaderInfo.adapterFormat = "VST2";

			const SymbiosisFactoryInterface* factoryApi = 0;
			SymbiosisFactory* factoryInstance = symbiosisCreateFactory(&loaderInfo, &factoryApi, 1023, errorText);
			if (factoryInstance == 0 || factoryApi == 0) {
				throw std::runtime_error("symbiosisCreateFactory failed");
			}

			factory.reset(new HostedFactory(factoryInstance, factoryApi));
			if (factory->getPlugInCount() == 0) {
				throw std::runtime_error("No Symbiosis plug-ins in factory");
			}

			plugInInfo = factory->getPlugInInfo(0);
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(plugInInfo->programCount > 0);
			SYMBIOSIS_ASSERT(plugInInfo->plugInId != 0);
			
			// Overwrite bootstrap identity as soon as real plug-in info is known.
			effect.uniqueID = static_cast<VstInt32>(plugInInfo->plugInId);
			effect.version = static_cast<VstInt32>(plugInInfo->versionHex);

				inputBusCount = plugInInfo->audioInputBusCount;
				outputBusCount = plugInInfo->audioOutputBusCount;

			inputChannelCount = 0;
			if (inputBusCount > 0) {
				selectedInputBusFormatValues.reset(new UInt32[inputBusCount]);
				selectedInputBusChannelCounts.reset(new UInt32[inputBusCount]);
				inputBusConnected.reset(new Bool8[inputBusCount]);
				inputBusSilent.reset(new Bool8[inputBusCount]);
				for (UInt32 i = 0; i < inputBusCount; ++i) {
					inputBusConnected[i] = 1;
					inputBusSilent[i] = 0;
					const UInt32 supported = plugInInfo->audioInputBuses[i].supportedBusFormats;
					SYMBIOSIS_ASSERT((supported & SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK) == 0
							|| (i < outputBusCount && plugInInfo->audioOutputBuses[i].supportedBusFormats == supported));
						const UInt32 formatMask = chooseBusFormatMask(supported);
						const UInt32 thisChannelCount = busFormatMaskToChannelCount(formatMask);
						selectedInputBusFormatValues[i] = formatMask;
					selectedInputBusChannelCounts[i] = thisChannelCount;
					inputChannelCount += thisChannelCount;
				}
				SYMBIOSIS_ASSERT(inputChannelCount > 0);
				inputChannelPointers.reset(new const Float32*[inputChannelCount]);
				inputPinProperties.reset(new VstPinProperties[inputChannelCount]);
				inputChannelConnected.reset(new bool[inputChannelCount]);
				UInt32 inputChannelIndex = 0;
				for (UInt32 bus = 0; bus < inputBusCount; ++bus) {
					const UInt32 busChannelCount = selectedInputBusChannelCounts[bus];
						const UInt32 selectedFormatMask = selectedInputBusFormatValues[bus];
						const VstInt32 arrangementType = arrangementTypeFromMask(selectedFormatMask);
					const SymbiosisAudioBusInfo* busInfo = &plugInInfo->audioInputBuses[bus];
					SYMBIOSIS_ASSERT(busInfo != 0);
					SYMBIOSIS_ASSERT(busInfo->displayName != 0);
					for (UInt32 channel = 0; channel < busChannelCount; ++channel) {
						VstPinProperties* pin = &inputPinProperties[inputChannelIndex];
						memset(pin, 0, sizeof (VstPinProperties));
						pin->flags = kVstPinIsActive;
						pin->arrangementType = arrangementType;
						if (busChannelCount == 2 && channel == 0) {
							pin->flags |= kVstPinIsStereo;
						}
						copyUTF8ToVstText(busInfo->displayName, pin->label, static_cast<UInt32>(sizeof (pin->label)));
						copyUTF8ToVstText(busInfo->displayName, pin->shortLabel, static_cast<UInt32>(sizeof (pin->shortLabel)));
						inputChannelConnected[inputChannelIndex] = true;
						++inputChannelIndex;
					}
				}
				SYMBIOSIS_ASSERT(inputChannelIndex == inputChannelCount);
			}

			outputChannelCount = 0;
			if (outputBusCount > 0) {
				selectedOutputBusFormatValues.reset(new UInt32[outputBusCount]);
				selectedOutputBusChannelCounts.reset(new UInt32[outputBusCount]);
				outputBusConnected.reset(new Bool8[outputBusCount]);
				outputBusSilent.reset(new Bool8[outputBusCount]);
				for (UInt32 i = 0; i < outputBusCount; ++i) {
					outputBusConnected[i] = 1;
					outputBusSilent[i] = 0;
					const UInt32 supported = plugInInfo->audioOutputBuses[i].supportedBusFormats;
					SYMBIOSIS_ASSERT((supported & SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK) == 0
							|| (i < inputBusCount && plugInInfo->audioInputBuses[i].supportedBusFormats == supported));
						const UInt32 formatMask = chooseBusFormatMask(supported);
						const UInt32 thisChannelCount = busFormatMaskToChannelCount(formatMask);
						selectedOutputBusFormatValues[i] = formatMask;
					selectedOutputBusChannelCounts[i] = thisChannelCount;
					outputChannelCount += thisChannelCount;
				}
				SYMBIOSIS_ASSERT(outputChannelCount > 0);
				outputChannelPointers.reset(new Float32*[outputChannelCount]);
				outputPinProperties.reset(new VstPinProperties[outputChannelCount]);
				outputChannelConnected.reset(new bool[outputChannelCount]);
				UInt32 outputChannelIndex = 0;
				for (UInt32 bus = 0; bus < outputBusCount; ++bus) {
					const UInt32 busChannelCount = selectedOutputBusChannelCounts[bus];
						const UInt32 selectedFormatMask = selectedOutputBusFormatValues[bus];
						const VstInt32 arrangementType = arrangementTypeFromMask(selectedFormatMask);
					const SymbiosisAudioBusInfo* busInfo = &plugInInfo->audioOutputBuses[bus];
					SYMBIOSIS_ASSERT(busInfo != 0);
					SYMBIOSIS_ASSERT(busInfo->displayName != 0);
					for (UInt32 channel = 0; channel < busChannelCount; ++channel) {
						VstPinProperties* pin = &outputPinProperties[outputChannelIndex];
						memset(pin, 0, sizeof (VstPinProperties));
						pin->flags = kVstPinIsActive;
						pin->arrangementType = arrangementType;
						if (busChannelCount == 2 && channel == 0) {
							pin->flags |= kVstPinIsStereo;
						}
						copyUTF8ToVstText(busInfo->displayName, pin->label, static_cast<UInt32>(sizeof (pin->label)));
						copyUTF8ToVstText(busInfo->displayName, pin->shortLabel, static_cast<UInt32>(sizeof (pin->shortLabel)));
						outputChannelConnected[outputChannelIndex] = true;
						++outputChannelIndex;
					}
				}
				SYMBIOSIS_ASSERT(outputChannelIndex == outputChannelCount);
			}

			SYMBIOSIS_ASSERT(plugInInfo->programCount >= 1);
			effect.numPrograms = static_cast<VstInt32>(plugInInfo->programCount);
			effect.numParams = static_cast<VstInt32>(plugInInfo->parameterCount);
			effect.numInputs = static_cast<VstInt32>(inputChannelCount);
			effect.numOutputs = static_cast<VstInt32>(outputChannelCount);
			effect.flags = effFlagsCanReplacing
					| effFlagsProgramChunks
					| (plugInInfo->hasCustomUIView ? effFlagsHasEditor : 0)
					| (plugInInfo->plugInType == SYMBIOSIS_PLUGIN_TYPE_INSTRUMENT ? effFlagsIsSynth : 0);
			plugIn.reset(factory->createPlugIn(0, &host));
			if (!plugIn) {
				UTF8Z errorText[1024];
				errorText[0] = 0;
				factory->getLastErrorText(static_cast<UInt32>(sizeof (errorText) - 1u), errorText);
				traceMessage("VST2 createPlugIn", (errorText[0] != 0 ? errorText : "Unknown error"));
				throw std::runtime_error("createPlugIn failed");
			}

			const UInt32 inputEventCapacity = plugInInfo->maxInputEventCountPerBlock;
			const UInt32 outputEventCapacity = plugInInfo->maxOutputEventCountPerBlock;
			const UInt32 eventCapabilities = plugInInfo->eventCapabilities;
			wantsMidiInput = ((eventCapabilities & SYMBIOSIS_WANTS_MIDI_INPUT_MASK) != 0);
			createsMidiOutput = ((eventCapabilities & SYMBIOSIS_CREATES_MIDI_OUTPUT_MASK) != 0);
			SYMBIOSIS_ASSERT(!wantsMidiInput || inputEventCapacity > 0);
			SYMBIOSIS_ASSERT(!createsMidiOutput || outputEventCapacity > 0);
			
			if (wantsMidiInput) {
				pendingInputEvents.reset(new SymbiosisEvent[inputEventCapacity]);
				pendingInputEventData.reset(new SymbiosisMidiEventData[inputEventCapacity]);
			}
			
			if (createsMidiOutput) {
				outputMidiEvents.reset(new VstMidiEvent[outputEventCapacity]);
				const size_t outgoingVstEventsBufferSize = offsetof(VstEvents, events) + sizeof (VstEvent*) * std::max(outputEventCapacity, 1u);
				outgoingVstEventsBuffer.reset(new UByte8[outgoingVstEventsBufferSize]);
				VstEvent** eventArray = reinterpret_cast<VstEvent**>(outgoingVstEventsBuffer.get() + offsetof(VstEvents, events));
				for (UInt32 i = 0; i < outputEventCapacity; ++i) {
					eventArray[i] = reinterpret_cast<VstEvent*>(&outputMidiEvents[i]);
				}
			}
			pendingInputEventCount = 0;
		}

		VstIntPtr dispatcher(VstInt32 opcode, VstInt32 index, VstIntPtr value, void* pointer, float opt) {
			switch (opcode) {
				case effOpen: {
					const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
					SYMBIOSIS_ASSERT(currentAudioState == AUDIO_UNCONFIGURED);
					const VstIntPtr hostSampleRate = audioMaster(&effect, audioMasterGetSampleRate, 0, 0, 0, 0.0f);
					if (hostSampleRate > 0) {
						sampleRate = static_cast<Float32>(hostSampleRate);
					}
					maxBlockSize = static_cast<UInt32>(audioMaster(&effect, audioMasterGetBlockSize, 0, 0, 0, 0.0f));
					return 1;
				}
					
				case effClose:
					delete this;
					return 1;
					
				case effSetProgram: {
					SYMBIOSIS_ASSERT(value >= 0 && static_cast<UInt32>(value) < plugInInfo->programCount);
					const VstIntPtr maxProgramIndex = static_cast<VstIntPtr>(plugInInfo->programCount - 1u);
					const VstIntPtr clampedProgram = std::min(std::max(value, static_cast<VstIntPtr>(0)), maxProgramIndex);
					currentProgram = static_cast<UInt32>(clampedProgram);
					plugIn->changeProgram(currentProgram);
					return 1;
				}
					
				case effGetProgram:
					return static_cast<VstIntPtr>(currentProgram);
					
				case effSetProgramName: {
					SYMBIOSIS_ASSERT(pointer != 0);
					UTF8Z utf8ProgramName[(kVstMaxProgNameLen + 1) * 2];
					copyVstTextToUTF8(reinterpret_cast<const char*>(pointer), kVstMaxProgNameLen, utf8ProgramName, static_cast<UInt32>(sizeof (utf8ProgramName)));
					plugIn->setProgramName(currentProgram, utf8ProgramName);
					return 1;
				}
					
				case effGetProgramName: {
					SYMBIOSIS_ASSERT(pointer != 0);
					UTF8Z utf8ProgramName[kVstMaxProgNameLen + 1];
					plugIn->getProgramName(currentProgram, kVstMaxProgNameLen, utf8ProgramName);
					copyUTF8ToVstText(utf8ProgramName, reinterpret_cast<char*>(pointer), kVstMaxProgNameLen + 1);
					return 1;
				}

				case effCanBeAutomated:
					SYMBIOSIS_ASSERT(index >= 0);
					SYMBIOSIS_ASSERT(static_cast<UInt32>(index) < plugInInfo->parameterCount);
					return 1;
					
				case effGetParamLabel: {
					SYMBIOSIS_ASSERT(pointer != 0);
					SYMBIOSIS_ASSERT(index >= 0);
					SYMBIOSIS_ASSERT(static_cast<UInt32>(index) < plugInInfo->parameterCount);
					const UTF8Z* label = plugInInfo->parameters[index].displayUnit;
					char* outPointer = reinterpret_cast<char*>(pointer);
					if (label == 0) {
						outPointer[0] = 0;
					} else {
						copyUTF8ToVstText(label, outPointer, kVstMaxParamStrLen + 1);
					}
					return 1;
				}
					
				case effGetParamDisplay: {
					SYMBIOSIS_ASSERT(pointer != 0);
					SYMBIOSIS_ASSERT(index >= 0);
					SYMBIOSIS_ASSERT(static_cast<UInt32>(index) < plugInInfo->parameterCount);
					const UInt32 parameterIndex = static_cast<UInt32>(index);
					const Float32 normalizedValue = plugIn->getParameter(parameterIndex);
					UTF8Z utf8Text[kVstMaxParamStrLen + 1];
					const bool converted = plugIn->convertParameterValueToText(parameterIndex, normalizedValue
							, kVstMaxParamStrLen, utf8Text);
					if (!converted) {
						convertParameterValueToTextDefault(plugInInfo->parameters[parameterIndex]
								, normalizedValue, static_cast<UInt32>(kVstMaxParamStrLen), utf8Text);
					}
					copyUTF8ToVstText(utf8Text, reinterpret_cast<char*>(pointer), kVstMaxParamStrLen + 1);
					return 1;
				}
					
				case effGetParamName:
					SYMBIOSIS_ASSERT(pointer != 0);
					SYMBIOSIS_ASSERT(index >= 0);
					SYMBIOSIS_ASSERT(static_cast<UInt32>(index) < plugInInfo->parameterCount);
					copyUTF8ToVstText(plugInInfo->parameters[index].displayName, reinterpret_cast<char*>(pointer), kVstMaxParamStrLen + 1);
					return 1;
					
				case effGetEffectName:
					SYMBIOSIS_ASSERT(pointer != 0);
					copyUTF8ToVstText(plugInInfo->displayName, reinterpret_cast<char*>(pointer), kVstMaxEffectNameLen + 1);
					return 1;
					
				case effGetVendorString:
					SYMBIOSIS_ASSERT(pointer != 0);
					copyUTF8ToVstText(plugInInfo->displayVendor, reinterpret_cast<char*>(pointer), kVstMaxVendorStrLen + 1);
					return 1;
					
				case effGetProductString:
					SYMBIOSIS_ASSERT(pointer != 0);
					copyUTF8ToVstText(plugInInfo->displayName, reinterpret_cast<char*>(pointer), kVstMaxProductStrLen + 1);
					return 1;
					
				case effGetVendorVersion: return static_cast<VstIntPtr>(plugInInfo->versionHex);
					
				case effCanDo:
					SYMBIOSIS_ASSERT(pointer != 0);
					return canDoResult(reinterpret_cast<const char*>(pointer));
					
				case effGetPlugCategory:
					SYMBIOSIS_ASSERT(plugInInfo != 0);
					if (plugInInfo->plugInType == SYMBIOSIS_PLUGIN_TYPE_INSTRUMENT) {
						return kPlugCategSynth;
					}
					if ((plugInInfo->plugInCategories & SYMBIOSIS_PLUGIN_CATEGORY_ANALYZER_MASK) != 0u) {
						return kPlugCategAnalysis;
					}
					if ((plugInInfo->plugInCategories & (SYMBIOSIS_PLUGIN_CATEGORY_DELAY_MASK | SYMBIOSIS_PLUGIN_CATEGORY_REVERB_MASK)) != 0u) {
						return kPlugCategRoomFx;
					}
					if ((plugInInfo->plugInCategories & SYMBIOSIS_PLUGIN_CATEGORY_SPATIAL_MASK) != 0u) {
						return kPlugCategSpacializer;
					}
					if ((plugInInfo->plugInCategories & SYMBIOSIS_PLUGIN_CATEGORY_RESTORATION_MASK) != 0u) {
						return kPlugCategRestoration;
					}
					return kPlugCategEffect;
					
				case effGetTailSize: return vstTailSize;
				case effGetVstVersion: return kVstVersion;
				case effGetInputProperties: return getPinProperties(true, index, pointer);
				case effGetOutputProperties: return getPinProperties(false, index, pointer);
				case DECLARE_VST_DEPRECATED(effConnectInput): return connectPin(true, index, value);
				case DECLARE_VST_DEPRECATED(effConnectOutput): return connectPin(false, index, value);
				
				case effSetSampleRate: {
					const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
					if (opt != sampleRate) {
						SYMBIOSIS_ASSERT(opt > 0.0f);
						sampleRate = opt;
						disableAndUnconfigureAudio();
					}
					return 1;
				}
					
				case effSetBlockSize: {
					const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
					SYMBIOSIS_ASSERT(value >= 0);
					if (static_cast<UInt32>(value) != maxBlockSize) {
						maxBlockSize = static_cast<UInt32>(value);
						disableAndUnconfigureAudio();
					}
					return 1;
				}
					
				case DECLARE_VST_DEPRECATED(effCopyProgram): return copyProgram(index);
					
				case DECLARE_VST_DEPRECATED(effSetBlockSizeAndSampleRate): {
					const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
					SYMBIOSIS_ASSERT(value >= 0);
					if (static_cast<UInt32>(value) != maxBlockSize || opt != sampleRate) {
						SYMBIOSIS_ASSERT(opt > 0.0f);
						sampleRate = opt;
						maxBlockSize = static_cast<UInt32>(value);
						disableAndUnconfigureAudio();
					}
					return 1;
				}
					
				case effSetBypass:
					if (plugInInfo->handlesBypass != 0 && plugInInfo->plugInType != SYMBIOSIS_PLUGIN_TYPE_INSTRUMENT) {
						isBypassed = (value != 0);
						plugIn->setBypass(toBool8(isBypassed));	// sticky, out-of-band (SymbiosisBypassPolicy.md)
						return 1;
					} else {
						return 0;	// unsupported => host performs its own hard bypass
					}
					
				case effMainsChanged: {
					const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
					if (value != 0) {
						if (currentAudioState != AUDIO_ENABLED) {
							for (UInt32 channel = 0; channel < inputChannelCount; ++channel) {
								inputChannelConnected[channel]
										= (audioMaster(&effect, DECLARE_VST_DEPRECATED(audioMasterPinConnected)
										, static_cast<VstInt32>(channel), 0, 0, 0.0f) == 0);
							}
							for (UInt32 channel = 0; channel < outputChannelCount; ++channel) {
								outputChannelConnected[channel]
										= (audioMaster(&effect, DECLARE_VST_DEPRECATED(audioMasterPinConnected)
										, static_cast<VstInt32>(channel), 1, 0, 0.0f) == 0);
							}
							if (wantsMidiInput) {
								audioMaster(&effect, DECLARE_VST_DEPRECATED(audioMasterWantMidi), 0, 1, 0, 0.0f);
							}
							configureAndEnableAudio();
						}
					} else {
						disableAudio();
					}
					return 1;
				}
					
				case effGetChunk: return getStateChunk(index != 0, pointer);
				case effSetChunk: return setStateChunk(index != 0, value, pointer);
					
				case effEditGetRect:
					if (!plugInInfo->hasCustomUIView) {
						SYMBIOSIS_ASSERT(0);
						return 0;
					}
					return getEditorRect(pointer);
					
				case effEditOpen:
					if (!plugInInfo->hasCustomUIView || pointer == 0) {
						SYMBIOSIS_ASSERT(0);
						return 0;
					}
					if (!editorOpen) {
						if (!plugIn->openUIView(pointer)) {
							tracePlugInLastError(plugIn.get(), "VST2 openUIView");
							return 0;
						}
						editorOpen = true;
					}
					return 1;
				
				case effEditClose:
					if (!plugInInfo->hasCustomUIView) {
						SYMBIOSIS_ASSERT(0);
						return 0;
					}
					if (editorOpen) {
						plugIn->closeUIView();
						editorOpen = false;
					}
					return 1;
					
				case effProcessEvents: return processEventsFromHost(pointer);
					
				case effString2Parameter: {
					SYMBIOSIS_ASSERT(pointer != 0);
					SYMBIOSIS_ASSERT(index >= 0);
					SYMBIOSIS_ASSERT(static_cast<UInt32>(index) < plugInInfo->parameterCount);
					Float32 normalizedValue = 0.0f;
					const UInt32 parameterIndex = static_cast<UInt32>(index);
					UTF8Z utf8Text[(kVstMaxParamStrLen + 1) * 2];
					copyVstTextToUTF8(reinterpret_cast<const char*>(pointer), kVstMaxParamStrLen, utf8Text, static_cast<UInt32>(sizeof (utf8Text)));
					bool ok = plugIn->convertTextToParameterValue(parameterIndex, utf8Text, &normalizedValue);
					if (!ok) {
						ok = convertTextToParameterValueDefault(plugInInfo->parameters[parameterIndex], utf8Text, &normalizedValue);
					}
					if (ok) {
						plugIn->updateParameter(parameterIndex, normalizedValue);
					}
					return ok ? 1 : 0;
				}
				
				case effGetProgramNameIndexed: {
					SYMBIOSIS_ASSERT(pointer != 0 && index >= 0 && static_cast<UInt32>(index) < plugInInfo->programCount);
					if (pointer == 0) {
						return 0;
					}
					const VstInt32 maxProgramIndex = static_cast<VstInt32>(plugInInfo->programCount - 1u);
					const VstInt32 clampedIndex = std::min(std::max(index, static_cast<VstInt32>(0)), maxProgramIndex);
					const UInt32 programIndex = static_cast<UInt32>(clampedIndex);
					UTF8Z utf8ProgramName[kVstMaxProgNameLen + 1];
					plugIn->getProgramName(programIndex, kVstMaxProgNameLen, utf8ProgramName);
					copyUTF8ToVstText(utf8ProgramName, reinterpret_cast<char*>(pointer), kVstMaxProgNameLen + 1);
					return 1;
				}
					
				default:
					(void)index;
					(void)value;
					(void)pointer;
					(void)opt;
					return 0;
			}
		}
		
		void setParameter(VstInt32 index, float parameter) {
			SYMBIOSIS_ASSERT(index >= 0);
			SYMBIOSIS_ASSERT(static_cast<UInt32>(index) < plugInInfo->parameterCount);
			plugIn->updateParameter(static_cast<UInt32>(index), parameter);
		}

		float getParameter(VstInt32 index) {
			SYMBIOSIS_ASSERT(index >= 0);
			SYMBIOSIS_ASSERT(static_cast<UInt32>(index) < plugInInfo->parameterCount);
			return plugIn->getParameter(static_cast<UInt32>(index));
		}

		void processReplacing(float** inputs, float** outputs, VstInt32 sampleFrames) {
			const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);

			SYMBIOSIS_ASSERT(inputChannelCount == 0 || inputs != 0);
			SYMBIOSIS_ASSERT(outputChannelCount == 0 || outputs != 0);
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(sampleFrames >= 0);
			SYMBIOSIS_ASSERT(maxBlockSize == 0 || static_cast<UInt32>(sampleFrames) <= maxBlockSize);
			
			if (sampleFrames == 0) {
				return;
			}
			
			if (currentAudioState != AUDIO_ENABLED) {
				SYMBIOSIS_ASSERT(0);	// this should have been taken care of by "resume()"
				configureAndEnableAudio();
				SYMBIOSIS_ASSERT(currentAudioState == AUDIO_ENABLED);
			}

			SYMBIOSIS_ASSERT(effect.numInputs == static_cast<VstInt32>(inputChannelCount));
			SYMBIOSIS_ASSERT(effect.numOutputs == static_cast<VstInt32>(outputChannelCount));

		#if SYMBIOSIS_VST2_ENABLE_CHANNEL_POINTER_WORKAROUND
			if (inputChannelCount > 0) {
				SYMBIOSIS_ASSERT(inputs != 0);
				const Float32* lastInputChannel = 0;
				for (UInt32 channel = 0; channel < inputChannelCount; ++channel) {
					const Float32* thisPointer = inputs[channel];
					if (thisPointer == 0) {
						SYMBIOSIS_ASSERT(0);
						thisPointer = lastInputChannel;
					}
					SYMBIOSIS_ASSERT(thisPointer != 0);
					lastInputChannel = thisPointer;
					inputChannelPointers[channel] = thisPointer;
				}
			}
			if (outputChannelCount > 0) {
				SYMBIOSIS_ASSERT(outputs != 0);
				Float32* lastOutputChannel = 0;
				for (UInt32 channel = 0; channel < outputChannelCount; ++channel) {
					float* thisPointer = outputs[channel];
					if (thisPointer == 0) {
						SYMBIOSIS_ASSERT(0);
						thisPointer = lastOutputChannel;
					}
					SYMBIOSIS_ASSERT(thisPointer != 0);
					lastOutputChannel = thisPointer;
					outputChannelPointers[channel] = thisPointer;
				}
			}
		#else
			SYMBIOSIS_ASSERT(inputs != 0);
			SYMBIOSIS_ASSERT(outputs != 0);
			for (UInt32 channel = 0; channel < inputChannelCount; ++channel) {
				SYMBIOSIS_ASSERT(inputs[channel] != 0);
				inputChannelPointers[channel] = inputs[channel];
			}
			for (UInt32 channel = 0; channel < outputChannelCount; ++channel) {
				SYMBIOSIS_ASSERT(outputs[channel] != 0);
				outputChannelPointers[channel] = outputs[channel];
			}
		#endif

			UInt32 inputChannelIndex = 0;
			for (UInt32 bus = 0; bus < inputBusCount; ++bus) {
				const UInt32 busChannelCount = selectedInputBusChannelCounts[bus];
				bool busConnected = false;
				for (UInt32 channel = 0; channel < busChannelCount; ++channel) {
					if (inputChannelConnected[inputChannelIndex + channel]) {
						busConnected = true;
						break;
					}
				}
				inputBusConnected[bus] = toBool8(busConnected);
				inputBusSilent[bus] = toBool8(!busConnected);
				inputChannelIndex += busChannelCount;
			}
			SYMBIOSIS_ASSERT(inputChannelIndex == inputChannelCount);
			UInt32 outputChannelIndex = 0;
			for (UInt32 bus = 0; bus < outputBusCount; ++bus) {
				const UInt32 busChannelCount = selectedOutputBusChannelCounts[bus];
				bool busConnected = false;
				for (UInt32 channel = 0; channel < busChannelCount; ++channel) {
					if (outputChannelConnected[outputChannelIndex + channel]) {
						busConnected = true;
						break;
					}
				}
				outputBusConnected[bus] = toBool8(busConnected);
				outputBusSilent[bus] = 0;
				outputChannelIndex += busChannelCount;
			}
			SYMBIOSIS_ASSERT(outputChannelIndex == outputChannelCount);

			Int64 samplePosition = renderFallbackSamplePosition;
			Double64 tempo = 120.0;
			Double64 ppqPosition = 0.0;
			Bool8 isTransportRunning = 1;
			Bool8 isTransportLooping = 0;
			Double64 loopStartPPQPosition = 0.0;
			Double64 loopEndPPQPosition = 0.0;
			const VstTimeInfo* timeInfo = reinterpret_cast<const VstTimeInfo*>
					(audioMaster(&effect, audioMasterGetTime, 0, kVstPpqPosValid | kVstTempoValid | kVstTransportPlaying | kVstTransportCycleActive
					| kVstCyclePosValid, 0, 0.0f));
			if (timeInfo != 0) {
				samplePosition = static_cast<Int64>(timeInfo->samplePos);
				if ((timeInfo->flags & kVstTempoValid) != 0) {
					tempo = timeInfo->tempo;
				}
				if ((timeInfo->flags & kVstPpqPosValid) != 0) {
					ppqPosition = timeInfo->ppqPos;
				}
				isTransportRunning = toBool8((timeInfo->flags & kVstTransportPlaying) != 0);
				const bool cycleActive = (timeInfo->flags & kVstTransportCycleActive) != 0;
				const bool cyclePosValid = (timeInfo->flags & kVstCyclePosValid) != 0;
				SYMBIOSIS_ASSERT(!cycleActive || cyclePosValid);
				if (cyclePosValid) {
					loopStartPPQPosition = timeInfo->cycleStartPos;
					loopEndPPQPosition = timeInfo->cycleEndPos;
				}
				isTransportLooping = toBool8(cycleActive && cyclePosValid);
			}
			renderFallbackSamplePosition = samplePosition + static_cast<UInt32>(sampleFrames);

			UInt32 currentInputEventCount = 0;
			SYMBIOSIS_ASSERT(pendingInputEventCount == 0 || pendingInputEvents.get() != 0);
			for (UInt32 i = 0; i < pendingInputEventCount; ++i) {
				if (pendingInputEvents[i].offset < static_cast<UInt32>(sampleFrames)) {
					++currentInputEventCount;
				}
				if (i > 0 && pendingInputEvents[i - 1].offset > pendingInputEvents[i].offset) {
					const SymbiosisEvent currentEvent = pendingInputEvents[i];
					const SymbiosisMidiEventData currentData = pendingInputEventData[i];
					UInt32 j = i;
					do {
						pendingInputEvents[j] = pendingInputEvents[j - 1];
						pendingInputEvents[j].data = &pendingInputEventData[j];
						pendingInputEventData[j] = pendingInputEventData[j - 1];
						--j;
					} while (j > 0 && pendingInputEvents[j - 1].offset > currentEvent.offset);
					pendingInputEvents[j] = currentEvent;
					pendingInputEvents[j].data = &pendingInputEventData[j];
					pendingInputEventData[j] = currentData;
				}
			}

			SymbiosisRenderInputArgs inArgs;
			memset(&inArgs, 0, sizeof (inArgs));
			inArgs.structVersion = 1;
			inArgs.bufferSize = static_cast<UInt32>(sampleFrames);
			inArgs.inputBusSilent = inputBusSilent.get();
			inArgs.inputChannels = inputChannelPointers.get();
			inArgs.inputEventCount = currentInputEventCount;
			inArgs.inputEvents = currentInputEventCount == 0 ? 0 : pendingInputEvents.get();
			inArgs.inputBusConnected = inputBusConnected.get();
			inArgs.outputBusConnected = outputBusConnected.get();
			inArgs.isBypassed = toBool8(isBypassed);
			inArgs.isTransportRunning = isTransportRunning;
			inArgs.samplePosition = samplePosition;
			inArgs.tempo = tempo;
			inArgs.ppqPosition = ppqPosition;
			inArgs.isTransportLooping = isTransportLooping;
			inArgs.loopStartPPQPosition = loopStartPPQPosition;
			inArgs.loopEndPPQPosition = loopEndPPQPosition;

			SymbiosisRenderOutputArgs outArgs;
			memset(&outArgs, 0, sizeof (outArgs));
			outArgs.structVersion = 1;
			outArgs.outputBusSilent = outputBusSilent.get();
			outArgs.outputChannels = outputChannelPointers.get();
			outArgs.outputEventCount = 0;
			outArgs.outputEvents = 0;

			{
				const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
				plugIn->renderAudio(&inArgs, &outArgs);
			}
			
			if (createsMidiOutput) {
				SYMBIOSIS_ASSERT(outArgs.outputEventCount <= plugInInfo->maxOutputEventCountPerBlock);
				UInt32 outputMidiEventCount = 0;
				SYMBIOSIS_ASSERT(outArgs.outputEventCount == 0 || outArgs.outputEvents != 0);
				for (UInt32 i = 0; i < outArgs.outputEventCount; ++i) {
					const SymbiosisEvent& outputEvent = outArgs.outputEvents[i];
					SYMBIOSIS_ASSERT(i == 0 || outArgs.outputEvents[i - 1].offset <= outputEvent.offset);
					SYMBIOSIS_ASSERT(outputEvent.type == SYMBIOSIS_EVENT_TYPE_MIDI);
					SYMBIOSIS_ASSERT(outputEvent.data != 0);
					SYMBIOSIS_ASSERT(outputEvent.offset < static_cast<UInt32>(sampleFrames));
					const SymbiosisMidiEventData* midiData = reinterpret_cast<const SymbiosisMidiEventData*>(outputEvent.data);
					VstMidiEvent& event = outputMidiEvents[outputMidiEventCount];
					memset(&event, 0, sizeof (event));
					event.type = kVstMidiType;
					event.byteSize = sizeof (VstMidiEvent);
					event.deltaFrames = static_cast<VstInt32>(outputEvent.offset);
					event.midiData[0] = static_cast<char>(midiData->status);
					event.midiData[1] = static_cast<char>(midiData->data1);
					event.midiData[2] = static_cast<char>(midiData->data2);
					++outputMidiEventCount;
				}
				if (outputMidiEventCount > 0) {
					VstEvents* outEvents = reinterpret_cast<VstEvents*>(outgoingVstEventsBuffer.get());
					outEvents->numEvents = static_cast<VstInt32>(outputMidiEventCount);
					outEvents->reserved = 0;
					audioMaster(&effect, audioMasterProcessEvents, 0, 0, outEvents, 0.0f);
				}
			} else {
				SYMBIOSIS_ASSERT(outArgs.outputEventCount == 0);
				SYMBIOSIS_ASSERT(outArgs.outputEvents == 0);
			}
			SYMBIOSIS_ASSERT(currentInputEventCount <= pendingInputEventCount);
			const UInt32 remainingEventCount = pendingInputEventCount - currentInputEventCount;
			for (UInt32 i = 0; i < remainingEventCount; ++i) {
				pendingInputEvents[i] = pendingInputEvents[currentInputEventCount + i];
				pendingInputEvents[i].offset -= static_cast<UInt32>(sampleFrames);
				pendingInputEvents[i].data = &pendingInputEventData[i];
				pendingInputEventData[i] = pendingInputEventData[currentInputEventCount + i];
			}
			pendingInputEventCount = remainingEventCount;
		}

	#if SYMBIOSIS_VST2_SUPPORT_ACCUMULATING_PROCESS
		void process(float** inputs, float** outputs, VstInt32 sampleFrames) {
			const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);

			SYMBIOSIS_ASSERT(inputChannelCount == 0 || inputs != 0);
			SYMBIOSIS_ASSERT(outputChannelCount == 0 || outputs != 0);
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(sampleFrames >= 0);
			
			if (sampleFrames == 0) {
				return;
			} 
			
			if (currentAudioState != AUDIO_ENABLED) {
				SYMBIOSIS_ASSERT(0);	// this should have been taken care of by "resume()"
				configureAndEnableAudio();
				SYMBIOSIS_ASSERT(currentAudioState == AUDIO_ENABLED);
			}

			SYMBIOSIS_ASSERT(effect.numInputs == static_cast<VstInt32>(inputChannelCount));
			SYMBIOSIS_ASSERT(effect.numOutputs == static_cast<VstInt32>(outputChannelCount));
			SYMBIOSIS_ASSERT(accumulatingProcessBufferFrameCapacity > 0);
			Float32** const scratchPointers = accumulatingProcessChannelPointers.get();
			Float32** const batchInputPointers = scratchPointers;
			Float32** const tempOutputPointers = batchInputPointers + inputChannelCount;
			Float32** const hostOutputBasePointers = tempOutputPointers + outputChannelCount;

			for (UInt32 channel = 0; channel < outputChannelCount; ++channel) {
				tempOutputPointers[channel] = &accumulatingProcessOutputSamples[channel * accumulatingProcessBufferFrameCapacity];
			}

		#if SYMBIOSIS_VST2_ENABLE_CHANNEL_POINTER_WORKAROUND
			const Float32* lastInputChannel = 0;
			for (UInt32 channel = 0; channel < inputChannelCount; ++channel) {
				const Float32* thisPointer = inputs[channel];
				if (thisPointer == 0) {
					SYMBIOSIS_ASSERT(0);
					thisPointer = lastInputChannel;
				}
				SYMBIOSIS_ASSERT(thisPointer != 0);
				lastInputChannel = thisPointer;
				inputChannelPointers[channel] = thisPointer;
			}
			Float32* lastOutputChannel = 0;
			for (UInt32 channel = 0; channel < outputChannelCount; ++channel) {
				Float32* thisPointer = outputs[channel];
				if (thisPointer == 0) {
					SYMBIOSIS_ASSERT(0);
					thisPointer = lastOutputChannel;
				}
				SYMBIOSIS_ASSERT(thisPointer != 0);
				lastOutputChannel = thisPointer;
				hostOutputBasePointers[channel] = thisPointer;
			}
		#else
			for (UInt32 channel = 0; channel < inputChannelCount; ++channel) {
				SYMBIOSIS_ASSERT(inputs[channel] != 0);
				inputChannelPointers[channel] = inputs[channel];
			}
			for (UInt32 channel = 0; channel < outputChannelCount; ++channel) {
				SYMBIOSIS_ASSERT(outputs[channel] != 0);
				hostOutputBasePointers[channel] = outputs[channel];
			}
		#endif

			VstInt32 renderedFrames = 0;
			while (renderedFrames < sampleFrames) {
				const VstInt32 batchFrameCount = static_cast<VstInt32>(std::min<UInt32>(
						static_cast<UInt32>(sampleFrames - renderedFrames),
						accumulatingProcessBufferFrameCapacity));
				for (UInt32 channel = 0; channel < inputChannelCount; ++channel) {
					batchInputPointers[channel] = const_cast<Float32*>(inputChannelPointers[channel] + renderedFrames);
				}
				processReplacing(batchInputPointers, tempOutputPointers, batchFrameCount);

				for (UInt32 channel = 0; channel < outputChannelCount; ++channel) {
					Float32* hostOutput = hostOutputBasePointers[channel] + renderedFrames;
					const Float32* rendered = tempOutputPointers[channel];
					for (VstInt32 i = 0; i < batchFrameCount; ++i) {
						hostOutput[i] += rendered[i];
					}
				}
				renderedFrames += batchFrameCount;
			}
		}
	#endif

		VstIntPtr dispatchHost(VstInt32 opcode, VstInt32 index, VstIntPtr value, void* pointer, float opt) const {
			return audioMaster(const_cast<AEffect*>(&effect), opcode, index, value, pointer, opt);
		}

		virtual ~VST2AdapterInstance() {
			if (editorOpen) {
				SYMBIOSIS_ASSERT(plugIn);
				plugIn->closeUIView();
				editorOpen = false;
			}
			releaseRetainedStateChunk();
			const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
			disableAudio();
		}

	protected:
		// exceptions at vst boundary should never happen, but if they do we trace them *and* fail with an assert
		void handleVstBoundaryException(const UTF8Z* context) {
			SYMBIOSIS_ASSERT(context != 0);
			try {
				throw;
			}
			catch (const std::exception& exception) {
				traceMessage(context, exception.what());
			}
			catch (...) {
				traceMessage(context, "Unknown exception");
			}
			SYMBIOSIS_ASSERT(0);
		}

		static UInt32 chooseBusFormatMask(UInt32 supportedBusFormats) {
			SYMBIOSIS_ASSERT(supportedBusFormats != 0);
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
			if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u) {
				return SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK;
			}
			SYMBIOSIS_ASSERT(0);	// should never happen since supportedBusFormats must have at least one bit set
			return SYMBIOSIS_BUS_FORMAT_STEREO_MASK;
		}

		static VstInt32 arrangementTypeFromMask(UInt32 formatMask) {
			switch (formatMask) {
				case SYMBIOSIS_BUS_FORMAT_MONO_MASK: return kSpeakerArrMono;
				case SYMBIOSIS_BUS_FORMAT_STEREO_MASK: return kSpeakerArrStereo;
				case SYMBIOSIS_BUS_FORMAT_LCR_MASK: return kSpeakerArr30Cine;
				case SYMBIOSIS_BUS_FORMAT_QUAD_MASK: return kSpeakerArr40Music;
				case SYMBIOSIS_BUS_FORMAT_5_0_MASK: return kSpeakerArr50;
				case SYMBIOSIS_BUS_FORMAT_5_1_MASK: return kSpeakerArr51;
				case SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK: return kSpeakerArr60Cine;
				case SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK: return kSpeakerArr61Cine;
				case SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK: return kSpeakerArr70Cine;
				case SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK: return kSpeakerArr71Cine;
				case SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK: return kSpeakerArr71Music;
				case SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK: return kSpeakerArrUserDefined;
				default: break;
			}
			SYMBIOSIS_ASSERT(0);
			return kSpeakerArrUserDefined;
		}

		VstIntPtr canDoResult(const char* canDoText) const {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(canDoText != 0);
			if (strcmp(canDoText, "receiveVstEvents") == 0) return wantsMidiInput ? 1 : -1;
			if (strcmp(canDoText, "receiveVstMidiEvent") == 0) return wantsMidiInput ? 1 : -1;
			if (strcmp(canDoText, "receiveVstTimeInfo") == 0) return 1;
			if (strcmp(canDoText, "sendVstEvents") == 0) return createsMidiOutput ? 1 : -1;
			if (strcmp(canDoText, "sendVstMidiEvent") == 0) return createsMidiOutput ? 1 : -1;
			if (strcmp(canDoText, "bypass") == 0) return ((plugInInfo->handlesBypass != 0 && plugInInfo->plugInType != SYMBIOSIS_PLUGIN_TYPE_INSTRUMENT) ? 1 : 0);
			if (strcmp(canDoText, "sizeWindow") == 0) return plugInInfo->hasCustomUIView != 0 ? 1 : -1;
			return 0;
		}

		void releaseRetainedStateChunk() {
			retainedStateChunkBytes.clear();
		}

		VstIntPtr getStateChunk(bool isPreset, void* pointer) {
			SYMBIOSIS_ASSERT(pointer != 0);
			releaseRetainedStateChunk();
			UInt32 dataSize = 0;
			UByte8* data = 0;
			const bool ok = isPreset
					? plugIn->createProgramSaveState(currentProgram, &dataSize, &data)
					: plugIn->createSaveState(&dataSize, &data);
			if (!ok) {
				tracePlugInLastError(plugIn.get(), isPreset ? "VST2 createProgramSaveState" : "VST2 createSaveState");
				return 0;
			}
			SYMBIOSIS_ASSERT(dataSize == 0 || data != 0);
			retainedStateChunkBytes.resize(STATE_HEADER_SIZE + dataSize);
			UByte8* stateBytes = retainedStateChunkBytes.data();
			encodeLE32(&stateBytes[0], STATE_MAGIC_SYMB);
			encodeLE32(&stateBytes[4], STATE_ADAPTER_VST2);
			encodeLE32(&stateBytes[8], 1u);
			if (dataSize > 0u) {
				memcpy(&stateBytes[STATE_HEADER_SIZE], data, dataSize);
			}
			if (data != 0) {
				plugIn->destroySaveState(data);
			}
			*reinterpret_cast<void**>(pointer) = stateBytes;
			return static_cast<VstIntPtr>(retainedStateChunkBytes.size());
		}

		VstIntPtr setStateChunk(bool isPreset, VstIntPtr dataSize, void* pointer) {
			SYMBIOSIS_ASSERT(dataSize >= 0 && (dataSize == 0 || pointer != 0));
			const UInt32 size = static_cast<UInt32>(dataSize);
			if (size < STATE_HEADER_SIZE) {
				return 0;
			}
			const UByte8* stateBytes = reinterpret_cast<const UByte8*>(pointer);
			SYMBIOSIS_ASSERT(stateBytes != 0);
			if (decodeLE32(&stateBytes[0]) != STATE_MAGIC_SYMB
					|| decodeLE32(&stateBytes[4]) != STATE_ADAPTER_VST2
					|| decodeLE32(&stateBytes[8]) != 1u) {
				return 0;
			}
			const UInt32 payloadSize = size - STATE_HEADER_SIZE;
			const UByte8* data = (payloadSize > 0u ? &stateBytes[STATE_HEADER_SIZE] : 0);
			const bool ok = isPreset
					? plugIn->loadProgramState(currentProgram, payloadSize, data)
					: plugIn->loadState(payloadSize, data);
			if (!ok) {
				tracePlugInLastError(plugIn.get(), isPreset ? "VST2 loadProgramState" : "VST2 loadState");
			}
			return ok ? 1 : 0;
		}

		VstIntPtr copyProgram(VstInt32 destinationProgram) {
			SYMBIOSIS_ASSERT(destinationProgram >= 0 && static_cast<UInt32>(destinationProgram) < plugInInfo->programCount);
			const UInt32 destination = static_cast<UInt32>(destinationProgram);
			if (destination == currentProgram) {
				return 1;
			}
			UInt32 dataSize = 0;
			UByte8* data = 0;
			if (!plugIn->createProgramSaveState(currentProgram, &dataSize, &data)) {
				tracePlugInLastError(plugIn.get(), "VST2 createProgramSaveState");
				return 0;
			}
			SYMBIOSIS_ASSERT(dataSize == 0 || data != 0);
			bool ok = false;
			try {
				ok = plugIn->loadProgramState(destination, dataSize, data);
			}
			catch (...) {
				if (data != 0) {
					plugIn->destroySaveState(data);
				}
				throw;
			}
			if (data != 0) {
				plugIn->destroySaveState(data);
			}
			if (!ok) {
				tracePlugInLastError(plugIn.get(), "VST2 loadProgramState");
			}
			return ok ? 1 : 0;
		}

		VstIntPtr getEditorRect(void* pointer) {
			SYMBIOSIS_ASSERT(pointer != 0);
			SYMBIOSIS_ASSERT(plugInInfo->hasCustomUIView);
			UInt32 width = 0;
			UInt32 height = 0;
			if (!plugIn->getUIViewSize(&width, &height)) {
				tracePlugInLastError(plugIn.get(), "VST2 getUIViewSize");
				return 0;
			}
			editorRect.left = 0;
			editorRect.top = 0;
			editorRect.right = static_cast<VstInt16>(std::min<UInt32>(width, 32767));
			editorRect.bottom = static_cast<VstInt16>(std::min<UInt32>(height, 32767));
			*reinterpret_cast<ERect**>(pointer) = &editorRect;
			return 1;
		}

		void configureAndEnableAudio() {										// This function assumes surrounding lock on audioGroupMutex!
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (currentAudioState == AUDIO_UNCONFIGURED) {

			#if SYMBIOSIS_VST2_SUPPORT_ACCUMULATING_PROCESS
				const UInt32 bufferFrameCapacity
					= (maxBlockSize > 0 ? maxBlockSize : SYMBIOSIS_VST2_ACCUMULATING_PROCESS_FALLBACK_BLOCK_SIZE);
				accumulatingProcessBufferFrameCapacity = bufferFrameCapacity;
				const UInt32 scratchPointerCount = inputChannelCount + outputChannelCount + outputChannelCount;
				accumulatingProcessChannelPointers.reset(scratchPointerCount > 0 ? new Float32*[scratchPointerCount] : 0);
				if (outputChannelCount > 0) {
					accumulatingProcessOutputSamples.reset(new Float32[outputChannelCount * bufferFrameCapacity]);
				} else {
					accumulatingProcessOutputSamples.reset();
				}
			#endif

				SymbiosisConfigureAudioInputArgs inArgs;
				memset(&inArgs, 0, sizeof (inArgs));
				inArgs.structVersion = 1;
				inArgs.sampleRate = sampleRate;
				inArgs.maxBufferSize = maxBlockSize;
				SYMBIOSIS_ASSERT(effect.numInputs == static_cast<VstInt32>(inputChannelCount));
				SYMBIOSIS_ASSERT(effect.numOutputs == static_cast<VstInt32>(outputChannelCount));
				inArgs.inputChannelCount = inputChannelCount;
				inArgs.outputChannelCount = outputChannelCount;
				inArgs.inputBusFormats = selectedInputBusFormatValues.get();
				inArgs.inputBusChannelCounts = selectedInputBusChannelCounts.get();
				inArgs.outputBusFormats = selectedOutputBusFormatValues.get();
				inArgs.outputBusChannelCounts = selectedOutputBusChannelCounts.get();

				SymbiosisConfigureAudioOutputArgs outArgs;
				memset(&outArgs, 0, sizeof (outArgs));
				outArgs.structVersion = 1;
				plugIn->configureAudio(&inArgs, &outArgs);
				effect.initialDelay = static_cast<VstInt32>(outArgs.latencySamples);
				/*
					VST2 effGetTailSize: 1 = explicit "no tail", N>1 = tail samples, 0 = host default.
					Symbiosis tailSamples: 0 = none, >0 = samples, -1 = infinite (no VST2 representation -> host default).
				*/
				const Int32 tailSamples = outArgs.tailSamples;
				vstTailSize = (tailSamples > 0 ? static_cast<VstInt32>(tailSamples) : (tailSamples == 0 ? 1 : 0));
				currentAudioState = AUDIO_CONFIGURED;
			}
			SYMBIOSIS_ASSERT(pendingInputEventCount == 0);
			SYMBIOSIS_ASSERT(currentAudioState == AUDIO_CONFIGURED);
			plugIn->enableAudio();
			currentAudioState = AUDIO_ENABLED;
		}

		void disableAudio() {													// This function assumes surrounding lock on audioGroupMutex!
			if (currentAudioState == AUDIO_ENABLED) {
				pendingInputEventCount = 0;
				plugIn->disableAudio();
				currentAudioState = AUDIO_CONFIGURED;
			}
		}
		
		void disableAndUnconfigureAudio() {										// This function assumes surrounding lock on audioGroupMutex!
			if (currentAudioState != AUDIO_UNCONFIGURED) {
				disableAudio();
				SYMBIOSIS_ASSERT(currentAudioState == AUDIO_CONFIGURED);
			#if SYMBIOSIS_VST2_SUPPORT_ACCUMULATING_PROCESS
				accumulatingProcessChannelPointers.reset();
				accumulatingProcessOutputSamples.reset();
				accumulatingProcessBufferFrameCapacity = 0;
			#endif
				currentAudioState = AUDIO_UNCONFIGURED;
			}
		}

		VstIntPtr connectPin(bool isInput, VstInt32 index, VstIntPtr value) {
			const UInt32 channelIndex = static_cast<UInt32>(index);
			const UInt32 totalChannelCount = isInput ? inputChannelCount : outputChannelCount;
			SYMBIOSIS_ASSERT(channelIndex < totalChannelCount);
			if (channelIndex >= totalChannelCount) {
				return 0;
			}
			bool* channelStates = (isInput ? inputChannelConnected.get() : outputChannelConnected.get());
			SYMBIOSIS_ASSERT(channelStates != 0);
			channelStates[channelIndex] = (value != 0);
			return 1;
		}

		VstIntPtr processEventsFromHost(void* pointer) {
			SYMBIOSIS_ASSERT(pointer != 0);
			const VstEvents* events = reinterpret_cast<const VstEvents*>(pointer);
			if (!wantsMidiInput || events->numEvents <= 0) {
				return 1;
			}
			for (VstInt32 i = 0; i < events->numEvents; ++i) {
				if (pendingInputEventCount >= plugInInfo->maxInputEventCountPerBlock) {
					break;
				}
				const VstEvent* event = events->events[i];
				if (event != 0 && event->type == kVstMidiType) {
					const VstMidiEvent* midiEvent = reinterpret_cast<const VstMidiEvent*>(event);
					SymbiosisEvent& pendingEvent = pendingInputEvents[pendingInputEventCount];
					pendingEvent.offset = static_cast<UInt32>(std::max<VstInt32>(0, midiEvent->deltaFrames));
					pendingEvent.type = SYMBIOSIS_EVENT_TYPE_MIDI;
					SymbiosisMidiEventData* eventData = &pendingInputEventData[pendingInputEventCount];
					pendingEvent.data = eventData;
					eventData->status = static_cast<UByte8>(midiEvent->midiData[0]);
					eventData->data1 = static_cast<UByte8>(midiEvent->midiData[1]);
					eventData->data2 = static_cast<UByte8>(midiEvent->midiData[2]);
					++pendingInputEventCount;
				}
			}
			return 1;
		}

		VstIntPtr getPinProperties(bool isInput, VstInt32 index, void* pointer) {
			SYMBIOSIS_ASSERT(pointer != 0 && index >= 0);
			VstPinProperties* pin = reinterpret_cast<VstPinProperties*>(pointer);
			SYMBIOSIS_ASSERT(pin != 0);
			const UInt32 totalChannelCount = isInput ? inputChannelCount : outputChannelCount;
			const UInt32 channelIndex = static_cast<UInt32>(index);
			SYMBIOSIS_ASSERT(channelIndex < totalChannelCount);
			if (channelIndex >= totalChannelCount) {
				return 0;
			}
			const VstPinProperties* source = (isInput ? &inputPinProperties[channelIndex] : &outputPinProperties[channelIndex]);
			*pin = *source;

			const bool* channelStates = (isInput ? inputChannelConnected.get() : outputChannelConnected.get());
			SYMBIOSIS_ASSERT(channelStates != 0);
			pin->flags = (pin->flags & ~kVstPinIsActive) | ((channelStates[channelIndex]) ? kVstPinIsActive : 0);
			return 1;
		}

		static VST2AdapterInstance* fromEffect(AEffect* effect) {
			SYMBIOSIS_ASSERT(effect != 0);
			SYMBIOSIS_ASSERT(effect->object != 0);
			return reinterpret_cast<VST2AdapterInstance*>(effect->object);
		}

		static VstIntPtr VSTCALLBACK dispatcherThunk(AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* pointer, float opt) {
			VST2AdapterInstance* self = fromEffect(effect);
			try {
				return self->dispatcher(opcode, index, value, pointer, opt);
			}
			catch (...) {
				self->handleVstBoundaryException("VST2 dispatcher");
				return 0;
			}
		}

		static void VSTCALLBACK setParameterThunk(AEffect* effect, VstInt32 index, float parameter) {
			VST2AdapterInstance* self = fromEffect(effect);
			try {
				self->setParameter(index, parameter);
			}
			catch (...) {
				self->handleVstBoundaryException("VST2 setParameter");
			}
		}

		static float VSTCALLBACK getParameterThunk(AEffect* effect, VstInt32 index) {
			VST2AdapterInstance* self = fromEffect(effect);
			try {
				return self->getParameter(index);
			}
			catch (...) {
				self->handleVstBoundaryException("VST2 getParameter");
				return 0.0f;
			}
		}

		static void VSTCALLBACK processReplacingThunk(AEffect* effect, float** inputs, float** outputs, VstInt32 sampleFrames) {
			VST2AdapterInstance* self = fromEffect(effect);
			try {
				self->processReplacing(inputs, outputs, sampleFrames);
			}
			catch (...) {
				self->handleVstBoundaryException("VST2 processReplacing");
			}
		}
	#if SYMBIOSIS_VST2_SUPPORT_ACCUMULATING_PROCESS
		static void VSTCALLBACK processThunk(AEffect* effect, float** inputs, float** outputs, VstInt32 sampleFrames) {
			VST2AdapterInstance* self = fromEffect(effect);
			try {
				self->process(inputs, outputs, sampleFrames);
			}
			catch (...) {
				self->handleVstBoundaryException("VST2 process");
			}
		}
	#endif
		
		enum AudioLifecycleState {
			AUDIO_UNCONFIGURED,
			AUDIO_CONFIGURED,
			AUDIO_ENABLED
		};
		AEffect effect;
		audioMasterCallback audioMaster;
		VST2AdapterHost host;
		SymbiosisLoaderInfo loaderInfo;
		char hostVendor[(kVstMaxVendorStrLen + 1) * 2];
		char hostName[(kVstMaxProductStrLen + 1) * 2];
		std::unique_ptr<HostedFactory> factory;
		std::unique_ptr<PlugInInterface> plugIn;	// plugIn declared after factory so it is destroyed before
		const SymbiosisPlugInInfo* plugInInfo;
		AudioLifecycleState currentAudioState;
		std::recursive_mutex audioGroupMutex;		// VST2 is not thread safe in general, and Symbiosis ABI guarantees non overlapping calls to audio functions. This mutex ensures this, but with a well-behaved host it will never block.
		UInt32 currentProgram;
		Float32 sampleRate;
		UInt32 maxBlockSize;
		VstInt32 vstTailSize;
		bool isBypassed;
		bool wantsMidiInput;
		bool createsMidiOutput;
		UInt32 inputBusCount;
		UInt32 outputBusCount;
		UInt32 inputChannelCount;
		UInt32 outputChannelCount;
		std::unique_ptr<UInt32[]> selectedInputBusFormatValues;
		std::unique_ptr<UInt32[]> selectedInputBusChannelCounts;
		std::unique_ptr<UInt32[]> selectedOutputBusFormatValues;
		std::unique_ptr<UInt32[]> selectedOutputBusChannelCounts;
		std::unique_ptr<VstPinProperties[]> inputPinProperties;
		std::unique_ptr<VstPinProperties[]> outputPinProperties;
		std::unique_ptr<Bool8[]> inputBusConnected;
		std::unique_ptr<Bool8[]> outputBusConnected;
		std::unique_ptr<Bool8[]> inputBusSilent;
		std::unique_ptr<Bool8[]> outputBusSilent;
		std::unique_ptr<bool[]> inputChannelConnected;
		std::unique_ptr<bool[]> outputChannelConnected;
		std::unique_ptr<const Float32*[]> inputChannelPointers;
		std::unique_ptr<Float32*[]> outputChannelPointers;
		std::unique_ptr<SymbiosisEvent[]> pendingInputEvents;
		std::unique_ptr<SymbiosisMidiEventData[]> pendingInputEventData;
		UInt32 pendingInputEventCount;
		std::unique_ptr<VstMidiEvent[]> outputMidiEvents;
		std::unique_ptr<UByte8[]> outgoingVstEventsBuffer;
	#if SYMBIOSIS_VST2_SUPPORT_ACCUMULATING_PROCESS
		std::unique_ptr<Float32*[]> accumulatingProcessChannelPointers;
		std::unique_ptr<Float32[]> accumulatingProcessOutputSamples;
		UInt32 accumulatingProcessBufferFrameCapacity = 0;
	#endif
		Int64 renderFallbackSamplePosition;
		ERect editorRect;
		bool editorOpen;
		std::vector<UByte8> retainedStateChunkBytes;
};

void VST2AdapterHost::updateDisplay() {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->dispatchHost(audioMasterUpdateDisplay, 0, 0, 0, 0.0f);
}

void VST2AdapterHost::beginEdit(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->dispatchHost(audioMasterBeginEdit, static_cast<VstInt32>(parameterNumber), 0, 0, 0.0f);
}

void VST2AdapterHost::writeParameter(UInt32 parameterNumber, Float32 normalizedValue) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->dispatchHost(audioMasterAutomate, static_cast<VstInt32>(parameterNumber), 0, 0, normalizedValue);
	// Round-trip contract: VST2 has no host-side guarantee that audioMasterAutomate comes back as a
	// parameter set, so deliver the incoming updateParameter ourselves (synchronously). A host echo
	// that also arrives is an idempotent duplicate. See docs/Symbiosis_Parameter_Policies.md (3.1).
	owner->setParameter(static_cast<VstInt32>(parameterNumber), normalizedValue);
}

void VST2AdapterHost::endEdit(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->dispatchHost(audioMasterEndEdit, static_cast<VstInt32>(parameterNumber), 0, 0, 0.0f);
}

bool VST2AdapterHost::requestResize(UInt32 width, UInt32 height) {
	SYMBIOSIS_ASSERT(owner != 0);
	return owner->dispatchHost(audioMasterSizeWindow, static_cast<VstInt32>(width), static_cast<VstIntPtr>(height), 0, 0.0f) != 0;
}

const void* VST2AdapterHost::queryExtension(UInt32 vendorId, UInt32 interfaceId) {
	(void)vendorId;
	(void)interfaceId;
	return 0;
}

} // namespace

VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback audioMaster);
VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback audioMaster) {
	SYMBIOSIS_ASSERT(runSelfTest());
	SYMBIOSIS_ASSERT(audioMaster != 0);
	try {
		return VST2AdapterInstance::createAdapterEffect(audioMaster);
	}
	catch (const std::exception& exception) {
		traceMessage("VST2 VSTPluginMain", exception.what());
		return 0;
	}
	catch (...) {
		traceMessage("VST2 VSTPluginMain", "Unknown exception");
		return 0;
	}
}

#if defined(_MSC_VER)
#if !defined(_WIN64)
VST_EXPORT int main(audioMasterCallback audioMaster) {
	return reinterpret_cast<int>(VSTPluginMain(audioMaster));
}
#endif
#elif defined(__APPLE__)
VST_EXPORT void* main_macho(audioMasterCallback audioMaster);
VST_EXPORT void* main_macho(audioMasterCallback audioMaster) {
	return VSTPluginMain(audioMaster);
}
#endif
