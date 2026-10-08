// A Windows-native, hidden-window VST3 host. Never opens an audio device or REAPER.
// Its PASS is deliberately narrower than real-host/manual visual acceptance.
#ifndef _WIN32
#error This host must be compiled and run natively on Windows.
#endif
#if !defined(_M_X64) && !defined(__x86_64__)
#error Build the native test host for x64, not ARM64 or x86.
#endif
#include "FrozenContract.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include "base/source/fstreamer.h"
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace fs = std::filesystem;
using just_windows_test::Product;
namespace {
unsigned checks = 0;
std::string phase = "startup";
std::ofstream logFile;
std::string loadedBinary;
bool dllLoaded = false, passed32 = false, passed64 = false;
void log(const std::string& value) {
    std::cout << value << '\n';
    if (logFile) { logFile << value << '\n'; logFile.flush(); }
}
void check(bool condition, const std::string& label) {
    if (!condition) throw std::runtime_error(phase + ": " + label);
    ++checks;
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::string quoted(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (c < 32) result += "?";
        else result += static_cast<char>(c);
    }
    return result + '"';
}
void pump() {
    MSG message{};
    unsigned dispatched = 0;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        check(message.message != WM_QUIT, "no unexpected WM_QUIT");
        TranslateMessage(&message); DispatchMessageW(&message);
        check(++dispatched < 10000, "bounded hidden-host message queue");
    }
}
void settle(unsigned milliseconds = 40) {
    const auto end = GetTickCount64() + milliseconds;
    do { pump(); Sleep(1); } while (GetTickCount64() < end);
    pump();
}

class Handler final : public FObject, public IComponentHandler {
public:
    unsigned starts = 0, writes = 0, ends = 0;
    std::vector<std::pair<ParamID, ParamValue>> edits;
    std::set<ParamID> open;
    bool valid = true;
    tresult PLUGIN_API beginEdit(ParamID id) override {
        valid &= open.insert(id).second; ++starts; return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        valid &= open.count(id) == 1 && std::isfinite(value) && value >= 0 && value <= 1;
        edits.emplace_back(id, value); ++writes; return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID id) override {
        valid &= open.erase(id) == 1; ++ends; return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
    OBJ_METHODS(Handler, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};

class Frame final : public FObject, public IPlugFrame {
public:
    HWND window = nullptr;
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* size) override {
        if (!view || !size || size->getWidth() <= 0 || size->getHeight() <= 0) return kInvalidArgument;
        SetWindowPos(window, nullptr, 0, 0, size->getWidth(), size->getHeight(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return view->onSize(size);
    }
    OBJ_METHODS(Frame, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugFrame)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)
};

class Window {
public:
    HWND handle = nullptr;
    Window() {
        // Intentionally never WS_VISIBLE, ShowWindow, SetForegroundWindow or SetFocus.
        handle = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"JUST hidden test host",
            WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1800, 1200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(handle != nullptr, "create hidden HWND parent");
    }
    ~Window() { if (handle) DestroyWindow(handle); }
};

HWND control(HWND parent, int id) {
    // Scope IDs to the shell rather than a module's independently numbered controls.
    HWND shell = GetWindow(parent, GW_CHILD);
    if (!shell) return nullptr;
    if (id < 200) return GetDlgItem(shell, id);
    HWND overlay = GetDlgItem(shell, 300);
    HWND panel = overlay ? GetDlgItem(overlay, 301) : nullptr;
    if (!panel) return nullptr;
    if (id < 210) return GetDlgItem(panel, id);
    HWND body = GetDlgItem(panel, 302);
    return body ? GetDlgItem(body, id) : nullptr;
}
void click(HWND parent, int id) {
    auto button = control(parent, id);
    check(button != nullptr && IsWindowEnabled(button), "enabled shell control " + std::to_string(id));
    // Synchronous control messages exercise handler code; these are NOT physical input.
    SendMessageW(button, BM_CLICK, 0, 0); settle();
}
std::wstring windowText(HWND window) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    const int copied = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
    text.resize(static_cast<std::size_t>(std::max(0, copied))); return text;
}
void inventory(HWND parent, const fs::path& path) {
    std::ofstream out(path);
    check(bool(out), "create control inventory");
    EnumChildWindows(parent, [](HWND child, LPARAM data) -> BOOL {
        auto& out = *reinterpret_cast<std::ofstream*>(data);
        wchar_t klass[256]{}; GetClassNameW(child, klass, 256);
        RECT rect{}; GetWindowRect(child, &rect);
        out << GetDlgCtrlID(child) << '\t' << utf8(klass) << '\t' << utf8(windowText(child))
            << '\t' << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom
            << '\t' << (IsWindowEnabled(child) ? "enabled" : "disabled") << '\n';
        return TRUE;
    }, reinterpret_cast<LPARAM>(&out));
}

void screenshot(HWND parent, const fs::path& path, int width, int height) {
    check(width > 0 && height > 0 && width <= 4096 && height <= 4096, "bounded screenshot geometry");
    HWND child = GetWindow(parent, GW_CHILD);
    check(child != nullptr, "native child exists for capture");
    HDC source = GetDC(child), memory = CreateCompatibleDC(source);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!source || !memory || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap); if (memory) DeleteDC(memory); if (source) ReleaseDC(child, source);
        throw std::runtime_error("cannot allocate BMP capture");
    }
    const auto previous = SelectObject(memory, bitmap);
    const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
    std::memset(pixels, 0x7f, bytes);
    // WM_PRINT is a request to the plugin to paint into our memory DC. A saved BMP
    // is supporting evidence only; it does not certify activated-window rendering.
    SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
    GdiFlush();
    BITMAPFILEHEADER file{}; file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(bytes);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(bytes));
    const bool wrote = bool(out);
    const auto* p = static_cast<const std::uint32_t*>(pixels);
    const bool varied = std::any_of(p + 1, p + bytes / 4, [p](std::uint32_t value) { return value != p[0]; });
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(child, source);
    check(wrote, "write BMP evidence");
    log(std::string("CAPTURE ") + path.filename().u8string() + (varied ? " pixels vary; manual review required" : " uniform pixels; capture unsupported/needs investigation"));
}

template<class T> std::vector<std::uint8_t> state(T* object) {
    MemoryStream stream; check(object->getState(&stream) == kResultOk, "serialize complete state");
    check(stream.getSize() > 0 && stream.getSize() < 1024 * 1024, "bounded nonempty state chunk");
    auto bytes = reinterpret_cast<const std::uint8_t*>(stream.getData());
    return {bytes, bytes + stream.getSize()};
}
void restoreComponent(IComponent* object, const std::vector<std::uint8_t>& bytes) {
    MemoryStream stream(const_cast<std::uint8_t*>(bytes.data()), static_cast<TSize>(bytes.size()));
    check(object->setState(&stream) == kResultOk, "restore sound state");
}
void restoreController(IEditController* object, const std::vector<std::uint8_t>& bytes, bool sound) {
    MemoryStream stream(const_cast<std::uint8_t*>(bytes.data()), static_cast<TSize>(bytes.size()));
    check((sound ? object->setComponentState(&stream) : object->setState(&stream)) == kResultOk, "restore controller chunk");
}
struct UiState { int32 version = 0, width = 0, height = 0, language = -1, analyzerRange = 0; bool advanced = false; };
UiState uiState(IEditController* controller) {
    MemoryStream stream; check(controller->getState(&stream) == kResultOk, "save UI state");
    stream.seek(0, IBStream::kIBSeekSet, nullptr); IBStreamer reader(&stream, kLittleEndian);
    UiState result; double scale = 0;
    check(reader.readInt32(result.version) && reader.readInt32(result.width) && reader.readInt32(result.height)
        && reader.readDouble(scale) && reader.readBool(result.advanced) && reader.readInt32(result.language), "read frozen UI v2/v3 prefix");
    check(result.version == 2 || result.version == 3, "preserved UI state version");
    if (result.version == 3) {
        bool background = false, reduced = false, low = false; int32 fps = 0, tag = 0, version = 0, bytes = 0;
        double renderScale = 0;
        check(reader.readBool(background) && reader.readBool(reduced) && reader.readBool(low) && reader.readInt32(fps)
            && reader.readDouble(renderScale) && reader.readInt32(tag) && reader.readInt32(version)
            && reader.readInt32(bytes) && reader.readInt32(result.analyzerRange), "read frozen analyzer UI suffix");
    }
    return result;
}

void validateParameters(IEditController* controller, const Product& expected) {
    check(controller->getParameterCount() == static_cast<int32>(expected.count), "frozen parameter count");
    for (std::size_t i = 0; i < expected.count; ++i) {
        const auto& golden = expected.parameters[i]; ParameterInfo actual{};
        check(controller->getParameterInfo(static_cast<int32>(i), actual) == kResultOk, "read parameter metadata");
        check(actual.id == golden.id && actual.stepCount == golden.steps, "frozen parameter order/ID/steps");
        check(std::abs(actual.defaultNormalizedValue - golden.initial) < 1e-12, "frozen parameter default");
        check(bool(actual.flags & ParameterInfo::kCanAutomate) == golden.automatable, "frozen automation flag");
        check(bool(actual.flags & ParameterInfo::kIsBypass) == (golden.id == 0), "single standard bypass parameter");
        check(std::isfinite(controller->getParamNormalized(actual.id)), "finite initial controller value");
    }
}

std::uint32_t readLE(const std::vector<unsigned char>& bytes, std::size_t offset, std::size_t count) {
    check(offset <= bytes.size() && count <= bytes.size() - offset, "PE field inside file");
    std::uint32_t value = 0; for (std::size_t i = 0; i < count; ++i) value |= std::uint32_t(bytes[offset + i]) << (8 * i);
    return value;
}
fs::path validateBundle(const fs::path& bundle, const Product& product) {
    check(fs::is_directory(bundle), "input is a VST3 bundle directory");
    const std::string filename = std::string("Just_") + product.slug + ".vst3";
    check(bundle.filename() == fs::u8path(filename), "preserved bundle name");
    const auto binary = bundle / "Contents" / "x86_64-win" / fs::u8path(filename);
    check(fs::is_regular_file(binary), "standard x86_64-win bundle binary exists");
    const auto size = fs::file_size(binary); check(size >= 128 && size <= 1024ull * 1024 * 1024, "bounded PE file size");
    std::ifstream in(binary, std::ios::binary); std::vector<unsigned char> bytes(4096);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    check(readLE(bytes, 0, 2) == 0x5a4d, "MZ header"); const std::size_t pe = readLE(bytes, 0x3c, 4);
    check(readLE(bytes, pe, 4) == 0x00004550, "PE signature");
    check(readLE(bytes, pe + 4, 2) == 0x8664, "PE machine AMD64/x64 (not arm64 or x86)");
    check(readLE(bytes, pe + 24, 2) == 0x20b, "PE32+ optional header");
    check((readLE(bytes, pe + 22, 2) & 0x2000) != 0, "PE DLL characteristic");
    check(fs::is_regular_file(bundle / "Contents/Resources/JustUI/manifest.json"), "bundled UI asset manifest");
    check(fs::is_regular_file(bundle / "Contents/Resources/JustUI/icons" / (std::string(product.slug) + ".png")), "bundled plugin icon");
    check(fs::is_regular_file(bundle / "Contents/Resources/Licenses/VST3-SDK-MIT.txt"), "bundled SDK license");
    return binary;
}

struct Classes { VST3::UID processor, controller; };
Classes validateFactory(const VST3::Hosting::Module::Ptr& module, const Product& product) {
    const auto& factory = module->getFactory();
    check(factory.info().vendor() == "Yee Huang", "factory vendor Yee Huang");
    const auto classes = factory.classInfos();
    check(factory.classCount() == 2 && classes.size() == 2, "exact processor/controller pair");
    Classes result; bool processor = false, controller = false;
    const VST3::UID expectedProcessor(0x4a555354, 0x20261001, product.slot, 0x50524f43);
    const VST3::UID expectedController(0x4a555354, 0x20261001, product.slot, 0x4354524c);
    for (const auto& entry : classes) {
        check(entry.name() == product.name && entry.version() == "0.1.0", "actual factory class name/version");
        check(entry.vendor() == "Yee Huang", "actual class vendor Yee Huang");
        if (entry.category() == kVstAudioEffectClass) { check(!processor && entry.ID() == expectedProcessor, "frozen processor FUID"); processor = true; result.processor = entry.ID(); }
        else if (entry.category() == kVstComponentControllerClass) { check(!controller && entry.ID() == expectedController, "frozen controller FUID"); controller = true; result.controller = entry.ID(); }
        else check(false, "no extra class category");
    }
    check(processor && controller, "both factory categories exist"); return result;
}

class Session {
public:
    Window window;
    IPtr<IComponent> component, reference;
    IPtr<IEditController> controller;
    IPtr<IAudioProcessor> processor, twin;
    IPtr<IConnectionPoint> processorConnection, controllerConnection;
    IPtr<IPlugView> view;
    IPtr<Handler> handler = owned(new Handler);
    IPtr<Frame> frame = owned(new Frame);
    bool componentReady = false, referenceReady = false, controllerReady = false;
    bool connectedProcessor = false, connectedController = false, active = false, processing = false, attached = false;
    Session() { frame->window = window.handle; }
    void initialize(const VST3::Hosting::Module::Ptr& module, HostApplication& host, const Classes& ids) {
        const auto& factory = module->getFactory();
        component = factory.createInstance<IComponent>(ids.processor);
        reference = factory.createInstance<IComponent>(ids.processor);
        controller = factory.createInstance<IEditController>(ids.controller);
        check(component && reference && controller, "create independent actual DLL instances");
        check(component->initialize(&host) == kResultOk, "initialize tested component"); componentReady = true;
        check(reference->initialize(&host) == kResultOk, "initialize reference component"); referenceReady = true;
        check(controller->initialize(&host) == kResultOk, "initialize edit controller"); controllerReady = true;
        check(controller->setComponentHandler(handler.get()) == kResultOk, "install host component handler");
        processor = FUnknownPtr<IAudioProcessor>(component);
        twin = FUnknownPtr<IAudioProcessor>(reference);
        processorConnection = FUnknownPtr<IConnectionPoint>(component);
        controllerConnection = FUnknownPtr<IConnectionPoint>(controller);
        check(processor && twin && processorConnection && controllerConnection, "required processor and connection interfaces");
        TUID cid{}; check(component->getControllerClassId(cid) == kResultOk && VST3::UID(cid) == ids.controller, "component advertises frozen controller class");
        check(processorConnection->connect(controllerConnection) == kResultOk, "connect processor to controller"); connectedProcessor = true;
        check(controllerConnection->connect(processorConnection) == kResultOk, "connect controller to processor"); connectedController = true;
    }
    void open() {
        view = owned(controller->createView(ViewType::kEditor)); check(bool(view), "native editor exists");
        check(view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue, "editor supports HWND");
        check(view->setFrame(frame.get()) == kResultOk, "editor accepts host frame");
        check(view->attached(window.handle, kPlatformTypeHWND) == kResultOk, "attach real HWND editor"); attached = true;
        ViewRect rect; check(view->getSize(&rect) == kResultOk && rect.getWidth() > 0 && rect.getHeight() > 0, "editor reports nonzero size");
        SetWindowPos(window.handle, nullptr, 0, 0, rect.getWidth(), rect.getHeight(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        settle();
    }
    void close() {
        if (view) { check(view->removed() == kResultOk, "remove native editor"); attached = false; view->setFrame(nullptr); view.reset(); }
        check(GetWindow(window.handle, GW_CHILD) == nullptr, "remove destroys hosted native children");
    }
    ~Session() {
        if (view) { if (attached) view->removed(); view->setFrame(nullptr); view.reset(); }
        if (processing) { processor->setProcessing(false); twin->setProcessing(false); }
        if (active) { component->setActive(false); reference->setActive(false); }
        if (connectedController) controllerConnection->disconnect(processorConnection);
        if (connectedProcessor) processorConnection->disconnect(controllerConnection);
        if (controllerReady) { controller->setComponentHandler(nullptr); controller->terminate(); }
        if (componentReady) component->terminate();
        if (referenceReady) reference->terminate();
    }
};

void select(HWND parent, int id, int index) {
    HWND combo = control(parent, id);
    check(combo && index >= 0 && index < SendMessageW(combo, CB_GETCOUNT, 0, 0), "settings combo choice exists");
    check(SendMessageW(combo, CB_SETCURSEL, index, 0) != CB_ERR, "select settings combo choice");
    SendMessageW(GetParent(combo), WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo)); settle();
}

template<class Sample> void run(const VST3::Hosting::Module::Ptr& module, HostApplication& host,
    const Classes& ids, const Product& product, const fs::path& evidence) {
    constexpr int format = std::is_same_v<Sample, double> ? kSample64 : kSample32;
    const std::string suffix = format == kSample64 ? "float64" : "float32";
    phase = std::string(product.slug) + "/" + suffix;
    Session s; s.initialize(module, host, ids); validateParameters(s.controller, product);
    const auto initial = state(s.component.get());
    restoreComponent(s.reference, initial); restoreComponent(s.component, initial); restoreController(s.controller, initial, true);
    check(state(s.component.get()) == state(s.reference.get()), "identical complete initial state including seed/configuration");
    check(uiState(s.controller).language == 1 && !uiState(s.controller).advanced, "fresh editor English and Simple");
    if (product.slot == 1) check(uiState(s.controller).version == 3 && uiState(s.controller).analyzerRange == 120, "fresh EQ analyzer default is 120 dB");
    check(s.processor->canProcessSampleSize(format) == kResultTrue && s.twin->canProcessSampleSize(format) == kResultTrue, "both instances support sample format");
    // Verify the supported main stereo layout and explicitly deactivate optional sidechain.
    const int32 inputs = s.component->getBusCount(kAudio, kInput);
    check(inputs == 1 || inputs == 2, "one main input plus at most one optional sidechain");
    check(s.reference->getBusCount(kAudio, kInput) == inputs && s.component->getBusCount(kAudio, kOutput) == 1, "matching main bus topology");
    SpeakerArrangement inputArrangements[]{SpeakerArr::kStereo, SpeakerArr::kEmpty};
    SpeakerArrangement outputArrangement[]{SpeakerArr::kStereo};
    check(s.processor->setBusArrangements(inputArrangements, inputs, outputArrangement, 1) == kResultOk
        && s.twin->setBusArrangements(inputArrangements, inputs, outputArrangement, 1) == kResultOk, "stereo main / disabled optional sidechain");
    for (auto* component : {s.component.get(), s.reference.get()}) {
        check(component->activateBus(kAudio, kInput, 0, true) == kResultOk && component->activateBus(kAudio, kOutput, 0, true) == kResultOk, "activate main buses");
        if (inputs == 2) check(component->activateBus(kAudio, kInput, 1, false) == kResultOk, "deactivate optional sidechain");
    }
    ProcessSetup setup{kRealtime, format, 64, 48000};
    check(s.processor->setupProcessing(setup) == kResultOk && s.twin->setupProcessing(setup) == kResultOk, "48000 Hz / 64 sample setup");
    s.active = true; // Also clean up a partially successful activation on failure.
    check(s.component->setActive(true) == kResultOk && s.reference->setActive(true) == kResultOk, "activate processors");
    s.processing = true;
    check(s.processor->setProcessing(true) == kResultOk && s.twin->setProcessing(true) == kResultOk, "start processors");
    std::array<Sample, 64> left{}, right{}, outLeft{}, outRight{}, refLeft{}, refRight{};
    Sample* inputChannels[]{left.data(), right.data()};
    Sample* outputChannels[]{outLeft.data(), outRight.data()};
    Sample* referenceChannels[]{refLeft.data(), refRight.data()};
    AudioBusBuffers input{}, output{}, refOutput{}; input.numChannels = output.numChannels = refOutput.numChannels = 2;
    if constexpr (std::is_same_v<Sample, double>) { input.channelBuffers64 = inputChannels; output.channelBuffers64 = outputChannels; refOutput.channelBuffers64 = referenceChannels; }
    else { input.channelBuffers32 = inputChannels; output.channelBuffers32 = outputChannels; refOutput.channelBuffers32 = referenceChannels; }
    ProcessContext context{}; context.state = ProcessContext::kTempoValid | ProcessContext::kTimeSigValid | ProcessContext::kProjectTimeMusicValid | ProcessContext::kPlaying;
    context.tempo = 137; context.sampleRate = 48000; context.timeSigNumerator = 4; context.timeSigDenominator = 4;
    ProcessData data{}; data.processMode = kRealtime; data.numSamples = 64; data.symbolicSampleSize = format;
    data.numInputs = data.numOutputs = 1; data.inputs = &input; data.outputs = &output; data.processContext = &context;
    auto referenceData = data; referenceData.outputs = &refOutput;
    std::uint64_t blocks = 0; double maximumDelta = 0; bool nonzeroOutput = false;
    auto render = [&](bool silence = false, IParameterChanges* events = nullptr) {
        for (unsigned i = 0; i < 64; ++i) {
            left[i] = silence ? Sample(0) : Sample(.37 * std::sin((blocks * 64 + i) * .071));
            right[i] = silence ? Sample(0) : Sample(-.23 * std::cos((blocks * 64 + i) * .113));
            outLeft[i] = outRight[i] = refLeft[i] = refRight[i] = std::numeric_limits<Sample>::quiet_NaN();
        }
        const auto beforeLeft = left, beforeRight = right;
        input.silenceFlags = silence ? 3 : 0; output.silenceFlags = refOutput.silenceFlags = 0;
        data.inputParameterChanges = referenceData.inputParameterChanges = events;
        check(s.processor->process(data) == kResultOk && s.twin->process(referenceData) == kResultOk, "render both DLL instances");
        check(left == beforeLeft && right == beforeRight, "out-of-place processing preserves host inputs");
        check(std::memcmp(outLeft.data(), refLeft.data(), sizeof(outLeft)) == 0
            && std::memcmp(outRight.data(), refRight.data(), sizeof(outRight)) == 0, "UI activity preserves bit-exact twin audio");
        check(output.silenceFlags == refOutput.silenceFlags, "matching output silence flags");
        check(state(s.component.get()) == state(s.reference.get()), "complete sound states remain equal");
        for (unsigned i = 0; i < 64; ++i) {
            check(std::isfinite(outLeft[i]) && std::isfinite(outRight[i]), "all output samples written and finite");
            maximumDelta = std::max(maximumDelta, std::abs(double(outLeft[i]) - double(refLeft[i])));
            maximumDelta = std::max(maximumDelta, std::abs(double(outRight[i]) - double(refRight[i])));
            nonzeroOutput |= outLeft[i] != 0 || outRight[i] != 0;
        }
        ++blocks; context.projectTimeSamples += 64; context.projectTimeMusic += 64 * 137. / (60 * 48000);
        pump();
    };
    auto capture = [&](const char* name) {
        if constexpr (std::is_same_v<Sample, double>) {
            ViewRect size; check(s.view->getSize(&size) == kResultOk, "capture view size");
            screenshot(s.window.handle, evidence / (std::string(name) + ".bmp"), size.getWidth(), size.getHeight());
            inventory(s.window.handle, evidence / (std::string(name) + "-controls.tsv"));
        }
    };
    for (unsigned i = 0; i < 24; ++i) render(); // Includes processors with look-ahead latency.
    s.open();
    for (int id : {100, 101, 102, 103, 104}) check(control(s.window.handle, id) != nullptr, "required shell control " + std::to_string(id));
    capture("simple");
    const unsigned initialWrites = s.handler->writes;
    for (int i = 0; i < 13; ++i) {
        const auto before = state(s.component.get()); const bool advanced = uiState(s.controller).advanced;
        click(s.window.handle, 102);
        check(uiState(s.controller).advanced != advanced, "Simple/Advanced changes UI state");
        check(state(s.component.get()) == before && s.handler->writes == initialWrites, "view toggle emits no sound edit");
        render(i > 8);
    }
    check(uiState(s.controller).advanced, "13 toggles end in Advanced"); capture("advanced");
    ViewRect original; check(s.view->getSize(&original) == kResultOk, "get initial editor bounds");
    ViewRect proposed(0, 0, original.getWidth() + 113, original.getHeight() + 67);
    check(s.view->canResize() == kResultTrue && s.view->checkSizeConstraint(&proposed) == kResultTrue, "host resize constrained");
    check(s.frame->resizeView(s.view, &proposed) == kResultOk, "host resizes hidden view");
    ViewRect actual; check(s.view->getSize(&actual) == kResultOk && actual.getWidth() == proposed.getWidth() && actual.getHeight() == proposed.getHeight(), "view accepts legal host size");
    render(); capture("resized");
    check(s.frame->resizeView(s.view, &original) == kResultOk, "restore original view geometry");
    click(s.window.handle, 103); capture("settings-english");
    check(control(s.window.handle, 211) != nullptr, "settings language selector exists");
    select(s.window.handle, 211, 0);
    check(uiState(s.controller).language == 0, "Chinese selector writes UI preference"); capture("settings-chinese");
    const auto localizedChunk = state(s.controller.get());
    select(s.window.handle, 211, 1);
    check(uiState(s.controller).language == 1, "English selector restores UI preference");
    click(s.window.handle, 202); capture("about");
    click(s.window.handle, 203);
    check(control(s.window.handle, 220) && SendMessageW(control(s.window.handle, 220), CB_GETCOUNT, 0, 0) > 0, "preset manager exposes factory choices");
    for (int id : {221, 222, 223, 224, 225}) check(control(s.window.handle, id) != nullptr, "preset manager control exists");
    capture("preset-manager");
    click(s.window.handle, 204);
    check(s.handler->writes == initialWrites, "resize/settings/language emit no automation"); render();
    const auto editorChunk = state(s.controller.get());
    s.close(); render(true); s.open();
    check(state(s.controller.get()) == editorChunk && uiState(s.controller).advanced, "close/reopen preserves separate UI state");
    render(true);
    // Isolated host automation at nonzero offsets must preserve deterministic audio.
    const just_windows_test::Parameter* automated = nullptr;
    for (std::size_t i = 0; i < product.count; ++i)
        if (product.parameters[i].id && product.parameters[i].automatable && product.parameters[i].steps == 0) { automated = &product.parameters[i]; break; }
    check(automated != nullptr, "continuous automation parameter available");
    ParameterChanges automation; int32 queue = 0, point = 0;
    auto* changes = automation.addParameterData(automated->id, queue);
    check(changes && changes->addPoint(7, .38, point) == kResultOk && changes->addPoint(47, .64, point) == kResultOk
        && changes->addPoint(63, automated->initial, point) == kResultOk, "three sample-offset automation points");
    render(false, &automation); render();
    const unsigned starts = s.handler->starts, writes = s.handler->writes, ends = s.handler->ends;
    click(s.window.handle, 101);
    check(s.controller->getParamNormalized(0) == 1 && s.handler->starts == starts + 1 && s.handler->writes == writes + 1
        && s.handler->ends == ends + 1 && s.handler->valid && s.handler->open.empty(), "bypass produces one complete host gesture");
    check(s.handler->edits.back() == std::make_pair(ParamID(0), ParamValue(1)), "bypass gesture is ID0/on");
    ParameterChanges bypass; auto* bypassQueue = bypass.addParameterData(0, queue);
    check(bypassQueue && bypassQueue->addPoint(0, 1, point) == kResultOk, "host delivers bypass event to both instances");
    render(false, &bypass); for (unsigned i = 0; i < 48; ++i) render();
    capture("bypass"); render(true);
    s.close();
    restoreController(s.controller, localizedChunk, false);
    check(uiState(s.controller).language == 0 && s.controller->getParamNormalized(0) == 1, "UI chunk restores language without overwriting sound bypass");
    restoreController(s.controller, editorChunk, false);
    check(s.controller->getParamNormalized(0) == 1, "original editor chunk also preserves sound bypass"); render(true);
    check(nonzeroOutput && maximumDelta == 0, "nonempty audio stimulus and zero observed twin difference");
    // State restoration is checked after stopping; this does not assume that DSP
    // histories or delay buffers are serialized into a preset.
    check(s.processor->setProcessing(false) == kResultOk && s.twin->setProcessing(false) == kResultOk, "stop processors"); s.processing = false;
    check(s.component->setActive(false) == kResultOk && s.reference->setActive(false) == kResultOk, "deactivate processors"); s.active = false;
    const auto saved = state(s.component.get());
    restoreComponent(s.component, initial); restoreComponent(s.reference, initial);
    restoreComponent(s.component, saved); restoreComponent(s.reference, saved);
    check(state(s.component.get()) == saved && state(s.reference.get()) == saved, "sound save/restore round-trip exact");
    log("PASS " + phase + " blocks=" + std::to_string(blocks) + " maximum_twin_sample_delta=" + std::to_string(maximumDelta));
}

void report(const fs::path& path, const std::string& slug, bool passed, const std::string& error) {
    std::ofstream out(path);
    out << "{\n  \"schema\": 1,\n  \"product\": " << quoted(slug)
        << ",\n  \"execution_platform\": \"Windows x64 native CLI\",\n  \"status\": " << quoted(passed ? "PASS" : "FAIL")
        << ",\n  \"checks_completed\": " << checks << ",\n  \"last_phase\": " << quoted(phase)
        << ",\n  \"error\": " << quoted(error)
        << ",\n  \"actual_vst3_dll_loaded\": " << (dllLoaded ? "true" : "false")
        << ",\n  \"loaded_binary\": " << quoted(loadedBinary)
        << ",\n  \"float32_passed\": " << (passed32 ? "true" : "false")
        << ",\n  \"float64_passed\": " << (passed64 ? "true" : "false")
        << ",\n  \"real_reaper_validation\": \"NOT_RUN\",\n  \"physical_mouse_keyboard_input\": \"NOT_RUN\","
        << "\n  \"audio_device_playback\": \"NOT_RUN\",\n  \"mac_vs_windows_audio_comparison\": \"NOT_RUN\","
        << "\n  \"bmp_capture_scope\": \"hidden child HWND WM_PRINT; manual pixel review required\","
        << "\n  \"test_baseline\": \"b99abd3be4ad89b21e3a0c3caf1f12904503f168\"\n}\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    static_assert(sizeof(void*) == 8, "build the host for Windows x64");
    if (argc != 4) {
        std::cerr << "Usage: just_editor_host_windows <bundle.vst3> <slug> <new-evidence-directory>\n"; return 2;
    }
    fs::path evidence; std::string slug; bool comReady = false;
    try {
        slug = utf8(argv[2]); evidence = fs::absolute(argv[3]);
        const Product* product = nullptr;
        for (const auto& candidate : just_windows_test::products) if (slug == candidate.slug) product = &candidate;
        check(product != nullptr, "known frozen product slug");
        // An existing directory might contain earlier evidence or user files.
        check(!fs::exists(evidence), "evidence directory must be new (do not overwrite earlier results)");
        check(fs::create_directories(evidence), "create new isolated evidence directory");
        logFile.open(evidence / "runtime.log"); check(bool(logFile), "create runtime log");
        log("SCOPE Windows x64 DLL runtime + hidden HWND; NOT REAPER or physical-input acceptance");
        phase = "bundle"; const fs::path binary = validateBundle(fs::absolute(argv[1]), *product);
        const auto presetRoot = evidence / "isolated-presets";
        check(SetEnvironmentVariableW(L"JUST_USER_PRESET_ROOT", presetRoot.c_str()) != 0, "isolate potential user preset access");
        check(SetEnvironmentVariableW(L"JUST_PRESET_ROOT", presetRoot.c_str()) != 0, "isolate legacy test preset override");
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        check(SUCCEEDED(initialized), "initialize test process COM STA"); comReady = true;
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES}; check(InitCommonControlsEx(&controls) != FALSE, "initialize common controls");
        std::string error;
        // The native SDK loader uses LoadLibraryW and validates the factory export.
        // Loading the actual DLL after checking bundle structure avoids the SDK's
        // package-path ACP conversion when the user's directory contains Chinese.
        auto module = VST3::Hosting::Module::create(binary.u8string(), error);
        check(bool(module), "load actual candidate DLL: " + error); dllLoaded = true; loadedBinary = binary.u8string(); phase = "factory";
        HostApplication host; module->getFactory().setHostContext(&host);
        const auto ids = validateFactory(module, *product);
        run<float>(module, host, ids, *product, evidence); passed32 = true;
        run<double>(module, host, ids, *product, evidence); passed64 = true;
        module->getFactory().setHostContext(nullptr); module.reset();
        phase = "completed"; report(evidence / "result.json", slug, true, "");
        log("PASS native CLI checks=" + std::to_string(checks) + "; real REAPER/visual/input/audio acceptance remains NOT_RUN");
        CoUninitialize(); return 0;
    } catch (const std::exception& error) {
        log(std::string("FAIL ") + error.what());
        // Only write into a directory created by this invocation; never overwrite
        // an older result when the fresh-directory precondition failed.
        if (logFile.is_open()) report(evidence / "result.json", slug, false, error.what());
        if (comReady) CoUninitialize(); return 1;
    }
}
