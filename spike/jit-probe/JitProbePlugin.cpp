//
//  JitProbePlugin.cpp
//
//  Spike A1 host-probe plug-in. A minimal Symbiosis effect that, on audio configuration, runs the
//  GAZL JIT executable-memory probe once (see probe/GazlJitProbe.*), then behaves as a plain stereo
//  pass-through so it loads and runs cleanly in every host. Built as AU / VST3 / VST2 / AAX from the
//  vendored Symbiosis adapters — see build_*.sh.
//
//  The probe fires from configureAudio(), NOT enableAudio(): configureAudio is the Symbiosis hook that
//  "may allocate resources, called only while audio is disabled", whereas enableAudio() is contractually
//  alloc/blocking/IO-free and may be the realtime thread. The probe mmaps, writes a file and spawns a
//  thread, so configureAudio is the correct, contract-respecting place — and it matches the handoff's
//  "run on audio-processing setup".
//

#include "symbiosis/src/SymbiosisCpp.h"
#include "probe/GazlJitProbe.h"

#include <cstdio>
#include <cstring>

using namespace symbiosis;

namespace {

const SymbiosisAudioBusInfo INPUT_BUSES[1] = {
	{ 1, "Input",  SYMBIOSIS_BUS_FORMAT_STEREO_MASK | SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK }
};
const SymbiosisAudioBusInfo OUTPUT_BUSES[1] = {
	{ 1, "Output", SYMBIOSIS_BUS_FORMAT_STEREO_MASK | SYMBIOSIS_BUS_FORMAT_IO_COUPLED_MASK }
};

SymbiosisPlugInInfo makePlugInInfo() {
	SymbiosisPlugInInfo info;
	memset(&info, 0, sizeof (info));
	info.structVersion = 1;
	info.plugInId = 0x474A6250;					// 'GJbP' (Gazl Jit-probe Plug-in)
	info.displayName = "GAZL JIT Probe";
	info.displayVendor = "GAZL";
	info.versionHex = 0x000100;					// 0.1.0
	info.plugInType = SYMBIOSIS_PLUGIN_TYPE_EFFECT;
	info.plugInCategories = SYMBIOSIS_PLUGIN_CATEGORY_UTILITY_MASK;
	info.vendorId = 0x47415A4C;					// 'GAZL'
	info.vendorUrl = "";						// required non-null by the VST3 factory-metadata path
	info.hasCustomUIView = 0;
	info.handlesBypass = 0;
	info.programCount = 1;
	info.parameterCount = 0;
	info.parameters = 0;
	info.audioInputBusCount = 1;
	info.audioInputBuses = INPUT_BUSES;
	info.audioOutputBusCount = 1;
	info.audioOutputBuses = OUTPUT_BUSES;
	info.eventCapabilities = 0;					// no MIDI, no parameter events
	info.maxInputEventCountPerBlock = 0;
	info.maxOutputEventCountPerBlock = 0;
	return info;
}

const SymbiosisPlugInInfo PLUGIN_INFO = makePlugInInfo();

class JitProbePlugIn : public PlugIn {
	public:		explicit JitProbePlugIn(HostInterface* host)
					: PlugIn(host), inputChannelCount(0), outputChannelCount(0) { }

	// --- required overrides this probe does not use ---
	public:		virtual void changeProgram(UInt32) override { }
	public:		virtual void updateParameter(UInt32, Float32) override { }
	public:		virtual Float32 getParameter(UInt32) override { return 0.0f; }
	public:		virtual void setBypass(bool) override { }
	public:		virtual void setProgramName(UInt32, const UTF8Z*) override { }
	public:		virtual void getProgramName(UInt32, UInt32 maxLength, UTF8Z* outText) override {
					if (outText != 0) std::snprintf(outText, static_cast<size_t>(maxLength) + 1, "Init");
				}
	public:		virtual bool loadState(UInt32, const UByte8*) override { return true; }
	public:		virtual bool createSaveState(UInt32* dataSize, UByte8** data) override {
					if (dataSize != 0) *dataSize = 0;
					if (data != 0) *data = 0;
					return true;
				}
	public:		virtual bool loadProgramState(UInt32, UInt32, const UByte8*) override { return true; }
	public:		virtual bool createProgramSaveState(UInt32, UInt32* dataSize, UByte8** data) override {
					if (dataSize != 0) *dataSize = 0;
					if (data != 0) *data = 0;
					return true;
				}
	public:		virtual void destroySaveState(UByte8*) override { }

	// --- audio ---
	public:		virtual void configureAudio(const SymbiosisConfigureAudioInputArgs* inArgs
						, SymbiosisConfigureAudioOutputArgs* outArgs) override {
					if (inArgs != 0) {
						inputChannelCount = inArgs->inputChannelCount;
						outputChannelCount = inArgs->outputChannelCount;
					}
					if (outArgs != 0) { outArgs->latencySamples = 0; outArgs->tailSamples = 0; }

					// The whole point of this plug-in: run the executable-memory probe once per process.
					// Idempotent and self-contained; never throws, never aborts on a denied rung.
					gazlJitProbeRunOnce("Symbiosis configureAudio");
				}
	public:		virtual void enableAudio() override { }
	public:		virtual void disableAudio() override { }

	public:		virtual void renderAudio(const SymbiosisRenderInputArgs* inArgs
						, SymbiosisRenderOutputArgs* outArgs) override {
					const UInt32 frames = inArgs->bufferSize;
					const UInt32 channels = (outputChannelCount < inputChannelCount)
							? outputChannelCount : inputChannelCount;
					const bool inputConnected = (inArgs->inputBusConnected != 0
							&& inArgs->inputBusConnected[0] != 0 && inArgs->inputChannels != 0);

					for (UInt32 c = 0; c < outputChannelCount; ++c) {
						Float32* out = outArgs->outputChannels[c];
						if (inputConnected && c < channels && inArgs->inputChannels[c] != 0) {
							std::memcpy(out, inArgs->inputChannels[c], frames * sizeof (Float32));
						} else {
							std::memset(out, 0, frames * sizeof (Float32));
						}
					}
					if (outArgs->outputBusSilent != 0) {
						outArgs->outputBusSilent[0] = inputConnected ? 0 : 1;
					}
				}

	protected:	UInt32 inputChannelCount;
	protected:	UInt32 outputChannelCount;
};

class JitProbeFactory : public Factory {
	public:		virtual UInt32 getPlugInCount() override { return 1; }
	public:		virtual const SymbiosisPlugInInfo* getPlugInInfo(UInt32 index) override {
					return (index == 0) ? &PLUGIN_INFO : 0;
				}
	public:		virtual PlugIn* createPlugIn(UInt32 index, HostInterface* host) override {
					return (index == 0 && host != 0) ? new JitProbePlugIn(host) : 0;
				}
};

} // namespace

extern "C" {

SYMBIOSIS_EXPORT void SYMBIOSIS_CALL symbiosisTrace(const UTF8Z* text) {
#if !defined(NDEBUG)
	std::fprintf(stderr, "JitProbe: %s\n", text != 0 ? text : "");
#else
	(void)text;
#endif
}

SYMBIOSIS_EXPORT void SYMBIOSIS_CALL symbiosisAssertFailure(const UTF8Z* text, const char* file, int line) {
	std::fprintf(stderr, "JitProbe assert failed: %s (%s:%d)\n", text != 0 ? text : "", file, line);
	std::abort();
}

SYMBIOSIS_EXPORT SymbiosisFactory* SYMBIOSIS_CALL symbiosisCreateFactory(const SymbiosisLoaderInfo* loaderInfo
		, const SymbiosisFactoryInterface** outFactoryInterface, UInt32 errorTextMaxLength, UTF8Z* outErrorText) {
	(void)loaderInfo;
	(void)errorTextMaxLength;
	(void)outErrorText;
	assert(outFactoryInterface != 0);
	return (new JitProbeFactory())->bridgeToCFactory(outFactoryInterface);
}

} // extern "C"
