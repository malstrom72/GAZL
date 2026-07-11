#include "SymbiosisCpp.h"

#include <algorithm>
#include <memory>
#include <vector>
#include <exception>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "Interfaces/ACF/acfbaseapi.h"
#include "Interfaces/ACF/acfresult.h"
#include "Interfaces/AAX.h"
#include "Interfaces/AAX_CBinaryDisplayDelegate.h"
#include "Interfaces/AAX_CBinaryTaperDelegate.h"
#include "Interfaces/AAX_CEffectParameters.h"
#include "Interfaces/AAX_CEffectGUI.h"
#include "Interfaces/AAX_CLinearTaperDelegate.h"
#include "Interfaces/AAX_CNumberDisplayDelegate.h"
#include "Interfaces/AAX_CParameter.h"
#include "Interfaces/AAX_Errors.h"
#include "Interfaces/AAX_ICollection.h"
#include "Interfaces/AAX_IComponentDescriptor.h"
#include "Interfaces/AAX_IController.h"
#include "Interfaces/AAX_IEffectDescriptor.h"
#include "Interfaces/AAX_IViewContainer.h"
#include "Interfaces/AAX_IMIDINode.h"
#include "Interfaces/AAX_IPropertyMap.h"
#include "Interfaces/AAX_Init.h"

using namespace symbiosis;

#ifndef SYMBIOSIS_AAX_TRACE
#if defined(NDEBUG)
#define SYMBIOSIS_AAX_TRACE 0
#else
#define SYMBIOSIS_AAX_TRACE 1
#endif
#endif

#ifndef SYMBIOSIS_AAX_LIFECYCLE_TRACE
#if defined(NDEBUG)
#define SYMBIOSIS_AAX_LIFECYCLE_TRACE 0
#else
#define SYMBIOSIS_AAX_LIFECYCLE_TRACE 1
#endif
#endif

namespace {

static const UInt32 STATE_MAGIC_SYMB = static_cast<UInt32>('S')
		| (static_cast<UInt32>('y') << 8)
		| (static_cast<UInt32>('m') << 16)
		| (static_cast<UInt32>('b') << 24);
static const UInt32 STATE_ADAPTER_AAX = static_cast<UInt32>('A')
		| (static_cast<UInt32>('A') << 8)
		| (static_cast<UInt32>('X') << 16)
		| (static_cast<UInt32>(' ') << 24);
static const UInt32 STATE_VERSION = 1u;
static const UInt32 AAX_STATE_HEADER_SIZE = 12u;
static const AAX_CTypeID SYMBIOSIS_AAX_STATE_CHUNK_ID = static_cast<AAX_CTypeID>(
		(static_cast<UInt32>('S') << 24)
		| (static_cast<UInt32>('y') << 16)
		| (static_cast<UInt32>('m') << 8)
		| static_cast<UInt32>('b'));

static UInt32 chooseBusFormatMask(UInt32 supportedBusFormats) {
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
	for (UInt32 i = 0u; i < static_cast<UInt32>(sizeof(ORDERED_MASKS) / sizeof(ORDERED_MASKS[0])); ++i) {
		const UInt32 mask = ORDERED_MASKS[i];
		if ((supportedBusFormats & mask) != 0u) {
			return mask;
		}
	}
	SYMBIOSIS_ASSERT(0);
	return SYMBIOSIS_BUS_FORMAT_STEREO_MASK;
}

static UInt32 chooseAAXAuxOutputBusFormatMask(UInt32 supportedBusFormats) {
	SYMBIOSIS_ASSERT(supportedBusFormats != 0);
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u) {
		return SYMBIOSIS_BUS_FORMAT_STEREO_MASK;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_STEREO_MASK) != 0u) {
		return SYMBIOSIS_BUS_FORMAT_STEREO_MASK;
	}
	if ((supportedBusFormats & SYMBIOSIS_BUS_FORMAT_MONO_MASK) != 0u) {
		return SYMBIOSIS_BUS_FORMAT_MONO_MASK;
	}
	SYMBIOSIS_ASSERT(0);
	return SYMBIOSIS_BUS_FORMAT_MONO_MASK;
}

struct AAXMainTypeDescriptor {
	UInt32 inputFormatMask;
	UInt32 outputFormatMask;
	AAX_EStemFormat inputStem;
	AAX_EStemFormat outputStem;
};

struct StemFormatSelection {
	AAX_EStemFormat stem;
	UInt32 formatMask;
};

static bool encodeSymbiosisAAXParameterId(UInt32 parameterIndex, char* outParameterId, size_t outParameterIdSize) {
	SYMBIOSIS_ASSERT(outParameterId != 0);
	SYMBIOSIS_ASSERT(outParameterIdSize > 0u);
	const int written = snprintf(outParameterId, outParameterIdSize, "P%04u", static_cast<unsigned int>(parameterIndex));
	return written > 0 && static_cast<size_t>(written) < outParameterIdSize;
}

#include AAX_ALIGN_FILE_BEGIN
#include AAX_ALIGN_FILE_ALG
#include AAX_ALIGN_FILE_END

struct SymbiosisAAX_Alg_Context {
	int32_t* bufferSize;
	float** inputChannels;
	float** outputChannels;
	AAX_IMIDINode* midiInputNode;
	AAX_IMIDINode* midiOutputNode;
	int32_t* bypass;
	int32_t* sideChainInputIndex;
	UByte8* privateData;
};

#include AAX_ALIGN_FILE_BEGIN
#include AAX_ALIGN_FILE_RESET
#include AAX_ALIGN_FILE_END

enum SymbiosisAAXAlgField {
	SYMBIOSIS_AAX_ALG_FIELD_BUFFER_SIZE = AAX_FIELD_INDEX(SymbiosisAAX_Alg_Context, bufferSize),
	SYMBIOSIS_AAX_ALG_FIELD_AUDIO_IN = AAX_FIELD_INDEX(SymbiosisAAX_Alg_Context, inputChannels),
	SYMBIOSIS_AAX_ALG_FIELD_AUDIO_OUT = AAX_FIELD_INDEX(SymbiosisAAX_Alg_Context, outputChannels),
	SYMBIOSIS_AAX_ALG_FIELD_MIDI_INPUT = AAX_FIELD_INDEX(SymbiosisAAX_Alg_Context, midiInputNode),
	SYMBIOSIS_AAX_ALG_FIELD_MIDI_OUTPUT = AAX_FIELD_INDEX(SymbiosisAAX_Alg_Context, midiOutputNode),
	SYMBIOSIS_AAX_ALG_FIELD_BYPASS = AAX_FIELD_INDEX(SymbiosisAAX_Alg_Context, bypass),
	SYMBIOSIS_AAX_ALG_FIELD_SIDECHAIN_IN = AAX_FIELD_INDEX(SymbiosisAAX_Alg_Context, sideChainInputIndex),
	SYMBIOSIS_AAX_ALG_FIELD_PRIVATE_DATA = AAX_FIELD_INDEX(SymbiosisAAX_Alg_Context, privateData)
};

static void processAAXAlgorithmInstance(SymbiosisAAX_Alg_Context* instance);

static const int32_t SYMBIOSIS_AAX_PRIVATE_DATA_SIZE = 256;
static const UInt32 SYMBIOSIS_AAX_PRIVATE_DATA_MAGIC = static_cast<UInt32>('S')
		| (static_cast<UInt32>('A') << 8)
		| (static_cast<UInt32>('P') << 16)
		| (static_cast<UInt32>('D') << 24);

class AAXAdapterInstance;

static void traceAAXBoundaryException(const UTF8Z* context) {
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
}

#if SYMBIOSIS_AAX_TRACE
static UInt32 hashTraceBytes(const UByte8* data, UInt32 dataSize) {
	UInt32 hash = 2166136261u;
	for (UInt32 index = 0u; index < dataSize; ++index) {
		hash ^= data[index];
		hash *= 16777619u;
	}
	return hash;
}
#endif

static void traceAAXEvent(const char* message) {
#if SYMBIOSIS_AAX_TRACE
	traceMessage("AAX trace", message);
#else
	(void)message;
#endif
}

static void traceAAXLifecycleEvent(const char* message) {
#if SYMBIOSIS_AAX_LIFECYCLE_TRACE
	traceMessage("AAX lifecycle", message);
#else
	(void)message;
#endif
}

#if SYMBIOSIS_AAX_TRACE
static const char* aaxUpdateSourceName(AAX_EUpdateSource source) {
	switch (source) {
		case AAX_eUpdateSource_Unspecified: return "Unspecified";
		case AAX_eUpdateSource_Parameter: return "Parameter";
		case AAX_eUpdateSource_Chunk: return "Chunk";
		case AAX_eUpdateSource_Delay: return "Delay";
	}
	return "Unknown";
}

static const char* aaxNotificationName(AAX_CTypeID notificationType) {
	switch (notificationType) {
		case AAX_eNotificationEvent_SessionBeingOpened: return "SessionBeingOpened";
		case AAX_eNotificationEvent_PresetOpened: return "PresetOpened";
		case AAX_eNotificationEvent_GUIOpened: return "GUIOpened";
		case AAX_eNotificationEvent_GUIClosed: return "GUIClosed";
		case AAX_eNotificationEvent_EnteringOfflineMode: return "EnteringOfflineMode";
		case AAX_eNotificationEvent_ExitingOfflineMode: return "ExitingOfflineMode";
		case AAX_eNotificationEvent_SideChainBeingConnected: return "SideChainBeingConnected";
		case AAX_eNotificationEvent_SideChainBeingDisconnected: return "SideChainBeingDisconnected";
		case AAX_eNotificationEvent_SignalLatencyChanged: return "SignalLatencyChanged";
		case AAX_eNotificationEvent_TrackNameChanged: return "TrackNameChanged";
		case AAX_eNotificationEvent_SessionPathChanged: return "SessionPathChanged";
	}
	return "Unknown";
}
#endif

struct SymbiosisAAXPrivateData {
	UInt32 magic;
	UInt32 reserved;
	AAXAdapterInstance* adapter;
};

static void AAX_CALLBACK symbiosisAAXAlgorithmProcessFunction(SymbiosisAAX_Alg_Context* const inInstancesBegin[]
		, const void* inInstancesEnd) {
	try {
		for (SymbiosisAAX_Alg_Context* const* walk = inInstancesBegin; walk < inInstancesEnd; ++walk) {
			SymbiosisAAX_Alg_Context* const instance = *walk;
			processAAXAlgorithmInstance(instance);
		}
	}
	catch (...) {
		traceAAXBoundaryException("AAX algorithm process");
	}
}

static const UInt32 SYMBIOSIS_FIXED_FORMAT_COUNT = 11u;
static const UInt32 SYMBIOSIS_FIXED_FORMAT_MASKS[SYMBIOSIS_FIXED_FORMAT_COUNT] = {
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

static const AAX_EStemFormat AAX_CANONICAL_STEMS[10] = {
	AAX_eStemFormat_Mono,
	AAX_eStemFormat_Stereo,
	AAX_eStemFormat_LCR,
	AAX_eStemFormat_Quad,
	AAX_eStemFormat_5_0,
	AAX_eStemFormat_5_1,
	AAX_eStemFormat_6_0,
	AAX_eStemFormat_6_1,
	AAX_eStemFormat_7_0_SDDS,
	AAX_eStemFormat_7_1_SDDS
};

static const AAX_EStemFormat AAX_STEM_PREFERENCE[10][10] = {
	{ AAX_eStemFormat_Mono, AAX_eStemFormat_Stereo, AAX_eStemFormat_LCR, AAX_eStemFormat_Quad, AAX_eStemFormat_5_0, AAX_eStemFormat_5_1, AAX_eStemFormat_6_0, AAX_eStemFormat_6_1, AAX_eStemFormat_7_0_SDDS, AAX_eStemFormat_7_1_SDDS },
	{ AAX_eStemFormat_Stereo, AAX_eStemFormat_Mono, AAX_eStemFormat_LCR, AAX_eStemFormat_Quad, AAX_eStemFormat_5_0, AAX_eStemFormat_5_1, AAX_eStemFormat_6_0, AAX_eStemFormat_6_1, AAX_eStemFormat_7_0_SDDS, AAX_eStemFormat_7_1_SDDS },
	{ AAX_eStemFormat_LCR, AAX_eStemFormat_Stereo, AAX_eStemFormat_Quad, AAX_eStemFormat_Mono, AAX_eStemFormat_5_0, AAX_eStemFormat_5_1, AAX_eStemFormat_6_0, AAX_eStemFormat_6_1, AAX_eStemFormat_7_0_SDDS, AAX_eStemFormat_7_1_SDDS },
	{ AAX_eStemFormat_Quad, AAX_eStemFormat_LCR, AAX_eStemFormat_5_0, AAX_eStemFormat_Stereo, AAX_eStemFormat_5_1, AAX_eStemFormat_6_0, AAX_eStemFormat_Mono, AAX_eStemFormat_6_1, AAX_eStemFormat_7_0_SDDS, AAX_eStemFormat_7_1_SDDS },
	{ AAX_eStemFormat_5_0, AAX_eStemFormat_Quad, AAX_eStemFormat_5_1, AAX_eStemFormat_6_0, AAX_eStemFormat_LCR, AAX_eStemFormat_6_1, AAX_eStemFormat_7_0_SDDS, AAX_eStemFormat_Stereo, AAX_eStemFormat_7_1_SDDS, AAX_eStemFormat_Mono },
	{ AAX_eStemFormat_5_1, AAX_eStemFormat_6_0, AAX_eStemFormat_5_0, AAX_eStemFormat_6_1, AAX_eStemFormat_7_0_SDDS, AAX_eStemFormat_Quad, AAX_eStemFormat_7_1_SDDS, AAX_eStemFormat_LCR, AAX_eStemFormat_Stereo, AAX_eStemFormat_Mono },
	{ AAX_eStemFormat_6_0, AAX_eStemFormat_5_1, AAX_eStemFormat_5_0, AAX_eStemFormat_6_1, AAX_eStemFormat_7_0_SDDS, AAX_eStemFormat_Quad, AAX_eStemFormat_7_1_SDDS, AAX_eStemFormat_LCR, AAX_eStemFormat_Stereo, AAX_eStemFormat_Mono },
	{ AAX_eStemFormat_6_1, AAX_eStemFormat_7_0_SDDS, AAX_eStemFormat_6_0, AAX_eStemFormat_5_1, AAX_eStemFormat_7_1_SDDS, AAX_eStemFormat_5_0, AAX_eStemFormat_Quad, AAX_eStemFormat_LCR, AAX_eStemFormat_Stereo, AAX_eStemFormat_Mono },
	{ AAX_eStemFormat_7_0_SDDS, AAX_eStemFormat_6_1, AAX_eStemFormat_6_0, AAX_eStemFormat_5_1, AAX_eStemFormat_7_1_SDDS, AAX_eStemFormat_5_0, AAX_eStemFormat_Quad, AAX_eStemFormat_LCR, AAX_eStemFormat_Stereo, AAX_eStemFormat_Mono },
	{ AAX_eStemFormat_7_1_SDDS, AAX_eStemFormat_7_0_SDDS, AAX_eStemFormat_6_1, AAX_eStemFormat_6_0, AAX_eStemFormat_5_1, AAX_eStemFormat_5_0, AAX_eStemFormat_Quad, AAX_eStemFormat_LCR, AAX_eStemFormat_Stereo, AAX_eStemFormat_Mono }
};

static AAX_EStemFormat symbiosisBusFormatMaskToAAXStem(UInt32 formatMask) {
	switch (formatMask) {
		case SYMBIOSIS_BUS_FORMAT_MONO_MASK: return AAX_eStemFormat_Mono;
		case SYMBIOSIS_BUS_FORMAT_STEREO_MASK: return AAX_eStemFormat_Stereo;
		case SYMBIOSIS_BUS_FORMAT_LCR_MASK: return AAX_eStemFormat_LCR;
		case SYMBIOSIS_BUS_FORMAT_QUAD_MASK: return AAX_eStemFormat_Quad;
		case SYMBIOSIS_BUS_FORMAT_5_0_MASK: return AAX_eStemFormat_5_0;
		case SYMBIOSIS_BUS_FORMAT_5_1_MASK: return AAX_eStemFormat_5_1;
		case SYMBIOSIS_BUS_FORMAT_6_0_CINE_MASK: return AAX_eStemFormat_6_0;
		case SYMBIOSIS_BUS_FORMAT_6_1_CINE_MASK: return AAX_eStemFormat_6_1;
		case SYMBIOSIS_BUS_FORMAT_7_0_CINE_MASK: return AAX_eStemFormat_7_0_SDDS;
		case SYMBIOSIS_BUS_FORMAT_7_1_CINE_MASK: return AAX_eStemFormat_7_1_SDDS;
		case SYMBIOSIS_BUS_FORMAT_7_1_MUSIC_MASK: return AAX_eStemFormat_7_1_SDDS;
		case SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK: return AAX_eStemFormat_Any;
	}
	SYMBIOSIS_ASSERT(0);
	return AAX_eStemFormat_Any;
}

static Int32 stemIndex(AAX_EStemFormat stem) {
	for (Int32 i = 0; i < 10; ++i) {
		if (AAX_CANONICAL_STEMS[i] == stem) {
			return i;
		}
	}
	return -1;
}

static void appendSupportedFixedFormats(UInt32 supportedMask, std::vector<UInt32>* formatMasks) {
	SYMBIOSIS_ASSERT(formatMasks != 0);
	if ((supportedMask & SYMBIOSIS_BUS_FORMAT_ANY_COUNT_MASK) != 0u) {
		for (UInt32 i = 0u; i < SYMBIOSIS_FIXED_FORMAT_COUNT; ++i) {
			formatMasks->push_back(SYMBIOSIS_FIXED_FORMAT_MASKS[i]);
		}
		return;
	}
	for (UInt32 i = 0u; i < SYMBIOSIS_FIXED_FORMAT_COUNT; ++i) {
		const UInt32 mask = SYMBIOSIS_FIXED_FORMAT_MASKS[i];
		if ((supportedMask & mask) != 0u) {
			formatMasks->push_back(mask);
		}
	}
}

static void buildUniqueStemSelections(UInt32 supportedMask, std::vector<StemFormatSelection>* outSelections) {
	SYMBIOSIS_ASSERT(outSelections != 0);
	outSelections->clear();
	std::vector<UInt32> supportedFormats;
	appendSupportedFixedFormats(supportedMask, &supportedFormats);
	for (UInt32 i = 0u; i < static_cast<UInt32>(supportedFormats.size()); ++i) {
		const UInt32 formatMask = supportedFormats[i];
		const AAX_EStemFormat stem = symbiosisBusFormatMaskToAAXStem(formatMask);
		bool alreadyAdded = false;
		for (UInt32 j = 0u; j < static_cast<UInt32>(outSelections->size()); ++j) {
			if ((*outSelections)[j].stem == stem) {
				alreadyAdded = true;
				break;
			}
		}
		if (!alreadyAdded) {
			StemFormatSelection selection;
			selection.stem = stem;
			selection.formatMask = formatMask;
			outSelections->push_back(selection);
		}
	}
}

static StemFormatSelection choosePreferredOppositeSelection(AAX_EStemFormat driverStem
		, const std::vector<StemFormatSelection>& oppositeSelections) {
	const Int32 driverIndex = stemIndex(driverStem);
	SYMBIOSIS_ASSERT(driverIndex >= 0);
	for (UInt32 prefIndex = 0u; prefIndex < 10u; ++prefIndex) {
		const AAX_EStemFormat preferredStem = AAX_STEM_PREFERENCE[driverIndex][prefIndex];
		for (UInt32 candidateIndex = 0u; candidateIndex < static_cast<UInt32>(oppositeSelections.size()); ++candidateIndex) {
			if (oppositeSelections[candidateIndex].stem == preferredStem) {
				return oppositeSelections[candidateIndex];
			}
		}
	}
	SYMBIOSIS_ASSERT(0);
	return oppositeSelections[0];
}

static void buildAAXTypeList(UInt32 inputSupportedMask, UInt32 outputSupportedMask
		, std::vector<AAXMainTypeDescriptor>* outTypes) {
	SYMBIOSIS_ASSERT(outTypes != 0);
	outTypes->clear();
	std::vector<StemFormatSelection> inputSelections;
	std::vector<StemFormatSelection> outputSelections;
	buildUniqueStemSelections(inputSupportedMask, &inputSelections);
	buildUniqueStemSelections(outputSupportedMask, &outputSelections);
	SYMBIOSIS_ASSERT(!inputSelections.empty());
	SYMBIOSIS_ASSERT(!outputSelections.empty());

	const bool ioCoupled = ((inputSupportedMask & SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK) != 0u);
	const bool inputDrives = (ioCoupled || inputSelections.size() >= outputSelections.size());

	if (inputDrives) {
		for (UInt32 i = 0u; i < static_cast<UInt32>(inputSelections.size()); ++i) {
			const StemFormatSelection inputSelection = inputSelections[i];
			const StemFormatSelection outputSelection = choosePreferredOppositeSelection(inputSelection.stem, outputSelections);
			AAXMainTypeDescriptor desc;
			desc.inputFormatMask = inputSelection.formatMask;
			desc.outputFormatMask = outputSelection.formatMask;
			desc.inputStem = inputSelection.stem;
			desc.outputStem = outputSelection.stem;
			outTypes->push_back(desc);
		}
	} else {
		for (UInt32 i = 0u; i < static_cast<UInt32>(outputSelections.size()); ++i) {
			const StemFormatSelection outputSelection = outputSelections[i];
			const StemFormatSelection inputSelection = choosePreferredOppositeSelection(outputSelection.stem, inputSelections);
			AAXMainTypeDescriptor desc;
			desc.inputFormatMask = inputSelection.formatMask;
			desc.outputFormatMask = outputSelection.formatMask;
			desc.inputStem = inputSelection.stem;
			desc.outputStem = outputSelection.stem;
			outTypes->push_back(desc);
		}
	}
}

static void buildAAXOutputOnlyTypeList(UInt32 outputSupportedMask
		, std::vector<AAXMainTypeDescriptor>* outTypes) {
	SYMBIOSIS_ASSERT(outTypes != 0);
	outTypes->clear();
	std::vector<StemFormatSelection> outputSelections;
	buildUniqueStemSelections(outputSupportedMask, &outputSelections);
	SYMBIOSIS_ASSERT(!outputSelections.empty());
	for (UInt32 i = 0u; i < static_cast<UInt32>(outputSelections.size()); ++i) {
		const StemFormatSelection outputSelection = outputSelections[i];
		AAXMainTypeDescriptor desc;
		desc.inputFormatMask = outputSelection.formatMask;
		desc.outputFormatMask = outputSelection.formatMask;
		desc.inputStem = outputSelection.stem;
		desc.outputStem = outputSelection.stem;
		outTypes->push_back(desc);
	}
}

static uint32_t mapPlugInTypeToAAXRole(UInt32 /*plugInType*/) {
	return static_cast<uint32_t>(AAX_ePlugInRole_InsertOrAudioSuite);
}

static uint32_t mapPlugInCategoriesToAAXCategoryBits(UInt32 categoriesMask) {
	uint32_t bits = 0u;
	if ((categoriesMask & SYMBIOSIS_PLUGIN_CATEGORY_EQ_MASK) != 0u) bits |= static_cast<uint32_t>(AAX_ePlugInCategory_EQ);
	if ((categoriesMask & SYMBIOSIS_PLUGIN_CATEGORY_DYNAMICS_MASK) != 0u) bits |= static_cast<uint32_t>(AAX_ePlugInCategory_Dynamics);
	if ((categoriesMask & SYMBIOSIS_PLUGIN_CATEGORY_REVERB_MASK) != 0u) bits |= static_cast<uint32_t>(AAX_ePlugInCategory_Reverb);
	if ((categoriesMask & SYMBIOSIS_PLUGIN_CATEGORY_DELAY_MASK) != 0u) bits |= static_cast<uint32_t>(AAX_ePlugInCategory_Delay);
	if ((categoriesMask & SYMBIOSIS_PLUGIN_CATEGORY_MODULATION_MASK) != 0u) bits |= static_cast<uint32_t>(AAX_ePlugInCategory_Modulation);
	if ((categoriesMask & SYMBIOSIS_PLUGIN_CATEGORY_DISTORTION_MASK) != 0u) bits |= static_cast<uint32_t>(AAX_ePlugInCategory_Harmonic);
	if ((categoriesMask & SYMBIOSIS_PLUGIN_CATEGORY_PITCH_MASK) != 0u) bits |= static_cast<uint32_t>(AAX_ePlugInCategory_PitchShift);
	if ((categoriesMask & SYMBIOSIS_PLUGIN_CATEGORY_SPATIAL_MASK) != 0u) bits |= static_cast<uint32_t>(AAX_ePlugInCategory_SoundField);
	if ((categoriesMask & SYMBIOSIS_PLUGIN_CATEGORY_RESTORATION_MASK) != 0u) bits |= static_cast<uint32_t>(AAX_ePlugInCategory_NoiseReduction);
	return bits;
}

static uint32_t mapPlugInTypeToAAXCategoryBits(UInt32 plugInType) {
	if (plugInType == SYMBIOSIS_PLUGIN_TYPE_INSTRUMENT) {
		return static_cast<uint32_t>(AAX_ePlugInCategory_SWGenerators);
	}
	if (plugInType == SYMBIOSIS_PLUGIN_TYPE_MIDI_PROCESSOR) {
		return static_cast<uint32_t>(AAX_ePlugInCategory_MIDIEffect);
	}
	return 0u;
}

static bool isMonoOrStereoStem(AAX_EStemFormat stem) {
	return stem == AAX_eStemFormat_Mono || stem == AAX_eStemFormat_Stereo;
}

static AAX_CTypeID makeAAXFourCC(unsigned char c0, unsigned char c1, unsigned char c2, unsigned char c3) {
	return static_cast<AAX_CTypeID>((static_cast<UInt32>(c0) << 24)
			| (static_cast<UInt32>(c1) << 16)
			| (static_cast<UInt32>(c2) << 8)
			| static_cast<UInt32>(c3));
}

static AAX_CTypeID buildProductTypeId(UInt32 plugInId) {
	return static_cast<AAX_CTypeID>(plugInId);
}

static AAX_CTypeID buildNativeTypeId(UInt32 /*plugInId*/, UInt32 typeIndex) {
	static const unsigned char SUFFIXES[] = "P123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const UInt32 suffixIndex = (typeIndex < static_cast<UInt32>(sizeof(SUFFIXES) - 1u)
			? typeIndex
			: static_cast<UInt32>((sizeof(SUFFIXES) - 2u)));
	return makeAAXFourCC('N', 'a', 't', SUFFIXES[suffixIndex]);
}

static AAX_CTypeID buildAudioSuiteTypeId(UInt32 /*plugInId*/, UInt32 typeIndex) {
	static const unsigned char SUFFIXES[] = "Aabcdefghijklmnoqrstuvwxyz23456789";
	const UInt32 suffixIndex = (typeIndex < static_cast<UInt32>(sizeof(SUFFIXES) - 1u)
			? typeIndex
			: static_cast<UInt32>((sizeof(SUFFIXES) - 2u)));
	return makeAAXFourCC('A', 'u', 'S', SUFFIXES[suffixIndex]);
}

class AAXAdapterHost : public HostInterface {
	public:
		explicit AAXAdapterHost(AAXAdapterInstance* owner) : owner(owner) {}
		void updateDisplay() override;
		void beginEdit(UInt32 parameterNumber) override;
		void writeParameter(UInt32 parameterNumber, Float32 normalizedValue) override;
		void endEdit(UInt32 parameterNumber) override;
		bool requestResize(UInt32 width, UInt32 height) override;
		const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) override;

	private:
		AAXAdapterInstance* owner;
};

class AAXAdapterInstance {
	public:
		enum AudioLifecycleState {
			AUDIO_UNCONFIGURED = 0,
			AUDIO_CONFIGURED = 1,
			AUDIO_ALLOCATED = 2,
			AUDIO_PROCESSING = 3
		};

		static const char* audioLifecycleStateName(AudioLifecycleState state) {
			switch (state) {
				case AUDIO_UNCONFIGURED: return "UNCONFIGURED";
				case AUDIO_CONFIGURED: return "CONFIGURED";
				case AUDIO_ALLOCATED: return "ALLOCATED";
				case AUDIO_PROCESSING: return "PROCESSING";
			}
			SYMBIOSIS_ASSERT(0);
			return "UNKNOWN";
		}

		void traceAudioLifecycle(const char* event) const {
#if SYMBIOSIS_AAX_LIFECYCLE_TRACE
			char message[192];
			snprintf(message, sizeof(message), "%s state=%s type=%u sr=%.1f max=%u in=%u out=%u",
					event,
					audioLifecycleStateName(audioState),
					static_cast<unsigned int>(configuredMainTypeIndex),
					static_cast<double>(configuredSampleRate),
					static_cast<unsigned int>(configuredMaxBufferSize),
					static_cast<unsigned int>(activeInputChannelCount),
					static_cast<unsigned int>(activeOutputChannelCount));
			traceMessage("AAX lifecycle", message);
#else
			(void)event;
#endif
		}

		void traceAudioLifecycleTransition(const char* event, AudioLifecycleState fromState, AudioLifecycleState toState) const {
#if SYMBIOSIS_AAX_LIFECYCLE_TRACE
			char message[224];
			snprintf(message, sizeof(message), "%s %s->%s type=%u sr=%.1f max=%u in=%u out=%u",
					event,
					audioLifecycleStateName(fromState),
					audioLifecycleStateName(toState),
					static_cast<unsigned int>(configuredMainTypeIndex),
					static_cast<double>(configuredSampleRate),
					static_cast<unsigned int>(configuredMaxBufferSize),
					static_cast<unsigned int>(activeInputChannelCount),
					static_cast<unsigned int>(activeOutputChannelCount));
			traceMessage("AAX lifecycle", message);
#else
			(void)event;
			(void)fromState;
			(void)toState;
#endif
		}

		void traceUILifecycle(const char* event) const {
#if SYMBIOSIS_AAX_LIFECYCLE_TRACE
			char message[192];
			snprintf(message, sizeof(message), "%s uiOpen=%d boundView=%p nativeView=%p",
					event,
					static_cast<int>(uiOpen),
					static_cast<void*>(boundViewContainer),
					nativeParentView);
			traceAAXLifecycleEvent(message);
#else
			(void)event;
#endif
		}

			AAXAdapterInstance()
			: host(this)
			, metadataInitialized(false)
			, plugInInfo(0)
			, audioState(AUDIO_UNCONFIGURED)
			, inputBusCount(0u)
			, outputBusCount(0u)
			, inputChannelCount(0u)
			, outputChannelCount(0u)
			, activeInputChannelCount(0u)
			, activeOutputChannelCount(0u)
				, configuredMainTypeIndex(0u)
				, configuredSampleRate(44100.0f)
				, configuredMaxBufferSize(1024u)
				, configuredLatencySamples(0u)
				, configuredTailSamples(0)
				, renderFallbackSamplePosition(0)
#if SYMBIOSIS_AAX_TRACE
				, lastTracedTransportRunning(2)
#endif
				, aaxRole(static_cast<uint32_t>(AAX_ePlugInRole_InsertOrAudioSuite))
				, aaxCategoryBits(0u)
				, aaxSidechainStem(AAX_eStemFormat_None)
				, currentInputEventCount(0u)
			, uiOpen(false)
			, nativeParentView(0)
			, nativeParentWindow(0)
				{
					memset(&loaderInfo, 0, sizeof(loaderInfo));
					memset(factoryErrorText, 0, sizeof(factoryErrorText));
					memset(hostApplicationName, 0, sizeof(hostApplicationName));
					memset(hostApplicationVendor, 0, sizeof(hostApplicationVendor));
					traceAAXEvent("adapter constructed");
					traceAudioLifecycle("adapter constructed");
				}

				~AAXAdapterInstance() {
					traceAAXEvent("adapter destruct begin");
					traceAudioLifecycle("adapter destruct begin");
					closeUIViewFromAAX();
					if (audioState == AUDIO_PROCESSING) {
						deactivateProcessing();
					}
					if (audioState == AUDIO_ALLOCATED) {
						releaseAudioResourcesToConfigured();
					}
					traceAudioLifecycle("adapter destruct end");
					traceAAXEvent("adapter destruct end");
				}

			bool initializeMetadata(IACFUnknown* /*pUnkHost*/) {
				if (metadataInitialized) {
					return (factory.get() != 0 && plugInInfo != 0);
				}
				metadataInitialized = true;
				SYMBIOSIS_ASSERT(STATE_MAGIC_SYMB != 0u);
				SYMBIOSIS_ASSERT(STATE_ADAPTER_AAX != 0u);
				SYMBIOSIS_ASSERT(STATE_VERSION == 1u);

				memset(&loaderInfo, 0, sizeof(loaderInfo));
				loaderInfo.structVersion = 1;
				loaderInfo.maxSymbiosisVersion = 1;
				loaderInfo.adapterFormat = "AAX";
				loaderInfo.applicationName = (hostApplicationName[0] != 0 ? hostApplicationName : 0);
				loaderInfo.applicationVendor = (hostApplicationVendor[0] != 0 ? hostApplicationVendor : 0);

				const SymbiosisFactoryInterface* factoryApi = 0;
				SymbiosisFactory* factoryInstance = symbiosisCreateFactory(
						&loaderInfo,
						&factoryApi,
						static_cast<UInt32>(sizeof(factoryErrorText) - 1u),
						factoryErrorText);
				if (factoryInstance == 0 || factoryApi == 0) {
					traceMessage("AAX bootstrap", (factoryErrorText[0] != 0 ? factoryErrorText : "symbiosisCreateFactory failed"));
					return false;
				}

				factory.reset(new HostedFactory(factoryInstance, factoryApi));
				if (factory->getPlugInCount() == 0) {
					traceMessage("AAX bootstrap", "Factory has no plug-ins");
					factory.reset();
					return false;
				}

				plugInInfo = factory->getPlugInInfo(0u);
				SYMBIOSIS_ASSERT(plugInInfo != 0);
				SYMBIOSIS_ASSERT(plugInInfo->programCount > 0);
				SYMBIOSIS_ASSERT(plugInInfo->plugInId != 0);
				inputBusCount = plugInInfo->audioInputBusCount;
				outputBusCount = plugInInfo->audioOutputBusCount;
				if (inputBusCount > 0u && outputBusCount > 0u) {
					buildAAXTypeList(
							plugInInfo->audioInputBuses[0].supportedBusFormats,
							plugInInfo->audioOutputBuses[0].supportedBusFormats,
							&aaxMainTypes);
				} else if (outputBusCount > 0u) {
					buildAAXOutputOnlyTypeList(
							plugInInfo->audioOutputBuses[0].supportedBusFormats,
							&aaxMainTypes);
				}
				aaxRole = mapPlugInTypeToAAXRole(plugInInfo->plugInType);
				aaxCategoryBits = mapPlugInCategoriesToAAXCategoryBits(plugInInfo->plugInCategories)
						| mapPlugInTypeToAAXCategoryBits(plugInInfo->plugInType);
				// The AAX sidechain stem is mono-only. Strict bus contract: expose it only when the
				// second input bus actually declares a mono format, so it maps 1:1 with the host key
				// (no channel fabrication). A side bus without mono support gets no AAX sidechain and is
				// left unconnected/silent (see docs/SymbiosisBusHandling.md).
				aaxSidechainStem = AAX_eStemFormat_None;
				if (inputBusCount > 1u
						&& (plugInInfo->audioInputBuses[1].supportedBusFormats & SYMBIOSIS_BUS_FORMAT_MONO_MASK) != 0u) {
					aaxSidechainStem = AAX_eStemFormat_Mono;
				}
				if (aaxSidechainStem != AAX_eStemFormat_None) {
					std::vector<AAXMainTypeDescriptor> filteredTypes;
					for (UInt32 i = 0u; i < static_cast<UInt32>(aaxMainTypes.size()); ++i) {
						const AAXMainTypeDescriptor& type = aaxMainTypes[i];
						if (isMonoOrStereoStem(type.inputStem) && isMonoOrStereoStem(type.outputStem)) {
							filteredTypes.push_back(type);
						}
					}
					if (!filteredTypes.empty()) {
						aaxMainTypes.swap(filteredTypes);
					}
				}
				aaxAuxOutputStems.clear();
				for (UInt32 busIndex = 1u; busIndex < outputBusCount; ++busIndex) {
					const UInt32 selected = chooseAAXAuxOutputBusFormatMask(
							plugInInfo->audioOutputBuses[busIndex].supportedBusFormats);
					const AAX_EStemFormat stem = symbiosisBusFormatMaskToAAXStem(selected);
					SYMBIOSIS_ASSERT(stem == AAX_eStemFormat_Mono || stem == AAX_eStemFormat_Stereo);
					aaxAuxOutputStems.push_back(stem);
				}

				return true;
			}

			bool initialize(IACFUnknown* pUnkHost) {
				if (plugIn.get() != 0) {
					return true;
				}
				if (!initializeMetadata(pUnkHost)) {
					return false;
				}

				if (inputBusCount > 0u) {
					selectedInputBusFormatValues.reset(new UInt32[inputBusCount]);
					selectedInputBusChannelCounts.reset(new UInt32[inputBusCount]);
					inputBusConnectedFlags.reset(new Bool8[inputBusCount]);
					inputBusSilentFlags.reset(new Bool8[inputBusCount]);
					for (UInt32 i = 0u; i < inputBusCount; ++i) {
						const UInt32 supported = plugInInfo->audioInputBuses[i].supportedBusFormats;
						// Input bus 1 is the AAX sidechain when exposed; its stem is mono, so select the
						// matching mono format 1:1 — the adapter must not fabricate a multichannel side feed
						// (strict bus contract). The plug-in may duplicate mono->wider sockets internally.
						const UInt32 selected = ((i == 1u && aaxSidechainStem == AAX_eStemFormat_Mono)
								? static_cast<UInt32>(SYMBIOSIS_BUS_FORMAT_MONO_MASK)
								: chooseBusFormatMask(supported));
						const UInt32 channels = busFormatMaskToChannelCount(selected);
						selectedInputBusFormatValues[i] = selected;
						selectedInputBusChannelCounts[i] = channels;
						inputBusConnectedFlags[i] = 1;
						inputBusSilentFlags[i] = 0;
					}
				}

				if (outputBusCount > 0u) {
					selectedOutputBusFormatValues.reset(new UInt32[outputBusCount]);
					selectedOutputBusChannelCounts.reset(new UInt32[outputBusCount]);
					outputBusConnectedFlags.reset(new Bool8[outputBusCount]);
					outputBusSilentFlags.reset(new Bool8[outputBusCount]);
					for (UInt32 i = 0u; i < outputBusCount; ++i) {
						const UInt32 supported = plugInInfo->audioOutputBuses[i].supportedBusFormats;
						const UInt32 selected = (i == 0u
								? chooseBusFormatMask(supported)
								: chooseAAXAuxOutputBusFormatMask(supported));
						const UInt32 channels = busFormatMaskToChannelCount(selected);
						selectedOutputBusFormatValues[i] = selected;
						selectedOutputBusChannelCounts[i] = channels;
						outputBusConnectedFlags[i] = 1;
						outputBusSilentFlags[i] = 0;
					}
				}

				const UInt32 nonMainInputChannels = [] (const UInt32* busChannelCounts, UInt32 busCount) -> UInt32 {
					UInt32 sum = 0u;
					for (UInt32 bus = 1u; bus < busCount; ++bus) {
						sum += busChannelCounts[bus];
					}
					return sum;
				}(selectedInputBusChannelCounts.get(), inputBusCount);
				const UInt32 nonMainOutputChannels = [] (const UInt32* busChannelCounts, UInt32 busCount) -> UInt32 {
					UInt32 sum = 0u;
					for (UInt32 bus = 1u; bus < busCount; ++bus) {
						sum += busChannelCounts[bus];
					}
					return sum;
				}(selectedOutputBusChannelCounts.get(), outputBusCount);

				UInt32 maxMainInputChannels = (inputBusCount > 0u ? selectedInputBusChannelCounts[0] : 0u);
				UInt32 maxMainOutputChannels = (outputBusCount > 0u ? selectedOutputBusChannelCounts[0] : 0u);
				for (UInt32 typeIndex = 0u; typeIndex < static_cast<UInt32>(aaxMainTypes.size()); ++typeIndex) {
					const AAXMainTypeDescriptor& type = aaxMainTypes[typeIndex];
					const UInt32 thisMainInputChannels = (type.inputStem != AAX_eStemFormat_None
							? busFormatMaskToChannelCount(type.inputFormatMask) : 0u);
					const UInt32 thisMainOutputChannels = busFormatMaskToChannelCount(type.outputFormatMask);
					if (thisMainInputChannels > maxMainInputChannels) {
						maxMainInputChannels = thisMainInputChannels;
					}
					if (thisMainOutputChannels > maxMainOutputChannels) {
						maxMainOutputChannels = thisMainOutputChannels;
					}
				}
				inputChannelCount = nonMainInputChannels + maxMainInputChannels;
				outputChannelCount = nonMainOutputChannels + maxMainOutputChannels;

				if (inputChannelCount > 0u) {
					inputChannelPointers.reset(new const Float32*[inputChannelCount]);
				}
				if (outputChannelCount > 0u) {
					outputChannelPointers.reset(new Float32*[outputChannelCount]);
				}

				const UInt32 maxInputEventCount = plugInInfo->maxInputEventCountPerBlock;
				if (maxInputEventCount > 0u) {
					pendingInputEvents.reset(new SymbiosisEvent[maxInputEventCount]);
					pendingInputEventData.reset(new SymbiosisMidiEventData[maxInputEventCount]);
				}
				const UInt32 maxOutputEventCount = plugInInfo->maxOutputEventCountPerBlock;
				if (maxOutputEventCount > 0u) {
					pendingOutputEvents.reset(new SymbiosisEvent[maxOutputEventCount]);
					pendingOutputEventData.reset(new SymbiosisMidiEventData[maxOutputEventCount]);
				}

					plugIn.reset(factory->createPlugIn(0u, &host));
					if (!plugIn) {
						UTF8Z errorText[1024];
						errorText[0] = 0;
						factory->getLastErrorText(static_cast<UInt32>(sizeof(errorText) - 1u), errorText);
					traceMessage("AAX createPlugIn", (errorText[0] != 0 ? errorText : "createPlugIn failed"));
					return false;
				}
					SYMBIOSIS_ASSERT(audioState == AUDIO_UNCONFIGURED);
					traceProgramSnapshot("after createPlugIn");

					return true;
				}

			void resolveAndConfigureAudioType(UInt32 mainTypeIndex, Float32 sampleRate, UInt32 maxBufferSize) {
				SYMBIOSIS_ASSERT(audioState == AUDIO_UNCONFIGURED || audioState == AUDIO_CONFIGURED);
				SYMBIOSIS_ASSERT(sampleRate > 0.0f);
				SYMBIOSIS_ASSERT(maxBufferSize > 0u);
				SYMBIOSIS_ASSERT(mainTypeIndex < static_cast<UInt32>(aaxMainTypes.size()));
				const AAXMainTypeDescriptor& mainType = aaxMainTypes[mainTypeIndex];
				if (inputBusCount > 0u) {
					selectedInputBusFormatValues[0] = mainType.inputFormatMask;
					selectedInputBusChannelCounts[0] = busFormatMaskToChannelCount(mainType.inputFormatMask);
				}
				if (outputBusCount > 0u) {
					selectedOutputBusFormatValues[0] = mainType.outputFormatMask;
					selectedOutputBusChannelCounts[0] = busFormatMaskToChannelCount(mainType.outputFormatMask);
				}
				configuredMainTypeIndex = mainTypeIndex;
				configuredSampleRate = sampleRate;
				configuredMaxBufferSize = maxBufferSize;
				traceAudioLifecycle("resolveAndConfigureAudioType begin");
				configureAudioForCurrentType();
			}

		void allocateAudioResources() {
			SYMBIOSIS_ASSERT(audioState == AUDIO_CONFIGURED);
			const AudioLifecycleState fromState = audioState;
			audioState = AUDIO_ALLOCATED;
			traceAudioLifecycleTransition("allocateAudioResources", fromState, audioState);
		}

		void activateProcessing() {
			SYMBIOSIS_ASSERT(audioState == AUDIO_ALLOCATED);
			const AudioLifecycleState fromState = audioState;
			plugIn->enableAudio();
			audioState = AUDIO_PROCESSING;
			traceAudioLifecycleTransition("activateProcessing enableAudio", fromState, audioState);
		}

		void deactivateProcessing() {
			SYMBIOSIS_ASSERT(audioState == AUDIO_PROCESSING || audioState == AUDIO_ALLOCATED);
			if (audioState == AUDIO_PROCESSING) {
				const AudioLifecycleState fromState = audioState;
				plugIn->disableAudio();
				audioState = AUDIO_ALLOCATED;
				traceAudioLifecycleTransition("deactivateProcessing disableAudio", fromState, audioState);
			} else {
				traceAudioLifecycle("deactivateProcessing already allocated");
			}
		}

		void releaseAudioResourcesToConfigured() {
			SYMBIOSIS_ASSERT(audioState == AUDIO_ALLOCATED || audioState == AUDIO_CONFIGURED);
			if (audioState == AUDIO_ALLOCATED) {
				const AudioLifecycleState fromState = audioState;
				audioState = AUDIO_CONFIGURED;
				traceAudioLifecycleTransition("releaseAudioResources", fromState, audioState);
			} else {
				traceAudioLifecycle("releaseAudioResources already configured");
			}
		}

		void setProcessingActive(bool active) {
			traceAudioLifecycle(active ? "setProcessingActive(true)" : "setProcessingActive(false)");
			if (active) {
				if (audioState == AUDIO_CONFIGURED) {
					allocateAudioResources();
				}
				if (audioState == AUDIO_ALLOCATED) {
					activateProcessing();
				}
			} else {
				deactivateProcessing();
			}
		}

		void reconfigureAudioType(UInt32 mainTypeIndex, Float32 sampleRate, UInt32 maxBufferSize) {
			traceAudioLifecycle("reconfigureAudioType begin");
			if (audioState == AUDIO_PROCESSING) {
				deactivateProcessing();
			}
			if (audioState == AUDIO_ALLOCATED) {
				releaseAudioResourcesToConfigured();
			}
			SYMBIOSIS_ASSERT(audioState == AUDIO_CONFIGURED || audioState == AUDIO_UNCONFIGURED);
			const AudioLifecycleState fromState = audioState;
			audioState = AUDIO_UNCONFIGURED;
			traceAudioLifecycleTransition("reconfigureAudioType unconfigure", fromState, audioState);
			resolveAndConfigureAudioType(mainTypeIndex, sampleRate, maxBufferSize);
			reportLatencyToAAXController();
			traceAudioLifecycle("reconfigureAudioType end");
		}

		bool findMainTypeIndexByStems(AAX_EStemFormat inputStem, AAX_EStemFormat outputStem, UInt32* outMainTypeIndex) const {
			SYMBIOSIS_ASSERT(outMainTypeIndex != 0);
			for (UInt32 typeIndex = 0u; typeIndex < static_cast<UInt32>(aaxMainTypes.size()); ++typeIndex) {
				const AAXMainTypeDescriptor& mainType = aaxMainTypes[typeIndex];
				if (mainType.inputStem == inputStem && mainType.outputStem == outputStem) {
					*outMainTypeIndex = typeIndex;
					return true;
				}
			}
			return false;
		}

		void syncAudioConfigurationFromController(const AAX_IController* controller) {
			AAX_CSampleRate sampleRate = 0.0;
			Float32 resolvedSampleRate = configuredSampleRate;
			if (controller != 0 && controller->GetSampleRate(&sampleRate) == AAX_SUCCESS && sampleRate > 0.0) {
				resolvedSampleRate = static_cast<Float32>(sampleRate);
			}
			UInt32 resolvedTypeIndex = configuredMainTypeIndex;
			if (controller != 0) {
				AAX_EStemFormat inputStem = AAX_eStemFormat_None;
				AAX_EStemFormat outputStem = AAX_eStemFormat_None;
				if (controller->GetInputStemFormat(&inputStem) == AAX_SUCCESS
						&& controller->GetOutputStemFormat(&outputStem) == AAX_SUCCESS) {
					(void)findMainTypeIndexByStems(inputStem, outputStem, &resolvedTypeIndex);
				}
			}
			if (audioState == AUDIO_UNCONFIGURED) {
				resolveAndConfigureAudioType(resolvedTypeIndex, resolvedSampleRate, configuredMaxBufferSize);
				return;
			}
			if (resolvedTypeIndex != configuredMainTypeIndex || resolvedSampleRate != configuredSampleRate) {
#if SYMBIOSIS_AAX_TRACE
				char message[160];
				snprintf(message, sizeof(message), "syncAudioConfigurationFromController sampleRate=%.1f type=%u",
						static_cast<double>(resolvedSampleRate),
						static_cast<unsigned int>(resolvedTypeIndex));
				traceAAXEvent(message);
#endif
				reconfigureAudioType(resolvedTypeIndex, resolvedSampleRate, configuredMaxBufferSize);
			}
		}

		UInt32 getAdvertisedAAXInputChannelCount() const {
			SYMBIOSIS_ASSERT(configuredMainTypeIndex < static_cast<UInt32>(aaxMainTypes.size()));
			const AAXMainTypeDescriptor& mainType = aaxMainTypes[configuredMainTypeIndex];
			UInt32 count = static_cast<UInt32>(AAX_STEM_FORMAT_CHANNEL_COUNT(mainType.inputStem));
			if (aaxSidechainStem != AAX_eStemFormat_None) {
				count += static_cast<UInt32>(AAX_STEM_FORMAT_CHANNEL_COUNT(aaxSidechainStem));
			}
			return count;
		}

		UInt32 getAdvertisedAAXOutputChannelCount() const {
			SYMBIOSIS_ASSERT(configuredMainTypeIndex < static_cast<UInt32>(aaxMainTypes.size()));
			const AAXMainTypeDescriptor& mainType = aaxMainTypes[configuredMainTypeIndex];
			UInt32 count = static_cast<UInt32>(AAX_STEM_FORMAT_CHANNEL_COUNT(mainType.outputStem));
			for (UInt32 auxIndex = 0u; auxIndex < static_cast<UInt32>(aaxAuxOutputStems.size()); ++auxIndex) {
				count += static_cast<UInt32>(AAX_STEM_FORMAT_CHANNEL_COUNT(aaxAuxOutputStems[auxIndex]));
			}
			return count;
		}

		UInt32 getCurrentMainInputStemChannelCount() const {
			SYMBIOSIS_ASSERT(configuredMainTypeIndex < static_cast<UInt32>(aaxMainTypes.size()));
			const AAXMainTypeDescriptor& mainType = aaxMainTypes[configuredMainTypeIndex];
			return static_cast<UInt32>(AAX_STEM_FORMAT_CHANNEL_COUNT(mainType.inputStem));
		}

		UInt32 getCurrentMainOutputStemChannelCount() const {
			SYMBIOSIS_ASSERT(configuredMainTypeIndex < static_cast<UInt32>(aaxMainTypes.size()));
			const AAXMainTypeDescriptor& mainType = aaxMainTypes[configuredMainTypeIndex];
			return static_cast<UInt32>(AAX_STEM_FORMAT_CHANNEL_COUNT(mainType.outputStem));
		}

		void buildMidiInputEvents(AAX_IMIDINode* midiInputNode, UInt32 bufferSize) {
			currentInputEventCount = 0u;
			if ((plugInInfo->eventCapabilities & SYMBIOSIS_WANTS_MIDI_INPUT_MASK) == 0u) {
				return;
			}
			SYMBIOSIS_ASSERT(pendingInputEvents.get() != 0);
			SYMBIOSIS_ASSERT(pendingInputEventData.get() != 0);
			if (midiInputNode == 0) {
				return;
			}
			const AAX_CMidiStream* const midiStream = midiInputNode->GetNodeBuffer();
			if (midiStream == 0 || midiStream->mBuffer == 0) {
				return;
			}
			for (UInt32 packetIndex = 0u; packetIndex < midiStream->mBufferSize; ++packetIndex) {
				if (currentInputEventCount >= plugInInfo->maxInputEventCountPerBlock) {
					break;
				}
				const AAX_CMidiPacket& packet = midiStream->mBuffer[packetIndex];
				if (packet.mLength == 0u || packet.mLength > 3u) {
					continue;
				}
				const UByte8 status = static_cast<UByte8>(packet.mData[0]);
				if (status == 0xF0u || status == 0xF7u) {
					continue;
				}
				UInt32 offset = packet.mTimestamp;
				if (offset >= bufferSize) {
					offset = bufferSize - 1u;
				}
				SymbiosisEvent event;
				event.offset = offset;
				event.type = SYMBIOSIS_EVENT_TYPE_MIDI;
				event.data = &pendingInputEventData[currentInputEventCount];
				SymbiosisMidiEventData data;
				data.status = status;
				data.data1 = (packet.mLength > 1u ? static_cast<UByte8>(packet.mData[1]) : 0u);
				data.data2 = (packet.mLength > 2u ? static_cast<UByte8>(packet.mData[2]) : 0u);

				UInt32 insertIndex = currentInputEventCount;
				while (insertIndex > 0u && pendingInputEvents[insertIndex - 1u].offset > event.offset) {
					pendingInputEvents[insertIndex] = pendingInputEvents[insertIndex - 1u];
					pendingInputEventData[insertIndex] = pendingInputEventData[insertIndex - 1u];
					pendingInputEvents[insertIndex].data = &pendingInputEventData[insertIndex];
					--insertIndex;
				}
				pendingInputEventData[insertIndex] = data;
				pendingInputEvents[insertIndex] = event;
				pendingInputEvents[insertIndex].data = &pendingInputEventData[insertIndex];
				++currentInputEventCount;
			}
		}

		void processAAXAudio(float* const* hostInputChannels, float* const* hostOutputChannels
					, AAX_IMIDINode* midiInputNode, AAX_IMIDINode* midiOutputNode
					, UInt32 bufferSize, Bool8 isBypassed, Int32 sideChainInputIndex) {
				SYMBIOSIS_ASSERT(plugIn.get() != 0);
				SYMBIOSIS_ASSERT(bufferSize > 0u);
				if (!isAudioProcessing()) {
					traceAudioLifecycle("processAAXAudio first active boundary");
					setProcessingActive(true);
					if (!isAudioProcessing()) {
						traceAudioLifecycle("processAAXAudio activation skipped");
						return;
					}
				}
				if (bufferSize > configuredMaxBufferSize) {
					const UInt32 hostMainOutputChannels = (hostOutputChannels != 0 ? getCurrentMainOutputStemChannelCount() : 0u);
					for (UInt32 channel = 0u; channel < hostMainOutputChannels; ++channel) {
				if (hostOutputChannels != 0 && hostOutputChannels[channel] != 0) {
					memset(hostOutputChannels[channel], 0, bufferSize * sizeof(Float32));
				}
			}
			return;
		}
		SYMBIOSIS_ASSERT(silentInputScratch.size() >= bufferSize);
		SYMBIOSIS_ASSERT(sinkOutputScratch.size() >= bufferSize);

		memset(sinkOutputScratch.data(), 0, bufferSize * sizeof(Float32));

		const UInt32 mainHostInputChannelCount = (hostInputChannels != 0 ? getCurrentMainInputStemChannelCount() : 0u);
		const UInt32 mainHostOutputChannelCount = (hostOutputChannels != 0 ? getCurrentMainOutputStemChannelCount() : 0u);
		const bool hasSidechainBus = (inputBusCount > 1u && aaxSidechainStem != AAX_eStemFormat_None);
		const bool hasValidSidechainIndex = (hasSidechainBus
				&& sideChainInputIndex >= 0
				&& static_cast<UInt32>(sideChainInputIndex) == mainHostInputChannelCount);
		const UInt32 sidechainHostChannelCount = (hasValidSidechainIndex ? 1u : 0u);

		const Float32* const fallbackInput = silentInputScratch.data();
		Float32* const fallbackOutput = sinkOutputScratch.data();

		UInt32 inputPointerIndex = 0u;
		UInt32 outputPointerIndex = 0u;
		UInt32 hostOutputOffset = 0u;

		for (UInt32 busIndex = 0u; busIndex < inputBusCount; ++busIndex) {
			const UInt32 busChannelCount = selectedInputBusChannelCounts[busIndex];
			UInt32 hostBusChannelCount = 0u;
			UInt32 hostBusOffset = 0u;
			if (busIndex == 0u) {
				hostBusChannelCount = mainHostInputChannelCount;
				hostBusOffset = 0u;
			} else if (busIndex == 1u && hasSidechainBus) {
				hostBusChannelCount = sidechainHostChannelCount;
				hostBusOffset = static_cast<UInt32>(sideChainInputIndex);
			}

			// Delivery-time fallback: the format is agreed strictly at setup, but if the host
			// under-delivers buffers for a connected bus, alias the channels it did provide up to the
			// agreed count rather than dropping signal; a fully unconnected bus (hostBusChannelCount
			// == 0) uses the shared silent fallback. Aliasing avoids allocating extra silent buffers.
			for (UInt32 channel = 0u; channel < busChannelCount; ++channel) {
				const Float32* channelPointer = 0;
				if (hostInputChannels != 0 && hostBusChannelCount > 0u) {
					const UInt32 mappedHostChannel = hostBusOffset + (channel % hostBusChannelCount);
					channelPointer = hostInputChannels[mappedHostChannel];
				}
				inputChannelPointers[inputPointerIndex++] = (channelPointer != 0 ? channelPointer : fallbackInput);
			}

			inputBusConnectedFlags[busIndex] = toBool8(hostBusChannelCount > 0u);
			inputBusSilentFlags[busIndex] = toBool8(hostBusChannelCount == 0u);
		}
		SYMBIOSIS_ASSERT(inputPointerIndex == activeInputChannelCount);

		for (UInt32 busIndex = 0u; busIndex < outputBusCount; ++busIndex) {
			const UInt32 busChannelCount = selectedOutputBusChannelCounts[busIndex];
			UInt32 hostBusChannelCount = 0u;
			if (busIndex == 0u) {
				hostBusChannelCount = mainHostOutputChannelCount;
				} else {
					const UInt32 auxIndex = busIndex - 1u;
					SYMBIOSIS_ASSERT(auxIndex < static_cast<UInt32>(aaxAuxOutputStems.size()));
					hostBusChannelCount = static_cast<UInt32>(AAX_STEM_FORMAT_CHANNEL_COUNT(aaxAuxOutputStems[auxIndex]));
			}
			const UInt32 hostBusOffset = hostOutputOffset;
			hostOutputOffset += hostBusChannelCount;

			// Delivery-time fallback (see input path above): alias provided channels up to the agreed
			// count; a fully unconnected bus uses the shared output fallback.
			for (UInt32 channel = 0u; channel < busChannelCount; ++channel) {
				Float32* channelPointer = 0;
				if (hostOutputChannels != 0 && hostBusChannelCount > 0u) {
					const UInt32 mappedHostChannel = hostBusOffset + (channel % hostBusChannelCount);
					channelPointer = hostOutputChannels[mappedHostChannel];
				}
				if (channelPointer == 0) {
					channelPointer = fallbackOutput;
				}
				memset(channelPointer, 0, bufferSize * sizeof(Float32));
				outputChannelPointers[outputPointerIndex++] = channelPointer;
			}

			outputBusConnectedFlags[busIndex] = toBool8(hostBusChannelCount > 0u);
			outputBusSilentFlags[busIndex] = toBool8(hostBusChannelCount == 0u);
		}
		SYMBIOSIS_ASSERT(outputPointerIndex == activeOutputChannelCount);

		SymbiosisRenderInputArgs inArgs;
		memset(&inArgs, 0, sizeof(inArgs));
		inArgs.structVersion = 1;
		inArgs.bufferSize = bufferSize;
		inArgs.inputBusConnected = (inputBusCount > 0u ? inputBusConnectedFlags.get() : 0);
		inArgs.inputBusSilent = (inputBusCount > 0u ? inputBusSilentFlags.get() : 0);
		inArgs.inputChannels = (activeInputChannelCount > 0u ? inputChannelPointers.get() : 0);
		buildMidiInputEvents(midiInputNode, bufferSize);
		inArgs.inputEventCount = currentInputEventCount;
		inArgs.inputEvents = (currentInputEventCount > 0u ? pendingInputEvents.get() : 0);
		inArgs.outputBusConnected = (outputBusCount > 0u ? outputBusConnectedFlags.get() : 0);
		inArgs.isBypassed = isBypassed;
			// Transport (JUCE-style): read the version-negotiated AAX_ITransport from the bound
			// parameters object inline in render. Never use the unversioned AAX_IMIDINode::GetTransport()
			// (SDK-warned, "usually a crash"). Each field keeps its default below if there is no
			// transport object or the host does not implement that getter.
		inArgs.isTransportRunning = 0;
		inArgs.tempo = 120.0;
		inArgs.ppqPosition = 0.0;
		inArgs.isTransportLooping = 0;
		inArgs.loopStartPPQPosition = 0.0;
		inArgs.loopEndPPQPosition = 0.0;
		inArgs.samplePosition = renderFallbackSamplePosition;
			{
				const AAX_ITransport* const transport
						= (boundParameters != 0 ? boundParameters->Transport() : 0);
				if (transport != 0) {
					uint32_t ticksPerQuarter = 960000u;					// Pro Tools' fixed tick resolution
					uint32_t hostTicksPerQuarter = 0;
					if (transport->GetTicksPerQuarter(&hostTicksPerQuarter) == AAX_SUCCESS
							&& hostTicksPerQuarter != 0u) {
						ticksPerQuarter = hostTicksPerQuarter;
					}
					const Double64 ticksToPPQ = 1.0 / static_cast<Double64>(ticksPerQuarter);
					bool isPlaying = false;
					if (transport->IsTransportPlaying(&isPlaying) == AAX_SUCCESS) {
						inArgs.isTransportRunning = toBool8(isPlaying);
					}
					Double64 tempoBPM = 0.0;
					if (transport->GetCurrentTempo(&tempoBPM) == AAX_SUCCESS && tempoBPM > 0.0) {
						inArgs.tempo = tempoBPM;
					}
					int64_t tickPosition = 0;
					if (transport->GetCurrentTickPosition(&tickPosition) == AAX_SUCCESS) {
						inArgs.ppqPosition = static_cast<Double64>(tickPosition) * ticksToPPQ;
					}
					bool isLooping = false;
					int64_t loopStartTick = 0;
					int64_t loopEndTick = 0;
					if (transport->GetCurrentLoopPosition(&isLooping, &loopStartTick, &loopEndTick)
							== AAX_SUCCESS) {
						inArgs.isTransportLooping = toBool8(isLooping);
						inArgs.loopStartPPQPosition = static_cast<Double64>(loopStartTick) * ticksToPPQ;
						inArgs.loopEndPPQPosition = static_cast<Double64>(loopEndTick) * ticksToPPQ;
					}
					int64_t sampleLocation = 0;
					if (transport->GetCurrentNativeSampleLocation(&sampleLocation) == AAX_SUCCESS) {
						inArgs.samplePosition = sampleLocation;
					}
				}
			}
#if SYMBIOSIS_AAX_TRACE
			if (inArgs.isTransportRunning != lastTracedTransportRunning) {
				lastTracedTransportRunning = inArgs.isTransportRunning;
				char transportMessage[160];
				snprintf(transportMessage, sizeof(transportMessage),
						"transport run=%d tempo=%.3f ppq=%.4f loop=%d sample=%lld",
						static_cast<int>(inArgs.isTransportRunning), inArgs.tempo, inArgs.ppqPosition,
						static_cast<int>(inArgs.isTransportLooping),
						static_cast<long long>(inArgs.samplePosition));
				traceAAXEvent(transportMessage);
			}
#endif

		SymbiosisRenderOutputArgs outArgs;
		memset(&outArgs, 0, sizeof(outArgs));
		outArgs.structVersion = 1;
		outArgs.outputBusSilent = (outputBusCount > 0u ? outputBusSilentFlags.get() : 0);
		outArgs.outputChannels = (activeOutputChannelCount > 0u ? outputChannelPointers.get() : 0);
		outArgs.outputEventCount = 0;
		outArgs.outputEvents = 0;

			plugIn->renderAudio(&inArgs, &outArgs);
			postMidiOutputEventsToAAX(midiOutputNode, outArgs, bufferSize);
			renderFallbackSamplePosition = inArgs.samplePosition + static_cast<Int64>(bufferSize);
		}

		void postMidiOutputEventsToAAX(AAX_IMIDINode* midiOutputNode
				, const SymbiosisRenderOutputArgs& outArgs, UInt32 bufferSize) {
			if ((plugInInfo->eventCapabilities & SYMBIOSIS_CREATES_MIDI_OUTPUT_MASK) == 0u) {
				SYMBIOSIS_ASSERT(outArgs.outputEventCount == 0u);
				SYMBIOSIS_ASSERT(outArgs.outputEvents == 0);
				return;
			}
			SYMBIOSIS_ASSERT(outArgs.outputEventCount <= plugInInfo->maxOutputEventCountPerBlock);
			SYMBIOSIS_ASSERT(outArgs.outputEventCount == 0u || outArgs.outputEvents != 0);
			if (midiOutputNode == 0) {
				return;
			}
			for (UInt32 eventIndex = 0u; eventIndex < outArgs.outputEventCount; ++eventIndex) {
				const SymbiosisEvent& outputEvent = outArgs.outputEvents[eventIndex];
				SYMBIOSIS_ASSERT(eventIndex == 0u || outArgs.outputEvents[eventIndex - 1u].offset <= outputEvent.offset);
				SYMBIOSIS_ASSERT(outputEvent.type == SYMBIOSIS_EVENT_TYPE_MIDI);
				SYMBIOSIS_ASSERT(outputEvent.data != 0);
				SYMBIOSIS_ASSERT(outputEvent.offset < bufferSize);
				const SymbiosisMidiEventData* const midiData =
						reinterpret_cast<const SymbiosisMidiEventData*>(outputEvent.data);
				AAX_CMidiPacket packet;
				memset(&packet, 0, sizeof(packet));
				packet.mTimestamp = outputEvent.offset;
				packet.mLength = 3u;
				packet.mData[0] = midiData->status;
				packet.mData[1] = midiData->data1;
				packet.mData[2] = midiData->data2;
				packet.mIsImmediate = toBool8(outputEvent.offset == 0u);
				(void)midiOutputNode->PostMIDIPacket(&packet);
			}
		}

		const SymbiosisPlugInInfo* getPlugInInfo() const { return plugInInfo; }
		const std::vector<AAXMainTypeDescriptor>& getAAXMainTypes() const { return aaxMainTypes; }
		uint32_t getAAXRole() const { return aaxRole; }
		uint32_t getAAXCategoryBits() const { return aaxCategoryBits; }
		AAX_EStemFormat getAAXSidechainStem() const { return aaxSidechainStem; }
		const std::vector<AAX_EStemFormat>& getAAXAuxOutputStems() const { return aaxAuxOutputStems; }

		void setBypassFromAAX(bool bypassed) {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(plugInInfo->handlesBypass != 0);	// bypass parameter only exists when handled
			plugIn->setBypass(toBool8(bypassed));	// sticky, out-of-band (SymbiosisBypassPolicy.md)
		}

		void updateParameterFromAAX(UInt32 parameterNumber, Float32 normalizedValue) {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (parameterNumber >= plugInInfo->parameterCount) {
				return;
			}
			plugIn->updateParameter(parameterNumber, normalizedValue);
		}

		Float32 getParameterForAAX(UInt32 parameterNumber) {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(parameterNumber < plugInInfo->parameterCount);
			return plugIn->getParameter(parameterNumber);
		}

		void traceProgramSnapshot(const char* context) {
			SYMBIOSIS_ASSERT(context != 0);
#if SYMBIOSIS_AAX_TRACE
			if (plugIn.get() == 0 || plugInInfo == 0) {
				return;
		}
			Float32 parameterValues[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			const UInt32 parameterCount = (plugInInfo->parameterCount < 4u ? plugInInfo->parameterCount : 4u);
			for (UInt32 parameterIndex = 0u; parameterIndex < parameterCount; ++parameterIndex) {
				parameterValues[parameterIndex] = plugIn->getParameter(parameterIndex);
			}
			char message[512];
			snprintf(message, sizeof(message), "%s params=%u %.6f %.6f %.6f %.6f",
					context,
					static_cast<unsigned int>(plugInInfo->parameterCount),
					static_cast<double>(parameterValues[0]),
					static_cast<double>(parameterValues[1]),
					static_cast<double>(parameterValues[2]),
					static_cast<double>(parameterValues[3]));
			traceAAXEvent(message);
#else
			(void)context;
#endif
		}

		bool buildRawSymbiosisState(std::vector<UByte8>* outState) {
			SYMBIOSIS_ASSERT(outState != 0);
			UInt32 payloadSize = 0u;
			UByte8* payload = 0;
			if (!plugIn->createSaveState(&payloadSize, &payload)) {
				tracePlugInLastError(plugIn.get(), "AAX createSaveState");
				return false;
			}
			SYMBIOSIS_ASSERT(payloadSize == 0u || payload != 0);
			outState->resize(AAX_STATE_HEADER_SIZE + payloadSize);
			UByte8* stateBytes = outState->data();
			encodeLE32(&stateBytes[0], STATE_MAGIC_SYMB);
			encodeLE32(&stateBytes[4], STATE_ADAPTER_AAX);
			encodeLE32(&stateBytes[8], STATE_VERSION);
			if (payloadSize > 0u) {
				memcpy(&stateBytes[AAX_STATE_HEADER_SIZE], payload, payloadSize);
			}
			if (payload != 0) {
				plugIn->destroySaveState(payload);
			}
#if SYMBIOSIS_AAX_TRACE
			char message[160];
			snprintf(message, sizeof(message), "buildRawSymbiosisState total=%u payload=%u hash=0x%08x",
					static_cast<unsigned int>(outState->size()),
					static_cast<unsigned int>(payloadSize),
					static_cast<unsigned int>(hashTraceBytes(outState->data(), static_cast<UInt32>(outState->size()))));
			traceAAXEvent(message);
			traceProgramSnapshot("after buildRawSymbiosisState");
#endif
			return true;
		}

		bool loadRawSymbiosisState(UInt32 dataSize, const UByte8* data) {
			if (dataSize > 0u && data == 0) {
				return false;
			}
			if (dataSize < AAX_STATE_HEADER_SIZE) {
				return false;
			}
			if (decodeLE32(&data[0]) != STATE_MAGIC_SYMB
					|| decodeLE32(&data[4]) != STATE_ADAPTER_AAX
					|| decodeLE32(&data[8]) != STATE_VERSION) {
				return false;
			}
			const UInt32 payloadSize = dataSize - AAX_STATE_HEADER_SIZE;
			const UByte8* payload = (payloadSize > 0u ? &data[AAX_STATE_HEADER_SIZE] : 0);
#if SYMBIOSIS_AAX_TRACE
			char message[160];
			snprintf(message, sizeof(message), "loadRawSymbiosisState total=%u payload=%u hash=0x%08x",
					static_cast<unsigned int>(dataSize),
					static_cast<unsigned int>(payloadSize),
					static_cast<unsigned int>(hashTraceBytes(data, dataSize)));
			traceAAXEvent(message);
#endif
			if (!plugIn->loadState(payloadSize, payload)) {
				tracePlugInLastError(plugIn.get(), "AAX loadState");
				return false;
			}
#if SYMBIOSIS_AAX_TRACE
			traceProgramSnapshot("after loadRawSymbiosisState");
#endif
			return true;
		}

		bool isAudioConfigured() const { return audioState != AUDIO_UNCONFIGURED; }
		bool isAudioAllocated() const { return audioState == AUDIO_ALLOCATED || audioState == AUDIO_PROCESSING; }
		bool isAudioProcessing() const { return audioState == AUDIO_PROCESSING; }

		void updateDisplayFromPlugIn() {
			// TODO AAX parameter bridge wiring.
		}

		void beginEditFromPlugIn(UInt32 parameterNumber) {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (boundParameters == 0) {
				return;
			}
			if (parameterNumber >= plugInInfo->parameterCount) {
				return;
			}
			char parameterId[16];
			if (!encodeSymbiosisAAXParameterId(parameterNumber, parameterId, sizeof(parameterId))) {
				return;
			}
			(void)boundParameters->TouchParameter(parameterId);
		}

		void writeParameterFromPlugIn(UInt32 parameterNumber, Float32 normalizedValue) {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (boundParameters == 0) {
				return;
			}
			if (parameterNumber >= plugInInfo->parameterCount) {
				return;
			}
			char parameterId[16];
			if (!encodeSymbiosisAAXParameterId(parameterNumber, parameterId, sizeof(parameterId))) {
				return;
			}
			double clampedValue = static_cast<double>(normalizedValue);
			if (clampedValue < 0.0) {
				clampedValue = 0.0;
			}
			if (clampedValue > 1.0) {
				clampedValue = 1.0;
			}
			(void)boundParameters->SetParameterNormalizedValue(parameterId, clampedValue);
		}

		void endEditFromPlugIn(UInt32 parameterNumber) {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			if (boundParameters == 0) {
				return;
			}
			if (parameterNumber >= plugInInfo->parameterCount) {
				return;
			}
			char parameterId[16];
			if (!encodeSymbiosisAAXParameterId(parameterNumber, parameterId, sizeof(parameterId))) {
				return;
			}
			(void)boundParameters->ReleaseParameter(parameterId);
		}

		bool requestResizeFromPlugIn(UInt32 width, UInt32 height) {
			if (boundViewContainer == 0) {
				return false;
			}
			AAX_Point size;
			size.horz = static_cast<float>(width);
			size.vert = static_cast<float>(height);
			return boundViewContainer->SetViewSize(size) == AAX_SUCCESS;
		}

		const void* queryExtensionFromPlugIn(UInt32 /*vendorId*/, UInt32 /*interfaceId*/) {
			return 0;
		}

		bool getUIViewSizeFromAAX(UInt32* outWidth, UInt32* outHeight) const {
			SYMBIOSIS_ASSERT(outWidth != 0);
			SYMBIOSIS_ASSERT(outHeight != 0);
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			traceUILifecycle("getUIViewSizeFromAAX begin");
			if (plugInInfo->hasCustomUIView == 0) {
				return false;
			}
			const bool ok = plugIn->getUIViewSize(outWidth, outHeight);
			if (!ok) {
				tracePlugInLastError(plugIn.get(), "AAX getUIViewSize");
				return false;
			}
			SYMBIOSIS_ASSERT(*outWidth > 0u);
			SYMBIOSIS_ASSERT(*outHeight > 0u);
			traceUILifecycle("getUIViewSizeFromAAX end");
			return true;
		}

		bool openUIViewFromAAX(void* nativeGUIElement) {
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			SYMBIOSIS_ASSERT(nativeGUIElement != 0);
			if (plugInInfo->hasCustomUIView == 0) {
				return false;
			}
			if (uiOpen) {
				traceUILifecycle("openUIViewFromAAX already open");
			return true;
		}
			traceUILifecycle("openUIViewFromAAX begin");
			if (!plugIn->openUIView(nativeGUIElement)) {
				tracePlugInLastError(plugIn.get(), "AAX openUIView");
				return false;
			}
			uiOpen = true;
			nativeParentView = nativeGUIElement;
			nativeParentWindow = nativeGUIElement;
			traceProgramSnapshot("after openUIViewFromAAX");
			traceUILifecycle("openUIViewFromAAX end");
			return true;
		}

		void closeUIViewFromAAX() {
			if (!uiOpen) {
				traceUILifecycle("closeUIViewFromAAX already closed");
				return;
		}
			traceUILifecycle("closeUIViewFromAAX begin");
			traceProgramSnapshot("before closeUIViewFromAAX");
			plugIn->closeUIView();
			uiOpen = false;
			nativeParentView = 0;
			nativeParentWindow = 0;
			traceProgramSnapshot("after closeUIViewFromAAX");
			traceUILifecycle("closeUIViewFromAAX end");
		}

		void configureAudioForCurrentType() {
			SYMBIOSIS_ASSERT(audioState == AUDIO_UNCONFIGURED || audioState == AUDIO_CONFIGURED);
			SYMBIOSIS_ASSERT(!isAudioAllocated());
			UInt32 configuredInputChannels = 0u;
			UInt32 configuredOutputChannels = 0u;
			for (UInt32 busIndex = 0u; busIndex < inputBusCount; ++busIndex) {
				configuredInputChannels += selectedInputBusChannelCounts[busIndex];
			}
			for (UInt32 busIndex = 0u; busIndex < outputBusCount; ++busIndex) {
				configuredOutputChannels += selectedOutputBusChannelCounts[busIndex];
			}
			SYMBIOSIS_ASSERT(configuredInputChannels <= inputChannelCount);
			SYMBIOSIS_ASSERT(configuredOutputChannels <= outputChannelCount);
			activeInputChannelCount = configuredInputChannels;
			activeOutputChannelCount = configuredOutputChannels;
			traceAudioLifecycle("configureAudioForCurrentType begin");

			SymbiosisConfigureAudioInputArgs inArgs;
			memset(&inArgs, 0, sizeof(inArgs));
			inArgs.structVersion = 1;
			inArgs.sampleRate = configuredSampleRate;
			inArgs.maxBufferSize = configuredMaxBufferSize;
			inArgs.inputChannelCount = configuredInputChannels;
			inArgs.outputChannelCount = configuredOutputChannels;
			inArgs.inputBusFormats = (inputBusCount > 0u ? selectedInputBusFormatValues.get() : 0);
			inArgs.inputBusChannelCounts = (inputBusCount > 0u ? selectedInputBusChannelCounts.get() : 0);
			inArgs.outputBusFormats = (outputBusCount > 0u ? selectedOutputBusFormatValues.get() : 0);
			inArgs.outputBusChannelCounts = (outputBusCount > 0u ? selectedOutputBusChannelCounts.get() : 0);

			SymbiosisConfigureAudioOutputArgs outArgs;
			memset(&outArgs, 0, sizeof(outArgs));
			outArgs.structVersion = 1;

			plugIn->configureAudio(&inArgs, &outArgs);
			configuredLatencySamples = outArgs.latencySamples;
			configuredTailSamples = outArgs.tailSamples;
			silentInputScratch.assign(configuredMaxBufferSize, 0.0f);
			sinkOutputScratch.assign(configuredMaxBufferSize, 0.0f);
			renderFallbackSamplePosition = 0;
			const AudioLifecycleState fromState = audioState;
			audioState = AUDIO_CONFIGURED;
			traceAudioLifecycleTransition("configureAudioForCurrentType", fromState, audioState);
		}

		void bindParameters(AAX_CEffectParameters* parameters) {
			boundParameters = parameters;
			reportLatencyToAAXController();
		}

		void reportLatencyToAAXController() {
			if (boundParameters == 0) {
				return;
		}
			AAX_IController* const controller = boundParameters->Controller();
			if (controller == 0) {
				return;
			}
			SYMBIOSIS_ASSERT(configuredLatencySamples <= 0x7fffffffu);
			const int32_t latencySamples = static_cast<int32_t>(configuredLatencySamples);
			const AAX_Result result = controller->SetSignalLatency(latencySamples);
			if (result != AAX_SUCCESS) {
				traceAAXEvent("SetSignalLatency failed");
			}
		}

		void bindViewContainer(AAX_IViewContainer* viewContainer) {
			boundViewContainer = viewContainer;
			traceUILifecycle(viewContainer != 0 ? "bindViewContainer attach" : "bindViewContainer detach");
		}

		bool metadataInitialized;
		SymbiosisLoaderInfo loaderInfo;
		UTF8Z factoryErrorText[1024];
		UTF8Z hostApplicationName[256];
		UTF8Z hostApplicationVendor[256];
		AAXAdapterHost host;
		std::unique_ptr<symbiosis::HostedFactory> factory;
		std::unique_ptr<symbiosis::PlugInInterface> plugIn;
		const SymbiosisPlugInInfo* plugInInfo;
		AudioLifecycleState audioState;
		UInt32 inputBusCount;
		UInt32 outputBusCount;
		UInt32 inputChannelCount;
		UInt32 outputChannelCount;
		UInt32 activeInputChannelCount;
		UInt32 activeOutputChannelCount;
		UInt32 configuredMainTypeIndex;
		Float32 configuredSampleRate;
		UInt32 configuredMaxBufferSize;
		UInt32 configuredLatencySamples;
		Int32 configuredTailSamples;
		Int64 renderFallbackSamplePosition;
#if SYMBIOSIS_AAX_TRACE
		Bool8 lastTracedTransportRunning;					// transition-throttled transport trace state
#endif
		std::unique_ptr<UInt32[]> selectedInputBusFormatValues;
		std::unique_ptr<UInt32[]> selectedInputBusChannelCounts;
		std::unique_ptr<UInt32[]> selectedOutputBusFormatValues;
		std::unique_ptr<UInt32[]> selectedOutputBusChannelCounts;
		std::unique_ptr<Bool8[]> inputBusConnectedFlags;
		std::unique_ptr<Bool8[]> inputBusSilentFlags;
		std::unique_ptr<Bool8[]> outputBusConnectedFlags;
		std::unique_ptr<Bool8[]> outputBusSilentFlags;
		std::unique_ptr<const Float32*[]> inputChannelPointers;
		std::unique_ptr<Float32*[]> outputChannelPointers;
		std::unique_ptr<SymbiosisEvent[]> pendingInputEvents;
		std::unique_ptr<SymbiosisMidiEventData[]> pendingInputEventData;
		UInt32 currentInputEventCount;
		std::unique_ptr<SymbiosisEvent[]> pendingOutputEvents;
		std::unique_ptr<SymbiosisMidiEventData[]> pendingOutputEventData;
		std::vector<Float32> silentInputScratch;
		std::vector<Float32> sinkOutputScratch;
		std::vector<UByte8> retainedStateChunkBytes;
		std::vector<AAXMainTypeDescriptor> aaxMainTypes;
		uint32_t aaxRole;
		uint32_t aaxCategoryBits;
		AAX_EStemFormat aaxSidechainStem;
		std::vector<AAX_EStemFormat> aaxAuxOutputStems;
		bool uiOpen;
		void* nativeParentView;
		void* nativeParentWindow;
		AAX_CEffectParameters* boundParameters = 0;
		AAX_IViewContainer* boundViewContainer = 0;

		friend class AAXAdapterHost;
};

void AAXAdapterHost::updateDisplay() {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->updateDisplayFromPlugIn();
}

void AAXAdapterHost::beginEdit(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->beginEditFromPlugIn(parameterNumber);
}

void AAXAdapterHost::writeParameter(UInt32 parameterNumber, Float32 normalizedValue) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->writeParameterFromPlugIn(parameterNumber, normalizedValue);
}

void AAXAdapterHost::endEdit(UInt32 parameterNumber) {
	SYMBIOSIS_ASSERT(owner != 0);
	owner->endEditFromPlugIn(parameterNumber);
}

bool AAXAdapterHost::requestResize(UInt32 width, UInt32 height) {
	SYMBIOSIS_ASSERT(owner != 0);
	return owner->requestResizeFromPlugIn(width, height);
}

const void* AAXAdapterHost::queryExtension(UInt32 vendorId, UInt32 interfaceId) {
	SYMBIOSIS_ASSERT(owner != 0);
	return owner->queryExtensionFromPlugIn(vendorId, interfaceId);
}

	class SymbiosisAAXParameters : public AAX_CEffectParameters {
	public:
		SymbiosisAAXParameters()
			: suppressInitialDefaultUpdates(false) {}

		~SymbiosisAAXParameters() AAX_OVERRIDE {}

		static AAX_CEffectParameters* AAX_CALLBACK Create() {
			try {
				return new SymbiosisAAXParameters();
			}
			catch (...) {
				traceAAXBoundaryException("AAX parameters create");
				return 0;
			}
		}

		AAX_Result EffectInit() AAX_OVERRIDE {
			try {
				traceAAXEvent("EffectInit begin");
				const AAX_Result baseResult = AAX_CEffectParameters::EffectInit();
				if (baseResult != AAX_SUCCESS) {
					return baseResult;
						}
					adapterInstance.reset(new AAXAdapterInstance());
					if (!adapterInstance->initialize(0)) {
						return AAX_ERROR_UNIMPLEMENTED;
				}
						adapterInstance->bindParameters(this);
						adapterInstance->syncAudioConfigurationFromController(Controller());
						adapterInstance->traceAudioLifecycle("EffectInit after controller sync");
						if (adapterInstance->getPlugInInfo()->handlesBypass != 0) {
					traceAAXEvent("EffectInit adding bypass parameter");
					std::unique_ptr<AAX_IParameter> bypassParameter(new AAX_CParameter<bool>(
							cDefaultMasterBypassID,
							AAX_CString("Master Bypass"),
							false,
							AAX_CBinaryTaperDelegate<bool>(),
							AAX_CBinaryDisplayDelegate<bool>("off", "on"),
							true));
					bypassParameter->SetNumberOfSteps(2);
					bypassParameter->SetType(AAX_eParameterType_Discrete);
					mParameterManager.AddParameter(bypassParameter.release());
					mPacketDispatcher.RegisterPacket(cDefaultMasterBypassID, SYMBIOSIS_AAX_ALG_FIELD_BYPASS);
					traceAAXEvent("EffectInit added bypass parameter");
				}

						AAXAdapterInstance* const adapter = adapterInstance.get();
						const SymbiosisPlugInInfo* const plugInInfo = adapter->getPlugInInfo();
						SYMBIOSIS_ASSERT(plugInInfo != 0);
#if SYMBIOSIS_AAX_TRACE
							{
								char message[96];
								snprintf(message, sizeof(message), "EffectInit adding %u plug-in parameters",
										static_cast<unsigned int>(plugInInfo->parameterCount));
								traceAAXEvent(message);
							}
#endif
							for (UInt32 parameterIndex = 0u; parameterIndex < plugInInfo->parameterCount; ++parameterIndex) {
					char parameterIdText[16];
					snprintf(parameterIdText, sizeof(parameterIdText), "P%04u", static_cast<unsigned int>(parameterIndex));
					const SymbiosisParameterInfo& parameterInfo = plugInInfo->parameters[parameterIndex];
					const char* parameterNameText = (parameterInfo.displayName != 0 ? parameterInfo.displayName : "Parameter");
					float defaultValue = parameterInfo.defaultValue;
					if (defaultValue < 0.0f) {
						defaultValue = 0.0f;
					}
					if (defaultValue > 1.0f) {
						defaultValue = 1.0f;
					}
					std::unique_ptr<AAX_IParameter> parameter(new AAX_CParameter<float>(
							AAX_CString(parameterIdText),
							AAX_CString(parameterNameText),
							defaultValue,
							AAX_CLinearTaperDelegate<float>(0.0f, 1.0f),
							AAX_CNumberDisplayDelegate<float>(),
							true));
						parameter->SetType(AAX_eParameterType_Continuous);
						mParameterManager.AddParameter(parameter.release());
					}
					traceAAXEvent("EffectInit added plug-in parameters");
					traceAAXEvent("EffectInit syncParametersFromAdapter begin");
					syncParametersFromAdapter();
#if SYMBIOSIS_AAX_TRACE
					adapter->traceProgramSnapshot("after EffectInit syncParametersFromAdapter");
#endif
					suppressInitialDefaultUpdates = true;
					traceAAXEvent("EffectInit suppressing initial default updates until GenerateCoefficients");
					traceAAXEvent("EffectInit end");
					return AAX_SUCCESS;
				}
				catch (...) {
				traceAAXBoundaryException("AAX EffectInit");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result NotificationReceived(AAX_CTypeID inNotificationType, const void* inNotificationData, uint32_t inNotificationDataSize) AAX_OVERRIDE {
			try {
#if SYMBIOSIS_AAX_TRACE
				char message[160];
				snprintf(message, sizeof(message), "NotificationReceived type=0x%08x %s size=%u",
						static_cast<unsigned int>(inNotificationType),
						aaxNotificationName(inNotificationType),
						static_cast<unsigned int>(inNotificationDataSize));
				traceAAXEvent(message);
#endif
				return AAX_CEffectParameters::NotificationReceived(inNotificationType, inNotificationData, inNotificationDataSize);
			}
			catch (...) {
				traceAAXBoundaryException("AAX NotificationReceived");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result UpdateParameterNormalizedValue(AAX_CParamID iParameterID, double iValue, AAX_EUpdateSource iSource) AAX_OVERRIDE {
			try {
				const AAX_Result result = AAX_CEffectParameters::UpdateParameterNormalizedValue(iParameterID, iValue, iSource);
				if (result != AAX_SUCCESS) {
					return result;
				}
				if (adapterInstance && iParameterID != 0 && strcmp(iParameterID, cDefaultMasterBypassID) == 0
						&& adapterInstance->getPlugInInfo()->handlesBypass != 0) {
					// Out-of-band mirror of the packet-delivered render flag (SymbiosisBypassPolicy.md):
					// covers bypass changes while the host is not processing (chunk restore, stopped UI).
					adapterInstance->setBypassFromAAX(iValue >= 0.5);
					return AAX_SUCCESS;
				}
				UInt32 parameterIndex = 0u;
				if (!decodeSymbiosisParameterId(iParameterID, &parameterIndex)) {
					return AAX_SUCCESS;
				}
				if (!adapterInstance) {
					return AAX_SUCCESS;
				}
				const SymbiosisPlugInInfo* const plugInInfo = adapterInstance->getPlugInInfo();
				SYMBIOSIS_ASSERT(plugInInfo != 0);
				if (parameterIndex >= plugInInfo->parameterCount) {
					return AAX_ERROR_INVALID_PARAMETER_ID;
				}
				double normalizedValue = iValue;
				if (normalizedValue < 0.0) {
					normalizedValue = 0.0;
				}
				if (normalizedValue > 1.0) {
					normalizedValue = 1.0;
				}
#if SYMBIOSIS_AAX_TRACE
				const Float32 currentValue = adapterInstance->getParameterForAAX(parameterIndex);
				const Float32 defaultValue = std::min(1.0f, std::max(0.0f, plugInInfo->parameters[parameterIndex].defaultValue));
				{
					char message[220];
					snprintf(message, sizeof(message),
							"UpdateParameterNormalizedValue param=%u source=%s raw=%.6f clamped=%.6f current=%.6f default=%.6f",
							static_cast<unsigned int>(parameterIndex),
							aaxUpdateSourceName(iSource),
							iValue,
							normalizedValue,
							static_cast<double>(currentValue),
							static_cast<double>(defaultValue));
					traceAAXEvent(message);
				}
#else
				const Float32 defaultValue = std::min(1.0f, std::max(0.0f, plugInInfo->parameters[parameterIndex].defaultValue));
#endif
				if (suppressInitialDefaultUpdates
						&& iSource == AAX_eUpdateSource_Unspecified
						&& normalizedValue == static_cast<double>(defaultValue)) {
#if SYMBIOSIS_AAX_TRACE
					char message[160];
					snprintf(message, sizeof(message),
							"UpdateParameterNormalizedValue suppressed initial default param=%u value=%.6f",
							static_cast<unsigned int>(parameterIndex),
							normalizedValue);
					traceAAXEvent(message);
#endif
					return AAX_SUCCESS;
				}
				adapterInstance->updateParameterFromAAX(parameterIndex, static_cast<Float32>(normalizedValue));
#if SYMBIOSIS_AAX_TRACE
				{
					char message[128];
					snprintf(message, sizeof(message), "UpdateParameterNormalizedValue after param=%u current=%.6f",
							static_cast<unsigned int>(parameterIndex),
							static_cast<double>(adapterInstance->getParameterForAAX(parameterIndex)));
					traceAAXEvent(message);
				}
#endif
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX UpdateParameterNormalizedValue");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result GenerateCoefficients() AAX_OVERRIDE {
			try {
				if (suppressInitialDefaultUpdates) {
					traceAAXEvent("GenerateCoefficients ending initial default suppression");
					suppressInitialDefaultUpdates = false;
					if (adapterInstance) {
						syncParametersFromAdapter();
#if SYMBIOSIS_AAX_TRACE
						adapterInstance->traceProgramSnapshot("after initial default suppression syncParametersFromAdapter");
#endif
					}
				}
				return AAX_CEffectParameters::GenerateCoefficients();
			}
			catch (...) {
				traceAAXBoundaryException("AAX GenerateCoefficients");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result GetParameterNormalizedValue(AAX_CParamID iParameterID, double* oValuePtr) const AAX_OVERRIDE {
			try {
				if (oValuePtr == 0) {
					return AAX_ERROR_NULL_ARGUMENT;
				}
				UInt32 parameterIndex = 0u;
				if (!decodeSymbiosisParameterId(iParameterID, &parameterIndex)) {
					return AAX_CEffectParameters::GetParameterNormalizedValue(iParameterID, oValuePtr);
				}
				if (!adapterInstance) {
					return AAX_ERROR_UNIMPLEMENTED;
				}
				const SymbiosisPlugInInfo* const plugInInfo = adapterInstance->getPlugInInfo();
				SYMBIOSIS_ASSERT(plugInInfo != 0);
				if (parameterIndex >= plugInInfo->parameterCount) {
					return AAX_ERROR_INVALID_PARAMETER_ID;
				}
				Float32 normalizedValue = adapterInstance->getParameterForAAX(parameterIndex);
				if (normalizedValue < 0.0f) {
					normalizedValue = 0.0f;
				}
				if (normalizedValue > 1.0f) {
					normalizedValue = 1.0f;
				}
				*oValuePtr = static_cast<double>(normalizedValue);
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX GetParameterNormalizedValue");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result SetParameterNormalizedValue(AAX_CParamID iParameterID, double iValue) AAX_OVERRIDE {
			try {
				UInt32 parameterIndex = 0u;
				if (!decodeSymbiosisParameterId(iParameterID, &parameterIndex)) {
					return AAX_CEffectParameters::SetParameterNormalizedValue(iParameterID, iValue);
				}
				if (!adapterInstance) {
					return AAX_ERROR_UNIMPLEMENTED;
				}
				const SymbiosisPlugInInfo* const plugInInfo = adapterInstance->getPlugInInfo();
				SYMBIOSIS_ASSERT(plugInInfo != 0);
				if (parameterIndex >= plugInInfo->parameterCount) {
					return AAX_ERROR_INVALID_PARAMETER_ID;
				}
				double normalizedValue = iValue;
				if (normalizedValue < 0.0) {
					normalizedValue = 0.0;
				}
				if (normalizedValue > 1.0) {
					normalizedValue = 1.0;
				}
				AAX_IParameter* const parameter = mParameterManager.GetParameterByID(iParameterID);
				if (parameter == 0) {
					return AAX_ERROR_INVALID_PARAMETER_ID;
				}
				parameter->SetValueWithFloat(static_cast<float>(normalizedValue));
				adapterInstance->updateParameterFromAAX(parameterIndex, static_cast<Float32>(normalizedValue));
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX SetParameterNormalizedValue");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result GetNumberOfChunks(int32_t* oNumChunks) const AAX_OVERRIDE {
			try {
				if (oNumChunks == 0) {
					return AAX_ERROR_NULL_ARGUMENT;
				}
				*oNumChunks = 1;
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX GetNumberOfChunks");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result GetChunkIDFromIndex(int32_t iIndex, AAX_CTypeID* oChunkID) const AAX_OVERRIDE {
			try {
				if (oChunkID == 0) {
					return AAX_ERROR_NULL_ARGUMENT;
				}
				if (iIndex != 0) {
					return AAX_ERROR_INVALID_CHUNK_ID;
				}
				*oChunkID = SYMBIOSIS_AAX_STATE_CHUNK_ID;
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX GetChunkIDFromIndex");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result GetChunkSize(AAX_CTypeID iChunkID, uint32_t* oSize) const AAX_OVERRIDE {
			try {
				if (oSize == 0) {
					return AAX_ERROR_NULL_ARGUMENT;
				}
				if (iChunkID != SYMBIOSIS_AAX_STATE_CHUNK_ID) {
					return AAX_ERROR_INVALID_CHUNK_ID;
				}
				if (!adapterInstance || !adapterInstance->buildRawSymbiosisState(&retainedStateChunkBytes)) {
					return AAX_ERROR_UNIMPLEMENTED;
				}
				*oSize = static_cast<uint32_t>(retainedStateChunkBytes.size());
#if SYMBIOSIS_AAX_TRACE
				char message[128];
				snprintf(message, sizeof(message), "GetChunkSize id=0x%08x size=%u",
						static_cast<unsigned int>(iChunkID),
						static_cast<unsigned int>(*oSize));
				traceAAXEvent(message);
#endif
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX GetChunkSize");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result GetChunk(AAX_CTypeID iChunkID, AAX_SPlugInChunk* oChunk) const AAX_OVERRIDE {
			try {
				if (oChunk == 0) {
					return AAX_ERROR_NULL_ARGUMENT;
				}
				if (iChunkID != SYMBIOSIS_AAX_STATE_CHUNK_ID) {
					return AAX_ERROR_INVALID_CHUNK_ID;
				}
				if (retainedStateChunkBytes.empty()) {
					if (!adapterInstance || !adapterInstance->buildRawSymbiosisState(&retainedStateChunkBytes)) {
						return AAX_ERROR_UNIMPLEMENTED;
					}
				}
				oChunk->fSize = static_cast<int32_t>(retainedStateChunkBytes.size());
				oChunk->fVersion = static_cast<int32_t>(STATE_VERSION);
				oChunk->fManufacturerID = static_cast<AAX_CTypeID>(adapterInstance->getPlugInInfo()->vendorId);
				oChunk->fProductID = buildProductTypeId(adapterInstance->getPlugInInfo()->plugInId);
				oChunk->fPlugInID = buildNativeTypeId(adapterInstance->getPlugInInfo()->plugInId, 0u);
				oChunk->fChunkID = SYMBIOSIS_AAX_STATE_CHUNK_ID;
				memset(oChunk->fName, 0, sizeof(oChunk->fName));
				if (!retainedStateChunkBytes.empty()) {
					memcpy(oChunk->fData, retainedStateChunkBytes.data(), retainedStateChunkBytes.size());
				}
#if SYMBIOSIS_AAX_TRACE
				char message[160];
				snprintf(message, sizeof(message), "GetChunk id=0x%08x size=%d hash=0x%08x",
						static_cast<unsigned int>(iChunkID),
						static_cast<int>(oChunk->fSize),
						static_cast<unsigned int>(hashTraceBytes(reinterpret_cast<const UByte8*>(oChunk->fData),
								static_cast<UInt32>(oChunk->fSize))));
				traceAAXEvent(message);
#endif
				retainedStateChunkBytes.clear();
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX GetChunk");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result SetChunk(AAX_CTypeID iChunkID, const AAX_SPlugInChunk* iChunk) AAX_OVERRIDE {
			try {
				if (iChunk == 0) {
					return AAX_ERROR_NULL_ARGUMENT;
				}
				if (iChunkID != SYMBIOSIS_AAX_STATE_CHUNK_ID) {
					return AAX_ERROR_INVALID_CHUNK_ID;
				}
				if (iChunk->fSize < static_cast<int32_t>(AAX_STATE_HEADER_SIZE)) {
					return AAX_ERROR_INCORRECT_CHUNK_SIZE;
				}
				if (!adapterInstance) {
					return AAX_ERROR_UNIMPLEMENTED;
				}
				const UInt32 dataSize = static_cast<UInt32>(iChunk->fSize);
				const UByte8* const data = reinterpret_cast<const UByte8*>(iChunk->fData);
#if SYMBIOSIS_AAX_TRACE
				{
					char message[160];
					snprintf(message, sizeof(message), "SetChunk id=0x%08x size=%u hash=0x%08x",
							static_cast<unsigned int>(iChunkID),
							static_cast<unsigned int>(dataSize),
							static_cast<unsigned int>(hashTraceBytes(data, dataSize)));
					traceAAXEvent(message);
				}
#endif
				if (!adapterInstance->loadRawSymbiosisState(dataSize, data)) {
					return AAX_ERROR_INCORRECT_CHUNK_SIZE;
				}
				syncParametersFromAdapter();
#if SYMBIOSIS_AAX_TRACE
				adapterInstance->traceProgramSnapshot("after SetChunk syncParametersFromAdapter");
#endif
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX SetChunk");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result CompareActiveChunk(const AAX_SPlugInChunk* iChunkP, AAX_CBoolean* oIsEqual) const AAX_OVERRIDE {
			try {
				if (oIsEqual == 0) {
					return AAX_ERROR_NULL_ARGUMENT;
				}
				*oIsEqual = false;
				if (iChunkP == 0) {
					return AAX_SUCCESS;
				}
				if (iChunkP->fChunkID != SYMBIOSIS_AAX_STATE_CHUNK_ID) {
					return AAX_ERROR_INVALID_CHUNK_ID;
				}
				std::vector<UByte8> currentState;
				if (!adapterInstance || !adapterInstance->buildRawSymbiosisState(&currentState)) {
					return AAX_ERROR_UNIMPLEMENTED;
				}
				if (iChunkP->fSize != static_cast<int32_t>(currentState.size())) {
					return AAX_SUCCESS;
				}
				*oIsEqual = toBool8(memcmp(iChunkP->fData, currentState.data(), currentState.size()) == 0);
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX CompareActiveChunk");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAX_Result ResetFieldData (AAX_CFieldIndex inFieldIndex, void * oData, uint32_t inDataSize) const AAX_OVERRIDE {
			try {
				AAX_Result result = AAX_CEffectParameters::ResetFieldData(inFieldIndex, oData, inDataSize);
				if (result != AAX_SUCCESS) {
					return result;
				}
				if (inFieldIndex != SYMBIOSIS_AAX_ALG_FIELD_PRIVATE_DATA) {
					return AAX_SUCCESS;
				}
				if (oData == 0 || inDataSize < sizeof(SymbiosisAAXPrivateData) || !adapterInstance) {
					return AAX_ERROR_NULL_ARGUMENT;
				}
				SymbiosisAAXPrivateData* privateData = reinterpret_cast<SymbiosisAAXPrivateData*>(oData);
				memset(privateData, 0, inDataSize);
				privateData->magic = SYMBIOSIS_AAX_PRIVATE_DATA_MAGIC;
				privateData->adapter = adapterInstance.get();
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX ResetFieldData");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

		AAXAdapterInstance* getAdapterInstance() const {
			return adapterInstance.get();
		}

	private:
		static bool decodeSymbiosisParameterId(AAX_CParamID parameterId, UInt32* outParameterIndex) {
			SYMBIOSIS_ASSERT(outParameterIndex != 0);
			if (parameterId == 0) {
				return false;
			}
			if (parameterId[0] != 'P') {
				return false;
			}
			char* parseEnd = 0;
			const unsigned long indexValue = strtoul(parameterId + 1, &parseEnd, 10);
			if (parseEnd == 0 || *parseEnd != 0) {
				return false;
			}
			*outParameterIndex = static_cast<UInt32>(indexValue);
			return true;
		}

		void syncParametersFromAdapter() {
			SYMBIOSIS_ASSERT(adapterInstance.get() != 0);
			const SymbiosisPlugInInfo* const plugInInfo = adapterInstance->getPlugInInfo();
			SYMBIOSIS_ASSERT(plugInInfo != 0);
			for (UInt32 parameterIndex = 0u; parameterIndex < plugInInfo->parameterCount; ++parameterIndex) {
#if SYMBIOSIS_AAX_TRACE
				char message[128];
				snprintf(message, sizeof(message), "syncParametersFromAdapter parameter %u begin",
						static_cast<unsigned int>(parameterIndex));
				traceAAXEvent(message);
#endif
				char parameterId[16];
				if (!encodeSymbiosisAAXParameterId(parameterIndex, parameterId, sizeof(parameterId))) {
					continue;
			}
			Float32 normalizedValue = adapterInstance->getParameterForAAX(parameterIndex);
			if (normalizedValue < 0.0f) {
				normalizedValue = 0.0f;
			}
				if (normalizedValue > 1.0f) {
					normalizedValue = 1.0f;
				}
				(void)SetParameterNormalizedValue(parameterId, normalizedValue);
#if SYMBIOSIS_AAX_TRACE
				snprintf(message, sizeof(message), "syncParametersFromAdapter parameter %u end %.6f",
						static_cast<unsigned int>(parameterIndex),
						static_cast<double>(normalizedValue));
				traceAAXEvent(message);
#endif
			}
		}

		mutable std::unique_ptr<AAXAdapterInstance> adapterInstance;
		mutable std::vector<UByte8> retainedStateChunkBytes;
		bool suppressInitialDefaultUpdates;
};

class SymbiosisAAXGUI : public AAX_CEffectGUI {
	public:
		static AAX_IEffectGUI* AAX_CALLBACK Create() {
			try {
				return new SymbiosisAAXGUI();
			}
			catch (...) {
				traceAAXBoundaryException("AAX GUI create");
				return 0;
			}
		}

		SymbiosisAAXGUI() {
			traceAAXEvent("GUI constructed");
		}

		~SymbiosisAAXGUI() AAX_OVERRIDE {
			try {
				traceAAXEvent("GUI destructor begin");
				AAXAdapterInstance* const adapter = getAdapterInstance();
				if (adapter != 0) {
					adapter->closeUIViewFromAAX();
				}
				traceAAXEvent("GUI destructor end");
			}
			catch (...) {
				traceAAXBoundaryException("AAX GUI destructor");
			}
		}

		AAX_Result GetViewSize(AAX_Point* oViewSize) const AAX_OVERRIDE {
			try {
				if (oViewSize == 0) {
					return AAX_ERROR_NULL_ARGUMENT;
				}
				AAXAdapterInstance* const adapter = getAdapterInstance();
				if (adapter == 0) {
					return AAX_ERROR_UNIMPLEMENTED;
				}
				UInt32 width = 0u;
				UInt32 height = 0u;
				if (!adapter->getUIViewSizeFromAAX(&width, &height)) {
					return AAX_ERROR_UNIMPLEMENTED;
				}
				oViewSize->horz = static_cast<float>(width);
				oViewSize->vert = static_cast<float>(height);
				return AAX_SUCCESS;
			}
			catch (...) {
				traceAAXBoundaryException("AAX GUI GetViewSize");
				return AAX_ERROR_UNKNOWN_EXCEPTION;
			}
		}

	protected:
		void CreateViewContents(void) AAX_OVERRIDE {
			traceAAXEvent("GUI CreateViewContents");
		}

		void CreateViewContainer(void) AAX_OVERRIDE {
			try {
				traceAAXEvent("GUI CreateViewContainer begin");
				AAXAdapterInstance* const adapter = getAdapterInstance();
				if (adapter == 0) {
					traceAAXEvent("GUI CreateViewContainer no adapter");
					return;
				}
				AAX_IViewContainer* const viewContainer = this->GetViewContainer();
				if (viewContainer == 0) {
					traceAAXEvent("GUI CreateViewContainer no container");
					return;
				}
				if (viewContainer->GetType() == static_cast<int32_t>(AAX_eViewContainer_Type_NULL)) {
					traceAAXEvent("GUI CreateViewContainer null container type");
					return;
				}
				void* const nativeView = viewContainer->GetPtr();
				if (nativeView == 0) {
					traceAAXEvent("GUI CreateViewContainer null native view");
					return;
				}
				adapter->bindViewContainer(viewContainer);
				(void)adapter->openUIViewFromAAX(nativeView);
				traceAAXEvent("GUI CreateViewContainer end");
			}
			catch (...) {
				traceAAXBoundaryException("AAX GUI CreateViewContainer");
			}
		}

		void DeleteViewContainer(void) AAX_OVERRIDE {
			try {
				traceAAXEvent("GUI DeleteViewContainer begin");
				AAXAdapterInstance* const adapter = getAdapterInstance();
				if (adapter != 0) {
					adapter->bindViewContainer(0);
					adapter->closeUIViewFromAAX();
			}
				traceAAXEvent("GUI DeleteViewContainer end");
			}
			catch (...) {
				traceAAXBoundaryException("AAX GUI DeleteViewContainer");
		}
	}

	private:
		AAXAdapterInstance* getAdapterInstance() const {
			const AAX_IEffectParameters* const parametersBase = GetEffectParameters();
			if (parametersBase == 0) {
				return 0;
			}
			const SymbiosisAAXParameters* const parameters = static_cast<const SymbiosisAAXParameters*>(parametersBase);
			return parameters->getAdapterInstance();
		}
};

static void processAAXAlgorithmInstance(SymbiosisAAX_Alg_Context* instance) {
	SYMBIOSIS_ASSERT(instance != 0);
	if (instance->privateData == 0) {
		return;
	}
	SymbiosisAAXPrivateData* const privateData = reinterpret_cast<SymbiosisAAXPrivateData*>(instance->privateData);
	if (privateData->magic != SYMBIOSIS_AAX_PRIVATE_DATA_MAGIC || privateData->adapter == 0) {
		return;
	}
	AAXAdapterInstance* const adapter = privateData->adapter;
	SYMBIOSIS_ASSERT(instance->bufferSize != 0);
	const int32_t bufferSize = *instance->bufferSize;
	if (bufferSize <= 0) {
		return;
	}
	// The bypass field is a data-in port fed by the master-bypass packet, which is only registered
	// when `handlesBypass` (EffectInit ~1717). Without that packet the port is never connected and
	// `instance->bypass` holds the AAX sentinel fill (0xCD...), not null -- so gate on handlesBypass
	// FIRST and short-circuit before dereferencing (a null check alone is not enough). When bypass is
	// not handled the plug-in never bypasses anyway, so false is the correct value.
	const Bool8 isBypassed = toBool8(adapter->getPlugInInfo()->handlesBypass != 0
			&& instance->bypass != 0 && *instance->bypass != 0);
	Int32 sideChainInputIndex = 0;
	if (instance->sideChainInputIndex != 0) {
		sideChainInputIndex = *instance->sideChainInputIndex;
	}
	adapter->processAAXAudio(instance->inputChannels, instance->outputChannels, instance->midiInputNode
			, instance->midiOutputNode, static_cast<UInt32>(bufferSize), isBypassed, sideChainInputIndex);
}

static void copyUTF8OrFallback(const UTF8Z* text, const char* fallback, char* out, size_t outSize) {
	SYMBIOSIS_ASSERT(out != 0);
	SYMBIOSIS_ASSERT(outSize > 0u);
	const char* source = fallback;
	if (text != 0 && text[0] != 0) {
		source = text;
	}
	strncpy(out, source, outSize - 1u);
	out[outSize - 1u] = 0;
}

static AAX_Result describeAAXComponentType(const SymbiosisPlugInInfo* plugInInfo
		, const AAXMainTypeDescriptor& mainType
		, UInt32 typeIndex
		, AAX_EStemFormat sidechainStem
		, const std::vector<AAX_EStemFormat>& auxOutputStems
		, bool includeNativeTypeId
		, bool includeAudioSuiteTypeId
		, AAX_IComponentDescriptor* componentDescriptor) {
	AAX_Result result = componentDescriptor->Clear();
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = componentDescriptor->AddAudioIn(SYMBIOSIS_AAX_ALG_FIELD_AUDIO_IN);
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = componentDescriptor->AddAudioOut(SYMBIOSIS_AAX_ALG_FIELD_AUDIO_OUT);
	if (result != AAX_SUCCESS) {
		return result;
	}
	const bool wantsMidiInput = ((plugInInfo->eventCapabilities & SYMBIOSIS_WANTS_MIDI_INPUT_MASK) != 0u);
	const bool createsMidiOutput = ((plugInInfo->eventCapabilities & SYMBIOSIS_CREATES_MIDI_OUTPUT_MASK) != 0u);
	if (wantsMidiInput) {
		result = componentDescriptor->AddMIDINode(SYMBIOSIS_AAX_ALG_FIELD_MIDI_INPUT, AAX_eMIDINodeType_LocalInput
				, "MIDI In", 0xffff);
		if (result != AAX_SUCCESS) {
			return result;
		}
	} else {
		result = componentDescriptor->AddPrivateData(SYMBIOSIS_AAX_ALG_FIELD_MIDI_INPUT, sizeof(float));
		if (result != AAX_SUCCESS) {
			return result;
		}
	}
	if (createsMidiOutput) {
		result = componentDescriptor->AddMIDINode(
				SYMBIOSIS_AAX_ALG_FIELD_MIDI_OUTPUT,
				AAX_eMIDINodeType_LocalOutput,
				"MIDI Out",
				0xffff);
		if (result != AAX_SUCCESS) {
			return result;
		}
	} else {
		result = componentDescriptor->AddPrivateData(SYMBIOSIS_AAX_ALG_FIELD_MIDI_OUTPUT, sizeof(float));
		if (result != AAX_SUCCESS) {
			return result;
		}
	}
	result = componentDescriptor->AddAudioBufferLength(SYMBIOSIS_AAX_ALG_FIELD_BUFFER_SIZE);
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = componentDescriptor->AddDataInPort(SYMBIOSIS_AAX_ALG_FIELD_BYPASS, sizeof(int32_t));
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = componentDescriptor->AddPrivateData(SYMBIOSIS_AAX_ALG_FIELD_PRIVATE_DATA, SYMBIOSIS_AAX_PRIVATE_DATA_SIZE);
	if (result != AAX_SUCCESS) {
		return result;
	}
	if (sidechainStem == AAX_eStemFormat_Mono) {
		result = componentDescriptor->AddSideChainIn(SYMBIOSIS_AAX_ALG_FIELD_SIDECHAIN_IN);
		if (result != AAX_SUCCESS) {
			return result;
		}
	} else {
		result = componentDescriptor->AddPrivateData(SYMBIOSIS_AAX_ALG_FIELD_SIDECHAIN_IN, sizeof(int32_t));
		if (result != AAX_SUCCESS) {
			return result;
		}
	}
	for (UInt32 auxIndex = 0u; auxIndex < static_cast<UInt32>(auxOutputStems.size()); ++auxIndex) {
		char auxName[64];
		const char* stemText = (auxOutputStems[auxIndex] == AAX_eStemFormat_Mono ? "Mono" : "Stereo");
		snprintf(auxName, sizeof(auxName), "Aux Output %u %s", auxIndex + 1u, stemText);
		result = componentDescriptor->AddAuxOutputStem(0, static_cast<int32_t>(auxOutputStems[auxIndex]), auxName);
		if (result != AAX_SUCCESS) {
			return result;
		}
	}

	AAX_IPropertyMap* const properties = componentDescriptor->NewPropertyMap();
	if (properties == 0) {
		return AAX_ERROR_NULL_OBJECT;
	}
	result = properties->AddProperty(AAX_eProperty_ManufacturerID, static_cast<int32_t>(plugInInfo->vendorId));
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = properties->AddProperty(AAX_eProperty_ProductID, static_cast<int32_t>(buildProductTypeId(plugInInfo->plugInId)));
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = properties->AddProperty(AAX_eProperty_CanBypass, (plugInInfo->handlesBypass != 0));
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = properties->AddProperty(AAX_eProperty_UsesTransport, true);
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = properties->AddProperty(AAX_eProperty_Constraint_DoNotApplyDefaultSettings, true);
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = properties->AddProperty(AAX_eProperty_InputStemFormat, static_cast<int32_t>(mainType.inputStem));
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = properties->AddProperty(AAX_eProperty_OutputStemFormat, static_cast<int32_t>(mainType.outputStem));
	if (result != AAX_SUCCESS) {
		return result;
	}
	if (wantsMidiInput) {
		result = properties->AddProperty(AAX_eProperty_Constraint_MultiMonoSupport, false);
		if (result != AAX_SUCCESS) {
			return result;
		}
	}
	if (includeNativeTypeId) {
		result = properties->AddProperty(AAX_eProperty_PlugInID_Native, buildNativeTypeId(plugInInfo->plugInId, typeIndex));
		if (result != AAX_SUCCESS) {
			return result;
		}
	}
	if (sidechainStem != AAX_eStemFormat_None) {
		result = properties->AddProperty(AAX_eProperty_SupportsSideChainInput, true);
		if (result != AAX_SUCCESS) {
			return result;
		}
	}
	if (includeAudioSuiteTypeId) {
		result = properties->AddProperty(AAX_eProperty_PlugInID_AudioSuite, buildAudioSuiteTypeId(plugInInfo->plugInId, typeIndex));
		if (result != AAX_SUCCESS) {
			return result;
		}
	}
	return componentDescriptor->AddProcessProc_Native(symbiosisAAXAlgorithmProcessFunction, properties);
}

} // namespace

AAX_Result GetEffectDescriptions(AAX_ICollection* outCollection);

AAX_Result GetEffectDescriptions(AAX_ICollection* outCollection) {
	if (outCollection == 0) {
		return AAX_ERROR_NULL_OBJECT;
	}
	AAXAdapterInstance adapter;
	if (!adapter.initializeMetadata(0)) {
		return AAX_ERROR_UNIMPLEMENTED;
	}
	const SymbiosisPlugInInfo* const plugInInfo = adapter.getPlugInInfo();
	SYMBIOSIS_ASSERT(plugInInfo != 0);

	AAX_IEffectDescriptor* const effectDescriptor = outCollection->NewDescriptor();
	if (effectDescriptor == 0) {
		return AAX_ERROR_NULL_OBJECT;
	}

	char effectName[128];
	copyUTF8OrFallback(plugInInfo->displayName, "Symbiosis", effectName, sizeof(effectName));
	AAX_Result result = effectDescriptor->AddName(effectName);
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = effectDescriptor->SetRole(adapter.getAAXRole());
	if (result != AAX_SUCCESS) {
		return result;
	}
	const uint32_t categoryBits = adapter.getAAXCategoryBits();
	if (categoryBits != 0u) {
		result = effectDescriptor->AddCategory(categoryBits);
	} else {
		result = effectDescriptor->AddCategory(AAX_ePlugInCategory_Effect);
	}
	if (result != AAX_SUCCESS) {
		return result;
	}

	const std::vector<AAXMainTypeDescriptor>& types = adapter.getAAXMainTypes();
	for (UInt32 typeIndex = 0u; typeIndex < static_cast<UInt32>(types.size()); ++typeIndex) {
		const AAX_EStemFormat sidechainStem = adapter.getAAXSidechainStem();
		AAX_IComponentDescriptor* const componentDescriptor = effectDescriptor->NewComponentDescriptor();
		if (componentDescriptor == 0) {
			return AAX_ERROR_NULL_OBJECT;
		}
		result = describeAAXComponentType(plugInInfo, types[typeIndex], typeIndex, sidechainStem
				, adapter.getAAXAuxOutputStems(), true, (sidechainStem == AAX_eStemFormat_None), componentDescriptor);
		if (result != AAX_SUCCESS) {
			return result;
		}
		result = effectDescriptor->AddComponent(componentDescriptor);
		if (result != AAX_SUCCESS) {
			return result;
		}
		if (sidechainStem != AAX_eStemFormat_None) {
			AAX_IComponentDescriptor* const audioSuiteComponentDescriptor = effectDescriptor->NewComponentDescriptor();
			if (audioSuiteComponentDescriptor == 0) {
				return AAX_ERROR_NULL_OBJECT;
			}
			result = describeAAXComponentType(
					plugInInfo,
					types[typeIndex],
					typeIndex,
					AAX_eStemFormat_None,
					adapter.getAAXAuxOutputStems(),
					false,
					true,
					audioSuiteComponentDescriptor);
			if (result != AAX_SUCCESS) {
				return result;
			}
			result = effectDescriptor->AddComponent(audioSuiteComponentDescriptor);
			if (result != AAX_SUCCESS) {
				return result;
			}
		}
	}

	result = effectDescriptor->AddProcPtr(reinterpret_cast<void*>(SymbiosisAAXParameters::Create)
			, kAAX_ProcPtrID_Create_EffectParameters);
	if (result != AAX_SUCCESS) {
		return result;
	}
	if (plugInInfo->hasCustomUIView != 0) {
		result = effectDescriptor->AddProcPtr(reinterpret_cast<void*>(SymbiosisAAXGUI::Create)
				, kAAX_ProcPtrID_Create_EffectGUI);
		if (result != AAX_SUCCESS) {
			return result;
		}
	}

	char effectId[64];
	snprintf(effectId, sizeof(effectId), "symbiosis.%08X", static_cast<unsigned int>(plugInInfo->plugInId));
	result = outCollection->AddEffect(effectId, effectDescriptor);
	if (result != AAX_SUCCESS) {
		return result;
	}

	char manufacturerName[128];
	char packageName[128];
	copyUTF8OrFallback(plugInInfo->displayVendor, "Symbiosis", manufacturerName, sizeof(manufacturerName));
	copyUTF8OrFallback(plugInInfo->displayName, "Symbiosis", packageName, sizeof(packageName));
	result = outCollection->SetManufacturerName(manufacturerName);
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = outCollection->AddPackageName(packageName);
	if (result != AAX_SUCCESS) {
		return result;
	}
	result = outCollection->SetPackageVersion(plugInInfo->versionHex);
	return result;
}

#if defined(__GNUC__)
#define SYMBIOSIS_AAX_EXPORT extern "C" __attribute__((visibility("default"))) ACFRESULT
#elif defined(_MSC_VER)
#define SYMBIOSIS_AAX_EXPORT extern "C" __declspec(dllexport) ACFRESULT __stdcall
#else
#error Unknown compiler.
#endif

SYMBIOSIS_AAX_EXPORT ACFRegisterPlugin(IACFUnknown* pUnkHost, IACFPluginDefinition** ppPluginDefinition);
SYMBIOSIS_AAX_EXPORT ACFRegisterComponent(IACFUnknown* pUnkHost, acfUInt32 index, IACFComponentDefinition** ppComponentDefinition);
SYMBIOSIS_AAX_EXPORT ACFGetClassFactory(IACFUnknown* pUnkHost, const acfCLSID& clsid, const acfIID& iid, void** ppOut);
SYMBIOSIS_AAX_EXPORT ACFCanUnloadNow(IACFUnknown* pUnkHost);
SYMBIOSIS_AAX_EXPORT ACFStartup(IACFUnknown* pUnkHost);
SYMBIOSIS_AAX_EXPORT ACFShutdown(IACFUnknown* pUnkHost);
SYMBIOSIS_AAX_EXPORT ACFGetSDKVersion(acfUInt64* oSDKVersion);

SYMBIOSIS_AAX_EXPORT ACFRegisterPlugin(IACFUnknown* pUnkHost, IACFPluginDefinition** ppPluginDefinition) {
	ACFRESULT result = ACF_OK;
	try {
		if (ppPluginDefinition == 0) {
			return ACF_E_POINTER;
		}
		*ppPluginDefinition = 0;
		result = AAXRegisterPlugin(pUnkHost, ppPluginDefinition);
	}
	catch (...) {
		result = ACF_E_UNEXPECTED;
	}
	return result;
}

SYMBIOSIS_AAX_EXPORT ACFRegisterComponent(IACFUnknown* pUnkHost, acfUInt32 index
		, IACFComponentDefinition** ppComponentDefinition) {
	ACFRESULT result = ACF_OK;
	try {
		if (ppComponentDefinition == 0) {
			return ACF_E_POINTER;
		}
		*ppComponentDefinition = 0;
		result = AAXRegisterComponent(pUnkHost, index, ppComponentDefinition);
	}
	catch (...) {
		result = ACF_E_UNEXPECTED;
	}
	return result;
}

SYMBIOSIS_AAX_EXPORT ACFGetClassFactory(IACFUnknown* pUnkHost, const acfCLSID& clsid
		, const acfIID& iid, void** ppOut) {
	ACFRESULT result = ACF_OK;
	try {
		if (ppOut == 0) {
			return ACF_E_POINTER;
		}
		*ppOut = 0;
		result = AAXGetClassFactory(pUnkHost, clsid, iid, ppOut);
	}
	catch (...) {
		result = ACF_E_UNEXPECTED;
	}
	return result;
}

SYMBIOSIS_AAX_EXPORT ACFCanUnloadNow(IACFUnknown* pUnkHost) {
	ACFRESULT result = ACF_OK;
	try {
		result = AAXCanUnloadNow(pUnkHost);
	}
	catch (...) {
		result = ACF_E_UNEXPECTED;
	}
	return result;
}

SYMBIOSIS_AAX_EXPORT ACFStartup(IACFUnknown* pUnkHost) {
	ACFRESULT result = ACF_OK;
	try {
		result = AAXStartup(pUnkHost);
	}
	catch (...) {
		result = ACF_E_UNEXPECTED;
	}
	return result;
}

SYMBIOSIS_AAX_EXPORT ACFShutdown(IACFUnknown* pUnkHost) {
	ACFRESULT result = ACF_OK;
	try {
		result = AAXShutdown(pUnkHost);
	}
	catch (...) {
		result = ACF_E_UNEXPECTED;
	}
	return result;
}

SYMBIOSIS_AAX_EXPORT ACFGetSDKVersion(acfUInt64* oSDKVersion) {
	ACFRESULT result = ACF_OK;
	try {
		if (oSDKVersion == 0) {
			return ACF_E_POINTER;
		}
		result = AAXGetSDKVersion(oSDKVersion);
	}
	catch (...) {
		result = ACF_E_UNEXPECTED;
	}
	return result;
}
