#include "SymbiosisCpp.h"
#include "SymbiosisTests.h"

// You need an include path to the VST3 SDK root, at least for this specific .cpp file

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include <algorithm>
#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <memory>
#include <mutex>
#include <stdio.h>
#include <string>
#include <string.h>
#include <vector>

#ifndef SYMBIOSIS_VST3_SDK_SELF_CONTAINED
#define SYMBIOSIS_VST3_SDK_SELF_CONTAINED 1
#endif

#ifndef SYMBIOSIS_VST3_ENABLE_LIVE_SETACTIVE_WORKAROUND
#define SYMBIOSIS_VST3_ENABLE_LIVE_SETACTIVE_WORKAROUND 1
#endif

#ifndef SYMBIOSIS_VST3_ENABLE_SETUPPROCESSING_RECONFIG_WHEN_ACTIVE
#define SYMBIOSIS_VST3_ENABLE_SETUPPROCESSING_RECONFIG_WHEN_ACTIVE 1
#endif

#if (SYMBIOSIS_VST3_SDK_SELF_CONTAINED)
#include "public.sdk/source/vst/vstsinglecomponenteffect.cpp"

#include "base/source/baseiids.cpp"
#include "public.sdk/source/common/commoniids.cpp"
#include "pluginterfaces/base/coreiids.cpp"
#include "base/source/fbuffer.cpp"
#include "base/thread/source/fcondition.cpp"
#include "base/source/fdebug.cpp"
#include "base/thread/source/flock.cpp"
#include "base/source/fobject.cpp"
#include "base/source/fstreamer.cpp"
#include "base/source/fstring.cpp"
#include "pluginterfaces/base/funknown.cpp"
#include "public.sdk/source/main/pluginfactory.cpp"
#include "public.sdk/source/common/pluginview.cpp"
#include "base/source/updatehandler.cpp"
#include "pluginterfaces/base/ustring.cpp"
#include "public.sdk/source/vst/utility/vst2persistence.cpp"
#include "public.sdk/source/vst/vstaudioeffect.cpp"
#include "public.sdk/source/vst/vstbus.cpp"
#include "public.sdk/source/vst/vstcomponent.cpp"
#include "public.sdk/source/vst/vstcomponentbase.cpp"
#include "public.sdk/source/vst/vstinitiids.cpp"
#include "public.sdk/source/vst/vstparameters.cpp"
#endif // (SYMBIOSIS_VST3_SDK_SELF_CONTAINED)

extern "C" void SYMBIOSIS_CALL symbiosisAdapterTrace(const UTF8Z* text);

using namespace symbiosis;

namespace {

static const UInt32 HIDDEN_PARAM_CLASS_MASK = 0xFF000000u;
static const UInt32 HIDDEN_CC_PARAM_CLASS = 0x61000000u;
static const UInt32 HIDDEN_PARAM_CHANNEL_SHIFT = 20u;
static const UInt32 HIDDEN_PARAM_CC_SHIFT = 12u;
static const UInt32 HIDDEN_PARAM_CHANNEL_MASK = 0x0Fu;
static const UInt32 HIDDEN_PARAM_CC_MASK = 0x7Fu;

static const Steinberg::Vst::ParamID PROGRAM_LIST_ID = static_cast<Steinberg::Vst::ParamID>('pgch');
static const Steinberg::Vst::ParamID BYPASS_ID = static_cast<Steinberg::Vst::ParamID>('bypa');
static const UInt32 VST3_STATE_HEADER_SIZE = 16u;
static const UInt32 STATE_MAGIC_SYMB = static_cast<UInt32>('S')
		| (static_cast<UInt32>('y') << 8)
		| (static_cast<UInt32>('m') << 16)
		| (static_cast<UInt32>('b') << 24);
static const UInt32 STATE_ADAPTER_VST3 = static_cast<UInt32>('V')
		| (static_cast<UInt32>('S') << 8)
		| (static_cast<UInt32>('T') << 16)
		| (static_cast<UInt32>('3') << 24);

static bool readRemainingStream(Steinberg::IBStream* stream, std::vector<UByte8>* outBytes) {
	SYMBIOSIS_ASSERT(stream != 0);
	SYMBIOSIS_ASSERT(outBytes != 0);
	Steinberg::int64 start = 0;
	if (stream->tell(&start) == Steinberg::kResultOk) {
		Steinberg::int64 end = 0;
		if (stream->seek(0, Steinberg::IBStream::kIBSeekEnd, &end) == Steinberg::kResultOk) {
			if (end < start || static_cast<UInt64>(end - start) > 0xFFFFFFFFu) {
				return false;
			}
			const UInt32 payloadSize = static_cast<UInt32>(end - start);
			outBytes->resize(payloadSize);
			if (stream->seek(start, Steinberg::IBStream::kIBSeekSet, 0) != Steinberg::kResultOk) {
				return false;
			}
			if (payloadSize == 0u) {
				return true;
			}
			Steinberg::int32 readCount = 0;
			return stream->read(outBytes->data(), static_cast<Steinberg::int32>(payloadSize), &readCount) == Steinberg::kResultOk
					&& static_cast<UInt32>(readCount) == payloadSize;
		}
		if (stream->seek(start, Steinberg::IBStream::kIBSeekSet, 0) != Steinberg::kResultOk) {
			return false;
		}
	}
	outBytes->clear();
	UByte8 chunk[4096];
	for (;;) {
		Steinberg::int32 readCount = 0;
		const Steinberg::tresult result = stream->read(chunk, static_cast<Steinberg::int32>(sizeof(chunk)), &readCount);
		if (result != Steinberg::kResultOk && readCount <= 0) {
			break;
		}
		if (readCount < 0) {
			return false;
		}
		if (readCount > 0) {
			const UInt32 currentSize = static_cast<UInt32>(outBytes->size());
			const UInt32 additionalSize = static_cast<UInt32>(readCount);
			if (currentSize > 0xFFFFFFFFu - additionalSize) {
				return false;
			}
			outBytes->resize(currentSize + additionalSize);
			memcpy(outBytes->data() + currentSize, chunk, additionalSize);
		}
		if (readCount < static_cast<Steinberg::int32>(sizeof(chunk))) {
			break;
		}
	}
	return true;
}

static Steinberg::Vst::TChar* utf8ToVstString128(const UTF8Z* source, Steinberg::Vst::String128 target) {
	SYMBIOSIS_ASSERT(source != 0);
	UTF8Z truncatedUTF8[512];
	truncatedUTF8[0] = 0;
	appendText(truncatedUTF8, 511u, 0u, source);
	const UInt32 utf8Size = static_cast<UInt32>(strlen(truncatedUTF8));
	uint16_t utf16Scratch[512];
	UInt32 utf16Size = convertUTF8ToUTF16(utf8Size, truncatedUTF8, utf16Scratch);
	SYMBIOSIS_ASSERT(utf16Size <= 511u);
	utf16Size = std::min<UInt32>(utf16Size, 127u);
	if (utf16Size > 0u && utf16Scratch[utf16Size - 1u] >= 0xD800u && utf16Scratch[utf16Size - 1u] < 0xDC00u) {
		--utf16Size;
	}
	memcpy(target, utf16Scratch, utf16Size * sizeof(Steinberg::Vst::TChar));
	target[utf16Size] = 0;
	return target;
}

static void vstString128ToUTF8(const Steinberg::Vst::TChar* source, UTF8Z target[512]) {
	SYMBIOSIS_ASSERT(source != 0);
	SYMBIOSIS_ASSERT(target != 0);
	UInt32 utf16Size = 0u;
	while (utf16Size < 127u && source[utf16Size] != 0) {
		++utf16Size;
	}
	SYMBIOSIS_ASSERT(sizeof (*source) == sizeof (uint16_t));
	const UInt32 converted = convertUTF16ToUTF8(utf16Size, reinterpret_cast<const uint16_t*>(source), target);
	SYMBIOSIS_ASSERT(converted + 1u <= 512u);
	target[converted] = 0;
}

static UInt32 chooseDefaultChannelCountFromSupportedMask(UInt32 supportedBusFormats) {
	SYMBIOSIS_ASSERT(supportedBusFormats != 0);
	static const UInt32 ORDERED_MASKS[] = {
		SYMBIOSIS_BUS_FORMAT_STEREO_MASK,
		SYMBIOSIS_BUS_FORMAT_MONO_MASK,
		SYMBIOSIS_BUS_FORMAT_LCR_MASK,
		SYMBIOSIS_BUS_FORMAT_QUAD_MASK,
		SYMBIOSIS_BUS_FORMAT_5_0_MASK,
		SYMBIOSIS_BUS_FORMAT_5_1_MASK,
		SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK,
		SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK,
		SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK
	};
	for (UInt32 i = 0; i < sizeof(ORDERED_MASKS) / sizeof(ORDERED_MASKS[0]); ++i) {
		const UInt32 mask = ORDERED_MASKS[i];
		if ((supportedBusFormats & mask) != 0u) {
			return busFormatMaskToChannelCount(mask);
		}
	}
	SYMBIOSIS_ASSERT(0);
	return 2;
}

static Steinberg::Vst::SpeakerArrangement chooseDefaultArrangement(UInt32 supportedBusFormats) {
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_STEREO_MASK) != 0) return Steinberg::Vst::SpeakerArr::kStereo;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_MONO_MASK) != 0) return Steinberg::Vst::SpeakerArr::kMono;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_LCR_MASK) != 0) return Steinberg::Vst::SpeakerArr::k30Cine;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_QUAD_MASK) != 0) return Steinberg::Vst::SpeakerArr::k40Music;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_5_0_MASK) != 0) return Steinberg::Vst::SpeakerArr::k50;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_5_1_MASK) != 0) return Steinberg::Vst::SpeakerArr::k51;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK) != 0) return Steinberg::Vst::SpeakerArr::k60Cine;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK) != 0) return Steinberg::Vst::SpeakerArr::k61Cine;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK) != 0) return Steinberg::Vst::SpeakerArr::k70Cine;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK) != 0) return Steinberg::Vst::SpeakerArr::k71Cine;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK) != 0) return Steinberg::Vst::SpeakerArr::k71Music;
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0) return Steinberg::Vst::SpeakerArr::kStereo;
	SYMBIOSIS_ASSERT(0);
	return Steinberg::Vst::SpeakerArr::kStereo;
}

static bool resolveFormatFromArrangement(UInt32 supportedBusFormats, Steinberg::Vst::SpeakerArrangement arrangement
		, UInt32* outFormatMask, UInt32* outChannelCount) {
	SYMBIOSIS_ASSERT(outFormatMask != 0);
	SYMBIOSIS_ASSERT(outChannelCount != 0);
	const UInt32 channelCount = static_cast<UInt32>(Steinberg::Vst::SpeakerArr::getChannelCount(arrangement));
	if (channelCount == 0u) {
		return false;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_MONO_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::kMono) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_MONO_MASK;
		*outChannelCount = 1;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_STEREO_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::kStereo) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_STEREO_MASK;
		*outChannelCount = 2;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_LCR_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::k30Cine) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_LCR_MASK;
		*outChannelCount = 3;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_QUAD_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::k40Music) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_QUAD_MASK;
		*outChannelCount = 4;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_5_0_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::k50) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_5_0_MASK;
		*outChannelCount = 5;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_5_1_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::k51) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_5_1_MASK;
		*outChannelCount = 6;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::k60Cine) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK;
		*outChannelCount = 6;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::k61Cine) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK;
		*outChannelCount = 7;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::k70Cine) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK;
		*outChannelCount = 7;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::k71Cine) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK;
		*outChannelCount = 8;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK) != 0 && arrangement == Steinberg::Vst::SpeakerArr::k71Music) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK;
		*outChannelCount = 8;
		return true;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0) {
		*outFormatMask = SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK;
		*outChannelCount = channelCount;
		return true;
	}
	return false;
}

struct VST3FactoryMetadata {
	UInt32 plugInIndex;
	Steinberg::FUID classId;
	std::string className;
	std::string subCategories;
	std::string version;
	Steinberg::uint32 classFlags;
};

struct VST3FactoryIdentity {
	UTF8Z vendor[Steinberg::PFactoryInfo::kNameSize];
	UTF8Z url[Steinberg::PFactoryInfo::kURLSize];
	UTF8Z email[Steinberg::PFactoryInfo::kEmailSize];
};

static std::string mapVST3SubCategories(UInt32 plugInType, UInt32 plugInCategories) {
	std::string text;
	const bool isInstrument = (plugInType == SYMBIOSIS_PLUGIN_TYPE_INSTRUMENT);
	text = isInstrument ? "Instrument" : "Fx";

	static const UTF8Z* MAPPINGS[] = {
		"Synth", "Sampler", "Drum", "Piano", "Guitar", "Vocals", "Tools", "Analyzer", "ChannelStrip",
		"Delay", "Reverb", "EQ", "Dynamics", "PitchShift", "Distortion", "Modulation", "Filter",
		"Spatial", "Restoration"
	};
	UInt32 mask = 0x01u;
	for (const UTF8Z* token : MAPPINGS) {
		if ((plugInCategories & mask) != 0u) {
			text += "|";
			text += token;
		}
		mask <<= 1u;
	}
	return text;
}

static std::string versionHexToString(UInt32 versionHex) {
	UTF8Z text[32];
	text[0] = 0;
	UInt32 offset = 0;
	offset = appendUIntToString(text, 31, offset, (versionHex >> 16) & 0xFFu);
	offset = appendText(text, 31, offset, ".");
	offset = appendUIntToString(text, 31, offset, (versionHex >> 8) & 0xFFu);
	offset = appendText(text, 31, offset, ".");
	appendUIntToString(text, 31, offset, versionHex & 0xFFu);
	return std::string(text);
}

static void convertVST2UID_To_FUID(Steinberg::FUID& newOne, Steinberg::int32 myVST2UID_4Chars
		, const char* pluginName, bool forControllerUID = false) {
	SYMBIOSIS_ASSERT(pluginName != 0);
	char uidString[33];
	uidString[0] = 0;

	Steinberg::int32 vstfxid = 0;
	if (forControllerUID) {
		vstfxid = (('V' << 16) | ('S' << 8) | 'E');
	} else {
		vstfxid = (('V' << 16) | ('S' << 8) | 'T');
	}

	char vstfxidStr[7] = { 0 };
	snprintf(vstfxidStr, sizeof(vstfxidStr), "%06X", static_cast<unsigned int>(vstfxid));

	char uidStr[9] = { 0 };
	snprintf(uidStr, sizeof(uidStr), "%08X", static_cast<unsigned int>(myVST2UID_4Chars));

	strcat(uidString, vstfxidStr);
	strcat(uidString, uidStr);

	char nameidStr[3] = { 0 };
	const int len = static_cast<int>(strlen(pluginName));
	for (int i = 0; i <= 8; ++i) {
		const unsigned char c = (i < len ? static_cast<unsigned char>(pluginName[i]) : 0);
		snprintf(nameidStr, sizeof(nameidStr), "%02X", static_cast<unsigned int>(tolower(c)));
		strcat(uidString, nameidStr);
	}
	newOne.fromString(uidString);
}

static Steinberg::FUID makeFactoryClassId(UInt32 plugInId, const UTF8Z* displayName) {
	SYMBIOSIS_ASSERT(displayName != 0);
	Steinberg::FUID classId;
	convertVST2UID_To_FUID(classId, static_cast<Steinberg::int32>(plugInId), displayName, false);
	return classId;
}

static bool queryVST3FactoryMetadata(VST3FactoryIdentity& identity, std::vector<VST3FactoryMetadata>& values) {
	identity.email[0] = 0;
	values.clear();

	SymbiosisLoaderInfo loaderInfo;
	memset(&loaderInfo, 0, sizeof(loaderInfo));
	loaderInfo.structVersion = 1;
	loaderInfo.maxSymbiosisVersion = 1;
	loaderInfo.adapterFormat = "VST3";

	UTF8Z errorText[1024];
	errorText[0] = 0;
	const SymbiosisFactoryInterface* factoryApi = 0;
	SymbiosisFactory* factory = symbiosisCreateFactory(&loaderInfo, &factoryApi, 1023, errorText);
	if (factory == 0 || factoryApi == 0) {
		SYMBIOSIS_ASSERT(false);
		traceMessage("VST3 GetPluginFactory", (errorText[0] != 0 ? errorText : "symbiosisCreateFactory failed"));
		return false;
	}

	const UInt32 plugInCount = factoryApi->getPlugInCount(factory);
	if (plugInCount == 0u) {
		factoryApi->destroy(factory);
		return false;
	}

	const SymbiosisPlugInInfo* firstPlugInInfo = factoryApi->getPlugInInfo(factory, 0u);
	SYMBIOSIS_ASSERT(firstPlugInInfo != 0);
	SYMBIOSIS_ASSERT(firstPlugInInfo->displayVendor != 0);
	SYMBIOSIS_ASSERT(firstPlugInInfo->vendorUrl != 0);
	strncpy(identity.vendor, firstPlugInInfo->displayVendor, sizeof(identity.vendor) - 1u);
	identity.vendor[sizeof(identity.vendor) - 1u] = 0;
	strncpy(identity.url, firstPlugInInfo->vendorUrl, sizeof(identity.url) - 1u);
	identity.url[sizeof(identity.url) - 1u] = 0;

	values.reserve(plugInCount);
	for (UInt32 plugInIndex = 0u; plugInIndex < plugInCount; ++plugInIndex) {
		const SymbiosisPlugInInfo* plugInInfo = factoryApi->getPlugInInfo(factory, plugInIndex);
		SYMBIOSIS_ASSERT(plugInInfo != 0);
		SYMBIOSIS_ASSERT(plugInInfo->displayName != 0);
		SYMBIOSIS_ASSERT(plugInInfo->displayName[0] != 0);
		VST3FactoryMetadata metadata;
		metadata.plugInIndex = plugInIndex;
		metadata.classId = makeFactoryClassId(plugInInfo->plugInId, plugInInfo->displayName);
		metadata.className = plugInInfo->displayName;
		metadata.subCategories = mapVST3SubCategories(plugInInfo->plugInType, plugInInfo->plugInCategories);
		metadata.version = versionHexToString(plugInInfo->versionHex);
		metadata.classFlags = Steinberg::Vst::kSimpleModeSupported;
		values.push_back(metadata);
	}

	factoryApi->destroy(factory);
	return true;
}

class SymbiosisVST3Plugin;
class SymbiosisVST3PlugView;

class VST3AdapterHost : public Host {
	public:
		explicit VST3AdapterHost(SymbiosisVST3Plugin* owner) : owner(owner) {}
		void updateDisplay() override;
		void beginEdit(UInt32 parameterNumber) override;
		void writeParameter(UInt32 parameterNumber, Float32 normalizedValue) override;
		void endEdit(UInt32 parameterNumber) override;
		bool requestResize(UInt32 width, UInt32 height) override;
		const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) override;

		private:
			SymbiosisVST3Plugin* owner;
};

class SymbiosisVST3PlugView : public Steinberg::CPluginView {
	public:
		SymbiosisVST3PlugView(SymbiosisVST3Plugin* owner, UInt32 width, UInt32 height);
		Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
		Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
		Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
		bool requestHostResize(UInt32 width, UInt32 height);

	private:
		SymbiosisVST3Plugin* owner;
		bool isViewAttached = false;
};

class SymbiosisVST3Plugin : public Steinberg::Vst::SingleComponentEffect, public Steinberg::Vst::IMidiMapping {
	public:
		static Steinberg::FUnknown* createInstance(void* context) {
			const UInt32 plugInIndex = static_cast<UInt32>(reinterpret_cast<uintptr_t>(context));
			return static_cast<Steinberg::Vst::IAudioProcessor*>(new SymbiosisVST3Plugin(plugInIndex));
		}

		OBJ_METHODS(SymbiosisVST3Plugin, SingleComponentEffect)
		DEFINE_INTERFACES
		DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
		END_DEFINE_INTERFACES(SingleComponentEffect)
		REFCOUNT_METHODS(SingleComponentEffect)

		explicit SymbiosisVST3Plugin(UInt32 plugInIndex)
			: hostBridge(this)
			, plugInIndex(plugInIndex)
			, plugInInfo(0)
			, currentProgram(0)
			, isBypassed(false)
			, setupSampleRate(44100.0f)
			, setupMaxBlockSize(256)
			, currentAudioState(AUDIO_UNCONFIGURED)
		{
			memset(&loaderInfo, 0, sizeof(loaderInfo));
		}

		Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE {
			Steinberg::tresult result = Steinberg::Vst::SingleComponentEffect::initialize(context);
			if (result != Steinberg::kResultOk) {
				return result;
			}
			if (!initializeSymbiosisFactory(context)) {
				return Steinberg::kResultFalse;
			}

			processContextRequirements.needProjectTimeMusic();
			processContextRequirements.needBarPositionMusic();
			processContextRequirements.needCycleMusic();
			processContextRequirements.needTempo();
			processContextRequirements.needTimeSignature();
			processContextRequirements.needTransportState();

			addUnit(new Steinberg::Vst::Unit(STR16("Root"), Steinberg::Vst::kRootUnitId
					, Steinberg::Vst::kNoParentUnitId
					, (plugInInfo->programCount > 1u ? static_cast<Steinberg::Vst::ProgramListID>(PROGRAM_LIST_ID)
					: Steinberg::Vst::kNoProgramListId)));
			
			// initialize buses
			{
				SYMBIOSIS_ASSERT(plugInInfo != 0);
				inputBusCount = plugInInfo->audioInputBusCount;
				outputBusCount = plugInInfo->audioOutputBusCount;
				if (inputBusCount > 0) {
					selectedInputBusFormats.reset(new UInt32[inputBusCount]);
					selectedInputBusChannelCounts.reset(new UInt32[inputBusCount]);
					inputBusConnected.reset(new Bool8[inputBusCount]);
					inputBusSilent.reset(new Bool8[inputBusCount]);
					for (UInt32 i = 0; i < inputBusCount; ++i) {
						inputBusConnected[i] = 1;
						inputBusSilent[i] = 0;
						const SymbiosisAudioBusInfo& busInfo = plugInInfo->audioInputBuses[i];
						selectedInputBusFormats[i] = ((busInfo.supportedBusFormats & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0
								&& (busInfo.supportedBusFormats & ~SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK) == SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK)
								? SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK : 0;
						selectedInputBusChannelCounts[i] = chooseDefaultChannelCountFromSupportedMask(busInfo.supportedBusFormats);
						if (selectedInputBusFormats[i] == 0) {
							UInt32 formatValue = 0;
							UInt32 channelCount = 0;
							const bool ok = resolveFormatFromArrangement(busInfo.supportedBusFormats
									, chooseDefaultArrangement(busInfo.supportedBusFormats), &formatValue, &channelCount);
							SYMBIOSIS_ASSERT(ok);
							selectedInputBusFormats[i] = formatValue;
							selectedInputBusChannelCounts[i] = channelCount;
						}
						Steinberg::Vst::String128 busName;
						utf8ToVstString128(busInfo.displayName, busName);
						addAudioInput(busName, chooseDefaultArrangement(busInfo.supportedBusFormats)
								, i == 0u ? Steinberg::Vst::kMain : Steinberg::Vst::kAux);
					}
				}

				if (outputBusCount > 0) {
					selectedOutputBusFormats.reset(new UInt32[outputBusCount]);
					selectedOutputBusChannelCounts.reset(new UInt32[outputBusCount]);
					outputBusConnected.reset(new Bool8[outputBusCount]);
					outputBusSilent.reset(new Bool8[outputBusCount]);
					for (UInt32 i = 0; i < outputBusCount; ++i) {
						outputBusConnected[i] = 1;
						outputBusSilent[i] = 0;
						const SymbiosisAudioBusInfo& busInfo = plugInInfo->audioOutputBuses[i];
						UInt32 formatValue = 0;
						UInt32 channelCount = 0;
						const bool ok = resolveFormatFromArrangement(busInfo.supportedBusFormats
								, chooseDefaultArrangement(busInfo.supportedBusFormats), &formatValue, &channelCount);
						SYMBIOSIS_ASSERT(ok);
						selectedOutputBusFormats[i] = formatValue;
						selectedOutputBusChannelCounts[i] = channelCount;
						Steinberg::Vst::String128 busName;
						utf8ToVstString128(busInfo.displayName, busName);
						addAudioOutput(busName, chooseDefaultArrangement(busInfo.supportedBusFormats), Steinberg::Vst::kMain);
					}
				}

				const UInt32 eventCapabilities = plugInInfo->eventCapabilities;
				if ((eventCapabilities & SYMBIOSIS_WANTS_MIDI_INPUT_MASK) != 0u) {
					addEventInput(STR16("MIDI In"), 16);
				}
				if ((eventCapabilities & SYMBIOSIS_CREATES_MIDI_OUTPUT_MASK) != 0u) {
					addEventOutput(STR16("MIDI Out"), 16);
				}
			}
			
			// initialize parameters
			{
				SYMBIOSIS_ASSERT(plugInInfo != 0);
				publicQueueCommitCount = plugInInfo->parameterCount;
				publicQueueCommitStates.reset(publicQueueCommitCount == 0 ? 0 : new PublicQueueCommit[publicQueueCommitCount]);
				std::vector<UInt32> publicParameterOrder(plugInInfo->parameterCount);
				for (UInt32 i = 0; i < plugInInfo->parameterCount; ++i) {
					publicParameterOrder[i] = i;
				}
				stableSortNoAlloc(publicParameterOrder.data(), 0, static_cast<int>(publicParameterOrder.size())
						, [this](int a, int b) -> int {
					return (static_cast<int>(plugInInfo->parameters[a].sortOrder) - static_cast<int>(plugInInfo->parameters[b].sortOrder));
				});

				for (size_t orderedIndex = 0; orderedIndex < publicParameterOrder.size(); ++orderedIndex) {
					const UInt32 parameterNumber = publicParameterOrder[orderedIndex];
					const SymbiosisParameterInfo& parameterInfo = plugInInfo->parameters[parameterNumber];
					Steinberg::Vst::String128 name;
					Steinberg::Vst::String128 unit;
					utf8ToVstString128(parameterInfo.displayName, name);
					utf8ToVstString128(parameterInfo.displayUnit == 0 ? "" : parameterInfo.displayUnit, unit);
					Steinberg::int32 stepCount = 0;
					Steinberg::int32 flags = Steinberg::Vst::ParameterInfo::kCanAutomate;
					if (parameterInfo.scale == SYMBIOSIS_PARAMETER_SCALE_BOOLEAN) {
						stepCount = 1;
					} else if (parameterInfo.scale == SYMBIOSIS_PARAMETER_SCALE_STEPPED) {
						stepCount = static_cast<Steinberg::int32>(std::max<UInt32>(1u, static_cast<UInt32>(parameterInfo.maximum)));
						flags |= Steinberg::Vst::ParameterInfo::kIsList;
					}
					parameters.addParameter(name, unit, stepCount, parameterInfo.defaultValue, flags, parameterNumber
							, Steinberg::Vst::kRootUnitId, 0);
				}
				
				const bool wantsMidiInput = (plugInInfo->eventCapabilities & SYMBIOSIS_WANTS_MIDI_INPUT_MASK) != 0u;
				const bool usesMultiChannel = (plugInInfo->eventCapabilities & SYMBIOSIS_USES_MULTI_CHANNEL_MASK) != 0u;
				if (wantsMidiInput) {
					for (UInt32 cc = 0; cc < 128u; ++cc) {
						if (plugInInfo->requiredCCNumbers[cc] != 0) {
							const UInt32 channelCount = usesMultiChannel ? 16u : 1u;
							for (UInt32 channel = 0; channel < channelCount; ++channel) {
								UTF8Z text[64];
								text[0] = 0;
								UInt32 offset = 0;
								offset = appendText(text, 63, offset, "CC ");
								offset = appendUIntToString(text, 63, offset, cc);
								if (usesMultiChannel) {
									offset = appendText(text, 63, offset, " ch ");
									appendUIntToString(text, 63, offset, channel + 1u);
								}
								Steinberg::Vst::String128 name;
								utf8ToVstString128(text, name);
								Steinberg::Vst::ParameterInfo info;
								memset(&info, 0, sizeof(info));
								memcpy(info.title, name, sizeof(name));
								info.stepCount = 127;
								info.defaultNormalizedValue = 0.0;
								info.flags = Steinberg::Vst::ParameterInfo::kIsHidden
										| Steinberg::Vst::ParameterInfo::kIsReadOnly;
								info.id = HIDDEN_CC_PARAM_CLASS
										| (channel << HIDDEN_PARAM_CHANNEL_SHIFT)
										| (cc << HIDDEN_PARAM_CC_SHIFT);
								info.unitId = Steinberg::Vst::kRootUnitId;
								parameters.addParameter(info);
							}
						}
					}
				}

				if (plugInInfo->programCount > 1u) {
					parameters.addParameter(STR16("Program"), nullptr, static_cast<Steinberg::int32>(plugInInfo->programCount - 1u), 0
							, Steinberg::Vst::ParameterInfo::kIsList | Steinberg::Vst::ParameterInfo::kIsProgramChange
							/*| Steinberg::Vst::ParameterInfo::kCanAutomate*/
							, PROGRAM_LIST_ID, Steinberg::Vst::kRootUnitId, 0);
				}

				if (plugInInfo->handlesBypass != 0) {
					parameters.addParameter(STR16("Bypass"), nullptr, 1, 0
							, Steinberg::Vst::ParameterInfo::kIsBypass | Steinberg::Vst::ParameterInfo::kCanAutomate
							, BYPASS_ID, Steinberg::Vst::kRootUnitId, 0);
				}
				return Steinberg::kResultOk;
			}
		}

		Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE {
			const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
			if (currentAudioState == AUDIO_ENABLED) {
				disableAudio();
			}
			plugIn.reset();
			factory.reset();
			plugInInfo = 0;
			return Steinberg::Vst::SingleComponentEffect::terminate();
		}

		Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns
				, Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE {
			const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
			SYMBIOSIS_ASSERT(currentAudioState == AUDIO_UNCONFIGURED);
			if (numIns < 0 || numOuts < 0) {
				return Steinberg::kInvalidArgument;
			}
			if (numIns != static_cast<Steinberg::int32>(inputBusCount)
					|| numOuts != static_cast<Steinberg::int32>(outputBusCount)) {
				return Steinberg::kResultFalse;
			}
			std::vector<UInt32> nextInputBusFormats(inputBusCount);
			std::vector<UInt32> nextInputBusChannelCounts(inputBusCount);
			std::vector<UInt32> nextOutputBusFormats(outputBusCount);
			std::vector<UInt32> nextOutputBusChannelCounts(outputBusCount);
			for (Steinberg::int32 i = 0; i < numIns; ++i) {
				UInt32 formatValue = 0;
				UInt32 channelCount = 0;
				if (!resolveFormatFromArrangement(plugInInfo->audioInputBuses[i].supportedBusFormats, inputs[i]
						, &formatValue, &channelCount)) {
					return Steinberg::kResultFalse;
				}
				nextInputBusFormats[static_cast<size_t>(i)] = formatValue;
				nextInputBusChannelCounts[static_cast<size_t>(i)] = channelCount;
			}
			for (Steinberg::int32 i = 0; i < numOuts; ++i) {
				UInt32 formatValue = 0;
				UInt32 channelCount = 0;
				if (!resolveFormatFromArrangement(plugInInfo->audioOutputBuses[i].supportedBusFormats, outputs[i]
						, &formatValue, &channelCount)) {
					return Steinberg::kResultFalse;
				}
				nextOutputBusFormats[static_cast<size_t>(i)] = formatValue;
				nextOutputBusChannelCounts[static_cast<size_t>(i)] = channelCount;
			}
			const UInt32 coupledMask = SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK;
			const UInt32 pairedCount = static_cast<UInt32>(std::min(numIns, numOuts));
			for (UInt32 i = 0; i < pairedCount; ++i) {
				if ((plugInInfo->audioInputBuses[i].supportedBusFormats & coupledMask) != 0u) {
					if (nextInputBusFormats[i] == SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK
							|| nextOutputBusFormats[i] == SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) {
						if (nextInputBusChannelCounts[i] != nextOutputBusChannelCounts[i]) {
							return Steinberg::kResultFalse;
						}
					} else if (nextInputBusFormats[i] != nextOutputBusFormats[i]) {
						return Steinberg::kResultFalse;
					}
				}
			}
			for (UInt32 i = 0; i < inputBusCount; ++i) {
				selectedInputBusFormats[i] = nextInputBusFormats[i];
				selectedInputBusChannelCounts[i] = nextInputBusChannelCounts[i];
			}
			for (UInt32 i = 0; i < outputBusCount; ++i) {
				selectedOutputBusFormats[i] = nextOutputBusFormats[i];
				selectedOutputBusChannelCounts[i] = nextOutputBusChannelCounts[i];
			}
			return Steinberg::Vst::SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
		}

		Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) SMTG_OVERRIDE {
			const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
			if (state == 0) {
			#if SYMBIOSIS_VST3_ENABLE_LIVE_SETACTIVE_WORKAROUND
				if (currentAudioState == AUDIO_ENABLED) {
					disableAudio();
				} else {
					SYMBIOSIS_ASSERT(currentAudioState == AUDIO_CONFIGURED);
				}
			#else
				SYMBIOSIS_ASSERT(currentAudioState == AUDIO_ENABLED);
				disableAudio();
			#endif
			} else {
			#if SYMBIOSIS_VST3_ENABLE_LIVE_SETACTIVE_WORKAROUND
				if (currentAudioState == AUDIO_CONFIGURED) {
					plugIn->enableAudio();
					currentAudioState = AUDIO_ENABLED;
				} else {
					SYMBIOSIS_ASSERT(currentAudioState == AUDIO_ENABLED);
				}
			#else
				SYMBIOSIS_ASSERT(currentAudioState == AUDIO_CONFIGURED);
				plugIn->enableAudio();
				currentAudioState = AUDIO_ENABLED;
			#endif
			}
			return Steinberg::kResultOk;
		}

		Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE {
			const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
			if (state == 0) {
			#if SYMBIOSIS_VST3_ENABLE_LIVE_SETACTIVE_WORKAROUND
				// Workaround for Live violating VST3 ordering by calling setActive(false) while processing is still enabled.
				if (currentAudioState == AUDIO_ENABLED) {
					liveReactivateNeedsEnable = true;
					traceMessage("VST3 setActive", "Live workaround: setActive(false) called while audio enabled");
					disableAudio();
				}
			#endif
				SYMBIOSIS_ASSERT(currentAudioState != AUDIO_ENABLED);
				if (currentAudioState == AUDIO_CONFIGURED) {
					unconfigureAudio();
				}
				SYMBIOSIS_ASSERT(currentAudioState == AUDIO_UNCONFIGURED);
			}
			const Steinberg::tresult result = Steinberg::Vst::SingleComponentEffect::setActive(state);
			if (result != Steinberg::kResultOk) {
				return result;
			}
			if (state != 0) {
				SYMBIOSIS_ASSERT(currentAudioState == AUDIO_UNCONFIGURED);
				configureAudio(setupSampleRate, static_cast<Steinberg::int32>(setupMaxBlockSize));
				SYMBIOSIS_ASSERT(currentAudioState == AUDIO_CONFIGURED);
			#if SYMBIOSIS_VST3_ENABLE_LIVE_SETACTIVE_WORKAROUND
				// Workaround counterpart: if we had to force-disable on setActive(false), force re-enable on the next activation.
				if (liveReactivateNeedsEnable) {
					traceMessage("VST3 setActive", "Live workaround: auto-enabled audio on reactivation");
					liveReactivateNeedsEnable = false;
					plugIn->enableAudio();
					currentAudioState = AUDIO_ENABLED;
					SYMBIOSIS_ASSERT(currentAudioState == AUDIO_ENABLED);
				}
			#endif
			}
			return Steinberg::kResultOk;
		}

		Steinberg::tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir
				, Steinberg::int32 index, Steinberg::TBool state) SMTG_OVERRIDE {
			const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
			const Steinberg::tresult result = Steinberg::Vst::SingleComponentEffect::activateBus(type, dir, index, state);
			if (result != Steinberg::kResultOk || type != Steinberg::Vst::kAudio || index < 0) {
				return result;
			}
			const size_t busIndex = static_cast<size_t>(index);
			if (dir == Steinberg::Vst::kInput) {
				if (busIndex < inputBusCount) {
					inputBusConnected[busIndex] = toBool8(state != 0);
				}
			} else {
				if (busIndex < outputBusCount) {
					outputBusConnected[busIndex] = toBool8(state != 0);
				}
			}
			return result;
		}

		Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE {
			const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
		#if SYMBIOSIS_VST3_ENABLE_SETUPPROCESSING_RECONFIG_WHEN_ACTIVE
			const bool wasConfigured = (currentAudioState == AUDIO_CONFIGURED);
			SYMBIOSIS_ASSERT(currentAudioState != AUDIO_ENABLED);
			if (currentAudioState == AUDIO_CONFIGURED) {
				traceMessage("VST3 setupProcessing", "vst3validator workaround: setupProcessing() called when processor is active");
				unconfigureAudio();
			}
			SYMBIOSIS_ASSERT(currentAudioState == AUDIO_UNCONFIGURED);
		#else
			SYMBIOSIS_ASSERT(currentAudioState != AUDIO_ENABLED);
			SYMBIOSIS_ASSERT(currentAudioState != AUDIO_CONFIGURED);
			SYMBIOSIS_ASSERT(currentAudioState == AUDIO_UNCONFIGURED);
		#endif
			const Steinberg::tresult result = Steinberg::Vst::SingleComponentEffect::setupProcessing(newSetup);
			if (result != Steinberg::kResultOk) {
				return result;
			}
			setupSampleRate = static_cast<Float32>(newSetup.sampleRate);
			setupMaxBlockSize = static_cast<UInt32>(std::max<Steinberg::int32>(0, newSetup.maxSamplesPerBlock));
		#if SYMBIOSIS_VST3_ENABLE_SETUPPROCESSING_RECONFIG_WHEN_ACTIVE
			if (wasConfigured) {
				traceMessage("VST3 setupProcessing", "vst3validator workaround: reconfiguring audio");
				configureAudio(setupSampleRate, static_cast<Steinberg::int32>(setupMaxBlockSize));
			}
		#endif
			return result;
		}

		Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE {
			return symbolicSampleSize == Steinberg::Vst::kSample32 ? Steinberg::kResultTrue : Steinberg::kResultFalse;
		}

		Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE {
			return latencySamples;
		}

		Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE {
			if (tailSamples < 0) {
				return Steinberg::Vst::kInfiniteTail;
			}
			return static_cast<Steinberg::uint32>(tailSamples);
		}

		Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE {
			const std::lock_guard<std::recursive_mutex> lock(audioGroupMutex);
			SYMBIOSIS_ASSERT(data.numSamples >= 0);
			if (data.numSamples == 0) {
				buildInputEvents(data);
				commitPublicParameterQueues();
				return Steinberg::kResultOk;
			}
			SYMBIOSIS_ASSERT(currentAudioState == AUDIO_ENABLED);
			SYMBIOSIS_ASSERT(data.symbolicSampleSize == Steinberg::Vst::kSample32);
			buildInputOutputPointers(data);
			buildInputEvents(data);

			SymbiosisRenderInputArgs inArgs;
			memset(&inArgs, 0, sizeof(inArgs));
			inArgs.structVersion = 1;
			inArgs.bufferSize = static_cast<UInt32>(data.numSamples);
			inArgs.inputBusSilent = inputBusSilent.get();
			inArgs.inputChannels = inputChannelPointers.get();
			inArgs.inputEventCount = inputEventCount;
			inArgs.inputEvents = inputEvents.get();
			inArgs.inputBusConnected = inputBusConnected.get();
			inArgs.outputBusConnected = outputBusConnected.get();
			inArgs.isBypassed = toBool8(isBypassed);
			inArgs.samplePosition = renderFallbackSamplePosition;
			inArgs.isTransportRunning = 0;
			inArgs.isTransportLooping = 0;
			inArgs.tempo = 120.0;
			inArgs.ppqPosition = 0.0;
			inArgs.loopStartPPQPosition = 0.0;
			inArgs.loopEndPPQPosition = 0.0;

			if (data.processContext != 0) {
				const Steinberg::Vst::ProcessContext& context = *data.processContext;
				inArgs.samplePosition = static_cast<Int64>(context.projectTimeSamples);
				inArgs.tempo = context.tempo;
				inArgs.ppqPosition = context.projectTimeMusic;
				inArgs.isTransportRunning = toBool8((context.state & Steinberg::Vst::ProcessContext::kPlaying) != 0);
				const bool isLooping = (context.state & Steinberg::Vst::ProcessContext::kCycleActive) != 0;
				inArgs.isTransportLooping = toBool8(isLooping);
				if (isLooping) {
					inArgs.loopStartPPQPosition = context.cycleStartMusic;
					inArgs.loopEndPPQPosition = context.cycleEndMusic;
				}
			}

			SymbiosisRenderOutputArgs outArgs;
			memset(&outArgs, 0, sizeof(outArgs));
			outArgs.structVersion = 1;
			outArgs.outputBusSilent = outputBusSilent.get();
			outArgs.outputChannels = (outputChannelPointerCount == 0 ? 0 : outputChannelPointers.get());
			outArgs.outputEventCount = 0;
			outArgs.outputEvents = 0;

			plugIn->renderAudio(&inArgs, &outArgs);
			renderFallbackSamplePosition = inArgs.samplePosition + static_cast<Int64>(data.numSamples);

			for (Steinberg::int32 busIndex = 0; busIndex < data.numOutputs; ++busIndex) {
				Steinberg::Vst::AudioBusBuffers& busBuffers = data.outputs[busIndex];
				const UInt32 hostChannelCount = static_cast<UInt32>(std::max<Steinberg::int32>(0, busBuffers.numChannels));
				const bool thisBusSilent = (static_cast<UInt32>(busIndex) < outputBusCount && outputBusSilent[busIndex] != 0);
				if (thisBusSilent) {
					const uint64_t usedMask = hostChannelCount >= 64u ? UINT64_MAX : (hostChannelCount == 0u ? 0ull : ((1ull << hostChannelCount) - 1ull));
					busBuffers.silenceFlags = usedMask;
					continue;
				}
				busBuffers.silenceFlags = 0ull;
			}

			commitPublicParameterQueues();

			// Emit VST3 note events directly and translate MIDI CC to the legacy CC-out event type.
			// A plug-in that produced no output events leaves outArgs.outputEvents null (with
			// outputEventCount 0); only walk the array when there is something to emit, so a host that
			// supplies an output-event queue every block does not require the plug-in to hand one back.
			if (data.outputEvents != 0 && outArgs.outputEventCount > 0) {
				SYMBIOSIS_ASSERT(outArgs.outputEvents != 0);
				const UInt32 cappedOutputEventCount = std::min(outArgs.outputEventCount, plugInInfo->maxOutputEventCountPerBlock);
				for (UInt32 i = 0; i < cappedOutputEventCount; ++i) {
					const SymbiosisEvent& event = outArgs.outputEvents[i];
					if (event.type != SYMBIOSIS_EVENT_TYPE_MIDI || event.data == 0) {
						continue;
					}
					const SymbiosisMidiEventData* midi = reinterpret_cast<const SymbiosisMidiEventData*>(event.data);
					const UByte8 command = static_cast<UByte8>(midi->status & 0xF0);
					Steinberg::Vst::Event outEvent;
					memset(&outEvent, 0, sizeof(outEvent));
					outEvent.busIndex = 0;
					outEvent.sampleOffset = static_cast<Steinberg::int32>(event.offset);
					const Steinberg::int16 channel = static_cast<Steinberg::int16>(midi->status & 0x0F);
					if (command == 0x90u || command == 0x80u) {
						outEvent.type = (command == 0x90u && midi->data2 != 0u)
								? Steinberg::Vst::Event::kNoteOnEvent : Steinberg::Vst::Event::kNoteOffEvent;
						if (outEvent.type == Steinberg::Vst::Event::kNoteOnEvent) {
							outEvent.noteOn.channel = channel;
							outEvent.noteOn.pitch = midi->data1;
							outEvent.noteOn.velocity = static_cast<float>(midi->data2) / 127.0f;
						} else {
							outEvent.noteOff.channel = channel;
							outEvent.noteOff.pitch = midi->data1;
							outEvent.noteOff.velocity = static_cast<float>(midi->data2) / 127.0f;
						}
					} else if (command == 0xB0u) {
						outEvent.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
						outEvent.midiCCOut.controlNumber = midi->data1;
						outEvent.midiCCOut.channel = static_cast<Steinberg::int8>(channel);
						outEvent.midiCCOut.value = static_cast<Steinberg::int8>(midi->data2 & 0x7Fu);
						outEvent.midiCCOut.value2 = 0;
					} else {
						continue;
					}
					if (data.outputEvents->addEvent(outEvent) != Steinberg::kResultOk) {
						continue;
					}
				}
			}
			return Steinberg::kResultOk;
		}

		Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel
				, Steinberg::Vst::CtrlNumber midiControllerNumber, Steinberg::Vst::ParamID& tag) SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (busIndex != 0 || channel < 0 || channel >= 16) {
				return Steinberg::kResultFalse;
			}
			if ((plugInInfo->eventCapabilities & SYMBIOSIS_WANTS_MIDI_INPUT_MASK) == 0u) {
				return Steinberg::kResultFalse;
			}
			const Steinberg::int32 controllerNumber = static_cast<Steinberg::int32>(midiControllerNumber);
			if (controllerNumber >= 0 && controllerNumber < 128) {
				if (plugInInfo->requiredCCNumbers[controllerNumber] == 0) {
					return Steinberg::kResultFalse;
				}
				const bool usesMultiChannel = (plugInInfo->eventCapabilities & SYMBIOSIS_USES_MULTI_CHANNEL_MASK) != 0u;
				const UInt32 mappedChannel = usesMultiChannel ? static_cast<UInt32>(channel) : 0u;
				tag = HIDDEN_CC_PARAM_CLASS
						| (mappedChannel << HIDDEN_PARAM_CHANNEL_SHIFT)
						| (static_cast<UInt32>(controllerNumber) << HIDDEN_PARAM_CC_SHIFT);
				return Steinberg::kResultTrue;
			}
			return Steinberg::kResultFalse;
		}

		Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized(Steinberg::Vst::ParamID tag) SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (tag < plugInInfo->parameterCount) {
				return static_cast<Steinberg::Vst::ParamValue>(plugIn->getParameter(tag));
			}
			if (tag == PROGRAM_LIST_ID && plugInInfo->programCount > 1u) {
				const UInt32 stepCount = plugInInfo->programCount - 1u;
				return stepCount == 0 ? 0.0 : static_cast<Steinberg::Vst::ParamValue>(currentProgram) / static_cast<Steinberg::Vst::ParamValue>(stepCount);
			}
			if (tag == BYPASS_ID) {
				return (isBypassed ? 1.0 : 0.0);
			}
			return Steinberg::Vst::SingleComponentEffect::getParamNormalized(tag);
		}

		Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(0.0 <= value && value <= 1.0);
			if (tag == BYPASS_ID && plugInInfo->handlesBypass != 0) {
				// Bypass is sticky and orthogonal to the audio lifecycle: honor it in EVERY state (the
				// previous not-enabled gate silently dropped bypass changes during playback) and deliver
				// it out-of-band (SymbiosisBypassPolicy.md).
				isBypassed = (value >= 0.5);
				plugIn->setBypass(toBool8(isBypassed));
				value = (isBypassed ? 1.0 : 0.0);
			} else if (currentAudioState != AUDIO_ENABLED) {
				if (tag < plugInInfo->parameterCount) {
					plugIn->updateParameter(tag, static_cast<Float32>(value));
				} else if (tag == PROGRAM_LIST_ID && plugInInfo->programCount > 1u) {
					applyProgramNormalized(static_cast<Float32>(value));
				}
			}
			return Steinberg::Vst::SingleComponentEffect::setParamNormalized(tag, value);
		}

		Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue valueNormalized
				, Steinberg::Vst::String128 string) SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (tag < plugInInfo->parameterCount) {
				UTF8Z text[128];
				const bool converted = plugIn->convertParameterValueToText(tag, static_cast<Float32>(valueNormalized), 127, text);
				if (!converted) {
					convertParameterValueToTextDefault(plugInInfo->parameters[tag], static_cast<Float32>(valueNormalized), 127, text);
				}
				utf8ToVstString128(text, string);
				return Steinberg::kResultOk;
			}
			return Steinberg::Vst::SingleComponentEffect::getParamStringByValue(tag, valueNormalized, string);
		}

		Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID tag, Steinberg::Vst::TChar* string
				, Steinberg::Vst::ParamValue& valueNormalized) SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (tag < plugInInfo->parameterCount) {
				UTF8Z text[512];
				vstString128ToUTF8(string, text);
				Float32 parsedValue = 0.0f;
				bool ok = plugIn->convertTextToParameterValue(tag, text, &parsedValue);
				if (!ok) {
					ok = convertTextToParameterValueDefault(plugInInfo->parameters[tag], text, &parsedValue);
				}
				if (!ok) {
					return Steinberg::kResultFalse;
				}
				valueNormalized = static_cast<Steinberg::Vst::ParamValue>(parsedValue);
				return Steinberg::kResultOk;
			}
			return Steinberg::Vst::SingleComponentEffect::getParamValueByString(tag, string, valueNormalized);
		}

		Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(state != 0);
			UInt32 dataSize = 0;
			UByte8* data = 0;
			if (!plugIn->createSaveState(&dataSize, &data)) {
				tracePlugInLastError(plugIn.get(), "VST3 getState");
				return Steinberg::kResultFalse;
			}
			UByte8 header[VST3_STATE_HEADER_SIZE];
			memset(header, 0, sizeof(header));
			encodeLE32(&header[0], STATE_MAGIC_SYMB);
			encodeLE32(&header[4], STATE_ADAPTER_VST3);
			encodeLE32(&header[8], 1u);
			encodeLE32(&header[12], isBypassed ? 1u : 0u);
			if (state->write(header, static_cast<Steinberg::int32>(sizeof(header))) != Steinberg::kResultOk) {
				plugIn->destroySaveState(data);
				return Steinberg::kResultFalse;
			}
			if (dataSize > 0u && data != 0) {
				if (state->write(data, static_cast<Steinberg::int32>(dataSize)) != Steinberg::kResultOk) {
					plugIn->destroySaveState(data);
					return Steinberg::kResultFalse;
				}
			}
			plugIn->destroySaveState(data);
			return Steinberg::kResultOk;
		}

		Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(state != 0);
			UByte8 header[VST3_STATE_HEADER_SIZE];
			Steinberg::int32 readCount = 0;
			if (state->read(header, static_cast<Steinberg::int32>(sizeof(header)), &readCount) != Steinberg::kResultOk
					|| readCount != static_cast<Steinberg::int32>(sizeof(header))) {
				return Steinberg::kResultFalse;
			}
			if (decodeLE32(&header[0]) != STATE_MAGIC_SYMB) {
				return Steinberg::kResultFalse;
			}
			if (decodeLE32(&header[4]) != STATE_ADAPTER_VST3) {
				return Steinberg::kResultFalse;
			}
			if (decodeLE32(&header[8]) != 1u) {
				return Steinberg::kResultFalse;
			}
			const UInt32 bypassFlag = decodeLE32(&header[12]);
			if (bypassFlag > 1u) {
				return Steinberg::kResultFalse;
			}
			std::vector<UByte8> payload;
			if (!readRemainingStream(state, &payload)) {
				return Steinberg::kResultFalse;
			}
			const UInt32 payloadSize = static_cast<UInt32>(payload.size());
			if (!plugIn->loadState(payloadSize, payloadSize > 0u ? &payload[0] : 0)) {
				tracePlugInLastError(plugIn.get(), "VST3 setState");
				return Steinberg::kResultFalse;
			}
			if (plugInInfo->handlesBypass != 0) {
				isBypassed = (bypassFlag != 0u);
				plugIn->setBypass(toBool8(isBypassed));	// restore is out-of-band by definition (inactive)
				Steinberg::Vst::SingleComponentEffect::setParamNormalized(BYPASS_ID, isBypassed ? 1.0 : 0.0);
			} else {
				isBypassed = false;	// no bypass surface; ignore any stored flag
			}
			return Steinberg::kResultOk;
		}

		Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE {
			(void)state;
			return Steinberg::kResultOk;
		}

		Steinberg::tresult PLUGIN_API getUnitByBus(Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir
				, Steinberg::int32 busIndex, Steinberg::int32 channel, Steinberg::Vst::UnitID& unitId) SMTG_OVERRIDE {
			(void)channel;
			// Associate the MIDI event-input bus with the Root unit, which owns the program list. Without
			// this link a host (Cubase) will NOT route incoming MIDI Program Change to the kIsProgramChange
			// program-list param, so sequenced MIDI PC is silently dropped (GUI selection still works because
			// it drives the param directly). Verified empirically in Cubase 15; matches the old adapter.
			if (type == Steinberg::Vst::kEvent && dir == Steinberg::Vst::kInput && busIndex == 0
					&& plugInInfo->programCount > 1u) {
				unitId = Steinberg::Vst::kRootUnitId;
				return Steinberg::kResultTrue;
			}
			return Steinberg::kResultFalse;
		}

		Steinberg::int32 PLUGIN_API getProgramListCount() SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			return (plugInInfo->programCount > 1u ? 1 : 0);
		}

		Steinberg::tresult PLUGIN_API getProgramListInfo(Steinberg::int32 listIndex
				, Steinberg::Vst::ProgramListInfo& info) SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (plugInInfo->programCount <= 1u || listIndex != 0) {
				return Steinberg::kResultFalse;
			}
			info.id = static_cast<Steinberg::Vst::ProgramListID>(PROGRAM_LIST_ID);
			info.programCount = static_cast<Steinberg::int32>(plugInInfo->programCount);
			utf8ToVstString128("Programs", info.name);
			return Steinberg::kResultOk;
		}

		Steinberg::tresult PLUGIN_API getProgramName(Steinberg::Vst::ProgramListID listId, Steinberg::int32 programIndex
				, Steinberg::Vst::String128 name) SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (plugInInfo->programCount <= 1u || listId != static_cast<Steinberg::Vst::ProgramListID>(PROGRAM_LIST_ID)) {
				return Steinberg::kResultFalse;
			}
			if (programIndex < 0 || static_cast<UInt32>(programIndex) >= plugInInfo->programCount) {
				return Steinberg::kResultFalse;
			}
			UTF8Z text[128];
			plugIn->getProgramName(static_cast<UInt32>(programIndex), 127, text);
			utf8ToVstString128(text, name);
			return Steinberg::kResultOk;
		}

		Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (name == 0 || strcmp(name, Steinberg::Vst::ViewType::kEditor) != 0) {
				return 0;
			}
			if (activeView != 0 || isCustomUIViewOpen) {
				return 0;
			}
			if (plugInInfo->hasCustomUIView == 0) {
				return 0;
			}
			UInt32 width = 0;
			UInt32 height = 0;
			if (!plugIn->getUIViewSize(&width, &height)) {
				tracePlugInLastError(plugIn.get(), "VST3 getUIViewSize");
				return 0;
			}
			if (width == 0u || height == 0u) {
				return 0;
			}
			return new SymbiosisVST3PlugView(this, width, height);
		}

		bool onHostRequestResize(UInt32 width, UInt32 height) {
			if (activeView == 0) {
				return false;
			}
			return activeView->requestHostResize(width, height);
		}

		void onHostUpdateDisplay() {
			SYMBIOSIS_ASSERT(componentHandler != 0);
			if (componentHandler != 0) {
				componentHandler->restartComponent(Steinberg::Vst::kParamValuesChanged);
			}
		}

		void onHostBeginEdit(UInt32 parameterNumber) {
			SYMBIOSIS_ASSERT(componentHandler != 0);
			if (componentHandler != 0) {
				componentHandler->beginEdit(parameterNumber);
			}
		}

		void onHostWriteParameter(UInt32 parameterNumber, Float32 normalizedValue) {
			SYMBIOSIS_ASSERT(componentHandler != 0);
			if (componentHandler != 0) {
				componentHandler->performEdit(parameterNumber, normalizedValue);
			}
		}

		void onHostEndEdit(UInt32 parameterNumber) {
			SYMBIOSIS_ASSERT(componentHandler != 0);
			if (componentHandler != 0) {
				componentHandler->endEdit(parameterNumber);
			}
		}

		private:
			friend class SymbiosisVST3PlugView;

		struct PublicQueueCommit {
			bool hasOffsetZero = false;
			Float32 offsetZeroValue = 0.0f;
			bool hasLastPoint = false;
			Float32 lastValue = 0.0f;
		};

		bool initializeSymbiosisFactory(Steinberg::FUnknown* context) {
			loaderInfo.structVersion = 1;
			loaderInfo.maxSymbiosisVersion = 1;
			loaderInfo.adapterFormat = "VST3";
			loaderInfo.applicationName = 0;
			loaderInfo.applicationVendor = 0;
			loaderInfo.applicationVersionHex = 0;

			if (context != 0) {
				Steinberg::FUnknownPtr<Steinberg::Vst::IHostApplication> hostApp(context);
				if (hostApp != 0) {
					Steinberg::Vst::String128 hostName16;
					if (hostApp->getName(hostName16) == Steinberg::kResultOk) {
						UTF8Z hostName[512];
						vstString128ToUTF8(hostName16, hostName);
						hostApplicationNameStorage = hostName;
					}
				}
			}
			if (!hostApplicationNameStorage.empty()) {
				loaderInfo.applicationName = hostApplicationNameStorage.c_str();
			}

			UTF8Z errorText[1024];
			errorText[0] = 0;
			const SymbiosisFactoryInterface* factoryApi = 0;
			SymbiosisFactory* factoryInstance = symbiosisCreateFactory(&loaderInfo, &factoryApi, 1023, errorText);
			if (factoryInstance == 0 || factoryApi == 0) {
				SYMBIOSIS_ASSERT(false);
				traceMessage("VST3 initialize", (errorText[0] != 0 ? errorText : "symbiosisCreateFactory failed"));
				return false;
			}
			factory.reset(new HostedFactory(factoryInstance, factoryApi));
			const UInt32 plugInCount = factory->getPlugInCount();
			if (plugInCount == 0u) {
				return false;
			}
			if (plugInIndex >= plugInCount) {
				SYMBIOSIS_ASSERT(false);
				return false;
			}
			plugInInfo = factory->getPlugInInfo(plugInIndex);
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			plugIn.reset(factory->createPlugIn(plugInIndex, &hostBridge));
			if (!plugIn) {
				UTF8Z errorText[1024];
				errorText[0] = 0;
				factory->getLastErrorText(1023, errorText);
				traceMessage("VST3 initialize", (errorText[0] != 0 ? errorText : "createPlugIn failed (unknown error)"));
				SYMBIOSIS_ASSERT(false);
				return false;
			}
			return true;
		}

		bool attachCustomUIView(void* nativeParent) {
			SYMBIOSIS_ASSERT(plugIn != 0);
			SYMBIOSIS_ASSERT(isCustomUIViewOpen == false);
			if (isCustomUIViewOpen) {
				return false;
			}
			if (!plugIn->openUIView(nativeParent)) {
				tracePlugInLastError(plugIn.get(), "VST3 openUIView");
				return false;
			}
			isCustomUIViewOpen = true;
			return true;
		}

		void detachCustomUIView() {
			SYMBIOSIS_ASSERT(plugIn != 0);
			SYMBIOSIS_ASSERT(isCustomUIViewOpen == true);
			plugIn->closeUIView();
			isCustomUIViewOpen = false;
		}

		void configureAudio(double sampleRate, Steinberg::int32 maxSamplesPerBlock) {
			SYMBIOSIS_ASSERT(currentAudioState != AUDIO_ENABLED);
			SymbiosisConfigureAudioInputArgs inArgs;
			memset(&inArgs, 0, sizeof(inArgs));
			inArgs.structVersion = 1;
			inArgs.sampleRate = static_cast<Float32>(sampleRate);
			inArgs.maxBufferSize = maxSamplesPerBlock;
			inArgs.inputBusFormats = (inputBusCount == 0 ? 0 : selectedInputBusFormats.get());
			inArgs.inputBusChannelCounts = (inputBusCount == 0 ? 0 : selectedInputBusChannelCounts.get());
			inArgs.outputBusFormats = (outputBusCount == 0 ? 0 : selectedOutputBusFormats.get());
			inArgs.outputBusChannelCounts = (outputBusCount == 0 ? 0 : selectedOutputBusChannelCounts.get());
			UInt32 inputChannelCount = 0;
			for (UInt32 i = 0; i < inputBusCount; ++i) inputChannelCount += selectedInputBusChannelCounts[i];
			UInt32 outputChannelCount = 0;
			for (UInt32 i = 0; i < outputBusCount; ++i) outputChannelCount += selectedOutputBusChannelCounts[i];
			inArgs.inputChannelCount = inputChannelCount;
			inArgs.outputChannelCount = outputChannelCount;
			inputChannelPointerCount = inputChannelCount;
			outputChannelPointerCount = outputChannelCount;
			inputChannelPointers.reset(inputChannelPointerCount == 0 ? 0 : new const Float32*[inputChannelPointerCount]);
			outputChannelPointers.reset(outputChannelPointerCount == 0 ? 0 : new Float32*[outputChannelPointerCount]);
			const UInt32 maxSamples = maxSamplesPerBlock > 0 ? maxSamplesPerBlock : 1u;
			scratchInputSilenceBuffer.reset(new Float32[maxSamples]);
			memset(scratchInputSilenceBuffer.get(), 0, sizeof(Float32) * maxSamples);
			scratchOutputTrashBuffer.reset(new Float32[maxSamples]);
			memset(scratchOutputTrashBuffer.get(), 0, sizeof(Float32) * maxSamples);
			const UInt32 inputEventCapacity = plugInInfo->maxInputEventCountPerBlock;
			inputEvents.reset(inputEventCapacity == 0 ? 0 : new SymbiosisEvent[inputEventCapacity]);
			inputMidiEvents.reset(inputEventCapacity == 0 ? 0 : new SymbiosisMidiEventData[inputEventCapacity]);
			inputParameterEvents.reset(inputEventCapacity == 0 ? 0 : new SymbiosisParameterEventData[inputEventCapacity]);
			inputEventCount = 0;

			SymbiosisConfigureAudioOutputArgs outArgs;
			memset(&outArgs, 0, sizeof(outArgs));
			outArgs.structVersion = 1;
			plugIn->configureAudio(&inArgs, &outArgs);
			latencySamples = outArgs.latencySamples;
			tailSamples = outArgs.tailSamples;
			currentAudioState = AUDIO_CONFIGURED;
		}

		void disableAudio() {
			SYMBIOSIS_ASSERT(currentAudioState == AUDIO_ENABLED);
			plugIn->disableAudio();
			currentAudioState = AUDIO_CONFIGURED;
			inputEventCount = 0;
		}

		void unconfigureAudio() {
			SYMBIOSIS_ASSERT(currentAudioState == AUDIO_CONFIGURED);
			inputChannelPointerCount = 0;
			outputChannelPointerCount = 0;
			inputChannelPointers.reset();
			outputChannelPointers.reset();
			scratchInputSilenceBuffer.reset();
			scratchOutputTrashBuffer.reset();
			inputEvents.reset();
			inputMidiEvents.reset();
			inputParameterEvents.reset();
			inputEventCount = 0;
			latencySamples = 0;
			tailSamples = 0;
			currentAudioState = AUDIO_UNCONFIGURED;
		}

		void buildInputOutputPointers(Steinberg::Vst::ProcessData& data) {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			UInt32 inputPointerIndex = 0;
			UInt32 outputPointerIndex = 0;

			for (UInt32 busIndex = 0; busIndex < inputBusCount; ++busIndex) {
				const Steinberg::Vst::AudioBusBuffers& busBuffers = data.inputs[busIndex];
				const UInt32 busChannelCount = selectedInputBusChannelCounts[busIndex];
				const bool hasHostBuffers = (busBuffers.numChannels > 0 && busBuffers.channelBuffers32 != 0);
				const UInt32 hostChannelCount = hasHostBuffers ? static_cast<UInt32>(busBuffers.numChannels) : 0u;
				// Delivery-time fallback: the format was agreed strictly at setup, but a host may
				// under-deliver buffers for a connected bus (null/short channel arrays). Alias the
				// channels it did provide up to the agreed count rather than dropping signal; a fully
				// unconnected bus (0 host channels) uses the shared silence buffer. Aliasing avoids
				// having to allocate extra silent buffers for these edge cases.
				for (UInt32 channel = 0; channel < busChannelCount; ++channel) {
					const UInt32 mappedIndex = hostChannelCount > 0 ? (channel % hostChannelCount) : 0u;
					const float* pointer = (hostChannelCount > 0 ? busBuffers.channelBuffers32[mappedIndex] : 0);
					inputChannelPointers[inputPointerIndex] = (pointer != 0 ? pointer : scratchInputSilenceBuffer.get());
					++inputPointerIndex;
				}
				const uint64_t usedMask = hostChannelCount >= 64u ? UINT64_MAX : (hostChannelCount == 0 ? 0ull : ((1ull << hostChannelCount) - 1ull));
				inputBusConnected[busIndex] = toBool8(Steinberg::FCast<Steinberg::Vst::AudioBus>(audioInputs[busIndex])->isActive());
				inputBusSilent[busIndex] = toBool8((busBuffers.silenceFlags & usedMask) == usedMask);
			}
			SYMBIOSIS_ASSERT(inputPointerIndex == inputChannelPointerCount);

			for (UInt32 busIndex = 0; busIndex < outputBusCount; ++busIndex) {
				Steinberg::Vst::AudioBusBuffers& busBuffers = data.outputs[busIndex];
				const UInt32 busChannelCount = selectedOutputBusChannelCounts[busIndex];
				const bool hasHostBuffers = (busBuffers.numChannels > 0 && busBuffers.channelBuffers32 != 0);
				const UInt32 hostChannelCount = hasHostBuffers ? static_cast<UInt32>(busBuffers.numChannels) : 0u;
				// Delivery-time fallback (see input path above): alias provided channels up to the
				// agreed count; a fully unconnected bus uses the shared trash buffer.
				for (UInt32 channel = 0; channel < busChannelCount; ++channel) {
					const UInt32 mappedIndex = hostChannelCount > 0 ? (channel % hostChannelCount) : 0u;
					float* pointer = (hostChannelCount > 0 ? busBuffers.channelBuffers32[mappedIndex] : 0);
					outputChannelPointers[outputPointerIndex] = (pointer != 0 ? pointer : scratchOutputTrashBuffer.get());
					++outputPointerIndex;
				}
				outputBusConnected[busIndex] = toBool8(Steinberg::FCast<Steinberg::Vst::AudioBus>(audioOutputs[busIndex])->isActive());
				outputBusSilent[busIndex] = 0;
			}
			SYMBIOSIS_ASSERT(outputPointerIndex == outputChannelPointerCount);
		}

		void addMidiInputEvent(UInt32 offset, UByte8 status, UByte8 data1, UByte8 data2) {
			if (inputEventCount >= plugInInfo->maxInputEventCountPerBlock) {
				return;
			}
			SymbiosisMidiEventData* midi = &inputMidiEvents[inputEventCount];
			midi->status = status;
			midi->data1 = data1;
			midi->data2 = data2;
			SymbiosisEvent* event = &inputEvents[inputEventCount];
			event->offset = offset;
			event->type = SYMBIOSIS_EVENT_TYPE_MIDI;
			event->data = midi;
			++inputEventCount;
		}

		void buildInputEvents(Steinberg::Vst::ProcessData& data) {
			inputEventCount = 0;
			bool hasBypassValue = false;
			Float32 bypassValue = 0.0f;
			bool hasProgramValue = false;
			Float32 programValue = 0.0f;

			const UInt32 parameterCount = plugInInfo->parameterCount;
			SYMBIOSIS_ASSERT(publicQueueCommitCount == parameterCount);
			for (UInt32 i = 0; i < publicQueueCommitCount; ++i) {
				publicQueueCommitStates[i] = PublicQueueCommit();
			}

			if (data.inputEvents != 0) {
				const Steinberg::int32 eventCount = data.inputEvents->getEventCount();
				for (Steinberg::int32 i = 0; i < eventCount; ++i) {
					Steinberg::Vst::Event event;
					if (data.inputEvents->getEvent(i, event) != Steinberg::kResultOk) {
						continue;
					}
					const UInt32 offset = static_cast<UInt32>(std::max<Steinberg::int32>(0, event.sampleOffset));
					if (event.type == Steinberg::Vst::Event::kNoteOnEvent) {
						addMidiInputEvent(offset, static_cast<UByte8>(0x90u | (event.noteOn.channel & 0x0F))
								, static_cast<UByte8>(event.noteOn.pitch & 0x7F)
								, static_cast<UByte8>(std::max(1, std::min(127, static_cast<int>(event.noteOn.velocity * 127.0f + 0.5f)))));
					} else if (event.type == Steinberg::Vst::Event::kNoteOffEvent) {
						addMidiInputEvent(offset, static_cast<UByte8>(0x80u | (event.noteOff.channel & 0x0F))
								, static_cast<UByte8>(event.noteOff.pitch & 0x7F)
								, static_cast<UByte8>(std::max(1, std::min(127, static_cast<int>(event.noteOff.velocity * 127.0f + 0.5f)))));
					}
				}
			}

			if (data.inputParameterChanges != 0) {
				const Steinberg::int32 queueCount = data.inputParameterChanges->getParameterCount();
				for (Steinberg::int32 queueIndex = 0; queueIndex < queueCount; ++queueIndex) {
					Steinberg::Vst::IParamValueQueue* queue = data.inputParameterChanges->getParameterData(queueIndex);
					if (queue == 0) {
						continue;
					}
					const Steinberg::Vst::ParamID paramId = queue->getParameterId();
					const Steinberg::int32 pointCount = queue->getPointCount();
					if (pointCount <= 0) {
						continue;
					}

					if (paramId < parameterCount) {
						PublicQueueCommit& commit = publicQueueCommitStates[paramId];
						for (Steinberg::int32 pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
							Steinberg::int32 sampleOffset = 0;
							Steinberg::Vst::ParamValue value = 0.0;
							const Steinberg::tresult result = queue->getPoint(pointIndex, sampleOffset, value);
							SYMBIOSIS_ASSERT(result == Steinberg::kResultTrue);
							if (sampleOffset == 0) {
								commit.hasOffsetZero = true;
								commit.offsetZeroValue = static_cast<Float32>(value);
							}
							commit.hasLastPoint = true;
							commit.lastValue = static_cast<Float32>(value);
							if ((plugInInfo->eventCapabilities & SYMBIOSIS_WANTS_PARAMETER_EVENTS_MASK) != 0u
									&& inputEventCount < plugInInfo->maxInputEventCountPerBlock) {
								SymbiosisParameterEventData* parameterEvent = &inputParameterEvents[inputEventCount];
								parameterEvent->parameterNumber = static_cast<UInt32>(paramId);
								parameterEvent->normalizedValue = static_cast<Float32>(value);
								SymbiosisEvent* event = &inputEvents[inputEventCount];
								event->offset = static_cast<UInt32>(std::max<Steinberg::int32>(0, sampleOffset));
								event->type = SYMBIOSIS_EVENT_TYPE_PARAMETER;
								event->data = parameterEvent;
								++inputEventCount;
							}
						}
					} else if (paramId == BYPASS_ID) {
						for (Steinberg::int32 pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
							Steinberg::int32 sampleOffset = 0;
							Steinberg::Vst::ParamValue value = 0.0;
							const Steinberg::tresult result = queue->getPoint(pointIndex, sampleOffset, value);
							SYMBIOSIS_ASSERT(result == Steinberg::kResultTrue);
							hasBypassValue = true;
							bypassValue = static_cast<Float32>(value);
						}
					} else if (paramId == PROGRAM_LIST_ID && plugInInfo->programCount > 1u) {
						// VST3 has no MIDI program-change event. A host converts BOTH a GUI program-list
						// pick AND an incoming MIDI Program Change into the exact same thing: a value
						// change on the kIsProgramChange program-list param (this param, tied to the Root
						// unit's programListId; the bus->unit link comes from getUnitByBus). The two are
						// indistinguishable here, so we must NOT gate the actual program switch on the
						// plug-in's MIDI-PC handling. Therefore:
						//   (1) ALWAYS drive the selection path (-> changeProgram, below). This is what
						//       makes GUI list selection work even for a plug-in that filters MIDI PC
						//       (e.g. a "disable MIDI program change" preference) - the switch rides the
						//       ungated selection path, never the 0xC0.
						//   (2) ADDITIONALLY, when the plug-in opts into MIDI PC (USES_PROGRAM_CHANGE),
						//       emit a 0xC0 event ON TOP so its MIDI handler is notified too (channel 0,
						//       matching the old shipping adapter). changeProgram already applied the
						//       switch, so a plug-in whose 0xC0 handler also switches just no-ops.
						// Consequence, inherent to VST3: because GUI selection and MIDI PC are one event,
						// you CANNOT stop MIDI PC from changing the program while keeping GUI selection -
						// a "disable MIDI PC" preference can only suppress the plug-in's extra 0xC0
						// reaction, not the program change itself. See SymbiosisProgramPolicy.md.
						const bool alsoEmitProgramChangeEvent =
								(plugInInfo->eventCapabilities & SYMBIOSIS_USES_PROGRAM_CHANGE_MASK) != 0u;
						const UInt32 stepCount = plugInInfo->programCount - 1u;
						for (Steinberg::int32 pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
							Steinberg::int32 sampleOffset = 0;
							Steinberg::Vst::ParamValue value = 0.0;
							const Steinberg::tresult result = queue->getPoint(pointIndex, sampleOffset, value);
							SYMBIOSIS_ASSERT(result == Steinberg::kResultTrue);
							// (1) selection: last value in the block wins, applied via applyProgramNormalized
							// after the queue loop (-> changeProgram, deduped, also updates the display mirror).
							hasProgramValue = true;
							programValue = static_cast<Float32>(value);
							// (2) additive MIDI-PC notification, per point.
							if (alsoEmitProgramChangeEvent) {
								const UInt32 offset = static_cast<UInt32>(std::max<Steinberg::int32>(0, sampleOffset));
								UInt32 program = static_cast<UInt32>(std::min(std::max(value, 0.0), 1.0) * stepCount + 0.5);
								if (program > 127u) {
									program = 127u;
								}
								addMidiInputEvent(offset, static_cast<UByte8>(0xC0u), static_cast<UByte8>(program), 0);
							}
						}
					} else if ((paramId & HIDDEN_PARAM_CLASS_MASK) == HIDDEN_CC_PARAM_CLASS) {
						for (Steinberg::int32 pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
							Steinberg::int32 sampleOffset = 0;
							Steinberg::Vst::ParamValue value = 0.0;
							const Steinberg::tresult result = queue->getPoint(pointIndex, sampleOffset, value);
							SYMBIOSIS_ASSERT(result == Steinberg::kResultTrue);
							const UInt32 offset = static_cast<UInt32>(std::max<Steinberg::int32>(0, sampleOffset));
							const UInt32 channel = (paramId >> HIDDEN_PARAM_CHANNEL_SHIFT) & HIDDEN_PARAM_CHANNEL_MASK;
							const UInt32 cc = (paramId >> HIDDEN_PARAM_CC_SHIFT) & HIDDEN_PARAM_CC_MASK;
							addMidiInputEvent(offset, static_cast<UByte8>(0xB0u | (channel & HIDDEN_PARAM_CHANNEL_MASK))
									, static_cast<UByte8>(cc)
									, static_cast<UByte8>(static_cast<UInt32>(std::min(std::max(value, 0.0), 1.0) * 127.0 + 0.5)));
						}
					}
				}
			}

			for (UInt32 parameterNumber = 0; parameterNumber < parameterCount; ++parameterNumber) {
				const PublicQueueCommit& commit = publicQueueCommitStates[parameterNumber];
				if (commit.hasOffsetZero) {
					plugIn->updateParameter(parameterNumber, commit.offsetZeroValue);
				}
			}
			if (hasBypassValue && plugInInfo->handlesBypass != 0) {
				const bool newBypassed = (bypassValue >= 0.5f);
				if (newBypassed != isBypassed) {
					isBypassed = newBypassed;
					plugIn->setBypass(toBool8(isBypassed));	// out-of-band mirror (SymbiosisBypassPolicy.md)
				}
			}
			if (hasProgramValue) {
				applyProgramNormalized(programValue);
			}

			stableSortNoAlloc(inputEvents.get(), 0, static_cast<int>(inputEventCount)
					, [](const SymbiosisEvent& a, const SymbiosisEvent& b) -> int {
				return (a.offset != b.offset ? static_cast<int>(a.offset) - static_cast<int>(b.offset)
						: static_cast<int>(a.type) - static_cast<int>(b.type));
			});
		}

		void commitPublicParameterQueues() {
			for (UInt32 parameterNumber = 0; parameterNumber < publicQueueCommitCount; ++parameterNumber) {
				const PublicQueueCommit& commit = publicQueueCommitStates[parameterNumber];
				if (commit.hasLastPoint) {
					plugIn->updateParameter(parameterNumber, commit.lastValue);
				}
			}
		}

		void applyProgramNormalized(Float32 normalizedValue) {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(plugInInfo->programCount > 1u);
			if (normalizedValue < 0.0f) normalizedValue = 0.0f;
			if (normalizedValue > 1.0f) normalizedValue = 1.0f;
			const UInt32 stepCount = plugInInfo->programCount - 1u;
			const UInt32 newProgram = static_cast<UInt32>(normalizedValue * stepCount + 0.5f);
			if (newProgram != currentProgram) {
				currentProgram = newProgram;
				plugIn->changeProgram(currentProgram);
			}
		}

		enum AudioLifecycleState {
			AUDIO_UNCONFIGURED,
			AUDIO_CONFIGURED,
			AUDIO_ENABLED
		};

		VST3AdapterHost hostBridge;
		SymbiosisLoaderInfo loaderInfo;
		std::string hostApplicationNameStorage;
		std::unique_ptr<HostedFactory> factory;
		std::unique_ptr<PlugInInterface> plugIn;
		AudioLifecycleState currentAudioState;
		UInt32 plugInIndex;
		const SymbiosisPlugInInfo* plugInInfo;
		UInt32 currentProgram;
		bool isBypassed;
		Float32 setupSampleRate;
		UInt32 setupMaxBlockSize;
		UInt32 latencySamples = 0;
		Int32 tailSamples = 0;
		Int64 renderFallbackSamplePosition = 0;
	#if SYMBIOSIS_VST3_ENABLE_LIVE_SETACTIVE_WORKAROUND
		bool liveReactivateNeedsEnable = false;
	#endif

		UInt32 inputBusCount = 0;
		UInt32 outputBusCount = 0;
		std::unique_ptr<UInt32[]> selectedInputBusFormats;
		std::unique_ptr<UInt32[]> selectedInputBusChannelCounts;
		std::unique_ptr<UInt32[]> selectedOutputBusFormats;
		std::unique_ptr<UInt32[]> selectedOutputBusChannelCounts;
		std::unique_ptr<Bool8[]> inputBusConnected;
		std::unique_ptr<Bool8[]> outputBusConnected;
		std::unique_ptr<Bool8[]> inputBusSilent;
		std::unique_ptr<Bool8[]> outputBusSilent;

		UInt32 inputChannelPointerCount = 0;
		UInt32 outputChannelPointerCount = 0;
		std::unique_ptr<const Float32*[]> inputChannelPointers;
		std::unique_ptr<Float32*[]> outputChannelPointers;
		std::unique_ptr<Float32[]> scratchInputSilenceBuffer;
		std::unique_ptr<Float32[]> scratchOutputTrashBuffer;
		UInt32 publicQueueCommitCount = 0;
		std::unique_ptr<PublicQueueCommit[]> publicQueueCommitStates;
		UInt32 inputEventCount = 0;
		std::unique_ptr<SymbiosisEvent[]> inputEvents;
		std::unique_ptr<SymbiosisMidiEventData[]> inputMidiEvents;
		std::unique_ptr<SymbiosisParameterEventData[]> inputParameterEvents;
		SymbiosisVST3PlugView* activeView = 0;
		bool isCustomUIViewOpen = false;
		std::recursive_mutex audioGroupMutex; // Enforces Symbiosis audio ABI non-overlap at VST3 boundary.
};

SymbiosisVST3PlugView::SymbiosisVST3PlugView(SymbiosisVST3Plugin* owner, UInt32 width, UInt32 height)
	: CPluginView()
	, owner(owner) {
	SYMBIOSIS_ASSERT(owner != 0);
	Steinberg::ViewRect rect(0, 0, static_cast<Steinberg::int32>(width), static_cast<Steinberg::int32>(height));
	setRect(rect);
}

Steinberg::tresult PLUGIN_API SymbiosisVST3PlugView::isPlatformTypeSupported(Steinberg::FIDString type) {
#if SMTG_OS_WINDOWS
	return (type != 0 && strcmp(type, Steinberg::kPlatformTypeHWND) == 0) ? Steinberg::kResultTrue : Steinberg::kResultFalse;
#elif SMTG_OS_MACOS
	return (type != 0 && strcmp(type, Steinberg::kPlatformTypeNSView) == 0) ? Steinberg::kResultTrue : Steinberg::kResultFalse;
#else
	(void)type;
	return Steinberg::kResultFalse;
#endif
}

Steinberg::tresult PLUGIN_API SymbiosisVST3PlugView::attached(void* parent, Steinberg::FIDString type) {
	if (isPlatformTypeSupported(type) != Steinberg::kResultTrue) {
		return Steinberg::kResultFalse;
	}
	if (!owner->attachCustomUIView(parent)) {
		return Steinberg::kResultFalse;
	}
	Steinberg::tresult result = CPluginView::attached(parent, type);
	if (result != Steinberg::kResultTrue && result != Steinberg::kResultOk) {
		owner->detachCustomUIView();
		return result;
	}
	isViewAttached = true;
	owner->activeView = this;
	return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API SymbiosisVST3PlugView::removed() {
	if (isViewAttached) {
		owner->detachCustomUIView();
		isViewAttached = false;
	}
	if (owner->activeView == this) {
		owner->activeView = 0;
	}
	return CPluginView::removed();
}

bool SymbiosisVST3PlugView::requestHostResize(UInt32 width, UInt32 height) {
	if (plugFrame == 0) {
		return false;
	}
	Steinberg::ViewRect resizedRect = getRect();
	resizedRect.right = resizedRect.left + static_cast<Steinberg::int32>(width);
	resizedRect.bottom = resizedRect.top + static_cast<Steinberg::int32>(height);
	const Steinberg::tresult result = plugFrame->resizeView(this, &resizedRect);
	if (result != Steinberg::kResultTrue && result != Steinberg::kResultOk) {
		return false;
	}
	setRect(resizedRect);
	return true;
}

void VST3AdapterHost::updateDisplay() {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->onHostUpdateDisplay();
}

void VST3AdapterHost::beginEdit(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->onHostBeginEdit(parameterNumber);
}

void VST3AdapterHost::writeParameter(UInt32 parameterNumber, Float32 normalizedValue) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->onHostWriteParameter(parameterNumber, normalizedValue);
}

void VST3AdapterHost::endEdit(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->onHostEndEdit(parameterNumber);
}

bool VST3AdapterHost::requestResize(UInt32 width, UInt32 height) {
	SYMBIOSIS_ASSERT(owner != 0);
	return owner->onHostRequestResize(width, height);
}

const void* VST3AdapterHost::queryExtension(UInt32 vendorId, UInt32 interfaceId) {
	(void)vendorId;
	(void)interfaceId;
	return 0;
}

} // namespace

SMTG_EXPORT_SYMBOL Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() {
	using namespace Steinberg;
	if (!gPluginFactory) {
		SYMBIOSIS_ASSERT(runSelfTest());
		VST3FactoryIdentity identity;
		std::vector<VST3FactoryMetadata> metadataList;
		if (!queryVST3FactoryMetadata(identity, metadataList)) {
			return 0;
		}
		static PFactoryInfo factoryInfo(identity.vendor, identity.url, identity.email
				, Steinberg::Vst::kDefaultFactoryFlags);
		gPluginFactory = new CPluginFactory(factoryInfo);

		for (const VST3FactoryMetadata& metadata : metadataList) {
			TUID classId;
			metadata.classId.toTUID(classId);
			const PClassInfo2 classInfo(classId, PClassInfo::kManyInstances, kVstAudioEffectClass
					, metadata.className.c_str(), metadata.classFlags, metadata.subCategories.c_str(), 0
					, metadata.version.c_str(), kVstVersionString);
			if (!gPluginFactory->registerClass(&classInfo, SymbiosisVST3Plugin::createInstance
					, reinterpret_cast<void*>(static_cast<uintptr_t>(metadata.plugInIndex)))) {
				gPluginFactory->release();
				gPluginFactory = 0;
				return 0;
			}
		}
	} else {
		gPluginFactory->addRef();
	}
	return gPluginFactory;
}

#if (SYMBIOSIS_VST3_SDK_SELF_CONTAINED)
#if SMTG_OS_MACOS
extern "C" SMTG_EXPORT_SYMBOL bool bundleEntry(void* bundleRef);
extern "C" SMTG_EXPORT_SYMBOL bool bundleExit(void);

extern "C" SMTG_EXPORT_SYMBOL bool bundleEntry(void* bundleRef) {
	(void)bundleRef;
	return true;
}

extern "C" SMTG_EXPORT_SYMBOL bool bundleExit(void) {
	return true;
}
#endif // SMTG_OS_MACOS
#endif // (SYMBIOSIS_VST3_SDK_SELF_CONTAINED)
