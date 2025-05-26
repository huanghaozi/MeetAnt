#include "ConfigDialog.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/valnum.h> // For wxFloatingPointValidator
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/statline.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/dcclient.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <wx/busyinfo.h>
#include <wx/webrequest.h> // Already added in .h but good practice
#include <wx/sstream.h>   // For wxStringInputStream
#include <wx/uri.h>        // For wxURI
#include <wx/filefn.h>
#include <wx/textfile.h>
#include <nlohmann/json.hpp> // Use nlohmann/json
#include <portaudio.h>

// 如果未定义portaudio错误代码，则在此处定义
#ifndef paIncompatibleStreamInfo
#define paIncompatibleStreamInfo -9986  /* 使用一个不太可能与已有错误码冲突的负值 */
#endif
#ifndef paBufferTooBig
#define paBufferTooBig -9987  /* 使用一个不太可能与已有错误码冲突的负值 */
#endif
#ifndef paBufferTooSmall
#define paBufferTooSmall -9988  /* 使用一个不太可能与已有错误码冲突的负值 */
#endif

// Windows-specific headers for WASAPI
#ifdef _WIN32
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <Functiondiscoverykeys_devpkey.h> // For PKEY_Device_FriendlyName
// WASAPI回环模式标志 - 直接定义以避免链接依赖
#ifndef AUDCLNT_STREAMFLAGS_LOOPBACK
#define AUDCLNT_STREAMFLAGS_LOOPBACK 0x00020000
#endif

// PortAudio的WASAPI API类型定义，确保和PortAudio的pa_win_wasapi.h一致
typedef enum PaWasapiFlags
{
    paWinWasapiExclusive               = 1,
    paWinWasapiRedirectHostProcessor   = 2,
    paWinWasapiUseChannelMask          = 4,
    paWinWasapiPolling                 = 8,
    paWinWasapiThreadPriority          = 16
} PaWasapiFlags;

typedef struct PaWasapiStreamInfo {
    unsigned long size;             // sizeof(PaWasapiStreamInfo)
    PaHostApiTypeId hostApiType;    // paWASAPI
    unsigned long version;          // 1
    unsigned long flags;            // 组合PaWasapiFlags值
    UINT32 streamCategory;          // 流类别
    REFERENCE_TIME *hDuration;      // 缓冲区持续时间
} PaWasapiStreamInfo;

// Helper to release COM objects
template<class T> void SafeRelease(T **ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = NULL;
    }
}
#endif

// for convenience
using json = nlohmann::json;

// --- AudioConfigPanel 事件表 ---
BEGIN_EVENT_TABLE(AudioConfigPanel, wxPanel)
    EVT_BUTTON(wxID_ANY, AudioConfigPanel::OnTestAudio)
    EVT_TIMER(wxID_ANY, AudioConfigPanel::OnTimer)
    // EVT_CHECKBOX(wxID_ANY, AudioConfigPanel::OnSystemAudioToggled)
END_EVENT_TABLE()

// --- FunASRConfigPanel 事件表 ---
wxBEGIN_EVENT_TABLE(FunASRConfigPanel, wxPanel)
    EVT_RADIOBUTTON(wxID_ANY, FunASRConfigPanel::OnModeChanged)
    EVT_BUTTON(wxID_ANY, FunASRConfigPanel::OnDownloadModel)
    EVT_BUTTON(wxID_ANY, FunASRConfigPanel::OnCheckLocalModel)
wxEND_EVENT_TABLE()

// --- LLMConfigPanel 事件表 ---
wxBEGIN_EVENT_TABLE(LLMConfigPanel, wxPanel)
    EVT_BUTTON(LLM_TEST_BUTTON_ID, LLMConfigPanel::OnTestConnection)
wxEND_EVENT_TABLE()

// --- SystemConfigPanel 事件表 ---
wxBEGIN_EVENT_TABLE(SystemConfigPanel, wxPanel)
    EVT_BUTTON(wxID_ANY, SystemConfigPanel::OnRunDiagnostics)
wxEND_EVENT_TABLE()

// --- ConfigDialog 事件表 ---
wxBEGIN_EVENT_TABLE(ConfigDialog, wxDialog)
    EVT_BUTTON(wxID_OK, ConfigDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, ConfigDialog::OnCancel)
    EVT_BUTTON(wxID_APPLY, ConfigDialog::OnApply)
wxEND_EVENT_TABLE()

// --- AudioConfigPanel 实现 ---
AudioConfigPanel::AudioConfigPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
    , m_paStream(nullptr)
    , m_isAudioInitialized(false)
    , m_audioBuffer(nullptr)
    , m_testTimer(nullptr)
    // , m_loopbackDeviceCombo(nullptr) 
    // , m_loopbackDeviceLabel(nullptr)
#ifdef _WIN32
    , m_comInitialized(false) // Initialize new Windows-specific members
    , m_pEnumerator(nullptr)
    , m_pSelectedLoopbackDevice(nullptr)
    , m_pAudioClient(nullptr)
    , m_pCaptureClient(nullptr)
    , m_pWaveFormat(nullptr)
    , m_bDirectWasapiLoopbackActive(false)
#endif
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // 初始化PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        wxLogError(wxT("无法初始化PortAudio: %s"), wxString::FromUTF8(Pa_GetErrorText(err)));
    }
    
    // 设备选择
    wxStaticBoxSizer* deviceSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("音频输入设置"));
    wxBoxSizer* deviceRowSizer = new wxBoxSizer(wxHORIZONTAL);
    deviceRowSizer->Add(new wxStaticText(this, wxID_ANY, wxT("输入设备:")), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    m_deviceCombo = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_READONLY);
    deviceRowSizer->Add(m_deviceCombo, 1, wxEXPAND);
    deviceSizer->Add(deviceRowSizer, 0, wxEXPAND|wxALL, 5);
    
    // 填充设备列表
    PopulateAudioDevices();
    
    // 系统内录选项
    m_systemAudioCheckBox = new wxCheckBox(this, wxID_ANY, wxT("启用系统内录(若选中的是播放设备请手动勾选此处，系统无法自动识别)"));
    deviceSizer->Add(m_systemAudioCheckBox, 0, wxEXPAND|wxALL, 5);
    
    // 添加捕获类型选择下拉框
    // wxBoxSizer* captureModeSizer = new wxBoxSizer(wxHORIZONTAL);
    // captureModeSizer->Add(new wxStaticText(this, wxID_ANY, wxT("内录模式:")), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    
    // wxArrayString captureTypes;
    // captureTypes.Add(wxT("WASAPI (推荐)"));
    // captureTypes.Add(wxT("WDMKS (兼容旧系统)"));
    // m_captureTypeChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, captureTypes);
    // m_captureTypeChoice->SetSelection(0); // 默认选择WASAPI
    // captureModeSizer->Add(m_captureTypeChoice, 1, wxEXPAND);
    // deviceSizer->Add(captureModeSizer, 0, wxEXPAND|wxALL, 5);
    
    // 新增：系统内录源选择
    // wxBoxSizer* loopbackDeviceRowSizer = new wxBoxSizer(wxHORIZONTAL);
    // m_loopbackDeviceLabel = new wxStaticText(this, wxID_ANY, wxT("系统内录源:"));
    // loopbackDeviceRowSizer->Add(m_loopbackDeviceLabel, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    // m_loopbackDeviceCombo = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_READONLY);
    // loopbackDeviceRowSizer->Add(m_loopbackDeviceCombo, 1, wxEXPAND);
    // deviceSizer->Add(loopbackDeviceRowSizer, 0, wxEXPAND|wxALL, 5);

    // 初始状态设置
    // m_captureTypeChoice->Enable(m_systemAudioCheckBox->GetValue());
    // m_loopbackDeviceLabel->Show(m_systemAudioCheckBox->GetValue() && m_captureTypeChoice->GetSelection() == 0); // Show only if sys audio & WASAPI
    // m_loopbackDeviceCombo->Show(m_systemAudioCheckBox->GetValue() && m_captureTypeChoice->GetSelection() == 0); // Show only if sys audio & WASAPI
    m_deviceCombo->Enable(!m_systemAudioCheckBox->GetValue());
    
    // 采样率选择
    wxBoxSizer* rateRowSizer = new wxBoxSizer(wxHORIZONTAL);
    rateRowSizer->Add(new wxStaticText(this, wxID_ANY, wxT("采样率:")), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 5);
    wxArrayString sampleRates;
    sampleRates.Add(wxT("8000"));
    sampleRates.Add(wxT("16000"));
    sampleRates.Add(wxT("44100"));
    sampleRates.Add(wxT("48000"));
    m_sampleRateChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sampleRates);
    m_sampleRateChoice->SetSelection(1); // 默认16000
    rateRowSizer->Add(m_sampleRateChoice, 1, wxEXPAND);
    deviceSizer->Add(rateRowSizer, 0, wxEXPAND|wxALL, 5);
    
    // 静音检测阈值
    wxBoxSizer* thresholdSizer = new wxBoxSizer(wxVERTICAL);
    thresholdSizer->Add(new wxStaticText(this, wxID_ANY, wxT("静音检测阈值:")), 0, wxBOTTOM, 5);
    m_silenceThresholdSlider = new wxSlider(this, wxID_ANY, 5, 0, 100, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL|wxSL_LABELS);
    thresholdSizer->Add(m_silenceThresholdSlider, 0, wxEXPAND);
    deviceSizer->Add(thresholdSizer, 0, wxEXPAND|wxALL, 5);
    
    mainSizer->Add(deviceSizer, 0, wxEXPAND|wxALL, 10);
    
    // 音频测试部分
    wxStaticBoxSizer* testSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("音频测试"));
    
    // 音量计和标签
    wxBoxSizer* volumeSizer = new wxBoxSizer(wxHORIZONTAL);
    m_volumeMeter = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(250, 20), wxGA_HORIZONTAL);
    volumeSizer->Add(m_volumeMeter, 1, wxEXPAND|wxRIGHT, 5);
    m_volumeLabel = new wxStaticText(this, wxID_ANY, wxT("0 dB"));
    volumeSizer->Add(m_volumeLabel, 0, wxALIGN_CENTER_VERTICAL);
    testSizer->Add(volumeSizer, 0, wxEXPAND|wxALL, 5);
    
    // 测试按钮
    m_testAudioButton = new wxButton(this, wxID_ANY, wxT("测试音频"));
    testSizer->Add(m_testAudioButton, 0, wxALIGN_CENTER|wxALL, 5);
    
    mainSizer->Add(testSizer, 0, wxEXPAND|wxALL, 10);
    
    SetSizer(mainSizer);
    Layout();
    
    // 为音频缓冲区分配内存
    m_audioBuffer = new float[BUFFER_SIZE];
}

// 析构函数
AudioConfigPanel::~AudioConfigPanel() {
#ifdef _WIN32
    // 确保在关闭 PortAudio 或 Direct WASAPI 之前停止并清理捕获线程
    if (m_pCaptureThread) {
        wxLogDebug(wxT("AudioConfigPanel Destructor: Stopping WasapiCaptureThread..."));
        m_pCaptureThread->RequestStop();
        m_pCaptureThread->Wait(); // Wait for the thread to finish
        delete m_pCaptureThread;
        m_pCaptureThread = nullptr;
        wxLogDebug(wxT("AudioConfigPanel Destructor: WasapiCaptureThread stopped and deleted."));
    }
    // If Direct WASAPI was active and not cleaned up by thread stop (e.g. thread never started but init happened),
    // ShutdownAudioCapture will handle it. Or, if m_bDirectWasapiLoopbackActive is true, explicitly shut it down first.
    // However, ShutdownAudioCapture() is designed to check m_bDirectWasapiLoopbackActive internally.
#endif

    // 关闭PortAudio流和释放资源 (this will call ShutdownAudioCapture)
    // ShutdownAudioCapture() will internally check if it needs to shutdown PortAudio or DirectWASAPI.
    ShutdownAudioCapture(); 
    
    // 清理定时器
    if (m_testTimer) {
        if (m_testTimer->IsRunning()) {
            m_testTimer->Stop();
        }
        delete m_testTimer;
        m_testTimer = nullptr;
    }
    
    // 终止PortAudio (Pa_Terminate should be called after all PA streams are closed)
    PaError err = Pa_Terminate();
    if (err != paNoError) {
        wxLogError(wxT("AudioConfigPanel Destructor: Pa_Terminate failed: %s"), wxString::FromUTF8(Pa_GetErrorText(err)));
    }
    
    // 释放音频缓冲区
    if (m_audioBuffer) {
        delete[] m_audioBuffer;
        m_audioBuffer = nullptr;
    }

#ifdef _WIN32
    // 如果COM是由这个面板初始化的，并且还没有被反初始化
    // 通常 CoUninitialize 应该在应用程序级别管理，或者由最后使用COM的组件管理。
    // 如果 ShutdownDirectWASAPILoopback 中的 CoUninitialize 被注释掉了，
    // 并且确定此面板是唯一或最后一个用户，则可以在此处取消注释。
    // 但是，更安全的做法是在 App::OnExit() 中进行全局 CoUninitialize。
    // if (m_comInitialized) {
    //     CoUninitialize();
    //     m_comInitialized = false;
    // }
#endif
    wxLogDebug(wxT("AudioConfigPanel destroyed."));
}

#ifdef _WIN32
bool AudioConfigPanel::InitializeDirectWASAPILoopback(int paOutputDeviceIndex, int requestedSampleRateFromUI) {
    HRESULT hr;
    wxLogDebug(wxT("InitializeDirectWASAPILoopback: Starting for PA Output Device Index %d, UI Sample Rate %dHz."), paOutputDeviceIndex, requestedSampleRateFromUI);

    // 0. Get the PortAudio device info for the selected output device to find its name
    if (paOutputDeviceIndex < 0 || paOutputDeviceIndex >= Pa_GetDeviceCount()) {
        wxLogError(wxT("DirectWASAPI: Invalid PortAudio output device index: %d"), paOutputDeviceIndex);
        return false;
    }
    const PaDeviceInfo* paSelectedDevInfo = Pa_GetDeviceInfo(paOutputDeviceIndex);
    if (!paSelectedDevInfo) {
        wxLogError(wxT("DirectWASAPI: Could not get PaDeviceInfo for index: %d"), paOutputDeviceIndex);
        return false;
    }
    // The name from PortAudio might have API info, e.g., "Device Name [Windows WASAPI]"
    // We need the base name for matching with PKEY_Device_FriendlyName.
    wxString targetDeviceFullName = wxString::FromUTF8(paSelectedDevInfo->name);
    wxString targetDeviceBaseName = targetDeviceFullName;
    int bracketPos = targetDeviceBaseName.Find(wxT(" ["));
    if (bracketPos != wxNOT_FOUND) {
        targetDeviceBaseName = targetDeviceBaseName.Left(bracketPos);
    }
    wxLogDebug(wxT("DirectWASAPI: Target loopback device base name from PA: '%s' (Full: '%s', PA Index: %d)"), targetDeviceBaseName, targetDeviceFullName, paOutputDeviceIndex);

    // 1. Initialize COM
    if (!m_comInitialized) { // Initialize COM only once
        hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            wxLogError(wxT("DirectWASAPI: CoInitializeEx failed: hr = 0x%08lx"), hr);
            return false;
        }
        m_comInitialized = true;
    }

    // 2. Create Device Enumerator
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&m_pEnumerator);
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: CoCreateInstance(MMDeviceEnumerator) failed: hr = 0x%08lx"), hr);
        // Do not call full ShutdownDirectWASAPILoopback here as other members might not be init.
        SafeRelease(&m_pEnumerator);
        // CoUninitialize should happen in destructor or when app closes if m_comInitialized is true.
        return false;
    }

    // 3. Find the IMMDevice corresponding to paOutputDeviceIndex (by name)
    IMMDeviceCollection *pRenderEndpoints = nullptr;
    hr = m_pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pRenderEndpoints);
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: EnumAudioEndpoints(eRender) failed: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback(); // Safe to call now as it checks members
        return false;
    }

    UINT endpointCount = 0;
    pRenderEndpoints->GetCount(&endpointCount);
    bool foundDevice = false;
    for (UINT i = 0; i < endpointCount; ++i) {
        IMMDevice *pEndpoint = nullptr;
        hr = pRenderEndpoints->Item(i, &pEndpoint);
        if (FAILED(hr)) continue;

        IPropertyStore *pProps = nullptr;
        hr = pEndpoint->OpenPropertyStore(STGM_READ, &pProps);
        if (SUCCEEDED(hr)) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
            if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
                wxString currentDeviceSystemName = wxString(varName.pwszVal);
                // wxLogDebug(wxT("DirectWASAPI: Checking render device: '%s'"), currentDeviceSystemName);
                if (targetDeviceBaseName == currentDeviceSystemName) {
                    m_pSelectedLoopbackDevice = pEndpoint;
                    m_pSelectedLoopbackDevice->AddRef(); // We are storing this COM pointer
                    wxLogInfo(wxT("DirectWASAPI: Found matching render device by name: '%s'"), currentDeviceSystemName);
                    foundDevice = true;
                }
            }
            PropVariantClear(&varName);
            SafeRelease(&pProps);
        }
        SafeRelease(&pEndpoint); // Release the IMMDevice obtained from Item() if not stored
        if (foundDevice) break;
    }
    SafeRelease(&pRenderEndpoints);

    if (!foundDevice || !m_pSelectedLoopbackDevice) {
        wxLogError(wxT("DirectWASAPI: Could not find a matching active render device for '%s' to loopback from."), targetDeviceBaseName);
        ShutdownDirectWASAPILoopback();
        return false;
    }

    // 4. Activate IAudioClient
    hr = m_pSelectedLoopbackDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_pAudioClient);
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: m_pSelectedLoopbackDevice->Activate(IAudioClient) failed: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }

    // 5. Get Mix Format (THIS IS THE FORMAT WE MUST USE FOR LOOPBACK)
    hr = m_pAudioClient->GetMixFormat(&m_pWaveFormat); // m_pWaveFormat is a member
    if (FAILED(hr) || !m_pWaveFormat) {
        wxLogError(wxT("DirectWASAPI: pAudioClient->GetMixFormat failed: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }
    wxLogDebug(wxT("DirectWASAPI: Actual Mix Format to be used - Sample Rate: %u, Channels: %u, BitsPerSample: %u, FormatTag: %u, BlockAlign: %u, AvgBytesPerSec: %u"),
               m_pWaveFormat->nSamplesPerSec, m_pWaveFormat->nChannels, m_pWaveFormat->wBitsPerSample, m_pWaveFormat->wFormatTag, m_pWaveFormat->nBlockAlign, m_pWaveFormat->nAvgBytesPerSec);

    // 6. Initialize IAudioClient for Loopback
    REFERENCE_TIME hnsBufferDuration = 300000; // 30 ms buffer, example. Adjust as needed.
                                               // For loopback, a small buffer is often fine.
    hr = m_pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        hnsBufferDuration, // Buffer duration
        0,                 // Periodicity for event-driven (0 for timer-driven/pull mode)
        m_pWaveFormat,     // Use the device's mix format
        NULL);             // Audio session GUID (optional)
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: pAudioClient->Initialize failed: hr = 0x%08lx. This can happen if format is not supported for loopback or other issues."), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }

    // 7. Get IAudioCaptureClient
    hr = m_pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_pCaptureClient);
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: pAudioClient->GetService(IAudioCaptureClient) failed: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }

    // 8. Start the stream
    hr = m_pAudioClient->Start();
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: pAudioClient->Start failed: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }

    m_bDirectWasapiLoopbackActive = true;
    wxLogInfo(wxT("Direct WASAPI Loopback initialized and stream started successfully. Actual format: %u Hz, %u Ch."), m_pWaveFormat->nSamplesPerSec, m_pWaveFormat->nChannels);

    // NEXT STEP: Implement data capture using m_pCaptureClient, likely in a separate thread or adapted CaptureAndAnalyzeAudio.
    // For now, this method only initializes.
    return true;
}

void AudioConfigPanel::ShutdownDirectWASAPILoopback() {
    wxLogDebug(wxT("ShutdownDirectWASAPILoopback: Starting shutdown."));
    if (m_pAudioClient) {
        m_pAudioClient->Stop(); // Stop the stream first
    }
    SafeRelease(&m_pCaptureClient);
    SafeRelease(&m_pAudioClient);
    if (m_pWaveFormat) {
        CoTaskMemFree(m_pWaveFormat);
        m_pWaveFormat = nullptr;
    }
    SafeRelease(&m_pSelectedLoopbackDevice);
    SafeRelease(&m_pEnumerator);

    // CoUninitialize should typically be called when the application is sure COM is no longer needed,
    // or once per successful CoInitialize. For a panel, it might be better in the App's exit or
    // if this panel is the sole user of COM for its lifetime.
    // For simplicity here, if this panel initialized it, it uninitializes it.
    // Be cautious if other parts of your app use COM.
    if (m_comInitialized) {
        // CoUninitialize(); // Commented out: Manage COM lifetime carefully.
                          // Typically CoUninitialize is called when the app exits or the COM-using component is destroyed.
                          // If called too early, it can affect other COM users.
                          // For now, we'll leave m_comInitialized as true if it was set.
        // m_comInitialized = false; // Resetting this would mean CoInitializeEx is called again next time.
    }
    m_bDirectWasapiLoopbackActive = false;
    wxLogDebug(wxT("ShutdownDirectWASAPILoopback: Completed."));
}

// --- AudioConfigPanel::WasapiCaptureThread Implementation ---
AudioConfigPanel::WasapiCaptureThread::WasapiCaptureThread(
    AudioConfigPanel* panel, 
    IAudioClient* audioClient,
    IAudioCaptureClient* captureClient,
    WAVEFORMATEX* waveFormat)
    : wxThread(wxTHREAD_JOINABLE), m_panel(panel), 
      m_pAudioClientRef(audioClient), m_pCaptureClientRef(captureClient), 
      m_pWaveFormatRef(waveFormat), m_bStopRequested(false) {
    wxLogDebug(wxT("WasapiCaptureThread created."));
}

AudioConfigPanel::WasapiCaptureThread::~WasapiCaptureThread() {
    wxLogDebug(wxT("WasapiCaptureThread destroyed."));
}

float AudioConfigPanel::WasapiCaptureThread::ProcessAudioPacket(const BYTE* pData, UINT32 numFramesAvailable, const WAVEFORMATEX* wfex) {
    if (!pData || numFramesAvailable == 0 || !wfex) {
        return -60.0f; // Default silence
    }

    float sum = 0.0f;
    int numSamplesProcessed = 0;

    // Assuming wfex->wFormatTag == WAVE_FORMAT_IEEE_FLOAT and wfex->wBitsPerSample == 32
    // If not, you'll need to handle other formats (e.g., PCM16 and convert to float)
    if (wfex->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wfex->wBitsPerSample == 32) {
        const float* fData = reinterpret_cast<const float*>(pData);
        for (UINT32 i = 0; i < numFramesAvailable * wfex->nChannels; ++i) {
            sum += fData[i] * fData[i];
        }
        numSamplesProcessed = numFramesAvailable * wfex->nChannels;
    } else if (wfex->wFormatTag == WAVE_FORMAT_PCM && wfex->wBitsPerSample == 16) {
        const SHORT* iData = reinterpret_cast<const SHORT*>(pData);
        for (UINT32 i = 0; i < numFramesAvailable * wfex->nChannels; ++i) {
            float sample = static_cast<float>(iData[i]) / 32768.0f; // Normalize to -1.0 to 1.0
            sum += sample * sample;
        }
        numSamplesProcessed = numFramesAvailable * wfex->nChannels;
    } else {
        // wxLogWarning(wxT("WasapiCaptureThread: Unsupported audio format for RMS calculation. FormatTag: %u, BitsPerSample: %u"), wfex->wFormatTag, wfex->wBitsPerSample);
        // To avoid spamming logs for every packet, log once or handle appropriately
        return -60.0f; // Or some other indicator of format issue
    }

    if (numSamplesProcessed == 0) return -60.0f;

    float rms = sqrt(sum / numSamplesProcessed);
    float db = 20.0f * log10(rms);
    if (db < -60.0f || std::isinf(db) || std::isnan(db)) db = -60.0f;
    
    return db;
}

wxThread::ExitCode AudioConfigPanel::WasapiCaptureThread::Entry() {
    wxLogDebug(wxT("WasapiCaptureThread::Entry() started."));
    HRESULT hr;
    UINT32 packetLength = 0;
    UINT32 numFramesAvailable = 0;
    BYTE *pData = nullptr;
    DWORD flags = 0;
    wxLongLong loopStartTime, callStartTime, callEndTime;

    // 尝试在线程中初始化COM，以防主线程的初始化模式不适用于此线程
    // CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); // 或者 COINIT_MULTITHREADED，取决于主线程如何初始化
    // bool comInitializedInThread = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    // if (comInitializedInThread) {
    //     wxLogDebug(wxT("WasapiCaptureThread: COM initialized successfully in thread (COINIT_MULTITHREADED)."));
    // } else {
    //     wxLogWarning(wxT("WasapiCaptureThread: Failed to initialize COM in thread or already initialized differently."));
    // }

    while (!m_bStopRequested && !TestDestroy()) {
        loopStartTime = wxGetUTCTimeMillis();
        // wxLogDebug(wxT("T_LoopStart: %lld"), loopStartTime);

        callStartTime = wxGetUTCTimeMillis();
        hr = m_pCaptureClientRef->GetNextPacketSize(&packetLength);
        callEndTime = wxGetUTCTimeMillis();
        if (packetLength > 0 || (callEndTime - callStartTime > 5)) { // Log if packet available or call took > 5ms
             wxLogDebug(wxT("T_GNPS: hr=0x%08lx, len=%u, dur=%lldms"), hr, packetLength, callEndTime - callStartTime);
        }

        if (FAILED(hr)) {
            wxLogError(wxT("WasapiCaptureThread: GetNextPacketSize failed, hr=0x%08lx. Exiting thread."), hr);
            break; 
        }

        if (packetLength > 0) {
            callStartTime = wxGetUTCTimeMillis();
            hr = m_pCaptureClientRef->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
            callEndTime = wxGetUTCTimeMillis();
            // Log if call took > 5ms or if there are frames (even silent ones)
            if (numFramesAvailable > 0 || (callEndTime - callStartTime > 5)) {
                wxLogDebug(wxT("T_GBUF: hr=0x%08lx, frames=%u, flags=0x%lx, dur=%lldms"), hr, numFramesAvailable, flags, callEndTime - callStartTime);
            }

            if (SUCCEEDED(hr)) {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    // wxLogDebug(wxT("T_SILENT_PACKET"));
                    if (m_panel) m_panel->GetEventHandler()->CallAfter([this, panel = m_panel]() { panel->UpdateDirectWasapiDBValue(-60.0f); });
                } else if (numFramesAvailable > 0) {
                    // wxLogDebug(wxT("T_DATA_PACKET: %u frames"), numFramesAvailable);
                    float dbValue = ProcessAudioPacket(pData, numFramesAvailable, m_pWaveFormatRef);
                    if (m_panel) m_panel->GetEventHandler()->CallAfter([this, panel = m_panel, dbValue]() { panel->UpdateDirectWasapiDBValue(dbValue); });
                }
                
                callStartTime = wxGetUTCTimeMillis();
                m_pCaptureClientRef->ReleaseBuffer(numFramesAvailable);
                callEndTime = wxGetUTCTimeMillis();
                // if (callEndTime - callStartTime > 2) { // Log if ReleaseBuffer took > 2ms
                //    wxLogDebug(wxT("T_RLSB: dur=%lldms"), callEndTime - callStartTime);
                // }
            } else {
                wxLogError(wxT("WasapiCaptureThread: GetBuffer failed, hr=0x%08lx. Continuing loop."), hr);
                // 不一定是致命错误，可能只是暂时没buffer，尝试继续
            }
        } else { // packetLength == 0 (No data in the current capture period)
            // wxLogDebug(wxT("T_NO_PACKET_DATA. Sleeping."));
            callStartTime = wxGetUTCTimeMillis();
            wxMilliSleep(10); // 使用 10ms 睡眠时间进行测试
            callEndTime = wxGetUTCTimeMillis();
            // Log if sleep duration was significantly different from 10ms
            // if (abs((callEndTime - callStartTime) - 10) > 5) { 
            //    wxLogDebug(wxT("T_SLEEP: dur=%lldms"), callEndTime - callStartTime);
            // }
        }
        // wxLongLong loopEndTime = wxGetUTCTimeMillis();
        // if (loopEndTime - loopStartTime > 25) { // Log if entire loop took > 25ms
        //    wxLogDebug(wxT("T_LoopEnd: TotalLoopDur=%lldms"), loopEndTime - loopStartTime);
        // }
    }

    // if (comInitializedInThread) {
    //     CoUninitialize();
    //     wxLogDebug(wxT("WasapiCaptureThread: COM uninitialized in thread."));
    // }
    wxLogDebug(wxT("WasapiCaptureThread::Entry() finishing."));
    return (wxThread::ExitCode)0;
}

void AudioConfigPanel::UpdateDirectWasapiDBValue(float db) {
    wxCriticalSectionLocker lock(m_captureDataCritSec);
    m_latestDirectWasapiDBValue = db;
    // This method is called on the main thread via CallAfter,
    // so m_latestDirectWasapiDBValue is updated safely.
    // The OnTimer event will then pick this up to update the GUI.
}
#endif // _WIN32

// 初始化音频捕获
bool AudioConfigPanel::InitializeAudioCapture(bool systemAudioMode, int deviceIndex, int sampleRate) {
    // 如果已经初始化，先关闭当前流
    if (m_isAudioInitialized) { // This flag might need to be more specific if mixing PortAudio and DirectWASAPI
        ShutdownAudioCapture();
    }
    
    wxLogDebug(wxT("InitializeAudioCapture: Entry with deviceIndex: %d (PortAudio Index), sampleRate: %d, systemAudioMode: %s"),
              deviceIndex, sampleRate, systemAudioMode ? wxT("Yes") : wxT("No"));

#ifdef _WIN32
    // int captureTypeChoice = m_captureTypeChoice->GetSelection(); // 0 for WASAPI, 1 for WDMKS
    // if (systemAudioMode) {
    //     wxLogInfo(wxT("Attempting Direct WASAPI Loopback initialization."));
    //     if (InitializeDirectWASAPILoopback(deviceIndex, sampleRate)) { // deviceIndex is the PortAudio index of the selected OUTPUT device
    //         m_isAudioInitialized = true; // Indicates some form of audio is ready
    //         // Note: m_bDirectWasapiLoopbackActive is set true inside InitializeDirectWASAPILoopback
    //         wxLogInfo(wxT("Direct WASAPI Loopback successfully initialized."));
    //         return true;
    //     } else {
    //         wxLogError(wxT("Direct WASAPI Loopback initialization failed. Falling back to PortAudio attempt or failing."));
    //         // Decide on fallback behavior. For now, we'll let it fall through to PortAudio code below,
    //         // or you could return false here if direct WASAPI is a hard requirement for this mode.
    //         // For safety, let's try to make it clear it failed and won't proceed with PortAudio for this specific case.
    //         // If you want to fall back to PortAudio's WASAPI loopback, remove the 'return false'.
    //         // However, user explicitly wants to bypass PortAudio for this.
    //          // return false; // Or let it fall through to PortAudio code? User wants to bypass PA.
    //     }
    // }
    // If not (systemAudioMode && captureTypeChoice == 0 && _WIN32), then the PortAudio logic below will run.
#endif

    // --- Existing PortAudio Initialization Logic STARTS HERE ---
    // This block will execute if not on Windows, or if on Windows but not doing Direct WASAPI Loopback
    // (e.g., microphone input, WDMKS, or if the Direct WASAPI block above was skipped/failed and we decided to fall through)

    wxLogDebug(wxT("Proceeding with PortAudio-based initialization."));
    // (The rest of your existing InitializeAudioCapture method for PortAudio follows)
    // ... (original Pa_OpenStream logic etc.) ...
    // Example:
    // wxLogDebug(wxT("InitializeAudioCapture: Entry with deviceIndex: %d, sampleRate: %d, systemAudioMode: %s"), /* already logged */);
    
    // For system audio mode, the provided deviceIndex is a hint or might be stale if the combo was disabled.
    // Pa_GetDeviceInfo needs a valid index. If the provided one is invalid, pick a default.
    // The actual loopback device will be searched for later.
    if (deviceIndex < 0 || deviceIndex >= Pa_GetDeviceCount()) {
        wxLogWarning(wxT("InitializeAudioCapture (System Mode): Provided deviceIndex %d is invalid. Using default input device as initial context."), deviceIndex);
        deviceIndex = Pa_GetDefaultInputDevice();
        if (deviceIndex == paNoDevice) { // If no default input, try the first available device
            int numDev = Pa_GetDeviceCount();
            if (numDev > 0) {
                deviceIndex = 0; // Use device 0 as a last resort
                wxLogWarning(wxT("InitializeAudioCapture (System Mode): No default input device. Using device 0 as initial context."));
            } else {
                wxLogError(wxT("InitializeAudioCapture (System Mode): No audio devices found. Cannot initialize."));
                return false;
            }
        }
    }
    // At this point, deviceIndex should be a valid index for Pa_GetDeviceInfo()
    wxLogDebug(wxT("InitializeAudioCapture: Proceeding with effective deviceIndex: %d for initial setup."), deviceIndex);
    
    // 检查设备索引的有效性 - 这部分逻辑已移到上面并根据systemAudioMode调整
    // if (deviceIndex < 0 || deviceIndex >= Pa_GetDeviceCount()) {
    //     wxLogError(wxT("无效的设备索引"));
    //     return false;
    // }
    
    // 记录初始化参数 - 已移到函数开头
    // wxLogDebug(wxT("初始化音频捕获 - 系统内录模式: %s, 设备索引: %d, 采样率: %d"), 
    //           systemAudioMode ? wxT("是") : wxT("否"), deviceIndex, sampleRate);
    
    PaStreamParameters inputParameters;
    inputParameters.device = deviceIndex;
    
    // 获取设备信息
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
    
    wxLogDebug(wxT("选中设备: %s [%s], 输入通道: %d, 输出通道: %d"), 
              wxString::FromUTF8(deviceInfo->name),
              wxString::FromUTF8(hostApiInfo->name),
              deviceInfo->maxInputChannels,
              deviceInfo->maxOutputChannels);
    inputParameters.channelCount = (deviceInfo->maxInputChannels >= 1) ? 1 : 2;
    
    // wxLogDebug(wxT("使用通道数: %d"), inputParameters.channelCount);
    
    inputParameters.sampleFormat = paFloat32; // 32位浮点采样
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(deviceIndex)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;
    
    // WASAPI回环特定参数
#ifdef _WIN32
    PaWasapiStreamInfo wasapiInfo;
    // 初始化WASAPI结构体
    memset(&wasapiInfo, 0, sizeof(PaWasapiStreamInfo));
    wasapiInfo.size = sizeof(PaWasapiStreamInfo);
    wasapiInfo.hostApiType = paWASAPI;
    wasapiInfo.version = 1;
#endif
    
    wxLogDebug(wxT("PA Path: Final inputParameters.device before Pa_OpenStream: %d"), inputParameters.device);
    PaError err = Pa_OpenStream(
        reinterpret_cast<void**>(&m_paStream),
        &inputParameters,
        nullptr,
        sampleRate,
        BUFFER_SIZE,
        paNoFlag, // paNoFlag is often preferred for WASAPI to let hostApiSpecificStreamInfo rule.
        nullptr, 
        nullptr);

    if (err == paInvalidChannelCount && inputParameters.channelCount == 2) {
        wxLogWarning(wxT("PA Path: Pa_OpenStream failed with paInvalidChannelCount (2ch). Retrying with 1 channel for system audio."));
        inputParameters.channelCount = 1;
        err = Pa_OpenStream(reinterpret_cast<void**>(&m_paStream), &inputParameters, nullptr, sampleRate, BUFFER_SIZE, paNoFlag, nullptr, nullptr);
    }

    if (err != paNoError) {
        wxLogDebug(wxT("Attempting Pa_OpenStream with: device = %d, channels = %d, sampleRate = %d, hostApiSpecificStreamInfo = %p"),
                  inputParameters.device, inputParameters.channelCount, sampleRate, inputParameters.hostApiSpecificStreamInfo);
        if (inputParameters.hostApiSpecificStreamInfo != nullptr) {
    #ifdef _WIN32
            // 安全地转换并检查类型
            if (inputParameters.hostApiSpecificStreamInfo == &wasapiInfo && wasapiInfo.size == sizeof(PaWasapiStreamInfo) && wasapiInfo.hostApiType == paWASAPI) {
                 wxLogDebug(wxT("PaWasapiStreamInfo flags for Pa_OpenStream: 0x%lX"), wasapiInfo.flags);
            } else {
                 wxLogDebug(wxT("hostApiSpecificStreamInfo is set but not the expected PaWasapiStreamInfo structure or type."));
            }
    #endif
        }
        wxLogError(wxT("打开音频流失败: %s (错误码: %d)"), wxString::FromUTF8(Pa_GetErrorText(err)), err);
        // 根据不同的错误码给出更详细的建议
        switch (err) {
            case paInvalidDevice:
                wxLogError(wxT("无效的设备索引 %d - 请选择其他设备"), inputParameters.device); // 使用inputParameters.device而不是旧的deviceIndex
                break;
            case paUnanticipatedHostError:
                {
                    const PaHostErrorInfo* hostErrorInfo = Pa_GetLastHostErrorInfo();
                    wxLogError(wxT("主机错误: %s (错误码: %d)"), 
                             wxString::FromUTF8(hostErrorInfo->errorText), 
                             hostErrorInfo->errorCode);
                }
                break;
            case paInvalidSampleRate:
                wxLogError(wxT("设备不支持采样率 %d Hz，请尝试其他采样率"), sampleRate);
                break;
            case paInvalidChannelCount: // 明确处理无效声道数错误
                wxLogError(wxT("设备不支持 %d 声道输入 (错误码: %d). 设备名称: %s"), 
                         inputParameters.channelCount, err, wxString::FromUTF8(Pa_GetDeviceInfo(inputParameters.device)->name));
                break;
            case paIncompatibleHostApiSpecificStreamInfo:
                wxLogError(wxT("流信息不兼容，系统内录可能不被支持"));
                break;
            case paBufferTooBig:
            case paBufferTooSmall:
                wxLogError(wxT("缓冲区大小不适合，请尝试调整BUFFER_SIZE或采样率"));
                break;
            default:
                wxLogError(wxT("打开流时发生未知错误"));
                break;
        }
        return false;
    }
    
    // 启动流
    err = Pa_StartStream(m_paStream);
    if (err != paNoError) {
        wxLogError(wxT("启动音频流失败: %s (错误码: %d)"), wxString::FromUTF8(Pa_GetErrorText(err)), err);
        
        // 根据不同的错误码给出更详细的建议
        if (err == paUnanticipatedHostError) {
            const PaHostErrorInfo* hostErrorInfo = Pa_GetLastHostErrorInfo();
            wxLogError(wxT("主机错误: %s (错误码: %d)"), 
                     wxString::FromUTF8(hostErrorInfo->errorText), 
                     hostErrorInfo->errorCode);
        }
        
        Pa_CloseStream(m_paStream);
        m_paStream = nullptr;
        return false;
    }
    
    wxLogDebug(wxT("音频流初始化成功!"));
    m_isAudioInitialized = true;
    return true;
}

// 关闭音频捕获
void AudioConfigPanel::ShutdownAudioCapture() {
#ifdef _WIN32
    if (m_bDirectWasapiLoopbackActive) {
        ShutdownDirectWASAPILoopback();
        // m_bDirectWasapiLoopbackActive is set to false inside ShutdownDirectWASAPILoopback()
    } else {
        // PortAudio stream shutdown
        if (m_paStream) {
            if (Pa_IsStreamActive(m_paStream)) {
                Pa_StopStream(m_paStream);
            }
            Pa_CloseStream(m_paStream);
            m_paStream = nullptr;
            wxLogDebug(wxT("PortAudio stream shut down."));
        }
    }
#else
    // PortAudio stream shutdown (non-Windows)
    if (m_paStream) {
        if (Pa_IsStreamActive(m_paStream)) {
            Pa_StopStream(m_paStream);
        }
        Pa_CloseStream(m_paStream);
        m_paStream = nullptr;
        wxLogDebug(wxT("PortAudio stream shut down (non-Windows)."));
    }
#endif
    m_isAudioInitialized = false; // General flag, reset for both paths
}

// 捕获并分析音频
float AudioConfigPanel::CaptureAndAnalyzeAudio() {
    if (!m_isAudioInitialized || !m_paStream) {
        return 0.0f;
    }

    // 检查是否有可读取的数据
    long available = Pa_GetStreamReadAvailable(m_paStream);
    if (available < BUFFER_SIZE) {
        return 0.0f;
    }
    
    // 读取音频数据
    PaError err = Pa_ReadStream(m_paStream, m_audioBuffer, BUFFER_SIZE);
    if (err != paNoError) {
        wxLogError(wxT("读取音频流失败: %s"), wxString::FromUTF8(Pa_GetErrorText(err)));
        return 0.0f;
    }
    
    // 计算RMS (均方根) 音量
    float sum = 0.0f;
    for (int i = 0; i < BUFFER_SIZE; i++) {
        sum += m_audioBuffer[i] * m_audioBuffer[i];
    }
    float rms = sqrt(sum / BUFFER_SIZE);
    
    // 转换为dB (分贝)
    float db = 20.0f * log10(rms);
    
    // 限制下限为-60dB
    if (db < -60.0f) db = -60.0f;
    
    return db;
}

// 填充音频设备列表
void AudioConfigPanel::PopulateAudioDevices() {
    m_deviceCombo->Clear();
    
    int numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        wxLogError(wxT("获取设备数量失败: %s"), wxString::FromUTF8(Pa_GetErrorText(numDevices)));
        return;
    }
    
    // 默认设备
    int defaultDevice = Pa_GetDefaultInputDevice();
    
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo->maxInputChannels > 0) {
            // 添加设备名称，标记默认设备
            wxString deviceName = wxString::FromUTF8(deviceInfo->name);
            if (i == defaultDevice) {
                deviceName += wxT(" (默认)");
            }
            
            // 添加设备所属API信息
            const PaHostApiInfo* hostApi = Pa_GetHostApiInfo(deviceInfo->hostApi);
            deviceName += wxString::Format(wxT(" [%s]"), wxString::FromUTF8(hostApi->name));
            
            m_deviceCombo->Append(deviceName, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        }
    }
    
    // 选择默认设备
    if (m_deviceCombo->GetCount() > 0) {
        for (unsigned int i = 0; i < m_deviceCombo->GetCount(); i++) {
            intptr_t devIdx = reinterpret_cast<intptr_t>(m_deviceCombo->GetClientData(i));
            if (devIdx == defaultDevice) {
                m_deviceCombo->SetSelection(i);
                break;
            }
        }
    }
}

// 测试音频事件处理
void AudioConfigPanel::OnTestAudio(wxCommandEvent& event) {
    // 当前是否正在测试音频 (m_isAudioInitialized 也可以作为一个指标，但 m_testTimer->IsRunning() 更直接反映测试状态)
    bool isCurrentlyTesting = (m_testTimer && m_testTimer->IsRunning());

    if (!isCurrentlyTesting) { // --- 开始测试 --- 
        intptr_t deviceIndex = -1;
        // int captureType = m_captureTypeChoice->GetSelection(); // 0 for WASAPI, 1 for WDMKS

        int deviceSelection = m_deviceCombo->GetSelection();
        if (deviceSelection == wxNOT_FOUND) {
            wxMessageBox(wxT("请选择一个音频输入设备"), wxT("无法测试音频"), wxICON_ERROR);
            return;
        }
        deviceIndex = reinterpret_cast<intptr_t>(m_deviceCombo->GetClientData(deviceSelection));
        wxString sampleRateStr = m_sampleRateChoice->GetStringSelection();
        long sampleRate;
        if (!sampleRateStr.ToLong(&sampleRate)) {
            sampleRate = 16000; 
        }
        
        if (!InitializeAudioCapture(false, static_cast<int>(deviceIndex), static_cast<int>(sampleRate))) {
            wxMessageBox(wxT("初始化音频捕获失败"), wxT("错误"), wxICON_ERROR);
            // Ensure m_isAudioInitialized is false if init failed, ShutdownAudioCapture might also do this.
            m_isAudioInitialized = false; 
#ifdef _WIN32
            m_bDirectWasapiLoopbackActive = false;
#endif
            return;
        }
        
        // m_isAudioInitialized should be true now if InitializeAudioCapture succeeded.
        // m_bDirectWasapiLoopbackActive would be true if Direct WASAPI path was taken and succeeded.

#ifdef _WIN32
        if (m_bDirectWasapiLoopbackActive) {
            if (m_pAudioClient && m_pCaptureClient && m_pWaveFormat) {
                 m_pCaptureThread = new WasapiCaptureThread(this, m_pAudioClient, m_pCaptureClient, m_pWaveFormat);
                 if (m_pCaptureThread->Run() != wxTHREAD_NO_ERROR) {
                     wxLogError(wxT("OnTestAudio: Failed to run WasapiCaptureThread."));
                     delete m_pCaptureThread;
                     m_pCaptureThread = nullptr;
                     ShutdownAudioCapture(); // Clean up WASAPI resources as thread failed
                     wxMessageBox(wxT("启动WASAPI捕获线程失败。"), wxT("错误"), wxICON_ERROR);
                     return;
                 } else {
                     wxLogDebug(wxT("OnTestAudio: WasapiCaptureThread started."));
                 }
            } else {
                wxLogError(wxT("OnTestAudio: Direct WASAPI was active but essential interfaces are null. Cannot start thread."));
                ShutdownAudioCapture(); // Clean up partially initialized WASAPI
                wxMessageBox(wxT("直接WASAPI接口未完全初始化，无法启动测试。"), wxT("错误"), wxICON_ERROR);
                return;
            }
        } else {
            // PortAudio path was taken (either microphone or PortAudio-based WASAPI/WDMKS loopback)
            // CaptureAndAnalyzeAudio will be called by the timer for this path.
            wxLogDebug(wxT("OnTestAudio (Start): PortAudio path taken. Timer will use CaptureAndAnalyzeAudio."));
        }
#else
        // Non-Windows: PortAudio path always taken. Timer will use CaptureAndAnalyzeAudio.
        wxLogDebug(wxT("OnTestAudio (Start) [Non-Windows]: PortAudio path taken. Timer will use CaptureAndAnalyzeAudio."));
#endif
        
        m_testAudioButton->SetLabel(wxT("停止测试"));
        if (!m_testTimer) {
            m_testTimer = new wxTimer(this, wxID_ANY);
        }
        m_testTimer->Start(100); // Update UI every 100ms

    } else { // --- 停止测试 --- 
        wxLogDebug(wxT("OnTestAudio: Stopping test..."));
#ifdef _WIN32
        if (m_pCaptureThread) { // If WASAPI capture thread was running
            wxLogDebug(wxT("OnTestAudio: Requesting WasapiCaptureThread to stop..."));
            m_pCaptureThread->RequestStop();
            // It's good practice to wait for the thread to finish before deleting it and releasing resources it might use.
            // However, Wait() can block. If the thread doesn't exit promptly, this could hang the UI briefly.
            // For a quick stop, one might skip Wait() if ShutdownDirectWASAPILoopback is robust against the thread still running briefly.
            // But safer to Wait(). Consider a timeout for Wait() if needed.
            m_pCaptureThread->Wait(); 
            wxLogDebug(wxT("OnTestAudio: WasapiCaptureThread finished."));
            delete m_pCaptureThread;
            m_pCaptureThread = nullptr;
            wxLogDebug(wxT("OnTestAudio: WasapiCaptureThread deleted."));
        }
#endif
        // ShutdownAudioCapture handles both DirectWASAPI and PortAudio stream cleanup
        ShutdownAudioCapture(); 
        // m_isAudioInitialized and m_bDirectWasapiLoopbackActive are set to false inside ShutdownAudioCapture or its sub-calls.

        m_testAudioButton->SetLabel(wxT("测试音频"));
        if (m_testTimer && m_testTimer->IsRunning()) {
            m_testTimer->Stop();
            wxLogDebug(wxT("OnTestAudio: Test timer stopped."));
        }
        
        m_volumeMeter->SetValue(0);
        m_volumeLabel->SetLabel(wxT("0 dB"));
        wxLogDebug(wxT("OnTestAudio: Test stopped. UI reset."));
    }
}

// 定时器事件处理（更新音量计）
void AudioConfigPanel::OnTimer(wxTimerEvent& event) {
    wxLogDebug(wxT("AudioConfigPanel::OnTimer Fired!"));
#ifdef _WIN32
    if (m_bDirectWasapiLoopbackActive) {
        if (m_pCaptureThread) { // Ensure thread pointer is valid
            float dbValue;
            { // Scope for critical section locker
                wxCriticalSectionLocker lock(m_captureDataCritSec);
                dbValue = m_latestDirectWasapiDBValue;
            }
            
            // 计算音量计的值 (-60dB到0dB映射到0-100)
            int meterValue = static_cast<int>((dbValue + 60.0f) * (100.0f / 60.0f));
            if (meterValue < 0) meterValue = 0;
            if (meterValue > 100) meterValue = 100;
            
            // 更新UI
            m_volumeMeter->SetValue(meterValue);
            m_volumeLabel->SetLabel(wxString::Format(wxT("%.1f dB"), dbValue));
        } else {
            // Direct WASAPI was expected to be active but thread is missing. Log or handle.
            // This state should ideally not occur if OnTestAudio manages the thread correctly.
            // For safety, can reset VU meter.
            // m_volumeMeter->SetValue(0);
            // m_volumeLabel->SetLabel(wxT("Error"));
        }
    }
    else // Not Direct WASAPI, check PortAudio
#endif
    if (m_paStream && Pa_IsStreamActive(m_paStream)) { // Check if PortAudio stream is active
        float db = CaptureAndAnalyzeAudio(); // This is for PortAudio path
        
        // 计算音量计的值 (-60dB到0dB映射到0-100)
        int meterValue = static_cast<int>((db + 60.0f) * (100.0f / 60.0f));
        if (meterValue < 0) meterValue = 0;
        if (meterValue > 100) meterValue = 100;
        
        // 更新UI
        m_volumeMeter->SetValue(meterValue);
        m_volumeLabel->SetLabel(wxString::Format(wxT("%.1f dB"), db));
    } else {
        // Neither Direct WASAPI nor active PortAudio stream, but timer is running.
        // This can happen briefly during transitions or if a test was stopped.
        // Optionally, ensure VU meter is at a "stopped" state.
        // m_volumeMeter->SetValue(0);
        // m_volumeLabel->SetLabel(wxT("0 dB")); // Or indicate stopped
    }
}

// 获取采样率
int AudioConfigPanel::GetSampleRate() const {
    wxString sampleRateStr = m_sampleRateChoice->GetStringSelection();
    long sampleRate;
    if (!sampleRateStr.ToLong(&sampleRate)) {
        return 16000; // 默认值
    }
    return static_cast<int>(sampleRate);
}

// 设置采样率
void AudioConfigPanel::SetSampleRate(int sampleRate) {
    wxString sampleRateStr = wxString::Format(wxT("%d"), sampleRate);
    int index = m_sampleRateChoice->FindString(sampleRateStr);
    if (index != wxNOT_FOUND) {
        m_sampleRateChoice->SetSelection(index);
    } else {
        m_sampleRateChoice->SetSelection(1); // 默认选择16000
    }
}

// 获取静音阈值
double AudioConfigPanel::GetSilenceThreshold() const {
    return m_silenceThresholdSlider->GetValue() / 100.0;
}

// --- FunASRConfigPanel 实现 ---
FunASRConfigPanel::FunASRConfigPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY) {
    
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // 模式选择
    wxStaticBoxSizer* modeSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("识别模式"));
    
    wxBoxSizer* radioSizer = new wxBoxSizer(wxHORIZONTAL);
    m_localModeRadio = new wxRadioButton(modeSizer->GetStaticBox(), wxID_ANY, 
                                       wxT("本地模式"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    m_cloudModeRadio = new wxRadioButton(modeSizer->GetStaticBox(), wxID_ANY, 
                                       wxT("云端模式"), wxDefaultPosition, wxDefaultSize);
    
    radioSizer->Add(m_localModeRadio, 0, wxALL, 5);
    radioSizer->Add(m_cloudModeRadio, 0, wxALL, 5);
    modeSizer->Add(radioSizer, 0, wxALL, 5);
    
    // 服务器URL（仅云端模式）
    wxBoxSizer* urlSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* urlLabel = new wxStaticText(modeSizer->GetStaticBox(), wxID_ANY, wxT("服务器URL:"));
    m_serverUrlTextCtrl = new wxTextCtrl(modeSizer->GetStaticBox(), wxID_ANY, wxT("https://api.funasr.com/v1/recognize"));
    
    urlSizer->Add(urlLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    urlSizer->Add(m_serverUrlTextCtrl, 1, wxALL, 5);
    modeSizer->Add(urlSizer, 0, wxALL | wxEXPAND, 5);
    
    // 本地模型管理（仅本地模式）
    wxStaticBoxSizer* modelSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("本地模型管理"));
    
    m_modelStatusLabel = new wxStaticText(modelSizer->GetStaticBox(), wxID_ANY, 
                                        wxT("模型状态: 未检测"));
    modelSizer->Add(m_modelStatusLabel, 0, wxALL, 5);
    
    m_checkModelButton = new wxButton(modelSizer->GetStaticBox(), wxID_ANY, wxT("检查本地模型"));
    modelSizer->Add(m_checkModelButton, 0, wxALL | wxALIGN_CENTER, 5);
    
    m_downloadModelButton = new wxButton(modelSizer->GetStaticBox(), wxID_ANY, wxT("下载模型 (约500MB)"));
    modelSizer->Add(m_downloadModelButton, 0, wxALL | wxALIGN_CENTER, 5);
    
    m_downloadProgress = new wxGauge(modelSizer->GetStaticBox(), wxID_ANY, 100, 
                                   wxDefaultPosition, wxSize(-1, 15));
    modelSizer->Add(m_downloadProgress, 0, wxALL | wxEXPAND, 5);
    
    // VAD敏感度调节
    wxStaticBoxSizer* vadSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("语音活动检测 (VAD) 设置"));
    
    wxStaticText* vadLabel = new wxStaticText(vadSizer->GetStaticBox(), wxID_ANY, 
                                           wxT("VAD敏感度:"));
    vadSizer->Add(vadLabel, 0, wxALL, 5);
    
    m_vadSensitivitySlider = new wxSlider(vadSizer->GetStaticBox(), wxID_ANY, 
                                        50, 0, 100, wxDefaultPosition, wxDefaultSize, 
                                        wxSL_HORIZONTAL | wxSL_LABELS | wxSL_AUTOTICKS);
    vadSizer->Add(m_vadSensitivitySlider, 0, wxALL | wxEXPAND, 5);
    
    wxStaticText* vadHint = new wxStaticText(vadSizer->GetStaticBox(), wxID_ANY, 
                                           wxT("调高可减少误判断，但可能会漏掉部分语音；调低可避免漏掉语音，但可能会误判断"));
    vadHint->Wrap(400); // 自动换行
    vadSizer->Add(vadHint, 0, wxALL, 5);
    
    // 添加所有组件到主布局
    mainSizer->Add(modeSizer, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(modelSizer, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(vadSizer, 0, wxALL | wxEXPAND, 5);
    
    // 留出一些空间
    mainSizer->AddStretchSpacer();
    
    SetSizer(mainSizer);
    
    // 初始状态
    m_localModeRadio->SetValue(true); // 默认选择本地模式
    m_serverUrlTextCtrl->Enable(false); // 禁用服务器URL输入
    
    // 模拟进度条
    m_downloadProgress->SetValue(0);
}

double FunASRConfigPanel::GetVADSensitivity() const {
    return m_vadSensitivitySlider->GetValue() / 100.0;
}

void FunASRConfigPanel::OnModeChanged(wxCommandEvent& event) {
    bool isLocalMode = m_localModeRadio->GetValue();
    
    // 启用/禁用相应的控件
    m_serverUrlTextCtrl->Enable(!isLocalMode);
    m_downloadModelButton->Enable(isLocalMode);
    m_checkModelButton->Enable(isLocalMode);
}

void FunASRConfigPanel::OnDownloadModel(wxCommandEvent& event) {
    // 在实际应用中，这里应该启动一个真正的下载过程
    // 这里仅做模拟
    
    // 禁用按钮，避免重复点击
    m_downloadModelButton->Disable();
    
    wxMessageDialog confirmDlg(this, 
                              wxT("即将从官方服务器下载约500MB的模型文件。\n\n是否继续？"), 
                              wxT("下载确认"), 
                              wxYES_NO | wxICON_QUESTION);
    
    if (confirmDlg.ShowModal() == wxID_YES) {
        // 开始模拟下载
        m_modelStatusLabel->SetLabel(wxT("模型状态: 正在下载..."));
        
        // 使用静态变量来模拟下载进度
        static int downloadProgress = 0;
        downloadProgress = 0;
        
        // 在实际应用中应该使用线程或计时器
        for (int i = 0; i <= 100; i += 10) {
            downloadProgress = i;
            m_downloadProgress->SetValue(downloadProgress);
            m_modelStatusLabel->SetLabel(wxString::Format(wxT("模型状态: 正在下载... %d%%"), downloadProgress));
            wxMilliSleep(200); // 模拟耗时操作
            wxYield(); // 允许UI更新
        }
        
        // 下载完成
        m_modelStatusLabel->SetLabel(wxT("模型状态: 已安装 (版本: 2023.02)"));
        m_downloadModelButton->SetLabel(wxT("更新模型"));
        m_downloadModelButton->Enable();
    } else {
        // 用户取消，重新启用按钮
        m_downloadModelButton->Enable();
    }
}

void FunASRConfigPanel::OnCheckLocalModel(wxCommandEvent& event) {
    // 检查本地模型是否已安装
    // 在实际应用中，这里应该检查模型文件是否存在
    
    // 模拟检查过程
    m_modelStatusLabel->SetLabel(wxT("模型状态: 正在检查..."));
    
    // 使用wxBusyCursor显示繁忙光标
    wxBusyCursor wait;
    
    // 延迟一小段时间来模拟检查过程
    wxMilliSleep(1000);
    
    // 随机决定模型状态（在实际应用中应该是真正的检查）
    bool modelExists = (wxGetLocalTime() % 2) == 0;
    
    if (modelExists) {
        m_modelStatusLabel->SetLabel(wxT("模型状态: 已安装 (版本: 2023.01)"));
        m_downloadModelButton->SetLabel(wxT("更新模型"));
    } else {
        m_modelStatusLabel->SetLabel(wxT("模型状态: 未安装"));
        m_downloadModelButton->SetLabel(wxT("下载模型 (约500MB)"));
    }
}

// --- LLMConfigPanel 实现 ---
LLMConfigPanel::LLMConfigPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY), m_webRequest(), m_timeoutTimer(nullptr) {
    
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // API密钥设置
    wxStaticBoxSizer* apiKeySizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("API密钥设置"));
    
    wxStaticText* apiKeyLabel = new wxStaticText(apiKeySizer->GetStaticBox(), wxID_ANY, 
                                               wxT("API密钥:"));
    apiKeySizer->Add(apiKeyLabel, 0, wxALL, 5);
    
    m_apiKeyTextCtrl = new wxTextCtrl(apiKeySizer->GetStaticBox(), wxID_ANY, wxEmptyString, 
                                    wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    apiKeySizer->Add(m_apiKeyTextCtrl, 0, wxALL | wxEXPAND, 5);
    
    // 端点URL设置
    wxStaticText* endpointLabel = new wxStaticText(apiKeySizer->GetStaticBox(), wxID_ANY, 
                                                 wxT("端点URL:"));
    apiKeySizer->Add(endpointLabel, 0, wxALL, 5);
    
    m_endpointUrlTextCtrl = new wxTextCtrl(apiKeySizer->GetStaticBox(), wxID_ANY, 
                                         wxT("https://api.openai.com/v1/chat/completions"));
    apiKeySizer->Add(m_endpointUrlTextCtrl, 0, wxALL | wxEXPAND, 5);
    
    // 模型名称设置
    wxStaticText* modelNameLabel = new wxStaticText(apiKeySizer->GetStaticBox(), wxID_ANY, 
                                                  wxT("模型名称:"));
    apiKeySizer->Add(modelNameLabel, 0, wxALL, 5);
    
    m_modelNameTextCtrl = new wxTextCtrl(apiKeySizer->GetStaticBox(), wxID_ANY, 
                                       wxT("gpt-3.5-turbo"));
    apiKeySizer->Add(m_modelNameTextCtrl, 0, wxALL | wxEXPAND, 5);
    
    // 连接测试按钮
    m_testConnectionButton = new wxButton(apiKeySizer->GetStaticBox(), LLM_TEST_BUTTON_ID, 
                                        wxT("测试连接"));
    apiKeySizer->Add(m_testConnectionButton, 0, wxALL | wxALIGN_CENTER, 5);
    
    m_connectionStatus = new wxStaticText(apiKeySizer->GetStaticBox(), wxID_ANY, 
                                        wxT("连接状态: 未测试"));
    apiKeySizer->Add(m_connectionStatus, 0, wxALL, 5);
    
    // 模型参数设置
    wxStaticBoxSizer* paramSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("模型参数"));
    
    // 温度设置
    wxStaticText* tempLabel = new wxStaticText(paramSizer->GetStaticBox(), wxID_ANY, 
                                            wxT("温度 (Temperature):"));
    paramSizer->Add(tempLabel, 0, wxALL, 5);
    
    m_temperatureSlider = new wxSlider(paramSizer->GetStaticBox(), wxID_ANY, 
                                     7, 0, 20, wxDefaultPosition, wxDefaultSize,
                                     wxSL_HORIZONTAL | wxSL_LABELS);
    paramSizer->Add(m_temperatureSlider, 0, wxALL | wxEXPAND, 5);
    
    wxStaticText* tempHint = new wxStaticText(paramSizer->GetStaticBox(), wxID_ANY, 
                                           wxT("较低的温度使响应更加确定和一致，较高的温度使响应更加多样化和创造性. (0.0-2.0)"));
    tempHint->Wrap(400); // 自动换行
    paramSizer->Add(tempHint, 0, wxALL, 5);
    
    // 最大Token数设置
    wxBoxSizer* tokenSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* tokenLabel = new wxStaticText(paramSizer->GetStaticBox(), wxID_ANY, 
                                             wxT("最大Token数:"));
    m_maxTokensSpinCtrl = new wxSpinCtrl(paramSizer->GetStaticBox(), wxID_ANY, wxT("2048"),
                                       wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 
                                       1, 16000, 2048);
    
    tokenSizer->Add(tokenLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    tokenSizer->Add(m_maxTokensSpinCtrl, 1, wxALL, 5);
    paramSizer->Add(tokenSizer, 0, wxALL | wxEXPAND, 5);
    
    // 提示词模板
    wxStaticBoxSizer* promptSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("提示词模板 (测试时不使用)"));
    
    m_promptTemplateTextCtrl = new wxTextCtrl(promptSizer->GetStaticBox(), wxID_ANY, 
                                           wxT("你是MeetAnt会议助手，请根据会议记录帮助我总结以下要点：\\n1. 主要议题\\n2. 关键决策\\n3. 行动项目和负责人\\n4. 跟进事项和截止日期"), 
                                           wxDefaultPosition, wxSize(-1, 100), 
                                           wxTE_MULTILINE | wxTE_READONLY);
    promptSizer->Add(m_promptTemplateTextCtrl, 1, wxALL | wxEXPAND, 5);
    
    // 添加所有组件到主布局
    mainSizer->Add(apiKeySizer, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(paramSizer, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(promptSizer, 1, wxALL | wxEXPAND, 5);
    
    SetSizer(mainSizer);
    
    // 添加到事件表的替代方案：手动绑定WebRequest事件
    this->Bind(wxEVT_WEBREQUEST_STATE, &LLMConfigPanel::OnWebRequestStateChanged, this);
    this->Bind(wxEVT_WEBREQUEST_DATA, &LLMConfigPanel::OnWebRequestDataReceived, this);
}

double LLMConfigPanel::GetTemperature() const {
    return m_temperatureSlider->GetValue() / 10.0;
}

void LLMConfigPanel::OnTestConnection(wxCommandEvent& event) {
    wxString url = m_endpointUrlTextCtrl->GetValue();
    wxString apiKey = m_apiKeyTextCtrl->GetValue();
    wxString modelName = m_modelNameTextCtrl->GetValue();
    
    // 检查URL和API密钥是否有效
    if (url.IsEmpty() || !wxURI(url).HasScheme()) {
        m_connectionStatus->SetLabel(wxT("请输入有效的URL"));
        m_connectionStatus->SetForegroundColour(*wxRED);
        m_connectionStatus->Refresh();
        return;
    }
    
    if (apiKey.IsEmpty()) {
        m_connectionStatus->SetLabel(wxT("请输入API密钥"));
        m_connectionStatus->SetForegroundColour(*wxRED);
        m_connectionStatus->Refresh();
        return;
    }
    
    if (modelName.IsEmpty()) {
        m_connectionStatus->SetLabel(wxT("请输入模型名称"));
        m_connectionStatus->SetForegroundColour(*wxRED);
        m_connectionStatus->Refresh();
        return;
    }
    
    // 禁用测试按钮，直到请求完成
    m_testConnectionButton->Disable();
    
    // 更新连接状态
    m_connectionStatus->SetLabel(wxT("正在连接服务器..."));
    m_connectionStatus->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
    m_connectionStatus->Refresh();
    
    // 清除上一次测试的响应
    m_lastTestResponse.Clear();
    
    // 创建测试请求
    wxWebSession& session = wxWebSession::GetDefault();
    m_webRequest = session.CreateRequest(this, url);
    
    if (!m_webRequest.IsOk()) {
        m_connectionStatus->SetLabel(wxT("无法创建网络请求"));
        m_connectionStatus->SetForegroundColour(*wxRED);
        m_testConnectionButton->Enable();
        m_connectionStatus->Refresh();
        return;
    }
    
    m_webRequest.SetMethod(wxT("POST"));
    // 设置请求头
    m_webRequest.SetHeader(wxT("Content-Type"), wxT("application/json"));
    m_webRequest.SetHeader(wxT("Authorization"), wxT("Bearer ") + apiKey);
    
    // 准备简单的测试请求数据，使用用户输入的模型名称
    wxString jsonData = wxString::Format(wxT("{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"测试连接\"}]}"), modelName);
    
    // 设置请求数据
    m_webRequest.SetData(jsonData, wxT("application/json"));
    
    // 启动请求
    m_webRequest.Start();
    
    // 设置超时定时器（10秒）
    wxTimer* timer = new wxTimer(this);
    this->Connect(timer->GetId(), wxEVT_TIMER, wxTimerEventHandler(LLMConfigPanel::OnRequestTimeout));
    timer->StartOnce(10000); // 10秒超时
    
    // 保存计时器指针以便后续停止
    m_timeoutTimer = timer;
}

void LLMConfigPanel::OnWebRequestStateChanged(wxWebRequestEvent& event) {
    wxWebRequest request = event.GetRequest();
    wxWebRequest::State state = request.GetState();
    
    if (state == wxWebRequest::State_Completed) {
        // 请求完成，检查状态码
        wxWebResponse response = request.GetResponse();
        long statusCode = response.GetStatus();
        
        // 重新启用测试按钮
        m_testConnectionButton->Enable();
        
        if (statusCode >= 200 && statusCode < 300) {
            // 连接成功
            wxString responseText = response.AsString();
            
            // 请求成功，停止计时器
            StopTimeoutTimer();
            
            // 更新UI显示连接成功
            m_connectionStatus->SetLabel(wxT("连接成功"));
            m_connectionStatus->SetForegroundColour(wxColour(0, 128, 0)); // 绿色
            m_connectionStatus->GetParent()->Layout();
            m_connectionStatus->Refresh();
        } else {
            // 连接失败
            StopTimeoutTimer();
            
            // 更新UI显示连接失败
            wxString statusMsg = wxString::Format(wxT("连接失败: HTTP状态码 %ld"), (long)statusCode);
            m_connectionStatus->SetLabel(statusMsg);
            m_connectionStatus->SetForegroundColour(wxColour(255, 0, 0)); // 红色
            m_connectionStatus->GetParent()->Layout();
            m_connectionStatus->Refresh();
        }
    } else if (state == wxWebRequest::State_Failed || state == wxWebRequest::State_Cancelled) {
        // 请求失败
        StopTimeoutTimer();
        
        // 重新启用测试按钮
        m_testConnectionButton->Enable();
        
        // 更新UI显示连接失败
        wxString errorMsg = event.GetErrorDescription();
        m_connectionStatus->SetLabel(wxT("连接失败: ") + errorMsg);
        m_connectionStatus->SetForegroundColour(wxColour(255, 0, 0)); // 红色
        m_connectionStatus->GetParent()->Layout();
        m_connectionStatus->Refresh();
    } else if (state == wxWebRequest::State_Unauthorized) {
        // 未授权
        StopTimeoutTimer();
        
        // 重新启用测试按钮
        m_testConnectionButton->Enable();
        
        // 更新UI显示授权失败
        m_connectionStatus->SetLabel(wxT("连接失败: API密钥无效"));
        m_connectionStatus->SetForegroundColour(wxColour(255, 0, 0)); // 红色
        m_connectionStatus->GetParent()->Layout();
        m_connectionStatus->Refresh();
    } else if (state == wxWebRequest::State_Active) {
        // 请求活动中
        m_connectionStatus->SetLabel(wxT("正在连接服务器..."));
        m_connectionStatus->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
        m_connectionStatus->GetParent()->Layout();
        m_connectionStatus->Refresh();
    }
}

void LLMConfigPanel::OnWebRequestDataReceived(wxWebRequestEvent& event) {
    // 处理接收到的数据
    size_t dataSize = event.GetDataSize();
    
    if (dataSize > 0) {
        // 使用临时缓冲区读取数据
        std::vector<char> buffer(dataSize);
        m_webRequest.GetResponse().GetStream()->Read(buffer.data(), dataSize);
        
        // 在这里可以处理接收到的数据块
        // 此示例只是简单地记录接收到数据的大小
        wxLogDebug("接收到数据块: %zu 字节", dataSize);
    }
}

void LLMConfigPanel::OnRequestTimeout(wxTimerEvent& event) {
    // 请求超时，如果计时器还存在则取消请求
    if (m_webRequest.IsOk()) {
        // 如果请求正在进行中，则取消
        if (m_webRequest.GetState() == wxWebRequest::State_Active) {
            m_webRequest.Cancel();
            
            // 重新启用测试按钮
            m_testConnectionButton->Enable();
            
            // 更新UI显示连接超时
            m_connectionStatus->SetLabel(wxT("连接超时"));
            m_connectionStatus->SetForegroundColour(wxColour(255, 0, 0)); // 红色
            m_connectionStatus->GetParent()->Layout();
            m_connectionStatus->Refresh();
        }
    }
    
    // 清理计时器资源
    StopTimeoutTimer();
}

void LLMConfigPanel::StopTimeoutTimer() {
    if (m_timeoutTimer) {
        if (m_timeoutTimer->IsRunning()) {
            m_timeoutTimer->Stop();
        }
        
        // 断开事件连接
        this->Disconnect(m_timeoutTimer->GetId(), wxEVT_TIMER, 
                        wxTimerEventHandler(LLMConfigPanel::OnRequestTimeout));
        
        // 删除计时器对象
        delete m_timeoutTimer;
        m_timeoutTimer = nullptr;
    }
}

// 在析构函数中也应该停止计时器
LLMConfigPanel::~LLMConfigPanel() {
    StopTimeoutTimer();
}

// --- SystemConfigPanel 实现 ---
SystemConfigPanel::SystemConfigPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY) {
    
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // 自动更新设置
    wxStaticBoxSizer* updateSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("更新设置"));
    
    m_autoUpdateCheckBox = new wxCheckBox(updateSizer->GetStaticBox(), wxID_ANY, 
                                       wxT("启用自动更新检查"));
    updateSizer->Add(m_autoUpdateCheckBox, 0, wxALL, 5);
    
    m_versionLabel = new wxStaticText(updateSizer->GetStaticBox(), wxID_ANY, 
                                    wxT("当前版本: MeetAnt v1.0.0"));
    updateSizer->Add(m_versionLabel, 0, wxALL, 5);
    
    // 自动保存设置
    wxStaticBoxSizer* saveSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("自动保存设置"));
    
    m_autoSaveCheckBox = new wxCheckBox(saveSizer->GetStaticBox(), wxID_ANY, 
                                     wxT("启用自动保存"));
    saveSizer->Add(m_autoSaveCheckBox, 0, wxALL, 5);
    
    wxBoxSizer* intervalSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* intervalLabel = new wxStaticText(saveSizer->GetStaticBox(), wxID_ANY, 
                                                wxT("自动保存间隔 (分钟):"));
    m_autoSaveIntervalSpinCtrl = new wxSpinCtrl(saveSizer->GetStaticBox(), wxID_ANY, 
                                              wxT("5"), wxDefaultPosition, wxDefaultSize, 
                                              wxSP_ARROW_KEYS, 1, 60, 5);
    
    intervalSizer->Add(intervalLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
    intervalSizer->Add(m_autoSaveIntervalSpinCtrl, 0, wxALL, 5);
    saveSizer->Add(intervalSizer, 0, wxALL, 5);
    
    // 诊断工具
    wxStaticBoxSizer* diagSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("诊断工具"));
    
    m_runDiagnosticsButton = new wxButton(diagSizer->GetStaticBox(), wxID_ANY, 
                                        wxT("运行系统诊断"));
    diagSizer->Add(m_runDiagnosticsButton, 0, wxALL | wxALIGN_CENTER, 5);
    
    wxStaticText* diagHint = new wxStaticText(diagSizer->GetStaticBox(), wxID_ANY, 
                                           wxT("诊断项目包括: 网络连接、音频设备、模型完整性"));
    diagSizer->Add(diagHint, 0, wxALL, 5);
    
    // 帮助信息
    wxStaticBoxSizer* helpSizer = new wxStaticBoxSizer(wxVERTICAL, this, wxT("帮助信息"));
    
    m_helpLink = new wxHyperlinkCtrl(helpSizer->GetStaticBox(), wxID_ANY, 
                                   wxT("访问MeetAnt帮助中心"), wxT("https://meetant.example.com/help"));
    helpSizer->Add(m_helpLink, 0, wxALL, 5);
    
    // 添加所有组件到主布局
    mainSizer->Add(updateSizer, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(saveSizer, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(diagSizer, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(helpSizer, 0, wxALL | wxEXPAND, 5);
    
    // 留出一些空间
    mainSizer->AddStretchSpacer();
    
    SetSizer(mainSizer);
    
    // 初始状态
    m_autoUpdateCheckBox->SetValue(true);
    m_autoSaveCheckBox->SetValue(true);
}

void SystemConfigPanel::OnRunDiagnostics(wxCommandEvent& event) {
    // 使用作用域控制wxBusyInfo的生命周期
    {
        // 模拟诊断过程
        wxBusyInfo wait(wxT("正在运行系统诊断...\n\n请稍候..."), this);
        
        // 延迟一小段时间来模拟诊断过程
        wxMilliSleep(2000);
        
        // 这里wait对象会在作用域结束时自动销毁
    }
    
    // 模拟诊断结果
    wxString resultMessage = wxT("诊断完成！\n\n");
    resultMessage += wxT("- 网络连接: 正常 ✓\n");
    resultMessage += wxT("- 音频设备: 正常 ✓\n");
    resultMessage += wxT("- 默认麦克风: 已检测\n");
    resultMessage += wxT("- FunASR模型: 已安装 (版本: 2023.01)\n");
    resultMessage += wxT("- 存储空间: 充足 (剩余45.3GB)\n");
    
    wxMessageBox(resultMessage, wxT("诊断结果"), wxICON_INFORMATION);
}

// --- ConfigDialog 实现 ---
ConfigDialog::ConfigDialog(wxWindow* parent, wxWindowID id, const wxString& title,
                         const wxPoint& pos, const wxSize& size, long style)
    : wxDialog(parent, id, title, pos, size, style) {
    
    // 设置对话框的最小尺寸
    SetMinSize(wxSize(550, 550));
    
    // 创建主布局
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // 创建笔记本控件
    m_notebook = new wxNotebook(this, wxID_ANY);
    
    // 创建各个设置页面
    m_audioPanel = new AudioConfigPanel(m_notebook);
    m_funASRPanel = new FunASRConfigPanel(m_notebook);
    m_llmPanel = new LLMConfigPanel(m_notebook);
    m_systemPanel = new SystemConfigPanel(m_notebook);
    
    // 添加页面到笔记本
    m_notebook->AddPage(m_audioPanel, wxT("音频设置"));
    m_notebook->AddPage(m_funASRPanel, wxT("语音识别"));
    m_notebook->AddPage(m_llmPanel, wxT("大模型设置"));
    m_notebook->AddPage(m_systemPanel, wxT("系统设置"));
    
    // 添加笔记本到主布局
    mainSizer->Add(m_notebook, 1, wxEXPAND | wxALL, 10);
    
    // 添加分隔线
    mainSizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    
    // 创建按钮行
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    
    m_applyButton = new wxButton(this, wxID_APPLY, wxT("应用"));
    m_okButton = new wxButton(this, wxID_OK, wxT("确定"));
    m_cancelButton = new wxButton(this, wxID_CANCEL, wxT("取消"));
    
    buttonSizer->Add(m_applyButton, 0, wxALL, 5);
    buttonSizer->AddStretchSpacer();
    buttonSizer->Add(m_cancelButton, 0, wxALL, 5);
    buttonSizer->Add(m_okButton, 0, wxALL, 5);
    
    // 添加按钮行到主布局
    mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 10);
    
    // 设置布局
    SetSizer(mainSizer);
    
    // 调整对话框大小以适应内容
    Fit();
    
    // 居中显示
    Centre();
    
    // 加载配置
    LoadConfigFromJSON();
}

void ConfigDialog::ApplyConfig() {
    // 保存配置到JSON文件
    if (SaveConfigToJSON()) {
        wxMessageBox(wxT("配置已应用并保存！"), wxT("成功"), wxICON_INFORMATION);
    } else {
        wxMessageBox(wxT("配置已应用但无法保存到文件！"), wxT("警告"), wxICON_WARNING);
    }
}

// 获取配置文件路径
wxString ConfigDialog::GetConfigFilePath() const {
    wxString configPath;
    
#ifdef __WXMSW__
    // Windows 平台：使用 %USERPROFILE%\MeetAntConfig
    configPath = wxGetHomeDir() + wxT("\\MeetAntConfig");
#else
    // Linux/macOS 平台：使用 ~/.MeetAntConfig
    configPath = wxGetHomeDir() + wxT("/.MeetAntConfig");
#endif
    
    // 确保目录存在
    if (!wxDirExists(configPath)) {
        wxMkdir(configPath);
    }
    
    return configPath + wxFileName::GetPathSeparator() + wxT("config.json");
}

// 保存配置到JSON文件
bool ConfigDialog::SaveConfigToJSON() {
    try {
        // 创建JSON对象
        json config;
        
        // 保存音频设置
        config["audio"] = {
            {"device", m_audioPanel->GetSelectedDevice()},
            {"sampleRate", m_audioPanel->GetSampleRate()},
            {"silenceThreshold", m_audioPanel->GetSilenceThreshold()},
            {"systemAudioEnabled", m_audioPanel->IsSystemAudioEnabled()},
            // {"captureType", m_audioPanel->GetCaptureType()}
        };
        
        // 保存语音识别设置
        config["funASR"] = {
            {"localMode", m_funASRPanel->IsLocalMode()},
            {"serverURL", m_funASRPanel->GetServerURL().ToStdString()},
            {"vadSensitivity", m_funASRPanel->GetVADSensitivity()}
        };
        
        // 保存大模型设置
        config["llm"] = {
            {"apiKey", m_llmPanel->GetAPIKey().ToStdString()},
            {"endpointURL", m_llmPanel->GetEndpointURL().ToStdString()},
            {"modelName", m_llmPanel->GetModelName().ToStdString()},
            {"temperature", m_llmPanel->GetTemperature()},
            {"maxTokens", m_llmPanel->GetMaxTokens()}
        };
        
        // 保存系统设置
        config["system"] = {
            {"autoUpdate", m_systemPanel->IsAutoUpdateEnabled()},
            {"autoSave", m_systemPanel->IsAutoSaveEnabled()},
            {"autoSaveInterval", m_systemPanel->GetAutoSaveInterval()}
        };
        
        // 获取配置文件路径
        wxString configFilePath = GetConfigFilePath();
        
        // 使用wxFile替代std::ofstream
        wxFile file;
        if (!file.Open(configFilePath, wxFile::write)) {
            wxLogError(wxT("无法打开配置文件进行写入: %s"), configFilePath);
            return false;
        }
        
        // 将JSON数据转换为字符串
        std::string jsonData = config.dump(4); // 美化输出，缩进4个空格
        
        // 写入文件
        bool success = file.Write(jsonData.c_str(), jsonData.size());
        file.Close();
        
        if (!success) {
            wxLogError(wxT("写入配置文件失败: %s"), configFilePath);
            return false;
        }
        
        wxLogInfo(wxT("配置已保存到: %s"), configFilePath);
        return true;
    } 
    catch (const std::exception& e) {
        wxLogError(wxT("保存配置时发生错误: %s"), wxString(e.what()));
        return false;
    }
}

// 从JSON文件加载配置
bool ConfigDialog::LoadConfigFromJSON() {
    wxString configFilePath = GetConfigFilePath();
    
    // 检查配置文件是否存在
    if (!wxFileExists(configFilePath)) {
        wxLogInfo(wxT("配置文件不存在，将使用默认设置: %s"), configFilePath);
        return false;
    }
    
    try {
        // 使用wxFile替代std::ifstream
        wxFile file;
        if (!file.Open(configFilePath, wxFile::read)) {
            wxLogError(wxT("无法打开配置文件进行读取: %s"), configFilePath);
            return false;
        }
        
        // 读取整个文件内容
        wxFileOffset fileSize = file.Length();
        if (fileSize <= 0) {
            wxLogError(wxT("配置文件为空: %s"), configFilePath);
            file.Close();
            return false;
        }
        
        // 分配缓冲区
        char* buffer = new char[fileSize + 1];
        if (!buffer) {
            wxLogError(wxT("无法分配内存读取配置文件: %s"), configFilePath);
            file.Close();
            return false;
        }
        
        // 读取文件内容
        file.Read(buffer, fileSize);
        buffer[fileSize] = '\0'; // 确保字符串正确终止
        file.Close();
        
        // 解析JSON
        std::string jsonStr(buffer);
        delete[] buffer;
        
        json config = json::parse(jsonStr);
        
        try {
            // 加载音频设置
            if (config.contains("audio")) {
                auto& audio = config["audio"];
                
                if (audio.contains("device"))
                    m_audioPanel->SetSelectedDevice(audio["device"].get<int>());
                    
                if (audio.contains("sampleRate"))
                    m_audioPanel->SetSampleRate(audio["sampleRate"].get<int>());
                    
                if (audio.contains("silenceThreshold"))
                    m_audioPanel->SetSilenceThreshold(audio["silenceThreshold"].get<double>());
                    
                if (audio.contains("systemAudioEnabled"))
                    m_audioPanel->SetSystemAudioEnabled(audio["systemAudioEnabled"].get<bool>());

                // if (audio.contains("captureType"))
                //     m_audioPanel->SetCaptureType(audio["captureType"].get<int>());
            }
        } catch (const std::exception& e) {
            wxLogWarning(wxT("加载音频设置时发生错误: %s"), wxString(e.what()));
            // 继续加载其他设置，不中断
        }
        
        try {
            // 加载语音识别设置
            if (config.contains("funASR")) {
                auto& funASR = config["funASR"];
                
                if (funASR.contains("localMode"))
                    m_funASRPanel->SetLocalMode(funASR["localMode"].get<bool>());
                    
                if (funASR.contains("serverURL"))
                    m_funASRPanel->SetServerURL(wxString::FromUTF8(funASR["serverURL"].get<std::string>()));
                    
                if (funASR.contains("vadSensitivity"))
                    m_funASRPanel->SetVADSensitivity(funASR["vadSensitivity"].get<double>());
            }
        } catch (const std::exception& e) {
            wxLogWarning(wxT("加载语音识别设置时发生错误: %s"), wxString(e.what()));
            // 继续加载其他设置，不中断
        }
        
        try {
            // 加载大模型设置
            if (config.contains("llm")) {
                auto& llm = config["llm"];
                
                if (llm.contains("apiKey"))
                    m_llmPanel->SetAPIKey(wxString::FromUTF8(llm["apiKey"].get<std::string>()));
                
                if (llm.contains("endpointURL"))
                    m_llmPanel->SetEndpointURL(wxString::FromUTF8(llm["endpointURL"].get<std::string>()));
                
                if (llm.contains("modelName"))
                    m_llmPanel->SetModelName(wxString::FromUTF8(llm["modelName"].get<std::string>()));
                
                if (llm.contains("temperature"))
                    m_llmPanel->SetTemperature(llm["temperature"].get<double>());
                
                if (llm.contains("maxTokens"))
                    m_llmPanel->SetMaxTokens(llm["maxTokens"].get<int>());
            }
        } catch (const std::exception& e) {
            wxLogWarning(wxT("加载大模型设置时发生错误: %s"), wxString(e.what()));
            // 继续加载其他设置，不中断
        }
        
        try {
            // 加载系统设置
            if (config.contains("system")) {
                auto& system = config["system"];
                
                if (system.contains("autoUpdate"))
                    m_systemPanel->SetAutoUpdateEnabled(system["autoUpdate"].get<bool>());
                    
                if (system.contains("autoSave"))
                    m_systemPanel->SetAutoSaveEnabled(system["autoSave"].get<bool>());
                    
                if (system.contains("autoSaveInterval"))
                    m_systemPanel->SetAutoSaveInterval(system["autoSaveInterval"].get<int>());
            }
        } catch (const std::exception& e) {
            wxLogWarning(wxT("加载系统设置时发生错误: %s"), wxString(e.what()));
            // 继续加载其他设置，不中断
        }
        
        wxLogInfo(wxT("配置已从文件加载: %s"), configFilePath);
        return true;
    }
    catch (const json::parse_error& e) {
        wxLogError(wxT("配置文件格式错误: %s - %s"), configFilePath, wxString(e.what()));
        // 如果配置文件格式错误，尝试备份并创建新的配置文件
        wxString backupPath = configFilePath + wxT(".bak");
        wxCopyFile(configFilePath, backupPath, true);
        wxLogInfo(wxT("已将损坏的配置文件备份为: %s"), backupPath);
        // 创建新的空配置
        SaveConfigToJSON();
        return false;
    }
    catch (const std::exception& e) {
        wxLogError(wxT("加载配置时发生错误: %s"), wxString(e.what()));
        return false;
    }
    catch (...) {
        wxLogError(wxT("加载配置时发生未知错误"));
        return false;
    }
}

void ConfigDialog::OnOK(wxCommandEvent& event) {
    // 应用配置
    ApplyConfig();
    
    // 关闭对话框
    EndModal(wxID_OK);
}

void ConfigDialog::OnCancel(wxCommandEvent& event) {
    // 关闭对话框而不应用配置
    EndModal(wxID_CANCEL);
}

void ConfigDialog::OnApply(wxCommandEvent& event) {
    // 应用配置但不关闭对话框
    ApplyConfig();
} 