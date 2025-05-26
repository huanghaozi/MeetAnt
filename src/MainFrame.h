#ifndef MEETANT_MAINFRAME_H
#define MEETANT_MAINFRAME_H

#include <wx/wx.h>
#include <wx/splitter.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/taskbar.h>
#include <wx/artprov.h>
#include <wx/treectrl.h>
#include <wx/srchctrl.h>
#include <wx/panel.h>
#include <wx/statbmp.h>
#include <wx/toolbar.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/filedlg.h>
#include <wx/wfstream.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/popupwin.h>
#include <wx/notebook.h>
#include <wx/regex.h>
#include <wx/timer.h>
#include <wx/webrequest.h>
#include <memory>
#include <vector>
#include <map>
#include "Annotation.h"
#include "BookmarkDialog.h"
#include <portaudio.h>
#include <nlohmann/json.hpp>
#include "SSEClient.h"

#ifdef _WIN32
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#endif

// 向前声明我们的自定义任务栏图标类
class AppTaskBarIcon;

// 为任务栏菜单定义一些ID
enum {
    PU_RESTORE = wxID_HIGHEST + 1,
    PU_EXIT,
    PU_TOGGLE_RECORD,
    // 新增按钮ID
    ID_RecordButton,
    ID_AISendButton,
    // 导航ID
    ID_SessionTree,
    ID_SearchButton,
    ID_CreateSessionButton,
    ID_BookmarkTree,  // 新增：书签树控件ID
    // 工具栏ID
    ID_Toolbar_Highlight_Yellow,
    ID_Toolbar_Highlight_Green,
    ID_Toolbar_Highlight_Blue,
    ID_Toolbar_Highlight_Red,
    ID_Toolbar_Bookmark,
    ID_Toolbar_Note,  // 新增：添加批注按钮ID
    ID_Toolbar_Clear,
    ID_Toolbar_Toggle_Annotations, // 新增：切换批注显示按钮ID
    // 文件ID
    ID_Export_Text,
    ID_Export_JSON,
    ID_Export_Audio,
    ID_Export_Package,
    // 会话ID
    ID_LabelSpeaker,
    // 新增菜单ID
    ID_Menu_Settings,
    ID_Menu_New_Session,
    ID_Menu_Save_Session,
    ID_Menu_Remove_Session,
    ID_Menu_Add_Existing_Session,
    ID_Menu_Export,
    // 新增右键菜单ID
    ID_SessionTreeContext_Remove,
    // 新增导航栏标签页ID
    ID_NavNotebook,
    ID_NavPage_Sessions,
    ID_NavPage_Bookmarks,
    // 音频和识别相关ID
    ID_VolumeUpdate,
    ID_ASRResult,
    ID_AnnotationTree, // 新增: 批注树控件ID
    ID_TranscriptionTextCtrl // 新增: 转录文本控件ID，用于捕获滚动事件
};

// 定义会话数据结构
struct SessionItem {
    wxString name;
    wxString path;
    wxDateTime creationTime;
    bool isActive;
};

// 定义发言者数据结构
struct Speaker {
    wxString id;
    wxString name;
    wxColour color;
};

// 前向声明注释弹出窗口类
class NotePopup;

// 新增：用于批注树的数据结构
class AnnotationTreeItemData : public wxTreeItemData {
public:
    MeetAnt::TimeStamp timestamp;
    MeetAnt::AnnotationType type;
    MeetAnt::Annotation* annotationPtr; // Pointer to the actual annotation object (non-owning)

    AnnotationTreeItemData(MeetAnt::TimeStamp ts, MeetAnt::AnnotationType t, MeetAnt::Annotation* ptr)
        : timestamp(ts), type(t), annotationPtr(ptr) {}
};

// 音频格式枚举
enum class AudioFormat {
    WAV_PCM16,      // WAV 16位PCM (无损)
    WAV_PCM32,      // WAV 32位PCM (无损)
    MP3_128,        // MP3 128kbps
    MP3_192,        // MP3 192kbps
    MP3_320,        // MP3 320kbps
    OGG_VORBIS      // OGG Vorbis (开源有损压缩)
};

class MainFrame : public wxFrame {
public:
    MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    ~MainFrame();
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnRecordToggle(wxCommandEvent& event);
    void OnAISend(wxCommandEvent& event); // AI 发送按钮事件处理
    void OnShowConfigDialog(wxCommandEvent& event); // 显示配置对话框
    
    // 新增功能相关事件处理
    void OnSessionSelected(wxTreeEvent& event);
    void OnCreateSession(wxCommandEvent& event);
    void OnSearch(wxCommandEvent& event);
    void OnSearchCancel(wxCommandEvent& event); // 新增：搜索取消事件处理
    void OnHighlight(wxCommandEvent& event);
    void OnBookmark(wxCommandEvent& event);
    void OnAddNote(wxCommandEvent& event);  // 新增：添加批注
    void OnClearFormatting(wxCommandEvent& event);
    void OnLabelSpeaker(wxCommandEvent& event);
    void OnBookmarkSelected(wxTreeEvent& event);  // 新增：选择书签事件
    void OnAnnotationSelected(wxTreeEvent& event); // 新增: 批注树选择事件
    
    // 新增：会话树右键菜单
    void OnSessionTreeContextMenu(wxTreeEvent& event);
    
    // 文件操作
    void OnSaveSession(wxCommandEvent& event);
    void OnExport(wxCommandEvent& event);
    void OnExportText(wxCommandEvent& event);
    void OnExportJSON(wxCommandEvent& event);
    void OnExportAudio(wxCommandEvent& event);
    void OnExportPackage(wxCommandEvent& event);
    
    // 新增：从列表中移除会话
    void OnRemoveSession(wxCommandEvent& event);
    
    // 新增：添加现有会话
    void OnAddExistingSession(wxCommandEvent& event);
    
    // 系统托盘相关
    void ShowFrame();
    void ToggleRecordingFromTray();
    bool IsCurrentlyRecording() const { return m_isRecording; }
    void UpdateTaskBarIconState();

    // 新增：更新发言人列表
    void UpdateSpeakerList();

    // 音频输入处理
    bool InitializeAudioInput();
    void StartAudioCapture();
    void StopAudioCapture();
    void OnAudioDataReceived(const float* buffer, size_t bufferSize);
    void UpdateVolumeLevel(float level);

    // FunASR集成
    bool InitializeFunASR();
    void ProcessAudioChunk(const float* buffer, size_t bufferSize);
    void HandleRecognitionResult(const wxString& text, bool isFinal);

    void JumpToTimestamp(MeetAnt::TimeStamp timestamp);
    void UpdateBookmarksTree();
    void PopulateAnnotationTree(); // 新增: 填充批注树

    void ToggleAnnotationsDisplay(); // 新增：切换批注显示
    void SyncAnnotationScrollPosition(wxScrollWinEvent& event); // 新增：同步批注滚动位置
    void RefreshAnnotations(); // 新增：刷新批注显示

private:
    void CreateMenuBar();
    void CreateToolBar();
    void LoadSessions();
    void AddSessionToTree(const wxString& name, const wxString& path, const wxDateTime& creationTime, bool isActive = false);
    void CreateNewSession(const wxString& name);
    void SaveCurrentSession();
    wxString CreateSessionDirectory(const wxString& sessionName);
    wxString GetSessionsDirectory() const;
    
    // 新增：批注相关方法
    void InitAnnotationManager();
    void LoadBookmarks();
    void AddBookmarkToTree(MeetAnt::BookmarkAnnotation* bookmark);
    void CreateBookmark(const wxString& label, const wxString& description, MeetAnt::TimeStamp timestamp);
    void CreateNote(const wxString& title, const wxString& content, MeetAnt::TimeStamp timestamp);
    void ApplyHighlight(const wxColour& color);
    void ShowNotePopup(MeetAnt::NoteAnnotation* note);
    
    // 新增：刷新会话树
    void RefreshSessionTree();
    
    // UI 元素
    wxSplitterWindow* m_mainSplitter;       // 用于左右分割 (导航栏 vs 编辑器+批注栏+AI侧边栏)
    wxSplitterWindow* m_rightSplitter;      // 用于右侧分割 (编辑器+批注栏 vs AI侧边栏)
    wxSplitterWindow* m_editorAnnotationSplitter; // 用于分割 (编辑器 vs 批注栏)

    wxPanel* m_navPanel;            // 导航栏面板
    wxPanel* m_editorPanel;         // 主编辑器面板
    wxPanel* m_annotationPanel;     // 批注栏面板
    wxPanel* m_aiSidebarPanel;      // AI 侧边栏面板

    // 导航栏元素
    wxButton* m_recordButton;            // 录制按钮
    wxNotebook* m_navNotebook;           // 导航栏标签页
    wxTreeCtrl* m_sessionTree;           // 会话历史树
    wxTreeCtrl* m_bookmarkTree;          // 书签树
    wxSearchCtrl* m_searchCtrl;          // 搜索控件
    wxButton* m_createSessionButton;     // 创建会话按钮
    
    // 主编辑器元素
    wxRichTextCtrl* m_transcriptionTextCtrl; // 用于显示转录文本 (使用富文本控件)
    wxToolBar* m_editorToolbar;            // 编辑器工具栏
    
    // 批注元素 - 修改为新的版式
    wxRichTextCtrl* m_annotationTextCtrl;   // 新增：批注右侧面板富文本控件
    bool m_showAnnotations;                 // 新增：是否显示批注
    std::map<long, MeetAnt::Annotation*> m_textPositionAnnotations; // 新增：文本位置与批注的映射
    
    // AI 侧边栏
    wxTextCtrl* m_aiChatHistoryCtrl;     // 用于显示 AI 聊天记录
    wxTextCtrl* m_aiUserInputCtrl;       // 用于用户输入
    wxButton* m_aiSendButton;            // 发送按钮

    // 批注栏元素
    wxTreeCtrl* m_annotationTree;       // 用于显示批注列表

    // 录制状态
    bool m_isRecording;                  // 标记当前是否正在录制
    
    // 会话数据
    std::vector<SessionItem> m_sessions;  // 会话列表
    wxString m_currentSessionPath;        // 当前会话路径
    wxString m_currentSessionId;          // 当前会话ID
    std::vector<Speaker> m_speakers;      // 发言人列表
    
    // 批注数据
    std::unique_ptr<MeetAnt::AnnotationManager> m_annotationManager;
    std::vector<NotePopup*> m_activeNotePopups;  // 当前活动的批注弹窗
    
    // 系统托盘图标
    AppTaskBarIcon* m_taskBarIcon;

    // 音频输入相关
    void* m_audioStream;          // 在实际实现中，这应该是RtAudio或wxSound对象
    float m_silenceThreshold;     // 静音检测阈值
    int m_sampleRate;             // 采样率
    int m_audioDeviceIndex;       // 音频设备索引
    bool m_isAudioInitialized;    // 标记音频是否已初始化
    
    // 音频录制相关 - 新增
    PaStream* m_paStream;              // PortAudio流对象
    float* m_audioBuffer;              // 音频数据缓冲区
    static const int AUDIO_BUFFER_SIZE = 1024; // 缓冲区大小
    wxTimer* m_recordingTimer;         // 录制定时器
    bool m_systemAudioMode;            // 是否使用系统内录
    int m_captureType;                 // 捕获类型（WASAPI/WDMKS）
    
    // 音频保存相关 - 新增
    wxFile* m_audioFile;               // 音频文件对象
    wxString m_currentAudioFilePath;   // 当前音频文件路径
    std::vector<float> m_audioDataBuffer; // 音频数据缓冲区
    size_t m_totalAudioFrames;         // 总音频帧数
    wxTimer* m_audioSaveTimer;         // 音频保存定时器
    static const size_t AUDIO_SAVE_INTERVAL = 5000; // 每5秒保存一次音频数据
    static const size_t AUDIO_BUFFER_FRAMES = 48000 * 5; // 5秒的音频缓冲区（48kHz采样率）
    
    // MP3编码相关 - 新增
    AudioFormat m_audioFormat;         // 音频格式
    void* m_lameEncoder;               // LAME编码器句柄
    std::vector<unsigned char> m_mp3Buffer; // MP3编码缓冲区
    bool m_isMP3Format;                // 是否使用MP3格式
    int m_mp3Bitrate;                  // MP3比特率
    
    // 实际音频格式参数
    int m_actualSampleRate;            // 实际采样率
    int m_actualChannels;              // 实际声道数
    int m_actualBitsPerSample;         // 实际位深度
    
#ifdef _WIN32
    // Windows WASAPI 直接录制相关
    bool m_comInitialized;
    IMMDeviceEnumerator* m_pEnumerator;
    IMMDevice* m_pSelectedLoopbackDevice;
    IAudioClient* m_pAudioClient;
    IAudioCaptureClient* m_pCaptureClient;
    WAVEFORMATEX* m_pWaveFormat;
    bool m_bDirectWasapiLoopbackActive;
    
    // WASAPI 捕获线程相关
    class WasapiRecordingThread;
    WasapiRecordingThread* m_pRecordingThread;
    wxCriticalSection m_recordingDataCritSec;
    std::vector<float> m_latestAudioData;
#endif
    
    // 音频配置加载
    bool LoadAudioConfig();
    void InitializePortAudio();
    void ShutdownPortAudio();
    bool InitializePortAudioCapture(bool systemAudio);
    
    // 音频保存相关方法 - 新增
    bool InitializeAudioRecording();
    void StartAudioRecording();
    void StopAudioRecording();
    void SaveAudioData(const float* buffer, size_t bufferSize);
    void FlushAudioBuffer();
    void OnAudioSaveTimer(wxTimerEvent& event);
    wxString CreateAudioFilePath() const;
    bool WriteWAVHeader(wxFile& file, int sampleRate, int channels, int bitsPerSample);
    bool UpdateWAVHeader(wxFile& file, size_t totalFrames, int sampleRate, int channels, int bitsPerSample);
    
    // MP3编码相关方法 - 新增
    bool InitializeMP3Encoder();
    void ShutdownMP3Encoder();
    bool EncodeToMP3(const float* buffer, size_t bufferSize);
    bool FinalizeMP3Encoding();
    wxString GetAudioFileExtension() const;
    void LoadAudioFormatConfig();
    
#ifdef _WIN32
    bool InitializeDirectWASAPILoopback(int paOutputDeviceIndex, int requestedSampleRate);
    void ShutdownDirectWASAPILoopback();
    void CaptureWASAPIAudio();
    void ProcessWASAPIAudioData(const BYTE* pData, UINT32 numFrames, const WAVEFORMATEX* wfex);
#endif

    // FunASR相关
    bool m_isLocalMode;           // 是否使用本地模式
    wxString m_serverUrl;         // FunASR服务器URL
    void* m_asrHandle;            // FunASR句柄（在实际实现中应为具体类型）
    float m_vadSensitivity;       // VAD敏感度
    bool m_isFunASRInitialized;   // 标记FunASR是否已初始化
    
    // 搜索控件
    wxCheckBox* m_useRegexCheckBox;      // 使用正则表达式复选框
    wxComboBox* m_speakerFilterComboBox; // 发言人过滤下拉框

    // 新增：管理已移除会话的方法
    void SaveRemovedSessionsList();
    void LoadRemovedSessionsList();
    bool IsSessionRemoved(const wxString& sessionName) const;
    wxString GetRemovedSessionsFilePath() const;
    std::vector<wxString> m_removedSessions; // 已移除会话列表

    // AI配置结构
    struct AIConfig {
        wxString apiKey;
        wxString endpointURL;
        wxString modelName;
        double temperature;
        int maxTokens;
        wxString promptTemplate;
        
        // 默认构造函数
        AIConfig() : temperature(0.7), maxTokens(1000) {}
    };
    
    // AI对话相关成员变量
    AIConfig* m_aiConfig;
    wxWebRequest m_aiWebRequest;
    wxInputStream* m_aiRequest;
    wxOutputStream* m_aiResponse;
    wxString m_aiResponseText;
    wxString m_currentAIResponse;
    wxString m_accumulatedSSEData;
    wxString m_streamingBuffer;  // 新增：流式数据缓冲区
    bool m_isAIRequestActive;
    bool m_aiResponseStarted;
    wxTimer* m_aiTimeoutTimer;
    std::unique_ptr<SSEClient> m_sseClient;  // 添加 SSEClient 成员
    
    // AI对话相关方法
    bool LoadAIConfig();
    void SendAIMessage(const wxString& message);
    void SendAIRequest(const wxString& userInput);
    void ProcessSSEData(const wxString& data);
    void ProcessStreamingSSEData(const wxString& data);  // 新增：处理流式SSE数据
    void ProcessSingleSSELine(const wxString& line);     // 新增：处理单个SSE行
    void ProcessSSEMessage(const wxString& message);     // 新增：处理 SSE 消息
    void OnAIWebRequestStateChanged(wxWebRequestEvent& event);
    void OnAIWebRequestDataReceived(wxWebRequestEvent& event);
    void OnAIRequestTimeout(wxTimerEvent& event);
    void StopAITimeoutTimer();
    void AppendAIMessage(const wxString& sender, const wxString& message, const wxColour& color = *wxBLACK);
    
    // SSE 事件处理方法
    void OnSSEOpen(wxThreadEvent& event);
    void OnSSEMessage(wxThreadEvent& event);
    void OnSSEError(wxThreadEvent& event);
    void OnSSEClose(wxThreadEvent& event);

    // 声明事件表
    wxDECLARE_EVENT_TABLE();
};

#ifdef _WIN32
// WASAPI录制线程类声明
class MainFrame::WasapiRecordingThread : public wxThread {
public:
    WasapiRecordingThread(MainFrame* frame, 
                         IAudioClient* audioClient,
                         IAudioCaptureClient* captureClient,
                         WAVEFORMATEX* waveFormat);
    ~WasapiRecordingThread() override;

    void RequestStop() { m_bStopRequested = true; }

protected:
    ExitCode Entry() override;

private:
    MainFrame* m_frame;
    IAudioClient* m_pAudioClientRef;
    IAudioCaptureClient* m_pCaptureClientRef;
    WAVEFORMATEX* m_pWaveFormatRef;
    bool m_bStopRequested;
    
    void ProcessAudioPacket(const BYTE* pData, UINT32 numFramesAvailable, const WAVEFORMATEX* wfex);
};
#endif

// 自定义系统托盘图标类
class AppTaskBarIcon : public wxTaskBarIcon {
public:
    AppTaskBarIcon(MainFrame* frame);
    virtual wxMenu* CreatePopupMenu() wxOVERRIDE;
    void SetRecordingState(bool isRecording);

private:
    MainFrame* m_frame;
    void OnMenuRestore(wxCommandEvent& event);
    void OnMenuExit(wxCommandEvent& event);
    void OnMenuToggleRecord(wxCommandEvent& event);
    wxDECLARE_EVENT_TABLE();
};

// 批注弹出窗口类
class NotePopup : public wxPopupWindow {
public:
    NotePopup(wxWindow* parent, MeetAnt::NoteAnnotation* note);
    ~NotePopup();
    
    void SetPosition(const wxPoint& pos);
    void SetContent(const wxString& title, const wxString& content);
    MeetAnt::NoteAnnotation* GetNote() const { return m_note; }
    
private:
    void CreateControls();
    void OnClose(wxCommandEvent& event);
    void OnEdit(wxCommandEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);
    
    wxPanel* m_mainPanel;
    wxStaticText* m_titleText;
    wxStaticText* m_contentText;
    wxButton* m_closeButton;
    wxButton* m_editButton;
    
    MeetAnt::NoteAnnotation* m_note;
    wxPoint m_dragStartPos;
    bool m_isDragging;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // MEETANT_MAINFRAME_H 