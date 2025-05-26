#ifndef CONFIG_DIALOG_H
#define CONFIG_DIALOG_H

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/textctrl.h>
#include <wx/slider.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/statbmp.h>
#include <wx/combobox.h>
#include <wx/statline.h>
#include <wx/spinctrl.h>
#include <wx/hyperlink.h>
#include <memory>
#include <wx/webrequest.h>
#include <fstream>     // 添加文件流支持
#include <string>      // 添加字符串支持
#include <thread>
#include <atomic>
#include <portaudio.h>  // 添加PortAudio支持
#include <wx/thread.h>
#include <wx/event.h>

#ifdef _WIN32
// These Windows headers are needed for the WASAPI member variable declarations
#include <windows.h>
#include <mmdeviceapi.h>  // For IMMDeviceEnumerator, IMMDevice
#include <audioclient.h>  // For IAudioClient, IAudioCaptureClient, WAVEFORMATEX (often via other headers)
// WAVEFORMATEX might also be in mmsystem.h or other core audio headers if not pulled in by audioclient.h
#endif

#include <random> // For MeetAntApp random number generator

// Forward declare PortAudio types if not fully included in header
// struct PaStream; // This line caused redefinition error, portaudio.h is now fully included above.

// Define a unique ID for web request events for LLMConfigPanel
const int LLM_WEBREQUEST_ID = wxID_HIGHEST + 100; // Ensure this ID is unique
const int LLM_TEST_BUTTON_ID = wxID_HIGHEST + 1;

#ifdef _WIN32
// Forward declare Windows types used in the thread class if not already broadly included
// struct IAudioClient;
// struct IAudioCaptureClient;
// struct WAVEFORMATEX;
#endif

// 音频配置面板
class AudioConfigPanel : public wxPanel {
public:
    AudioConfigPanel(wxWindow* parent);
    ~AudioConfigPanel() override;
    
    // 获取配置数据
    int GetSelectedDevice() const { return m_deviceCombo->GetSelection(); }
    int GetSampleRate() const;
    double GetSilenceThreshold() const;
    bool IsSystemAudioEnabled() const { return m_systemAudioCheckBox->GetValue(); }
    // int GetCaptureType() const { return m_captureTypeChoice->GetSelection(); }
    
    // 设置配置数据
    void SetSelectedDevice(int device) { m_deviceCombo->SetSelection(device); }
    void SetSampleRate(int sampleRate);
    void SetSilenceThreshold(double threshold) { m_silenceThresholdSlider->SetValue(static_cast<int>(threshold * 100)); }
    void SetSystemAudioEnabled(bool enabled) { m_systemAudioCheckBox->SetValue(enabled); }
    // void SetCaptureType(int type) { m_captureTypeChoice->SetSelection(type); }
    
    // void PopulateLoopbackDevices();
    
    bool InitializeAudioCapture(bool systemAudioMode, int deviceIndex, int sampleRate);
    void ShutdownAudioCapture();
    float CaptureAndAnalyzeAudio();
    
#ifdef _WIN32
    void UpdateDirectWasapiDBValue(float db); // Called by capture thread via CallAfter
#endif

private:
    void OnTestAudio(wxCommandEvent& event);
    void OnTimer(wxTimerEvent& event);
    // void OnSystemAudioToggled(wxCommandEvent& event);
    
    void PopulateAudioDevices(); // 枚举系统音频设备
    
    wxComboBox* m_deviceCombo;        // 输入设备选择
    wxChoice* m_sampleRateChoice;     // 采样率选择
    wxSlider* m_silenceThresholdSlider; // 静音检测阈值调节
    wxButton* m_testAudioButton;      // 测试按钮
    wxGauge* m_volumeMeter;           // 音量计
    wxStaticText* m_volumeLabel;      // 音量显示标签
    wxCheckBox* m_systemAudioCheckBox; // 系统内录选项
    // wxChoice* m_captureTypeChoice;     // 捕获类型选择（WASAPI/WDMKS）
    
    // PortAudio相关成员
    PaStream* m_paStream;              // PortAudio流对象
    bool m_isAudioInitialized;         // 音频初始化状态
    float* m_audioBuffer;              // 音频数据缓冲区
    static const int BUFFER_SIZE = 1024; // 缓冲区大小
    
    wxTimer* m_testTimer;             // 测试用定时器
    
    // wxComboBox* m_loopbackDeviceCombo;
    // wxStaticText* m_loopbackDeviceLabel;
    
    bool m_portAudioActuallyUsed; 

#ifdef _WIN32
    // Members for direct WASAPI loopback
    bool m_comInitialized;
    IMMDeviceEnumerator* m_pEnumerator;
    IMMDevice* m_pSelectedLoopbackDevice;
    IAudioClient* m_pAudioClient;
    IAudioCaptureClient* m_pCaptureClient;
    WAVEFORMATEX* m_pWaveFormat;
    bool m_bDirectWasapiLoopbackActive;

    // Direct WASAPI methods
    bool InitializeDirectWASAPILoopback(int paOutputDeviceIndex, int requestedSampleRateFromUI);
    void ShutdownDirectWASAPILoopback();

    // WASAPI Capture Thread related
    class WasapiCaptureThread; // Forward declaration of the inner class or separate class
    WasapiCaptureThread* m_pCaptureThread = nullptr;
    wxCriticalSection m_captureDataCritSec; 
    float m_latestDirectWasapiDBValue = -60.0f; 
    wxEvtHandler* GetEventHandler() { return this; } // For CallAfter from thread
#endif

    DECLARE_EVENT_TABLE();
};

#ifdef _WIN32
// --- WasapiCaptureThread Declaration (can be an inner class or separate) ---
// For simplicity, defining it here. If it gets large, move to its own .h/.cpp
class AudioConfigPanel::WasapiCaptureThread : public wxThread {
public:
    WasapiCaptureThread(AudioConfigPanel* panel, 
                        IAudioClient* audioClient,
                        IAudioCaptureClient* captureClient,
                        WAVEFORMATEX* waveFormat);
    ~WasapiCaptureThread() override;

    void RequestStop() { m_bStopRequested = true; }

protected:
    ExitCode Entry() override;

private:
    AudioConfigPanel* m_panel; // Pointer to the parent panel to post events/data
    IAudioClient* m_pAudioClientRef; // Not owned by thread, just a reference
    IAudioCaptureClient* m_pCaptureClientRef; // Not owned by thread
    WAVEFORMATEX* m_pWaveFormatRef; // Not owned by thread
    bool m_bStopRequested;
    
    // Helper for processing audio data and calculating dB
    float ProcessAudioPacket(const BYTE* pData, UINT32 numFramesAvailable, const WAVEFORMATEX* wfex);
};
#endif

// FunASR配置面板
class FunASRConfigPanel : public wxPanel {
public:
    FunASRConfigPanel(wxWindow* parent);
    
    // 获取配置数据
    bool IsLocalMode() const { return m_localModeRadio->GetValue(); }
    wxString GetServerURL() const { return m_serverUrlTextCtrl->GetValue(); }
    double GetVADSensitivity() const;
    
    // 设置配置数据
    void SetLocalMode(bool localMode) { 
        m_localModeRadio->SetValue(localMode); 
        m_cloudModeRadio->SetValue(!localMode);
        m_serverUrlTextCtrl->Enable(!localMode);
    }
    void SetServerURL(const wxString& url) { m_serverUrlTextCtrl->SetValue(url); }
    void SetVADSensitivity(double sensitivity) { m_vadSensitivitySlider->SetValue(static_cast<int>(sensitivity * 100)); }
    
private:
    void OnModeChanged(wxCommandEvent& event);
    void OnDownloadModel(wxCommandEvent& event);
    void OnCheckLocalModel(wxCommandEvent& event);
    
    wxRadioButton* m_localModeRadio;   // 本地模式选择
    wxRadioButton* m_cloudModeRadio;   // 云端模式选择
    wxTextCtrl* m_serverUrlTextCtrl;   // 服务器URL输入
    wxSlider* m_vadSensitivitySlider;  // VAD敏感度调节
    wxButton* m_downloadModelButton;   // 模型下载按钮
    wxGauge* m_downloadProgress;       // 下载进度条
    wxStaticText* m_modelStatusLabel;  // 模型状态标签
    wxButton* m_checkModelButton;      // 检查模型按钮
    
    wxDECLARE_EVENT_TABLE();
};

// 大模型配置面板
class LLMConfigPanel : public wxPanel {
public:
    LLMConfigPanel(wxWindow* parent);
    ~LLMConfigPanel();
    
    // 获取配置数据
    wxString GetAPIKey() const { return m_apiKeyTextCtrl->GetValue(); }
    wxString GetEndpointURL() const { return m_endpointUrlTextCtrl->GetValue(); }
    double GetTemperature() const;
    int GetMaxTokens() const { return m_maxTokensSpinCtrl->GetValue(); }
    wxString GetModelName() const { return m_modelNameTextCtrl->GetValue(); }
    
    // 设置配置数据
    void SetAPIKey(const wxString& apiKey) { m_apiKeyTextCtrl->SetValue(apiKey); }
    void SetEndpointURL(const wxString& url) { m_endpointUrlTextCtrl->SetValue(url); }
    void SetTemperature(double temperature) { m_temperatureSlider->SetValue(static_cast<int>(temperature * 10)); }
    void SetMaxTokens(int tokens) { m_maxTokensSpinCtrl->SetValue(tokens); }
    void SetModelName(const wxString& modelName) { m_modelNameTextCtrl->SetValue(modelName); }
    
private:
    void OnTestConnection(wxCommandEvent& event);
    void OnWebRequestStateChanged(wxWebRequestEvent& event);
    void OnWebRequestDataReceived(wxWebRequestEvent& event);
    void OnRequestTimeout(wxTimerEvent& event);
    void StopTimeoutTimer();
    
    wxTextCtrl* m_apiKeyTextCtrl;       // API密钥输入
    wxTextCtrl* m_endpointUrlTextCtrl;  // 端点URL输入
    wxTextCtrl* m_modelNameTextCtrl;    // 模型名称输入
    wxSlider* m_temperatureSlider;      // 温度参数调节
    wxSpinCtrl* m_maxTokensSpinCtrl;    // 最大token数设置
    wxButton* m_testConnectionButton;   // 连接测试按钮
    wxStaticText* m_connectionStatus;   // 连接状态标签
    wxTextCtrl* m_promptTemplateTextCtrl; // 提示词模板

    wxWebRequest m_webRequest;          // Web request object
    wxString m_accumulatedResponse;     // To accumulate raw SSE data
    wxString m_currentResponseMessage;  // To accumulate message content from deltas
    wxString m_lastTestResponse;        // 保存最后一次测试的响应
    wxTimer* m_timeoutTimer;            // 超时计时器
    
    wxDECLARE_EVENT_TABLE();
};

// 系统配置面板
class SystemConfigPanel : public wxPanel {
public:
    SystemConfigPanel(wxWindow* parent);
    
    // 获取配置数据
    bool IsAutoUpdateEnabled() const { return m_autoUpdateCheckBox->GetValue(); }
    bool IsAutoSaveEnabled() const { return m_autoSaveCheckBox->GetValue(); }
    int GetAutoSaveInterval() const { return m_autoSaveIntervalSpinCtrl->GetValue(); }
    
    // 设置配置数据
    void SetAutoUpdateEnabled(bool enabled) { m_autoUpdateCheckBox->SetValue(enabled); }
    void SetAutoSaveEnabled(bool enabled) { m_autoSaveCheckBox->SetValue(enabled); }
    void SetAutoSaveInterval(int interval) { m_autoSaveIntervalSpinCtrl->SetValue(interval); }
    
private:
    void OnRunDiagnostics(wxCommandEvent& event);
    
    wxCheckBox* m_autoUpdateCheckBox;      // 自动更新选项
    wxCheckBox* m_autoSaveCheckBox;        // 自动保存选项
    wxSpinCtrl* m_autoSaveIntervalSpinCtrl;// 自动保存间隔设置
    wxButton* m_runDiagnosticsButton;      // 运行诊断按钮
    wxStaticText* m_versionLabel;          // 版本信息标签
    wxHyperlinkCtrl* m_helpLink;           // 帮助链接
    
    wxDECLARE_EVENT_TABLE();
};

// 主配置对话框
class ConfigDialog : public wxDialog {
public:
    ConfigDialog(wxWindow* parent, wxWindowID id, const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    
    // 应用配置更改
    void ApplyConfig();
    
private:
    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    void OnApply(wxCommandEvent& event);
    
    // 配置保存和加载方法
    bool SaveConfigToJSON();
    bool LoadConfigFromJSON();
    wxString GetConfigFilePath() const;
    
    wxNotebook* m_notebook;
    AudioConfigPanel* m_audioPanel;
    FunASRConfigPanel* m_funASRPanel;
    LLMConfigPanel* m_llmPanel;
    SystemConfigPanel* m_systemPanel;
    
    wxButton* m_okButton;
    wxButton* m_cancelButton;
    wxButton* m_applyButton;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // CONFIG_DIALOG_H 