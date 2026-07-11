#ifndef SymbiosisCpp_h
#define SymbiosisCpp_h

#include "SymbiosisABI.h"
#include <assert.h>
#include <memory>

#if defined(NDEBUG)
#define SYMBIOSIS_ASSERT(expression) ((void)0)
#else
extern "C" SYMBIOSIS_EXPORT void SYMBIOSIS_CALL symbiosisAssertFailure(const UTF8Z* text, const char* file, int line);
#define SYMBIOSIS_ASSERT(expression) ((expression) ? (void)0 : symbiosisAssertFailure(#expression, __FILE__, __LINE__))
#endif
extern "C" SYMBIOSIS_EXPORT void SYMBIOSIS_CALL symbiosisTrace(const UTF8Z* text);

#if defined(SYMBIOSIS_OVERRIDE_ASSERT)
	#ifdef assert
		#undef assert
	#endif
	#define assert(expression) SYMBIOSIS_ASSERT(expression)
#endif

namespace symbiosis {

void traceMessage(const UTF8Z* context, const UTF8Z* text);
void convertParameterValueToTextDefault(const SymbiosisParameterInfo& parameterInfo, Float32 normalizedValue, UInt32 maxLength, UTF8Z* outText);
bool convertTextToParameterValueDefault(const SymbiosisParameterInfo& parameterInfo, const UTF8Z* text, Float32* outNormalizedValue);
bool parseUnsignedIntegerText(const UTF8Z* text, UInt32* outValue);
bool parseSignedDecimalText(const UTF8Z* text, Float32* outValue);
// maxLength excludes the terminating zero; outText must have space for maxLength + 1 bytes.
// offset is an absolute write offset in [0, maxLength], and return value is the new absolute offset.
UInt32 appendText(UTF8Z* outText, UInt32 maxLength, UInt32 offset, const UTF8Z* source);
UInt32 appendUIntToString(UTF8Z* outText, UInt32 maxLength, UInt32 offset, UInt32 value);
UInt32 appendFloatToString(UTF8Z* outText, UInt32 maxLength, UInt32 offset, Float32 value, UInt32 maxPrecision);
UInt32 calcUTF16ToUTF8Size(UInt32 utf16Size, const UInt16* utf16Chars);
UInt32 convertUTF16ToUTF8(UInt32 utf16Size, const UInt16* utf16Chars, UTF8Z* utf8Chars);
UInt32 calcUTF8ToUTF16Size(UInt32 utf8Size, const UTF8Z* utf8Chars);
UInt32 convertUTF8ToUTF16(UInt32 utf8Size, const UTF8Z* utf8Chars, UInt16* utf16Chars);
Bool8 toBool8(bool value);
UInt32 busFormatMaskToChannelCount(UInt32 formatMask);
UInt16 decodeLE16(const UByte8 bytes[2]);
UInt32 decodeLE32(const UByte8 bytes[4]);
UInt64 decodeLE64(const UByte8 bytes[8]);
Float32 decodeLEFloat32(const UByte8 bytes[4]);
Double64 decodeLEDouble64(const UByte8 bytes[8]);
void encodeLE16(UByte8 bytes[2], UInt16 value);
void encodeLE32(UByte8 bytes[4], UInt32 value);
void encodeLE64(UByte8 bytes[8], UInt64 value);
void encodeLEFloat32(UByte8 bytes[4], Float32 value);
void encodeLEDouble64(UByte8 bytes[8], Double64 value);

// range is `[from, to)` (`to` is exclusive); use `int compare(const T& a, const T& b)`; return value < 0 if a < b, 0 if a == b, > 0 if a > b
template<class T, class Compare> void stableSortNoAlloc(T* array, size_t from, size_t to, Compare compare) {
	assert(array != nullptr || from == to);
	assert(from <= to);
	
	class StableSortNoAlloc {
		public:
			StableSortNoAlloc(T* array, Compare compare) : a(array), compare(compare) { }

			void sort(size_t from, size_t to) {
				const size_t count = to - from;
				if (count > 1) {
					if (count <= 16) {
						insertionSort(from, to);
					} else {
						const size_t middle = from + (count >> 1);
						sort(from, middle);
						sort(middle, to);
						inplaceStableMerge(from, middle, to);
					}
				}
			}

		private:
			void swap(size_t i0, size_t i1) { const T t = a[i0]; a[i0] = a[i1]; a[i1] = t; }
			void rev(size_t from, size_t to) { while (from < to) { --to; swap(from, to); ++from; } }
			void rot(size_t first, size_t middle, size_t last) { rev(first, middle); rev(middle, last); rev(first, last); }

			size_t lowerBound(size_t from, size_t to, T const& value) {
				while (from < to) {
					const size_t middle = from + ((to - from) >> 1);
					if (compare(a[middle], value) < 0) {
						from = middle + 1;
					} else {
						to = middle;
					}
				}
				return from;
			}

			size_t upperBound(size_t from, size_t to, T const& value) {
				while (from < to) {
					const size_t middle = from + ((to - from) >> 1);
					if (compare(value, a[middle]) < 0) {
						to = middle;
					} else {
						from = middle + 1;
					}
				}
				return from;
			}

			void insertionSort(size_t from, size_t to) {
				for (size_t index = from + 1; index < to; ++index) {
					const T value = a[index];
					size_t position = index;
					while (position > from && compare(value, a[position - 1]) < 0) {
						a[position] = a[position - 1];
						--position;
					}
					a[position] = value;
				}
			}

			void inplaceStableMerge(size_t from, size_t middle, size_t to) {
				if (from < middle && middle < to) {
					if (to - from == 2) {
						if (compare(a[middle], a[from]) < 0) {
							swap(from, middle);
						}
					} else if (compare(a[middle], a[middle - 1]) < 0) {
						size_t firstCut;
						size_t secondCut;
						if (middle - from > to - middle) {
							firstCut = from + ((middle - from) >> 1);
							secondCut = lowerBound(middle, to, a[firstCut]);
						} else {
							secondCut = middle + ((to - middle) >> 1);
							firstCut = upperBound(from, middle, a[secondCut]);
						}
						rot(firstCut, middle, secondCut);
						const size_t newMiddle = firstCut + (secondCut - middle);
						inplaceStableMerge(from, firstCut, newMiddle);
						inplaceStableMerge(newMiddle, secondCut, to);
					}
				}
			}

			T* a;
			Compare compare;
	};
	StableSortNoAlloc sorter(array, compare);
	sorter.sort(from, to);
}

using ::SymbiosisParameterScale;
using ::SymbiosisEventType;
using ::SymbiosisEventCapabilities;
using ::SymbiosisPlugInType;
using ::SymbiosisPlugInCategory;

using ::SymbiosisParameterInfo;
using ::SymbiosisAudioBusInfo;
using ::SymbiosisLoaderInfo;
using ::SymbiosisPlugInInfo;
using ::SymbiosisMidiEventData;
using ::SymbiosisParameterEventData;
using ::SymbiosisEvent;
using ::SymbiosisConfigureAudioInputArgs;
using ::SymbiosisConfigureAudioOutputArgs;
using ::SymbiosisRenderInputArgs;
using ::SymbiosisRenderOutputArgs;

class PlugIn;

// Pure C++ host callback interface used by plug-ins.
struct HostInterface {
	virtual void updateDisplay() = 0;
	virtual void beginEdit(UInt32 parameterNumber) = 0;
	virtual void writeParameter(UInt32 parameterNumber, Float32 normalizedValue) = 0;
	virtual void endEdit(UInt32 parameterNumber) = 0;
	virtual bool requestResize(UInt32 width, UInt32 height) = 0;
	virtual const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) = 0;
	virtual ~HostInterface() { }
};

// Pure C++ plug-in behavior interface (no ABI bridge methods).
struct PlugInInterface {
	// Ordered as ::SymbiosisPlugInInterface in SymbiosisABI.h (see there for contracts).
	virtual void changeProgram(UInt32 programNumber) = 0;
	virtual void updateParameter(UInt32 parameterNumber, Float32 normalizedValue) = 0;
	virtual Float32 getParameter(UInt32 parameterNumber) = 0;
	virtual void setBypass(bool bypassed) = 0;
	virtual void configureAudio(const SymbiosisConfigureAudioInputArgs* inArgs, SymbiosisConfigureAudioOutputArgs* outArgs) = 0;
	virtual void enableAudio() = 0;
	virtual void renderAudio(const SymbiosisRenderInputArgs* inArgs, SymbiosisRenderOutputArgs* outArgs) = 0;
	virtual void disableAudio() = 0;
	virtual void setProgramName(UInt32 programNumber, const UTF8Z* text) = 0;
	virtual void getProgramName(UInt32 programNumber, UInt32 maxLength, UTF8Z* outText) = 0;
	virtual bool convertParameterValueToText(UInt32 parameterNumber, Float32 normalizedValue, UInt32 maxLength
			, UTF8Z* outText) = 0;
	virtual bool convertTextToParameterValue(UInt32 parameterNumber, const UTF8Z* text, Float32* normalizedValue) = 0;
	virtual bool loadState(UInt32 dataSize, const UByte8* data) = 0;
	virtual bool createSaveState(UInt32* dataSize, UByte8** data) = 0;
	virtual bool loadProgramState(UInt32 programNumber, UInt32 dataSize, const UByte8* data) = 0;
	virtual bool createProgramSaveState(UInt32 programNumber, UInt32* dataSize, UByte8** data) = 0;
	virtual void destroySaveState(UByte8* data) = 0;
	virtual bool getUIViewSize(UInt32* outWidth, UInt32* outHeight) = 0;
	virtual bool openUIView(void* nativeGUIElement) = 0;
	virtual void closeUIView() = 0;
	virtual const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) = 0;
	virtual void getLastErrorText(UInt32 maxLength, UTF8Z* outText) = 0;
	virtual ~PlugInInterface() { }
};

void tracePlugInLastError(PlugInInterface* plugIn, const UTF8Z* context);

// Pure C++ factory interface. Generic users consume this level.
struct FactoryInterface {
	virtual UInt32 getPlugInCount() = 0;
	virtual const SymbiosisPlugInInfo* getPlugInInfo(UInt32 index) = 0;
	virtual PlugInInterface* createPlugIn(UInt32 index, HostInterface* host) = 0;
	virtual const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) = 0;
	virtual void getLastErrorText(UInt32 maxLength, UTF8Z* outText) = 0;
	virtual ~FactoryInterface() { }
};

// Shared exception-to-lastError support for C++ bridge classes.
class CommonBase {
	public:
		CommonBase();
		virtual ~CommonBase();
		
	protected:
		void recatch();												// call from within an exception catch clause to set error
		void setLastError(const UTF8Z* text);						// used when catching exceptions, you can use for any other errors
		void getLastError(UInt32 maxLength, UTF8Z* outText);		// call from the virtual overload in the sub-class

		UTF8Z lastErrorText[1024];
};

// Plug-in implementation base for the C ABI bridge (plugin side). See SymbiosisABI.h for method documentation.
class PlugIn : public CommonBase, public PlugInInterface {
	public:
		explicit PlugIn(HostInterface* hostInterface);
		
		// Implement some default behavior:
		virtual bool convertTextToParameterValue(UInt32 parameterNumber, const UTF8Z* text
				, Float32* normalizedValue) override;																	// default returns false for default conversion only
		virtual bool convertParameterValueToText(UInt32 parameterNumber, Float32 normalizedValue, UInt32 maxLength
				, UTF8Z* outText) override;																				// default returns false for default conversion only
		virtual bool getUIViewSize(UInt32* outWidth, UInt32* outHeight) override;										// default returns false
		virtual bool openUIView(void* nativeGUIElement) override;														// default returns false
		virtual void closeUIView() override;																			// default does nothing
		virtual const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) override;								// default returns 0
		virtual void getLastErrorText(UInt32 maxLength, UTF8Z* outText) override;										// default calls CommonBase::getLastError()

		// You have to implement these:
		virtual void changeProgram(UInt32 programNumber) override = 0;
		virtual void updateParameter(UInt32 parameterNumber, Float32 normalizedValue) override = 0;
		virtual Float32 getParameter(UInt32 parameterNumber) override = 0;
		virtual void setBypass(bool bypassed) override = 0;
		virtual void setProgramName(UInt32 programNumber, const UTF8Z* text) override = 0;
		virtual void getProgramName(UInt32 programNumber, UInt32 maxLength, UTF8Z* outText) override = 0;
		virtual bool loadState(UInt32 dataSize, const UByte8* data) override = 0;
		virtual bool createSaveState(UInt32* dataSize, UByte8** data) override = 0;
		virtual bool loadProgramState(UInt32 programNumber, UInt32 dataSize, const UByte8* data) override = 0;
		virtual bool createProgramSaveState(UInt32 programNumber, UInt32* dataSize, UByte8** data) override = 0;
		virtual void destroySaveState(UByte8* data) override = 0;
		virtual void configureAudio(const SymbiosisConfigureAudioInputArgs* inArgs
				, SymbiosisConfigureAudioOutputArgs* outArgs) override = 0;
		virtual void enableAudio() override = 0;
		virtual void renderAudio(const SymbiosisRenderInputArgs* inArgs, SymbiosisRenderOutputArgs* outArgs) override = 0;
		virtual void disableAudio() override = 0;

		virtual ~PlugIn();

	protected:
		friend class Factory;
		class CHostInterface;
		
		HostInterface* host() const;

		/*
			Plug-ins normally receive a borrowed HostInterface* (pure C++ mode). When created through the C ABI thunk,
			we wrap host/hostInterface in a CHostInterface and transfer ownership here so the adapter is destroyed
			together with the PlugIn.
		*/
		void adoptHostInterface(std::unique_ptr<HostInterface> hostInterface);

		static void SYMBIOSIS_CALL changeProgramThunk(::SymbiosisPlugIn* plugIn, UInt32 programNumber);
		static void SYMBIOSIS_CALL updateParameterThunk(::SymbiosisPlugIn* plugIn, UInt32 parameterNumber
				, Float32 normalizedValue);
		static Float32 SYMBIOSIS_CALL getParameterThunk(::SymbiosisPlugIn* plugIn, UInt32 parameterNumber);
		static void SYMBIOSIS_CALL setBypassThunk(::SymbiosisPlugIn* plugIn, Bool8 bypassed);
		static void SYMBIOSIS_CALL setProgramNameThunk(::SymbiosisPlugIn* plugIn, UInt32 programNumber
				, const UTF8Z* text);
		static void SYMBIOSIS_CALL getProgramNameThunk(::SymbiosisPlugIn* plugIn, UInt32 programNumber, UInt32 maxLength
				, UTF8Z* outText);
		static Bool8 SYMBIOSIS_CALL convertTextToParameterValueThunk(::SymbiosisPlugIn* plugIn, UInt32 parameterNumber
				, const UTF8Z* text, Float32* normalizedValue);
		static Bool8 SYMBIOSIS_CALL convertParameterValueToTextThunk(::SymbiosisPlugIn* plugIn, UInt32 parameterNumber
				, Float32 normalizedValue, UInt32 maxLength, UTF8Z* outText);
		static Bool8 SYMBIOSIS_CALL loadStateThunk(::SymbiosisPlugIn* plugIn, UInt32 dataSize, const UByte8* data);
		static Bool8 SYMBIOSIS_CALL createSaveStateThunk(::SymbiosisPlugIn* plugIn, UInt32* dataSize, UByte8** data);
		static Bool8 SYMBIOSIS_CALL loadProgramStateThunk(::SymbiosisPlugIn* plugIn, UInt32 programNumber
				, UInt32 dataSize, const UByte8* data);
		static Bool8 SYMBIOSIS_CALL createProgramSaveStateThunk(::SymbiosisPlugIn* plugIn, UInt32 programNumber
				, UInt32* dataSize, UByte8** data);
		static void SYMBIOSIS_CALL destroySaveStateThunk(::SymbiosisPlugIn* plugIn, UByte8* data);
		static Bool8 SYMBIOSIS_CALL getUIViewSizeThunk(::SymbiosisPlugIn* plugIn, UInt32* outWidth, UInt32* outHeight);
		static Bool8 SYMBIOSIS_CALL openUIViewThunk(::SymbiosisPlugIn* plugIn, void* nativeGUIElement);
		static void SYMBIOSIS_CALL closeUIViewThunk(::SymbiosisPlugIn* plugIn);
		static const void* SYMBIOSIS_CALL queryExtensionThunk(::SymbiosisPlugIn* plugIn, UInt32 vendorId
				, UInt32 interfaceId);
		static void SYMBIOSIS_CALL getLastErrorTextThunk(::SymbiosisPlugIn* plugIn, UInt32 maxLength, UTF8Z* outText);
		static void SYMBIOSIS_CALL destroyThunk(::SymbiosisPlugIn* plugIn);
		static void SYMBIOSIS_CALL configureAudioThunk(::SymbiosisPlugIn* plugIn, const SymbiosisConfigureAudioInputArgs* inArgs
				, SymbiosisConfigureAudioOutputArgs* outArgs);
		static void SYMBIOSIS_CALL enableAudioThunk(::SymbiosisPlugIn* plugIn);
		static void SYMBIOSIS_CALL renderAudioThunk(::SymbiosisPlugIn* plugIn, const SymbiosisRenderInputArgs* inArgs
				, SymbiosisRenderOutputArgs* outArgs);
		static void SYMBIOSIS_CALL disableAudioThunk(::SymbiosisPlugIn* plugIn);
		::SymbiosisPlugIn* getABIInstance();
		const ::SymbiosisPlugInInterface* getABIInterface();

		static const ::SymbiosisPlugInInterface abiInterface;
		HostInterface* hostInterface;
		// Non-null only when hostInterface ownership is transferred via adoptHostInterface().
		std::unique_ptr<HostInterface> ownedHostInterface;
};

// Factory implementation base for the C ABI bridge (plugin side). See SymbiosisABI.h for method documentation.
class Factory : public CommonBase, public FactoryInterface {
	public:
		// Implement some default behavior:
		virtual const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) override;								// default returns 0
		virtual void getLastErrorText(UInt32 maxLength, UTF8Z* outText) override;										// default calls CommonBase::getLastError()

		// You have to implement these:
		virtual UInt32 getPlugInCount() override = 0;
		virtual const SymbiosisPlugInInfo* getPlugInInfo(UInt32 index) override = 0;
		virtual PlugIn* createPlugIn(UInt32 index, HostInterface* host) override = 0;

		virtual ~Factory();

		::SymbiosisFactory* bridgeToCFactory(const ::SymbiosisFactoryInterface** outInterface);

	protected:
		static UInt32 SYMBIOSIS_CALL getPlugInCountThunk(::SymbiosisFactory* factory);
		static const SymbiosisPlugInInfo* SYMBIOSIS_CALL getPlugInInfoThunk(::SymbiosisFactory* factory, UInt32 index);
		static ::SymbiosisPlugIn* SYMBIOSIS_CALL createPlugInThunk(::SymbiosisFactory* factory, UInt32 index
				, ::SymbiosisHost* host, const ::SymbiosisHostInterface* hostInterface
				, const ::SymbiosisPlugInInterface** outPlugInInterface);
		static const void* SYMBIOSIS_CALL queryExtensionThunk(::SymbiosisFactory* factory, UInt32 vendorId
				, UInt32 interfaceId);
		static void SYMBIOSIS_CALL getLastErrorTextThunk(::SymbiosisFactory* factory, UInt32 maxLength
				, UTF8Z* outText);
		static void SYMBIOSIS_CALL destroyThunk(::SymbiosisFactory* factory);

		static const ::SymbiosisFactoryInterface abiInterface;
};

// Host callback implementation base when hosting a Symbiosis plug-in from C++.
class Host : public HostInterface {
	public:
		virtual void updateDisplay();
		virtual void beginEdit(UInt32 parameterNumber);
		virtual void writeParameter(UInt32 parameterNumber, Float32 normalizedValue);
		virtual void endEdit(UInt32 parameterNumber);
		virtual bool requestResize(UInt32 width, UInt32 height);
		virtual const void* queryExtension(UInt32 vendorId, UInt32 interfaceId);
		virtual ~Host();

	protected:
		friend class HostedFactory;

		static void SYMBIOSIS_CALL updateDisplayThunk(::SymbiosisHost* host);
		static void SYMBIOSIS_CALL beginEditThunk(::SymbiosisHost* host, UInt32 parameterNumber);
		static void SYMBIOSIS_CALL writeParameterThunk(::SymbiosisHost* host, UInt32 parameterNumber, Float32 normalizedValue);
		static void SYMBIOSIS_CALL endEditThunk(::SymbiosisHost* host, UInt32 parameterNumber);
		static Bool8 SYMBIOSIS_CALL requestResizeThunk(::SymbiosisHost* host, UInt32 width, UInt32 height);
		static const void* SYMBIOSIS_CALL queryExtensionThunk(::SymbiosisHost* host, UInt32 vendorId, UInt32 interfaceId);

		static const ::SymbiosisHostInterface abiInterface;
};

// Host-side adapter around an ABI plug-in instance.
class HostedPlugIn : public PlugInInterface {
	public:
		HostedPlugIn(const HostedPlugIn&) = delete;
		HostedPlugIn& operator=(const HostedPlugIn&) = delete;
		virtual void changeProgram(UInt32 programNumber) override;
		virtual void updateParameter(UInt32 parameterNumber, Float32 normalizedValue) override;
		virtual Float32 getParameter(UInt32 parameterNumber) override;
		virtual void setBypass(bool bypassed) override;
		virtual void setProgramName(UInt32 programNumber, const UTF8Z* text) override;
		virtual void getProgramName(UInt32 programNumber, UInt32 maxLength, UTF8Z* outText) override;
		virtual bool convertTextToParameterValue(UInt32 parameterNumber, const UTF8Z* text
				, Float32* normalizedValue) override;
		virtual bool convertParameterValueToText(UInt32 parameterNumber, Float32 normalizedValue, UInt32 maxLength
				, UTF8Z* outText) override;
		virtual bool loadState(UInt32 dataSize, const UByte8* data) override;
		virtual bool createSaveState(UInt32* dataSize, UByte8** data) override;
		virtual bool loadProgramState(UInt32 programNumber, UInt32 dataSize, const UByte8* data) override;
		virtual bool createProgramSaveState(UInt32 programNumber, UInt32* dataSize, UByte8** data) override;
		virtual void destroySaveState(UByte8* data) override;
		virtual bool getUIViewSize(UInt32* outWidth, UInt32* outHeight) override;
		virtual bool openUIView(void* nativeGUIElement) override;
		virtual void closeUIView() override;
		virtual const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) override;
		virtual void getLastErrorText(UInt32 maxLength, UTF8Z* outText) override;
		virtual void configureAudio(const SymbiosisConfigureAudioInputArgs* inArgs
				, SymbiosisConfigureAudioOutputArgs* outArgs) override;
		virtual void enableAudio() override;
		virtual void renderAudio(const SymbiosisRenderInputArgs* inArgs, SymbiosisRenderOutputArgs* outArgs) override;
		virtual void disableAudio() override;
		virtual ~HostedPlugIn();

	protected:
		friend class HostedFactory;
		HostedPlugIn(::SymbiosisPlugIn* instance, const ::SymbiosisPlugInInterface* api);

		::SymbiosisPlugIn* instance;
		const ::SymbiosisPlugInInterface* api;
};

// Host-side adapter around an ABI factory instance.
class HostedFactory : public FactoryInterface {
	public:
		HostedFactory(::SymbiosisFactory* instance, const ::SymbiosisFactoryInterface* api);
		HostedFactory(const HostedFactory&) = delete;
		HostedFactory& operator=(const HostedFactory&) = delete;
		virtual UInt32 getPlugInCount() override;
		virtual const SymbiosisPlugInInfo* getPlugInInfo(UInt32 index) override;
		virtual HostedPlugIn* createPlugIn(UInt32 index, HostInterface* host) override;
		virtual const void* queryExtension(UInt32 vendorId, UInt32 interfaceId) override;
		virtual void getLastErrorText(UInt32 maxLength, UTF8Z* outText) override;
		virtual ~HostedFactory();

	protected:
		::SymbiosisFactory* instance;
		const ::SymbiosisFactoryInterface* api;
};

} // namespace symbiosis

#endif // SymbiosisCpp_h
