#include "MainFrame.h"
#include "ConfigDialog.h"
#include "BookmarkDialog.h"
#include "NoteDialog.h"
#include "TranscriptionBubbleCtrl.h"  // 添加新控件头文件
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/colordlg.h>
#include <wx/artprov.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/regex.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>
#include <wx/webrequest.h>
#include <cmath>
#include <fstream>        // 添加std::ifstream支持
#include <algorithm>      // 添加标准算法支持
#include <vector>         // 添加std::vector支持
#include <cstring>        // 添加memcpy支持

#ifdef _WIN32
#include <comdef.h>
#include <functiondiscoverykeys_devpkey.h>  // 添加PKEY_Device_FriendlyName定义
#include <mmdeviceapi.h>
#include <audioclient.h>

// WASAPI回环模式标志 - 直接定义以避免链接依赖
#ifndef AUDCLNT_STREAMFLAGS_LOOPBACK
#define AUDCLNT_STREAMFLAGS_LOOPBACK 0x00020000
#endif

// 音频格式常量定义
#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif

#ifndef WAVE_FORMAT_EXTENSIBLE
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
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
#endif

// 实现获取已移除会话列表文件路径的方法
wxString MainFrame::GetRemovedSessionsFilePath() const {
    wxString sessionsDir = GetSessionsDirectory();
    if (sessionsDir.IsEmpty()) {
        return wxEmptyString;
    }
    return wxFileName(sessionsDir, wxT("removed_sessions.txt")).GetFullPath();
}

// 实现保存已移除会话列表的方法
void MainFrame::SaveRemovedSessionsList() {
    wxString filePath = GetRemovedSessionsFilePath();
    if (filePath.IsEmpty()) {
        wxLogError(wxT("无法获取已移除会话列表文件路径"));
        return;
    }
    
    // 尝试使用wxTextFile保存，这是更高级别的API
    wxTextFile textFile;
    bool useTextFile = false;
    
    try {
        if (wxFileExists(filePath)) {
            useTextFile = textFile.Open(filePath);
        } else {
            useTextFile = textFile.Create(filePath);
        }
        
        if (useTextFile) {
            // 清空现有内容
            textFile.Clear();
            
            // 添加所有移除会话
            for (const auto& sessionName : m_removedSessions) {
                textFile.AddLine(sessionName);
            }
            
            // 写入文件
            if (textFile.Write()) {
                wxLogInfo(wxT("已成功使用wxTextFile保存已移除会话列表，共 %d 个会话"), static_cast<int>(m_removedSessions.size()));
                textFile.Close();
                return;
            }
            
            textFile.Close();
            wxLogWarning(wxT("无法使用wxTextFile写入已移除会话列表文件: %s"), filePath);
        }
    } catch (...) {
        wxLogWarning(wxT("使用wxTextFile时发生异常"));
    }
    
    // 如果wxTextFile失败，回退到使用wxFile
    wxFile file;
    if (!file.Open(filePath, wxFile::write)) {
        wxLogError(wxT("无法打开已移除会话列表文件进行写入: %s"), filePath);
        return;
    }
    
    wxString content;
    for (const auto& sessionName : m_removedSessions) {
        content += sessionName + wxT("\n");
    }
    
    if (!file.Write(content)) {
        wxLogError(wxT("写入已移除会话列表文件失败: %s"), filePath);
    }
    
    file.Close();
}

// 实现加载已移除会话列表的方法
void MainFrame::LoadRemovedSessionsList() {
    // 清空已移除会话列表
    m_removedSessions.clear();
    
    wxString filePath = GetRemovedSessionsFilePath();
    if (filePath.IsEmpty()) {
        wxLogWarning(wxT("已移除会话列表文件路径为空"));
        return;
    }
    
    // 直接在调试日志中输出文件路径，以便确认
    wxLogDebug(wxT("尝试从文件加载已移除会话列表: %s"), filePath);
    
    // 检查文件是否存在
    bool fileExists = false;
    try {
        fileExists = wxFileExists(filePath);
    } catch (...) {
        wxLogWarning(wxT("检查已移除会话列表文件是否存在时发生异常"));
        return;
    }
    
    if (!fileExists) {
        wxLogInfo(wxT("已移除会话列表文件不存在，这可能是首次运行: %s"), filePath);
        // 尝试创建一个空文件
        wxFile createFile;
        if (createFile.Create(filePath, true)) {
            wxLogInfo(wxT("已创建空的已移除会话列表文件: %s"), filePath);
            createFile.Close();
        }
        return;
    }
    
    wxLogInfo(wxT("已移除会话列表文件存在: %s"), filePath);
    
    // 尝试使用wxTextFile读取文件，这是一个更高级别的API
    wxTextFile textFile;
    try {
        if (!textFile.Open(filePath)) {
            wxLogWarning(wxT("无法使用wxTextFile打开已移除会话列表文件: %s"), filePath);
            
            // 回退到使用wxFile和std::ifstream
            std::ifstream inFile(filePath.ToStdString());
            if (!inFile) {
                wxLogWarning(wxT("无法使用std::ifstream打开已移除会话列表文件: %s (错误: %s)"), 
                           filePath, wxSysErrorMsg());
                return;
            }
            
            // 逐行读取
            std::string line;
            int sessionCount = 0;
            while (std::getline(inFile, line)) {
                if (!line.empty()) {
                    wxString sessionName = wxString::FromUTF8(line.c_str());
                    sessionName.Trim();
                    if (!sessionName.IsEmpty()) {
                        m_removedSessions.push_back(sessionName);
                        sessionCount++;
                        wxLogDebug(wxT("添加已移除会话: %s"), sessionName);
                    }
                }
            }
            
            inFile.close();
            wxLogInfo(wxT("使用std::ifstream从文件加载了 %d 个已移除会话: %s"), sessionCount, filePath);
            return;
        }
    } catch (const std::exception& e) {
        wxLogWarning(wxT("打开文本文件时发生异常: %s - %s"), filePath, wxString(e.what()));
        return;
    } catch (...) {
        wxLogWarning(wxT("打开文本文件时发生未知异常: %s"), filePath);
        return;
    }
    
    // 如果wxTextFile成功打开
    int sessionCount = 0;
    if (textFile.GetLineCount() > 0) {
        for (size_t i = 0; i < textFile.GetLineCount(); i++) {
            wxString sessionName = textFile.GetLine(i).Trim();
            if (!sessionName.IsEmpty()) {
                m_removedSessions.push_back(sessionName);
                sessionCount++;
                wxLogDebug(wxT("添加已移除会话: %s"), sessionName);
            }
        }
    }
    
    textFile.Close();
    wxLogInfo(wxT("使用wxTextFile从文件加载了 %d 个已移除会话: %s"), sessionCount, filePath);
    
    // 如果老的方法没有读取到任何会话，尝试新的方法
    if (sessionCount == 0) {
        // 尝试读取文件内容
        wxFile file;
        bool fileOpened = false;
        try {
            fileOpened = file.Open(filePath, wxFile::read);
        } catch (...) {
            wxLogWarning(wxT("打开已移除会话列表文件时发生异常"));
            return;
        }
        
        if (!fileOpened) {
            wxLogWarning(wxT("无法打开已移除会话列表文件进行读取: %s (可能是权限问题, 错误: %s)"), 
                       filePath, wxSysErrorMsg());
            return;
        }
        
        // 读取整个文件内容
        wxString content;
        wxFileOffset length = file.Length();
        
        if (length <= 0) {
            wxLogInfo(wxT("已移除会话列表文件为空: %s"), filePath);
            file.Close();
            return;
        }
        
        bool readSuccess = false;
        try {
            // 使用更大的缓冲区确保有足够空间
            char* buffer = new char[length + 2];
            wxFileOffset bytesRead = file.Read(buffer, length);
            
            if (bytesRead != length) {
                wxLogWarning(wxT("读取已移除会话列表文件不完整: %s (读取了 %lld/%lld 字节, 错误: %s)"), 
                           filePath, bytesRead, length, wxSysErrorMsg());
            }
            
            buffer[bytesRead] = 0; // 确保字符串正确结束
            content = wxString::FromUTF8(buffer);
            delete[] buffer;
            readSuccess = true;
            
            // 记录实际读取的内容（用于调试）
            wxLogDebug(wxT("文件内容(HEX): "));
            for (size_t i = 0; i < (bytesRead > 100 ? 100 : bytesRead); i++) {
                wxLogDebug(wxT("%02X "), (unsigned char)buffer[i]);
            }
        } catch (const std::exception& e) {
            wxLogWarning(wxT("读取已移除会话列表文件时发生异常: %s"), wxString(e.what()));
        } catch (...) {
            wxLogWarning(wxT("读取已移除会话列表文件时发生未知异常"));
        }
        
        file.Close();
        
        if (!readSuccess || content.IsEmpty()) {
            wxLogWarning(wxT("无法读取已移除会话列表文件内容: %s (错误: %s)"), filePath, wxSysErrorMsg());
            return;
        }
        
        // 按行分割并添加到列表
        wxStringTokenizer tokenizer(content, wxT("\n\r")); // 同时处理Windows和Unix换行符
        
        while (tokenizer.HasMoreTokens()) {
            wxString sessionName = tokenizer.GetNextToken().Trim();
            if (!sessionName.IsEmpty()) {
                m_removedSessions.push_back(sessionName);
                sessionCount++;
                wxLogDebug(wxT("添加已移除会话: %s"), sessionName);
            }
        }
        
        wxLogInfo(wxT("从文件加载了 %d 个已移除会话: %s"), sessionCount, filePath);
    }
}

// 实现检查会话是否被移除的方法
bool MainFrame::IsSessionRemoved(const wxString& sessionName) const {
    // 添加调试日志，显示正在检查的会话名称
    wxLogDebug(wxT("检查会话是否被移除: %s"), sessionName);
    
    // 遍历已移除会话列表，进行更精确的比较（不区分大小写）
    for (const auto& removedSession : m_removedSessions) {
        if (sessionName.IsSameAs(removedSession, false)) { // false表示不区分大小写
            wxLogDebug(wxT("会话已被移除: %s 匹配 %s"), sessionName, removedSession);
            return true;
        }
    }
    
    // 如果使用标准算法查找
    bool isRemoved = std::find_if(m_removedSessions.begin(), m_removedSessions.end(),
        [&sessionName](const wxString& s) {
            return sessionName.IsSameAs(s, false); // 不区分大小写比较
        }) != m_removedSessions.end();
    
    if (isRemoved) {
        wxLogDebug(wxT("会话已被移除 (通过std::find): %s"), sessionName);
    } else {
        wxLogDebug(wxT("会话未被移除: %s"), sessionName);
    }
    
    return isRemoved;
}

// 获取会话数据目录的方法
wxString MainFrame::GetSessionsDirectory() const {
    // 获取应用数据目录
    wxString appDataDir;
    
#ifdef __WXMSW__
    // Windows 平台：使用 %USERPROFILE%\MeetAntRecords
    appDataDir = wxGetHomeDir() + wxT("\\MeetAntRecords");
#else
    // Linux/macOS 平台：使用 ~/MeetAntRecords
    appDataDir = wxGetHomeDir() + wxT("/MeetAntRecords");
#endif
    
    return appDataDir;
}

// 定义滚动事件类型常量
#ifndef wxEVT_SCROLLWIN
#define wxEVT_SCROLLWIN wxEVT_SCROLLWIN_THUMBTRACK
#endif

// --- AppTaskBarIcon 事件表 ---
wxBEGIN_EVENT_TABLE(AppTaskBarIcon, wxTaskBarIcon)
    EVT_MENU(PU_RESTORE, AppTaskBarIcon::OnMenuRestore)
    EVT_MENU(PU_EXIT,    AppTaskBarIcon::OnMenuExit)
    EVT_MENU(PU_TOGGLE_RECORD, AppTaskBarIcon::OnMenuToggleRecord)
    // 也可以处理 EVT_TASKBAR_LEFT_DCLICK 等事件
wxEND_EVENT_TABLE()

// --- AppTaskBarIcon 实现 ---
AppTaskBarIcon::AppTaskBarIcon(MainFrame* frame)
    : m_frame(frame) {
    // 初始图标和提示
    SetRecordingState(false);
}

wxMenu* AppTaskBarIcon::CreatePopupMenu() {
    wxMenu* menu = new wxMenu;
    menu->Append(PU_RESTORE, wxT("显示 MeetAnt"));
    menu->AppendSeparator();
    menu->Append(PU_TOGGLE_RECORD, m_frame->IsCurrentlyRecording() ? wxT("停止录制") : wxT("开始录制"));
    menu->AppendSeparator();
    menu->Append(PU_EXIT,    wxT("退出"));
    return menu;
}

void AppTaskBarIcon::SetRecordingState(bool isRecording) {
    wxIcon trayIcon;
    wxString tooltip;

    // 始终尝试使用嵌入的 MEETANT_ICON 资源作为托盘图标
    trayIcon = wxICON(MEETANT_ICON);

    if (isRecording) {
        tooltip = wxT("MeetAnt - 正在录制");
    } else {
        tooltip = wxT("MeetAnt - 空闲");
    }

    if (trayIcon.IsOk()) {
        SetIcon(trayIcon, tooltip);
    } else {
        // 如果嵌入的图标资源加载失败 (例如 .rc 文件有问题或资源名不对)
        // 则回退到 wxArtProvider 的一个通用图标，并且提示文本不变
        wxLogWarning(wxT("无法加载嵌入的托盘图标资源 MEETANT_ICON。将使用默认的 wxWidgets 图标。"));
        wxIcon defaultIcon = wxArtProvider::GetIcon(wxART_WX_LOGO, wxART_OTHER, wxSize(16, 16));
        if (defaultIcon.IsOk()) {
            SetIcon(defaultIcon, tooltip); // 使用 wxWidgets logo 作为回退，提示文本保持不变
        } else {
             wxLogWarning(wxT("无法加载任何有效的托盘图标。"));
        }
    }
}

void AppTaskBarIcon::OnMenuRestore(wxCommandEvent& WXUNUSED(event)) {
    if (m_frame) {
        m_frame->ShowFrame();
    }
}

void AppTaskBarIcon::OnMenuExit(wxCommandEvent& WXUNUSED(event)) {
    if (m_frame) {
        m_frame->Close(true);
    }
}

void AppTaskBarIcon::OnMenuToggleRecord(wxCommandEvent& WXUNUSED(event)){
    if(m_frame){
        m_frame->ToggleRecordingFromTray();
    }
}

// --- MainFrame 事件表 ---
wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    // EVT_MENU(wxID_EXIT,  MainFrame::OnExit)
    // EVT_MENU(wxID_ABOUT, MainFrame::OnAbout)
    EVT_MENU(ID_Menu_Settings, MainFrame::OnShowConfigDialog)
    EVT_MENU(ID_Menu_New_Session, MainFrame::OnCreateSession)
    EVT_MENU(ID_Menu_Save_Session, MainFrame::OnSaveSession)
    EVT_MENU(ID_Menu_Remove_Session, MainFrame::OnRemoveSession)
    EVT_MENU(ID_Menu_Add_Existing_Session, MainFrame::OnAddExistingSession)
    EVT_MENU(ID_Export_Text, MainFrame::OnExportText)
    EVT_MENU(ID_Export_JSON, MainFrame::OnExportJSON)
    EVT_MENU(ID_Export_Audio, MainFrame::OnExportAudio)
    EVT_MENU(ID_Export_Package, MainFrame::OnExportPackage)
    EVT_BUTTON(ID_RecordButton, MainFrame::OnRecordToggle)
    EVT_BUTTON(ID_AISendButton, MainFrame::OnAISend)
    EVT_BUTTON(ID_CreateSessionButton, MainFrame::OnCreateSession)
    EVT_TREE_SEL_CHANGED(ID_SessionTree, MainFrame::OnSessionSelected)
    EVT_TREE_SEL_CHANGED(ID_BookmarkTree, MainFrame::OnBookmarkSelected)
    EVT_TOOL(ID_Toolbar_Highlight_Yellow, MainFrame::OnHighlight)
    EVT_TOOL(ID_Toolbar_Highlight_Green, MainFrame::OnHighlight)
    EVT_TOOL(ID_Toolbar_Highlight_Blue, MainFrame::OnHighlight)
    EVT_TOOL(ID_Toolbar_Highlight_Red, MainFrame::OnHighlight)
    EVT_TOOL(ID_Toolbar_Bookmark, MainFrame::OnBookmark)
    // EVT_TOOL(ID_Toolbar_Note, MainFrame::OnAddNote)
    // EVT_TOOL(ID_Toolbar_Clear, MainFrame::OnClearFormatting)
    EVT_MENU(ID_LabelSpeaker, MainFrame::OnLabelSpeaker)
    EVT_TEXT_ENTER(wxID_ANY, MainFrame::OnSearch)
    EVT_SEARCHCTRL_SEARCH_BTN(wxID_ANY, MainFrame::OnSearch)
    EVT_SEARCHCTRL_CANCEL_BTN(wxID_ANY, MainFrame::OnSearchCancel)
    // EVT_TREE_SEL_CHANGED(ID_AnnotationTree, MainFrame::OnAnnotationSelected)
    EVT_TREE_ITEM_RIGHT_CLICK(ID_SessionTree, MainFrame::OnSessionTreeContextMenu)
    EVT_TIMER(wxID_ANY, MainFrame::OnAudioSaveTimer)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
    : wxFrame(NULL, wxID_ANY, title, pos, size), m_isRecording(false), m_taskBarIcon(nullptr), 
      m_editorToolbar(nullptr), m_isAudioInitialized(false), m_audioStream(nullptr),
      m_sampleRate(48000), m_audioDeviceIndex(0), m_silenceThreshold(0.05f),
      m_isLocalMode(true), m_isFunASRInitialized(false), m_asrHandle(nullptr),
      m_vadSensitivity(0.5f), m_showAnnotations(true), m_annotationTree(nullptr),
      // 新增音频录制相关成员变量初始化
      m_paStream(nullptr), m_audioBuffer(nullptr), m_recordingTimer(nullptr),
      m_systemAudioMode(false), m_captureType(0),
      // 音频保存相关成员变量初始化
      m_audioFile(nullptr), m_totalAudioFrames(0), m_audioSaveTimer(nullptr),
      // MP3编码相关成员变量初始化
      m_audioFormat(AudioFormat::MP3_192), m_lameEncoder(nullptr), m_isMP3Format(true), m_mp3Bitrate(192),
      // 实际音频格式参数初始化
      m_actualSampleRate(48000), m_actualChannels(1), m_actualBitsPerSample(16),
      // 录音开始时间初始化
      m_recordingStartTime(wxDateTime::Now()),
      // AI相关成员变量初始化
      m_aiConfig(nullptr), m_aiRequest(nullptr), m_aiResponse(nullptr), m_aiResponseText(wxEmptyString),
      m_currentAIResponse(wxEmptyString), m_accumulatedSSEData(wxEmptyString), m_streamingBuffer(wxEmptyString),
      m_isAIRequestActive(false), m_aiResponseStarted(false), m_aiTimeoutTimer(nullptr)
#ifdef _WIN32
      , m_comInitialized(false), m_pEnumerator(nullptr), m_pSelectedLoopbackDevice(nullptr),
      m_pAudioClient(nullptr), m_pCaptureClient(nullptr), m_pWaveFormat(nullptr),
      m_bDirectWasapiLoopbackActive(false), m_pRecordingThread(nullptr)
#endif
{

    // 加载已移除会话列表
    LoadRemovedSessionsList();

    CreateMenuBar();

    // 设置窗口图标 (从资源加载)
    // wxICON() 宏会查找名为 MEETANT_ICON 的资源，该资源在 meetant.rc 中定义
    SetIcon(wxICON(MEETANT_ICON)); 

    // 创建状态栏
    CreateStatusBar();
    SetStatusText(wxT("欢迎使用 MeetAnt!"));

    // --- 设置三栏布局 ---
    m_mainSplitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_BORDER | wxSP_LIVE_UPDATE);
    m_rightSplitter = new wxSplitterWindow(m_mainSplitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_BORDER | wxSP_LIVE_UPDATE);
    m_editorAnnotationSplitter = new wxSplitterWindow(m_rightSplitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_BORDER | wxSP_LIVE_UPDATE);

    // --- 创建导航栏面板 ---
    m_navPanel = new wxPanel(m_mainSplitter, wxID_ANY);
    m_navPanel->SetBackgroundColour(wxColour(200, 220, 255));
    wxBoxSizer* navSizer = new wxBoxSizer(wxVERTICAL);
    
    // 录制按钮
    m_recordButton = new wxButton(m_navPanel, ID_RecordButton, wxT("开始录制"));
    navSizer->Add(m_recordButton, 0, wxALL | wxEXPAND, 5);
    
    // 创建导航笔记本
    m_navNotebook = new wxNotebook(m_navPanel, ID_NavNotebook);
    
    // 会话标签页
    wxPanel* sessionsPage = new wxPanel(m_navNotebook);
    wxBoxSizer* sessionPageSizer = new wxBoxSizer(wxVERTICAL);
    
    // 搜索控件
    m_searchCtrl = new wxSearchCtrl(sessionsPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_searchCtrl->ShowSearchButton(true);
    m_searchCtrl->ShowCancelButton(true);
    sessionPageSizer->Add(m_searchCtrl, 0, wxALL | wxEXPAND, 5);
    
    // 添加搜索高级选项
    wxBoxSizer* searchOptionsSizer = new wxBoxSizer(wxHORIZONTAL);
    
    // 添加正则表达式选项
    m_useRegexCheckBox = new wxCheckBox(sessionsPage, wxID_ANY, wxT("使用正则表达式"));
    searchOptionsSizer->Add(m_useRegexCheckBox, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    
    // 添加发言人过滤选项
    wxStaticText* speakerLabel = new wxStaticText(sessionsPage, wxID_ANY, wxT("按发言人:"));
    searchOptionsSizer->Add(speakerLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    
    m_speakerFilterComboBox = new wxComboBox(sessionsPage, wxID_ANY, wxT("全部"), 
                                           wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_READONLY);
    m_speakerFilterComboBox->Append(wxT("全部"));
    searchOptionsSizer->Add(m_speakerFilterComboBox, 1, wxALL, 2);
    
    sessionPageSizer->Add(searchOptionsSizer, 0, wxALL | wxEXPAND, 5);
    
    // 会话历史树
    m_sessionTree = new wxTreeCtrl(sessionsPage, ID_SessionTree, wxDefaultPosition, wxDefaultSize, 
                                  wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_FULL_ROW_HIGHLIGHT);
    sessionPageSizer->Add(m_sessionTree, 1, wxALL | wxEXPAND, 5);
    
    // 添加创建会话按钮
    m_createSessionButton = new wxButton(sessionsPage, ID_CreateSessionButton, wxT("新建会话"));
    sessionPageSizer->Add(m_createSessionButton, 0, wxALL | wxEXPAND, 5);
    
    sessionsPage->SetSizer(sessionPageSizer);
    
    // 书签标签页
    wxPanel* bookmarksPage = new wxPanel(m_navNotebook);
    wxBoxSizer* bookmarkPageSizer = new wxBoxSizer(wxVERTICAL);
    
    // 书签树
    m_bookmarkTree = new wxTreeCtrl(bookmarksPage, ID_BookmarkTree, wxDefaultPosition, wxDefaultSize, 
                                   wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_FULL_ROW_HIGHLIGHT);
    bookmarkPageSizer->Add(m_bookmarkTree, 1, wxALL | wxEXPAND, 5);
    
    bookmarksPage->SetSizer(bookmarkPageSizer);
    
    // 添加标签页到导航笔记本
    m_navNotebook->AddPage(sessionsPage, wxT("会话"));
    m_navNotebook->AddPage(bookmarksPage, wxT("书签"));
    
    navSizer->Add(m_navNotebook, 1, wxALL | wxEXPAND, 5);
    m_navPanel->SetSizer(navSizer);

    // --- 创建主编辑器面板 ---
    m_editorPanel = new wxPanel(m_editorAnnotationSplitter, wxID_ANY);
    m_editorPanel->SetBackgroundColour(wxColour(255, 255, 255));
    wxBoxSizer* editorSizer = new wxBoxSizer(wxVERTICAL);
    
    // 添加播放控制条
    m_playbackControlBar = new PlaybackControlBar(m_editorPanel, wxID_ANY);
    m_playbackControlBar->SetMinSize(wxSize(-1, 100));
    editorSizer->Add(m_playbackControlBar, 0, wxEXPAND | wxALL, 5);
    
    // 主文本编辑器 - 使用新的气泡控件
    m_transcriptionBubbleCtrl = new TranscriptionBubbleCtrl(m_editorPanel, ID_TranscriptionTextCtrl);
    editorSizer->Add(m_transcriptionBubbleCtrl, 1, wxEXPAND | wxALL, 0);
    m_editorPanel->SetSizer(editorSizer);
    
    // 绑定播放控制条事件
    Bind(wxEVT_PLAYBACK_POSITION_CHANGED, &MainFrame::OnPlaybackPositionChanged, this);
    Bind(wxEVT_PLAYBACK_STATE_CHANGED, &MainFrame::OnPlaybackStateChanged, this);
    
    // 绑定气泡控件事件
    Bind(wxEVT_TRANSCRIPTION_MESSAGE_CLICKED, &MainFrame::OnTranscriptionMessageClicked, this);
    Bind(wxEVT_TRANSCRIPTION_MESSAGE_RIGHT_CLICKED, &MainFrame::OnTranscriptionMessageRightClicked, this);

    // --- 创建批注面板 ---
    m_annotationPanel = new wxPanel(m_editorAnnotationSplitter, wxID_ANY);
    m_annotationPanel->SetBackgroundColour(wxColour(255, 248, 220)); // 浅黄色背景，更像便签纸
    wxBoxSizer* annotationSizer = new wxBoxSizer(wxVERTICAL);
    
    // 添加标题面板
    wxPanel* annotationTitlePanel = new wxPanel(m_annotationPanel, wxID_ANY);
    annotationTitlePanel->SetBackgroundColour(wxColour(255, 235, 180)); // 更深的黄色作为标题背景
    wxBoxSizer* titleSizer = new wxBoxSizer(wxHORIZONTAL);
    
    // 标题文本
    wxStaticText* annotationTitle = new wxStaticText(annotationTitlePanel, wxID_ANY, wxT("划线批注便利贴"));
    annotationTitle->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    annotationTitle->SetForegroundColour(wxColour(100, 80, 20));
    titleSizer->Add(annotationTitle, 1, wxALL | wxALIGN_CENTER_VERTICAL, 10);
    
    annotationTitlePanel->SetSizer(titleSizer);
    annotationSizer->Add(annotationTitlePanel, 0, wxEXPAND);
    
    // 添加提示文本
    wxStaticText* annotationHint = new wxStaticText(m_annotationPanel, wxID_ANY, 
        wxT("鼠标移动到划线文字上显示"));
    annotationHint->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
    annotationHint->SetForegroundColour(wxColour(120, 100, 40));
    annotationSizer->Add(annotationHint, 0, wxALL, 10);
    
    // 批注区文本控件
    m_annotationTextCtrl = new wxRichTextCtrl(m_annotationPanel, wxID_ANY, wxEmptyString, 
                                            wxDefaultPosition, wxDefaultSize, 
                                            wxRE_MULTILINE | wxRE_READONLY | wxBORDER_NONE);
    m_annotationTextCtrl->SetBackgroundColour(wxColour(255, 248, 220)); // 保持和Panel一致的背景色
    
    // 设置批注区样式
    wxRichTextAttr annotationStyle;
    annotationStyle.SetAlignment(wxTEXT_ALIGNMENT_LEFT);
    annotationStyle.SetFontSize(10); // 稍小的字体
    annotationStyle.SetFontStyle(wxFONTSTYLE_NORMAL);
    annotationStyle.SetTextColour(wxColour(80, 60, 20)); // 深棕色字体
    m_annotationTextCtrl->SetDefaultStyle(annotationStyle);
    
    // 添加一些示例批注内容
    m_annotationTextCtrl->AppendText(wxT("微信用户1748349252\n\n"));
    
    // 不再使用Connect或Bind方法，而是在事件表中处理滚动事件
    
    annotationSizer->Add(m_annotationTextCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    m_annotationPanel->SetSizer(annotationSizer);
    
    // 设置编辑器和批注区的分割
    m_editorAnnotationSplitter->SplitVertically(m_editorPanel, m_annotationPanel, 600);
    m_editorAnnotationSplitter->SetSashGravity(0.7); // 编辑器占更多空间
    m_editorAnnotationSplitter->SetMinimumPaneSize(150); // 最小宽度

    // 如果不显示批注，则隐藏批注面板
    if (!m_showAnnotations) {
        m_editorAnnotationSplitter->Unsplit(m_annotationPanel);
    }

    // 创建编辑器工具栏
    CreateToolBar();
    
    // --- 创建 AI 侧边栏面板 ---
    m_aiSidebarPanel = new wxPanel(m_rightSplitter, wxID_ANY);
    m_aiSidebarPanel->SetBackgroundColour(wxColour(210, 230, 210)); // 淡绿色背景
    wxBoxSizer* aiSidebarSizer = new wxBoxSizer(wxVERTICAL);

    m_aiChatHistoryCtrl = new wxTextCtrl(m_aiSidebarPanel, wxID_ANY, wxT("--- AI 聊天记录 ---\n"),
                                         wxDefaultPosition, wxDefaultSize,
                                         wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    aiSidebarSizer->Add(m_aiChatHistoryCtrl, 1, wxEXPAND | wxALL, 5); // 占据大部分垂直空间

    // 创建底部Sizer用于水平排列输入框和按钮
    wxBoxSizer* aiBottomSizer = new wxBoxSizer(wxHORIZONTAL);

    m_aiUserInputCtrl = new wxTextCtrl(m_aiSidebarPanel, wxID_ANY, wxT(""),
                                       wxDefaultPosition, wxSize(-1, 75), // 固定高度，宽度随父控件
                                       wxTE_PROCESS_ENTER | wxTE_MULTILINE); // 多行，处理Enter键
    aiBottomSizer->Add(m_aiUserInputCtrl, 1, wxEXPAND | wxALL, 5); // 输入框占据水平大部分空间并扩展

    m_aiSendButton = new wxButton(m_aiSidebarPanel, ID_AISendButton, wxT("发送"));
    aiBottomSizer->Add(m_aiSendButton, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5); // 发送按钮垂直居中对齐

    aiSidebarSizer->Add(aiBottomSizer, 0, wxEXPAND | wxALL, 5);
    m_aiSidebarPanel->SetSizer(aiSidebarSizer);

    // --- 分割窗口 ---
    // 导航栏 (左) vs (编辑器+批注栏+AI栏) (右)
    m_mainSplitter->SplitVertically(m_navPanel, m_rightSplitter, 200);
    // (编辑器+批注栏) (左) vs AI栏 (右)
    m_rightSplitter->SplitVertically(m_editorAnnotationSplitter, m_aiSidebarPanel, -250); // AI栏宽度250px，从右边算起
    // 编辑器 (左) vs 批注栏 (右)
    m_editorAnnotationSplitter->SplitVertically(m_editorPanel, m_annotationPanel, -200); // 批注栏宽度200px, 从右边算起

    // 设置最小窗格大小，防止完全折叠 - 减小最小窗格大小以避免错误
    m_mainSplitter->SetMinimumPaneSize(100);
    m_rightSplitter->SetMinimumPaneSize(150);
    m_editorAnnotationSplitter->SetMinimumPaneSize(100);

    // 创建系统托盘图标
    m_taskBarIcon = new AppTaskBarIcon(this);
    
    // 初始化批注管理器
    InitAnnotationManager();
    
    // 加载会话
    LoadSessions();
    
    // 加载书签
    LoadBookmarks();
    
    // 使用布局
    SetSizer(new wxBoxSizer(wxVERTICAL));
    GetSizer()->Add(m_mainSplitter, 1, wxEXPAND);
    Layout();
    
    // 如果没有会话，自动创建一个默认会话
    if (m_sessions.empty()) {
        wxDateTime now = wxDateTime::Now();
        wxString sessionName = wxString::Format(wxT("默认会话-%s"), now.Format(wxT("%Y%m%d-%H%M%S")));
        
        // 创建会话目录
        wxString sessionPath = CreateSessionDirectory(sessionName);
        if (!sessionPath.IsEmpty()) {
            m_currentSessionPath = sessionPath;
            m_currentSessionId = sessionName;
            
            // 添加到会话列表
            SessionItem newSession;
            newSession.name = sessionName;
            newSession.path = sessionPath;
            newSession.creationTime = now;
            newSession.isActive = true;
            m_sessions.push_back(newSession);
            
            // 添加到会话树
            wxTreeItemId rootId = m_sessionTree->GetRootItem();
            wxTreeItemId newItemId = m_sessionTree->AppendItem(rootId, sessionName);
            m_sessionTree->SelectItem(newItemId);
            
            SetStatusText(wxString::Format(wxT("已创建默认会话: %s"), sessionName));
        }
    }
    
    // 添加测试对话数据
    AddTestTranscriptionData();
    
    // 初始化PortAudio
    InitializePortAudio();
    
    // 加载音频配置
    try {
        LoadAudioConfig();
    } catch (...) {
        wxLogWarning(wxT("音频配置加载失败，使用默认设置"));
    }
    
    // 加载音频格式配置
    try {
        LoadAudioFormatConfig();
    } catch (...) {
        wxLogWarning(wxT("音频格式配置加载失败，使用默认设置"));
    }
    
    // 初始化音频保存相关组件
    m_audioDataBuffer.reserve(AUDIO_BUFFER_FRAMES);
    m_audioSaveTimer = new wxTimer(this);
    
    // 加载AI配置
    try {
        LoadAIConfig();
    } catch (...) {
        wxLogWarning(wxT("AI配置加载失败，使用默认设置"));
    }
    
    // 绑定AI相关事件
    this->Bind(wxEVT_WEBREQUEST_STATE, &MainFrame::OnAIWebRequestStateChanged, this);
    this->Bind(wxEVT_WEBREQUEST_DATA, &MainFrame::OnAIWebRequestDataReceived, this);
    
    // 绑定 SSE 事件处理
    this->Bind(wxEVT_SSE_OPEN, &MainFrame::OnSSEOpen, this);
    this->Bind(wxEVT_SSE_MESSAGE, &MainFrame::OnSSEMessage, this);
    this->Bind(wxEVT_SSE_ERROR, &MainFrame::OnSSEError, this);
    this->Bind(wxEVT_SSE_CLOSE, &MainFrame::OnSSEClose, this);
    
    // 为AI输入框绑定Enter键事件
    m_aiUserInputCtrl->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent& event) {
        wxCommandEvent sendEvent(wxEVT_COMMAND_BUTTON_CLICKED, ID_AISendButton);
        this->OnAISend(sendEvent);
    });
    
    // 绑定AI超时定时器事件
    this->Bind(wxEVT_TIMER, &MainFrame::OnAIRequestTimeout, this);
}

MainFrame::~MainFrame() {
    // 停止录制（如果正在录制）
    if (m_isRecording) {
        StopAudioCapture();
        StopAudioRecording();
    }
    
    // 清理音频保存相关资源
    if (m_audioSaveTimer) {
        if (m_audioSaveTimer->IsRunning()) {
            m_audioSaveTimer->Stop();
        }
        delete m_audioSaveTimer;
        m_audioSaveTimer = nullptr;
    }
    
    if (m_audioFile) {
        FlushAudioBuffer();
        m_audioFile->Close();
        delete m_audioFile;
        m_audioFile = nullptr;
    }
    
    // 清理MP3编码器
    ShutdownMP3Encoder();
    
    // 清理音频资源
    ShutdownPortAudio();
    
    if (m_taskBarIcon) {
        m_taskBarIcon->RemoveIcon();
        delete m_taskBarIcon;
    }
    
    // 清理所有活动的批注弹窗
    for (auto popup : m_activeNotePopups) {
        popup->Destroy();
    }
    m_activeNotePopups.clear();
    
    // 清理AI相关资源
    StopAITimeoutTimer();
    if (m_aiWebRequest.IsOk() && m_aiWebRequest.GetState() == wxWebRequest::State_Active) {
        m_aiWebRequest.Cancel();
    }
    if (m_aiConfig) {
        delete m_aiConfig;
        m_aiConfig = nullptr;
    }
}

void MainFrame::CreateMenuBar() {
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(ID_Menu_New_Session, wxT("&新建会话\tCtrl-N"), wxT("新建会话"));
    menuFile->Append(ID_Menu_Add_Existing_Session, wxT("添加现有会话"), wxT("添加磁盘上已存在的会话到列表中"));
    menuFile->Append(ID_Menu_Save_Session, wxT("&保存会话\tCtrl-S"), wxT("保存当前会话"));
    menuFile->Append(ID_Menu_Remove_Session, wxT("从列表中移除会话"), wxT("从列表中移除当前会话，但不删除文件"));
    
    // 导出子菜单
    wxMenu* exportMenu = new wxMenu;
    exportMenu->Append(ID_Export_Text, wxT("&文本格式 (TXT)"), wxT("导出为纯文本格式"));
    exportMenu->Append(ID_Export_JSON, wxT("&JSON格式"), wxT("导出为JSON格式"));
    exportMenu->Append(ID_Export_Audio, wxT("&音频格式 (WAV)"), wxT("导出为WAV音频"));
    exportMenu->Append(ID_Export_Package, wxT("&会议包 (MTP)"), wxT("导出为完整会议包"));
    
    menuFile->AppendSubMenu(exportMenu, wxT("导出"));
    menuFile->AppendSeparator();
    menuFile->Append(ID_Menu_Settings, wxT("&设置...\tCtrl-P"), wxT("配置应用程序设置"));
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT, wxT("退出\tAlt-F4")); // 使用内置的 wxID_EXIT

    wxMenu *menuHelp = new wxMenu;
    menuHelp->Append(wxID_ABOUT, wxT("关于 MeetAnt...")); // 使用内置的 wxID_ABOUT

    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append(menuFile, wxT("&文件"));
    menuBar->Append(menuHelp, wxT("&帮助"));

    SetMenuBar(menuBar);
}

void MainFrame::CreateToolBar() {
    m_editorToolbar = new wxToolBar(m_editorPanel, wxID_ANY);
    
    // 添加功能标签
    wxStaticText* toolLabel = new wxStaticText(m_editorToolbar, wxID_ANY, wxT("划线批注："));
    m_editorToolbar->AddControl(toolLabel);
    
    // 添加高亮工具按钮 - 使用更安全的图标加载
    wxBitmap defaultIcon = wxArtProvider::GetBitmap(wxART_INFORMATION, wxART_TOOLBAR, wxSize(16, 16));
    
    wxBitmap yellowHighlight = wxArtProvider::GetBitmap(wxART_FIND, wxART_TOOLBAR, wxSize(16, 16));
    if (!yellowHighlight.IsOk()) yellowHighlight = defaultIcon;
    m_editorToolbar->AddTool(ID_Toolbar_Highlight_Yellow, wxT("黄色高亮"), yellowHighlight, wxT("黄色高亮所选文本"));
    
    wxBitmap greenHighlight = wxArtProvider::GetBitmap(wxART_FIND, wxART_TOOLBAR, wxSize(16, 16));
    if (!greenHighlight.IsOk()) greenHighlight = defaultIcon;
    m_editorToolbar->AddTool(ID_Toolbar_Highlight_Green, wxT("绿色高亮"), greenHighlight, wxT("绿色高亮所选文本"));
    
    wxBitmap blueHighlight = wxArtProvider::GetBitmap(wxART_FIND, wxART_TOOLBAR, wxSize(16, 16));
    if (!blueHighlight.IsOk()) blueHighlight = defaultIcon;
    m_editorToolbar->AddTool(ID_Toolbar_Highlight_Blue, wxT("蓝色高亮"), blueHighlight, wxT("蓝色高亮所选文本"));
    
    wxBitmap redHighlight = wxArtProvider::GetBitmap(wxART_FIND, wxART_TOOLBAR, wxSize(16, 16));
    if (!redHighlight.IsOk()) redHighlight = defaultIcon;
    m_editorToolbar->AddTool(ID_Toolbar_Highlight_Red, wxT("红色高亮"), redHighlight, wxT("红色高亮所选文本"));
    
    m_editorToolbar->AddSeparator();
    
    // 添加其他标注功能
    wxStaticText* annotationLabel = new wxStaticText(m_editorToolbar, wxID_ANY, wxT("其他功能："));
    m_editorToolbar->AddControl(annotationLabel);
    
    // 添加书签按钮
    wxBitmap bookmarkBitmap = wxArtProvider::GetBitmap(wxART_ADD_BOOKMARK, wxART_TOOLBAR, wxSize(16, 16));
    if (!bookmarkBitmap.IsOk()) bookmarkBitmap = defaultIcon;
    m_editorToolbar->AddTool(ID_Toolbar_Bookmark, wxT("添加书签"), bookmarkBitmap, wxT("在当前位置添加书签"));
    
    // 添加批注按钮
    wxBitmap noteBitmap = wxArtProvider::GetBitmap(wxART_INFORMATION, wxART_TOOLBAR, wxSize(16, 16));
    if (!noteBitmap.IsOk()) noteBitmap = defaultIcon;
    m_editorToolbar->AddTool(ID_Toolbar_Note, wxT("添加批注"), noteBitmap, wxT("为所选文本添加批注"));
    
    m_editorToolbar->AddSeparator();
    
    // 添加导出功能
    wxBitmap exportBitmap = wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_TOOLBAR, wxSize(16, 16));
    if (!exportBitmap.IsOk()) exportBitmap = defaultIcon;
    m_editorToolbar->AddTool(ID_Export_Text, wxT("导出"), exportBitmap, wxT("导出会议记录"));
    
    // 添加搜索功能
    wxBitmap searchBitmap = wxArtProvider::GetBitmap(wxART_FIND, wxART_TOOLBAR, wxSize(16, 16));
    if (!searchBitmap.IsOk()) searchBitmap = defaultIcon;
    m_editorToolbar->AddTool(ID_Menu_Search, wxT("搜索"), searchBitmap, wxT("搜索会议内容"));
    
    m_editorToolbar->AddSeparator();
    
    // 添加清除格式按钮
    wxBitmap clearBitmap = wxArtProvider::GetBitmap(wxART_DELETE, wxART_TOOLBAR, wxSize(16, 16));
    if (!clearBitmap.IsOk()) clearBitmap = defaultIcon;
    m_editorToolbar->AddTool(ID_Toolbar_Clear, wxT("清除格式"), clearBitmap, wxT("清除选中区域的格式"));
    
    // 添加切换批注显示按钮 - 使用wxITEM_CHECK类型使其可切换
    wxBitmap toggleAnnotationsBitmap = wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_TOOLBAR, wxSize(16, 16));
    if (!toggleAnnotationsBitmap.IsOk()) toggleAnnotationsBitmap = defaultIcon;
    m_editorToolbar->AddTool(ID_Toolbar_Toggle_Annotations, wxT("切换批注显示"), 
                           toggleAnnotationsBitmap, wxT("显示/隐藏批注面板"), 
                           wxITEM_CHECK);
    
    // 设置批注按钮的初始状态
    m_editorToolbar->ToggleTool(ID_Toolbar_Toggle_Annotations, m_showAnnotations);
    
    m_editorToolbar->Realize();
    
    // 将工具栏添加到编辑器面板的顶部
    wxSizer* sizer = m_editorPanel->GetSizer();
    if (sizer) {
        sizer->Insert(0, m_editorToolbar, 0, wxEXPAND | wxALL, 0);
        m_editorPanel->Layout();
    }
    
    // 动态绑定工具栏切换批注显示按钮的事件
    m_editorToolbar->Bind(wxEVT_TOOL, 
                        [this](wxCommandEvent& event){ 
                            this->ToggleAnnotationsDisplay(); 
                        }, 
                        ID_Toolbar_Toggle_Annotations);
}

// --- 批注相关方法 ---

void MainFrame::InitAnnotationManager() {
    m_annotationManager = std::make_unique<MeetAnt::AnnotationManager>();
    
    // 动态绑定滚动事件
    m_transcriptionBubbleCtrl->Bind(wxEVT_SCROLLWIN_THUMBTRACK, 
                                [this](wxScrollWinEvent& event){ 
                                    this->SyncAnnotationScrollPosition(event); 
                                });
    m_transcriptionBubbleCtrl->Bind(wxEVT_SCROLLWIN_LINEUP, 
                                [this](wxScrollWinEvent& event){ 
                                    this->SyncAnnotationScrollPosition(event); 
                                });
    m_transcriptionBubbleCtrl->Bind(wxEVT_SCROLLWIN_LINEDOWN, 
                                [this](wxScrollWinEvent& event){ 
                                    this->SyncAnnotationScrollPosition(event); 
                                });
}

void MainFrame::LoadBookmarks() {
    // 清空书签树
    m_bookmarkTree->DeleteAllItems();
    wxTreeItemId rootId = m_bookmarkTree->AddRoot(wxT("书签列表"));
    
    // 当前没有会话时不加载书签
    if (m_currentSessionPath.IsEmpty()) {
        return;
    }
    
    // 加载当前会话的批注
    m_annotationManager->LoadAnnotations(m_currentSessionPath);
    
    // 获取书签并添加到树
    auto bookmarks = m_annotationManager->GetAllBookmarks();
    for (auto bookmark : bookmarks) {
        AddBookmarkToTree(bookmark);
    }
}

void MainFrame::AddBookmarkToTree(MeetAnt::BookmarkAnnotation* bookmark) {
    if (!bookmark) {
        return;
    }
    
    wxTreeItemId rootId = m_bookmarkTree->GetRootItem();
    
    // 获取时间戳并格式化
    MeetAnt::TimeStamp ts = bookmark->GetTimestamp();
    int hours = ts / 3600000;
    int minutes = (ts % 3600000) / 60000;
    int seconds = (ts % 60000) / 1000;
    
    wxString label = bookmark->GetLabel();
    if (label.IsEmpty()) {
        label = wxT("未命名书签");
    }
    
    wxString timeStr = wxString::Format(wxT("[%02d:%02d:%02d]"), hours, minutes, seconds);
    wxString itemText = wxString::Format(wxT("%s %s"), timeStr, label);
    
    // 添加到书签树
    wxTreeItemId itemId = m_bookmarkTree->AppendItem(rootId, itemText);
    
    // 存储书签指针到项数据中
    m_bookmarkTree->SetItemData(itemId, new wxTreeItemData());
    
    // 不要尝试展开隐藏的根节点，因为使用了 wxTR_HIDE_ROOT 风格
    // m_bookmarkTree->Expand(rootId);
}

void MainFrame::UpdateBookmarksTree() {
    LoadBookmarks();
}

void MainFrame::CreateBookmark(const wxString& label, const wxString& description, MeetAnt::TimeStamp timestamp) {
    if (m_currentSessionId.IsEmpty()) {
        wxMessageBox(wxT("请先选择或创建一个会话"), wxT("错误"), wxICON_ERROR, this);
        return;
    }
    
    // 创建书签并加入管理器
    auto bookmark = std::make_unique<MeetAnt::BookmarkAnnotation>(
        m_currentSessionId, timestamp, description, label);
    
    // 添加到管理器前获取指针
    MeetAnt::BookmarkAnnotation* bookmarkPtr = bookmark.get();
    
    m_annotationManager->AddAnnotation(std::move(bookmark));
    
    // 添加到书签树
    AddBookmarkToTree(bookmarkPtr);
    
    // 保存批注
    m_annotationManager->SaveAnnotations(m_currentSessionPath);
}

void MainFrame::CreateNote(const wxString& title, const wxString& content, MeetAnt::TimeStamp timestamp) {
    if (m_currentSessionId.IsEmpty()) {
        // 这里不再显示错误消息，因为我们已经在OnAddNote方法中处理了这种情况
        return;
    }
    
    // 创建批注并加入管理器
    auto note = std::make_unique<MeetAnt::NoteAnnotation>(
        m_currentSessionId, timestamp, content, title);
    
    // 添加到管理器前获取指针
    MeetAnt::NoteAnnotation* notePtr = note.get();
    
    m_annotationManager->AddAnnotation(std::move(note));
    
    // 显示批注弹窗
    ShowNotePopup(notePtr);
    
    // 保存批注
    m_annotationManager->SaveAnnotations(m_currentSessionPath);
}

void MainFrame::ApplyHighlight(const wxColour& color) {
    // 获取选中的消息（如果有）
    if (m_selectedTranscriptionMessageId > 0) {
        m_transcriptionBubbleCtrl->HighlightMessage(m_selectedTranscriptionMessageId, true);
        
        // 获取消息内容用于创建高亮批注
        const auto& messages = m_transcriptionBubbleCtrl->GetMessages();
        for (const auto& msg : messages) {
            if (msg.messageId == m_selectedTranscriptionMessageId) {
                // 获取当前时间戳
                MeetAnt::TimeStamp timestamp = msg.timestamp.GetTicks() * 1000; // 转换为毫秒
                
                // 创建高亮批注并加入管理器
                if (!m_currentSessionId.IsEmpty()) {
                    auto highlight = std::make_unique<MeetAnt::HighlightAnnotation>(
                        m_currentSessionId, timestamp, msg.content, color);
                    
                    m_annotationManager->AddAnnotation(std::move(highlight));
                    
                    // 保存批注
                    m_annotationManager->SaveAnnotations(m_currentSessionPath);
                }
                break;
            }
        }
    } else {
        wxMessageBox(wxT("请先选择要高亮的消息"), wxT("提示"), wxICON_INFORMATION, this);
    }
}

void MainFrame::ShowNotePopup(MeetAnt::NoteAnnotation* note) {
    if (!note) {
        return;
    }
    
    // 创建批注弹窗
    NotePopup* popup = new NotePopup(this, note);
    
    // 设置位置（可以根据需要调整）
    wxSize clientSize = GetClientSize();
    wxPoint popupPos(clientSize.GetWidth() - 220, 
                    clientSize.GetHeight() / 2 - 100);
    popup->SetPosition(ClientToScreen(popupPos));
    
    // 显示弹窗
    popup->Show();
    
    // 添加到活动批注列表
    m_activeNotePopups.push_back(popup);
}

void MainFrame::JumpToTimestamp(MeetAnt::TimeStamp timestamp) {
    // 在实际应用中，这个方法应该跳转到音频文件的特定时间点
    // 并且可能需要滚动文本编辑器到相应位置
    
    // 示例：设置状态栏消息
    int hours = timestamp / 3600000;
    int minutes = (timestamp % 3600000) / 60000;
    int seconds = (timestamp % 60000) / 1000;
    
    wxString timeStr = wxString::Format(wxT("%02d:%02d:%02d"), hours, minutes, seconds);
    SetStatusText(wxString::Format(wxT("已跳转到时间点: %s"), timeStr));
}

// --- 事件处理方法 ---

void MainFrame::OnHighlight(wxCommandEvent& event) {
    wxColour color;
    
    // 根据工具ID选择高亮颜色
    switch (event.GetId()) {
        case ID_Toolbar_Highlight_Yellow:
            color = wxColour(255, 255, 0); // 黄色
            break;
        case ID_Toolbar_Highlight_Green:
            color = wxColour(0, 255, 0);   // 绿色
            break;
        case ID_Toolbar_Highlight_Blue:
            color = wxColour(0, 191, 255); // 浅蓝色
            break;
        case ID_Toolbar_Highlight_Red:
            color = wxColour(255, 0, 0);   // 红色
            break;
        default:
            color = wxColour(255, 255, 0); // 默认黄色
    }
    
    ApplyHighlight(color);
}

void MainFrame::OnBookmark(wxCommandEvent& event) {
    // 检查是否有活跃会话，如果没有则自动创建一个默认会话
    if (m_currentSessionId.IsEmpty() || !m_annotationManager) {
        // 提示用户将创建默认会话
        wxMessageDialog dlg(this, wxT("需要一个活跃的会话来添加书签。\n是否创建一个默认会话？"), 
                         wxT("创建默认会话"), wxYES_NO | wxICON_QUESTION);
        if (dlg.ShowModal() == wxID_YES) {
            // 创建默认会话
            wxDateTime now = wxDateTime::Now();
            wxString sessionName = wxString::Format(wxT("默认会话-%s"), now.Format(wxT("%Y%m%d-%H%M%S")));
            
            // 创建会话目录
            wxString sessionPath = CreateSessionDirectory(sessionName);
            if (!sessionPath.IsEmpty()) {
                m_currentSessionPath = sessionPath;
                m_currentSessionId = sessionName;
                
                // 确保批注管理器已初始化
                if (!m_annotationManager) {
                    InitAnnotationManager();
                }
                
                // 更新状态栏
                SetStatusText(wxString::Format(wxT("已创建默认会话: %s"), sessionName));
                
                // 可以选择将新会话添加到会话树中
                wxTreeItemId rootId = m_sessionTree->GetRootItem();
                wxTreeItemId newItemId = m_sessionTree->AppendItem(rootId, sessionName);
                m_sessionTree->SelectItem(newItemId);
            } else {
                wxMessageBox(wxT("无法创建会话目录"), wxT("错误"), wxICON_ERROR);
                return;
            }
        } else {
            // 用户取消，不继续
            return;
        }
    }

    // 使用当前时间作为时间戳，或者如果有选中的消息，使用该消息的时间戳
    MeetAnt::TimeStamp currentTime;
    if (m_selectedTranscriptionMessageId > 0) {
        // 查找选中消息的时间戳
        const auto& messages = m_transcriptionBubbleCtrl->GetMessages();
        for (const auto& msg : messages) {
            if (msg.messageId == m_selectedTranscriptionMessageId) {
                currentTime = msg.timestamp.GetTicks() * 1000; // 转换为毫秒
                break;
            }
        }
    } else {
        // 使用当前时间
        wxDateTime now = wxDateTime::Now();
        currentTime = now.GetTicks() * 1000; // 转换为毫秒
    }

    MeetAnt::BookmarkDialog dlg(this, wxT("添加书签"));
    if (dlg.ShowModal() == wxID_OK) {
        wxString label = dlg.GetBookmarkLabel();
        // dlg.ApplySettings(); // Hypothetical method to apply settings from dialog
        SetStatusText(wxT("设置已更新 (模拟)"));
        
        // Reload AI configuration
        LoadAIConfig();
    } else {
        SetStatusText(wxT("设置未更改"));
    }
}

// --- 导航和会话相关方法 ---
void MainFrame::OnSessionSelected(wxTreeEvent& event) {
    wxTreeItemId itemId = event.GetItem();
    if (!itemId.IsOk()) {
        return;
    }
    
    // 获取选中的会话名称
    wxString sessionName = m_sessionTree->GetItemText(itemId);
    
    // 如果会话名称前面有星号（表示活动会话），去掉星号和空格
    if (sessionName.StartsWith(wxT("* "))) {
        sessionName = sessionName.Mid(2);
    }
    
    // 查找对应的会话数据
    bool sessionFound = false;
    for (auto& session : m_sessions) {
        if (session.name == sessionName) {
            // 更新当前会话
            m_currentSessionId = sessionName;
            m_currentSessionPath = session.path;
            sessionFound = true;
            
            // 加载会话内容和批注
            if (m_annotationManager) {
                // 加载和显示批注
                m_annotationManager->LoadAnnotations(m_currentSessionPath);
                // PopulateAnnotationTree();
                LoadBookmarks();
                RefreshAnnotations();
            } else {
                // 初始化批注管理器
                InitAnnotationManager();
                m_annotationManager->LoadAnnotations(m_currentSessionPath);
                // PopulateAnnotationTree();
                LoadBookmarks();
            }
            
            // 更新状态栏消息
            SetStatusText(wxString::Format(wxT("已选择会话: %s"), sessionName));
            
            break;
        }
    }
    
    // 如果在会话列表中找不到该会话（可能是因为会话列表未正确加载）
    if (!sessionFound) {
        // 创建新的会话项
        SessionItem newSession;
        newSession.name = sessionName;
        
        // 尝试查找或创建会话目录
        wxString sessionPath = CreateSessionDirectory(sessionName);
        if (!sessionPath.IsEmpty()) {
            newSession.path = sessionPath;
            newSession.creationTime = wxDateTime::Now();
            newSession.isActive = true;
            
            // 添加到会话列表
            m_sessions.push_back(newSession);
            
            // 更新当前会话信息
            m_currentSessionId = sessionName;
            m_currentSessionPath = sessionPath;
            
            // 初始化或加载批注
            if (!m_annotationManager) {
                InitAnnotationManager();
            }
            
            // 尝试加载批注，但如果失败也不显示错误给用户
            try {
                m_annotationManager->LoadAnnotations(m_currentSessionPath);
                // PopulateAnnotationTree();
                LoadBookmarks();
            } catch (const std::exception& e) {
                wxLogWarning(wxT("加载批注时发生异常: %s - %s"), m_currentSessionPath, wxString(e.what()));
            } catch (...) {
                wxLogWarning(wxT("加载批注时发生未知异常: %s"), m_currentSessionPath);
            }
            
            SetStatusText(wxString::Format(wxT("已创建并选择会话: %s"), sessionName));
        } else {
            // 如果无法创建会话目录，至少设置会话ID以便于UI显示
            m_currentSessionId = sessionName;
            SetStatusText(wxString::Format(wxT("无法创建会话目录: %s (但会话仍可在列表中显示)"), sessionName));
        }
    }
}

void MainFrame::OnCreateSession(wxCommandEvent& event) {
    // 创建新会话
    wxString sessionName = wxGetTextFromUser(
        wxT("请输入新会话名称:"), wxT("新建会话"), 
        wxString::Format(wxT("会话-%s"), wxDateTime::Now().Format(wxT("%Y%m%d-%H%M%S"))),
        this);
    
    if (sessionName.IsEmpty()) {
        return; // 用户取消
    }
    
    // 检查是否已存在同名会话
    for (const auto& session : m_sessions) {
        if (session.name == sessionName) {
            wxMessageBox(
                wxString::Format(wxT("已存在名为\"%s\"的会话，请使用其他名称。"), sessionName),
                wxT("会话已存在"),
                wxICON_INFORMATION | wxOK,
                this
            );
            return;
        }
    }
    
    // 创建会话目录
    wxString sessionPath = CreateSessionDirectory(sessionName);
    if (sessionPath.IsEmpty()) {
        wxMessageBox(
            wxString::Format(wxT("无法创建会话目录：%s"), sessionName),
            wxT("创建会话失败"),
            wxICON_ERROR | wxOK,
            this
        );
        return;
    }
    
    // 创建新会话对象
    SessionItem newSession;
    newSession.name = sessionName;
    newSession.path = sessionPath;
    newSession.creationTime = wxDateTime::Now();
    newSession.isActive = true;
    
    // 更新当前活跃会话状态
    for (auto& session : m_sessions) {
        session.isActive = false;
    }
    
    // 添加到会话列表
    m_sessions.push_back(newSession);
    
    // 设置为当前会话
    m_currentSessionId = sessionName;
    m_currentSessionPath = sessionPath;
    
    // 添加到会话树并选中
    wxTreeItemId rootId = m_sessionTree->GetRootItem();
    wxTreeItemId newItemId = m_sessionTree->AppendItem(rootId, wxString::Format(wxT("* %s"), sessionName));
    m_sessionTree->SelectItem(newItemId);
    
    // 确保批注管理器已初始化
    if (!m_annotationManager) {
        InitAnnotationManager();
    }
    
    // 清空并刷新批注
    m_annotationManager->ClearAnnotations();
    LoadBookmarks();
    // PopulateAnnotationTree();
    RefreshAnnotations();
    
    SetStatusText(wxString::Format(wxT("已创建新会话: %s"), sessionName));
}

void MainFrame::OnSaveSession(wxCommandEvent& event) {
    if (m_currentSessionId.IsEmpty()) {
        wxMessageBox(wxT("没有活动的会话可以保存"), wxT("保存会话"), wxICON_INFORMATION, this);
        return;
    }
    
    // TODO: 实际保存会话的逻辑
    SaveCurrentSession();
    SetStatusText(wxT("会话已保存"));
}

void MainFrame::ShowFrame() {
    // 从托盘恢复窗口
    if (IsIconized()) {
        Iconize(false);
    }
    Show(true);
    Raise();
}

void MainFrame::ToggleRecordingFromTray() {
    // 从托盘图标切换录音状态 - 使用既有的OnRecordToggle实现
    wxCommandEvent dummyEvent;
    OnRecordToggle(dummyEvent);
}

void MainFrame::UpdateTaskBarIconState() {
    if (m_taskBarIcon) {
        m_taskBarIcon->SetRecordingState(m_isRecording);
    }
}

// --- 搜索相关方法 ---
void MainFrame::OnSearch(wxCommandEvent& event) {
    wxString searchQuery = m_searchCtrl->GetValue();
    if (searchQuery.IsEmpty()) {
        return;
    }
    
    // 使用新控件的搜索功能
    bool useRegex = m_useRegexCheckBox->GetValue();
    wxString speaker = m_speakerFilterComboBox->GetValue();
    
    // 执行搜索
    std::vector<int> results = m_transcriptionBubbleCtrl->SearchText(searchQuery, false);
    
    if (!results.empty()) {
        // 滚动到第一个结果
        m_transcriptionBubbleCtrl->ScrollToMessage(results[0]);
        m_transcriptionBubbleCtrl->HighlightMessage(results[0], true);
        
        SetStatusText(wxString::Format(wxT("找到 %zu 个匹配项"), results.size()));
    } else {
        SetStatusText(wxT("未找到匹配项"));
    }
}

void MainFrame::OnSearchCancel(wxCommandEvent& event) {
    // 取消搜索，清空搜索框
    m_searchCtrl->Clear();
    
    // TODO: 重置搜索结果等
    SetStatusText(wxT("搜索已取消"));
}

void MainFrame::OnLabelSpeaker(wxCommandEvent& event) {
    // 标记发言人
    wxString speakerName = wxGetTextFromUser(
        wxT("请输入发言人名称:"), wxT("标记发言人"), wxT(""), this);
    
    if (speakerName.IsEmpty()) {
        return; // 用户取消
    }
    
    // 如果有选中的消息，更新其发言人
    if (m_selectedTranscriptionMessageId > 0) {
        // 这里需要在TranscriptionBubbleCtrl中添加更新发言人的方法
        // 暂时使用颜色设置来标识不同发言人
        m_transcriptionBubbleCtrl->SetSpeakerColor(speakerName, 
            wxColour(rand() % 256, rand() % 256, rand() % 256));
    }
    
    // 更新发言人过滤下拉框
    if (m_speakerFilterComboBox->FindString(speakerName) == wxNOT_FOUND) {
        m_speakerFilterComboBox->Append(speakerName);
    }
}

void MainFrame::OnBookmarkSelected(wxTreeEvent& event) {
    wxTreeItemId itemId = event.GetItem();
    if (!itemId.IsOk()) {
        return;
    }
    
    // TODO: 读取选中的书签信息并跳转
    // 示例实现：
    wxString bookmarkText = m_bookmarkTree->GetItemText(itemId);
    
    // 从书签文本中解析时间戳（格式假设为[HH:MM:SS]）
    wxRegEx timeRegex(wxT("\\[(\\d{2}):(\\d{2}):(\\d{2})\\]"));
    if (timeRegex.Matches(bookmarkText)) {
        wxString hoursStr = timeRegex.GetMatch(bookmarkText, 1);
        wxString minutesStr = timeRegex.GetMatch(bookmarkText, 2);
        wxString secondsStr = timeRegex.GetMatch(bookmarkText, 3);
        
        long hours, minutes, seconds;
        hoursStr.ToLong(&hours);
        minutesStr.ToLong(&minutes);
        secondsStr.ToLong(&seconds);
        
        MeetAnt::TimeStamp timestamp = hours * 3600000 + minutes * 60000 + seconds * 1000;
        JumpToTimestamp(timestamp);
    } else {
        SetStatusText(wxT("无法从书签解析时间戳"));
    }
}

// --- 导出相关方法 ---
void MainFrame::OnExportText(wxCommandEvent& event) {
    if (m_currentSessionId.IsEmpty()) {
        wxMessageBox(wxT("没有活动的会话可以导出"), wxT("导出文本"), wxICON_INFORMATION, this);
        return;
    }
    
    wxFileDialog saveFileDialog(this, wxT("保存文本文件"), "", "",
                               wxT("文本文件 (*.txt)|*.txt"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    
    if (saveFileDialog.ShowModal() == wxID_CANCEL) {
        return;
    }
    
    // 使用新控件的导出功能
    wxString path = saveFileDialog.GetPath();
    
    wxFile file(path, wxFile::write);
    if (file.IsOpened()) {
        wxString content = m_transcriptionBubbleCtrl->ExportAsText();
        size_t bytesToWrite = content.Length();
        size_t bytesWritten = file.Write(content);
        file.Close();
        
        if (bytesWritten == bytesToWrite) {
            SetStatusText(wxString::Format(wxT("文本已导出到: %s"), path));
        } else {
            wxMessageBox(wxString::Format(wxT("写入文件失败，只写入了 %zu/%zu 字节: %s"), 
                                        bytesWritten, bytesToWrite, path), 
                        wxT("导出错误"), wxICON_ERROR);
        }
    } else {
        wxMessageBox(wxString::Format(wxT("无法写入文件: %s"), path), 
                    wxT("导出错误"), wxICON_ERROR);
    }
}

void MainFrame::OnExportJSON(wxCommandEvent& event) {
    if (m_currentSessionId.IsEmpty()) {
        wxMessageBox(wxT("没有活动的会话可以导出"), wxT("导出JSON"), wxICON_INFORMATION, this);
        return;
    }
    
    wxFileDialog saveFileDialog(this, wxT("保存JSON文件"), "", "",
                               wxT("JSON文件 (*.json)|*.json"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    
    if (saveFileDialog.ShowModal() == wxID_CANCEL) {
        return;
    }
    
    // TODO: 实际导出JSON的逻辑
    wxString path = saveFileDialog.GetPath();
    
    // 示例仅显示消息，实际应生成并保存JSON数据
    SetStatusText(wxString::Format(wxT("JSON导出功能尚未实现，选定路径: %s"), path));
}

void MainFrame::OnExportAudio(wxCommandEvent& event) {
    if (m_currentSessionId.IsEmpty()) {
        wxMessageBox(wxT("没有活动的会话可以导出"), wxT("导出音频"), wxICON_INFORMATION, this);
        return;
    }
    
    wxFileDialog saveFileDialog(this, wxT("保存音频文件"), "", "",
                               wxT("WAV文件 (*.wav)|*.wav"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    
    if (saveFileDialog.ShowModal() == wxID_CANCEL) {
        return;
    }
    
    // TODO: 实际导出音频的逻辑
    wxString path = saveFileDialog.GetPath();
    
    // 示例仅显示消息，实际应处理并保存音频数据
    SetStatusText(wxString::Format(wxT("音频导出功能尚未实现，选定路径: %s"), path));
}

void MainFrame::OnExportPackage(wxCommandEvent& event) {
    if (m_currentSessionId.IsEmpty()) {
        wxMessageBox(wxT("没有活动的会话可以导出"), wxT("导出会议包"), wxICON_INFORMATION, this);
        return;
    }
    
    wxFileDialog saveFileDialog(this, wxT("保存会议包"), "", "",
                               wxT("会议包文件 (*.mtp)|*.mtp"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    
    if (saveFileDialog.ShowModal() == wxID_CANCEL) {
        return;
    }
    
    // TODO: 实际导出会议包的逻辑
    wxString path = saveFileDialog.GetPath();
    
    // 示例仅显示消息，实际应创建包含所有数据的会议包
    SetStatusText(wxString::Format(wxT("会议包导出功能尚未实现，选定路径: %s"), path));
}

// --- 私有辅助方法 ---
void MainFrame::LoadSessions() {
    // 清空会话树和会话列表
    m_sessionTree->DeleteAllItems();
    wxTreeItemId rootId = m_sessionTree->AddRoot(wxT("会话列表"));
    m_sessions.clear();
    
    // 先加载已移除会话列表 - 确保在此处强制重新加载
    LoadRemovedSessionsList();
    
    // 显示已移除会话列表的统计信息，用于调试
    wxLogDebug(wxT("已加载已移除会话列表，共 %d 个会话被标记为已移除"), static_cast<int>(m_removedSessions.size()));
    for (const auto& sessionName : m_removedSessions) {
        wxLogDebug(wxT("已移除的会话: %s"), sessionName);
    }
    
    // 获取会话根目录
    wxString sessionsDir = GetSessionsDirectory();
    if (sessionsDir.IsEmpty()) {
        wxLogWarning(wxT("会话根目录路径为空"));
        SetStatusText(wxT("无法加载会话列表"));
        return;
    }
    
    // 确认会话根目录是否存在，如果不存在则尝试创建
    if (!wxDir::Exists(sessionsDir)) {
        wxLogInfo(wxT("会话根目录不存在，尝试创建: %s"), sessionsDir);
        if (!wxDir::Make(sessionsDir, wxS_DIR_DEFAULT)) {
            wxLogError(wxT("无法创建会话根目录: %s"), sessionsDir);
            SetStatusText(wxT("无法创建会话根目录"));
            return;
        }
    }
    
    // 尝试列出根目录内容
    wxDir dir;
    bool opened = false;
    try {
        opened = dir.Open(sessionsDir);
    } catch (const std::exception& e) {
        wxLogError(wxT("打开会话目录时发生异常: %s - %s"), sessionsDir, wxString(e.what()));
    } catch (...) {
        wxLogError(wxT("打开会话目录时发生未知异常: %s"), sessionsDir);
    }
    
    if (!opened) {
        wxLogWarning(wxT("无法打开会话目录: %s (可能是权限问题)"), sessionsDir);
        SetStatusText(wxT("会话目录访问受限"));
        return;
    }
    
    // 遍历会话目录
    wxString sessionDirName;
    bool cont = false;
    
    try {
        cont = dir.GetFirst(&sessionDirName, wxEmptyString, wxDIR_DIRS);
    } catch (const std::exception& e) {
        wxLogError(wxT("获取目录列表时发生异常: %s"), wxString(e.what()));
        SetStatusText(wxT("获取会话列表失败"));
        return;
    }
    
    while (cont) {
        // 检查此会话是否在已移除列表中 - 添加更多日志
        wxLogDebug(wxT("发现会话目录: %s"), sessionDirName);
        if (IsSessionRemoved(sessionDirName)) {
            // 跳过已移除的会话
            wxLogDebug(wxT("跳过已移除的会话: %s"), sessionDirName);
            try {
                cont = dir.GetNext(&sessionDirName);
            } catch (...) {
                wxLogError(wxT("获取下一个目录时发生异常"));
                break;
            }
            continue;
        } else {
            wxLogDebug(wxT("会话未被标记为已移除，将加载: %s"), sessionDirName);
        }
        
        wxString sessionPath = wxFileName(sessionsDir, sessionDirName).GetFullPath();
        
        // 创建会话项
        SessionItem session;
        session.name = sessionDirName;
        session.path = sessionPath;
        session.creationTime = wxDateTime::Now(); // 默认使用当前时间
        session.isActive = false;
        
        // 无论是否能获取到创建时间，我们都加载这个会话
        m_sessions.push_back(session);
        
        // 添加到会话树
        AddSessionToTree(session.name, session.path, session.creationTime, session.isActive);
        
        // 获取下一个会话目录
        try {
            cont = dir.GetNext(&sessionDirName);
        } catch (...) {
            wxLogError(wxT("获取下一个目录时发生异常"));
            break;
        }
    }
    
    // 确保树展开
    m_sessionTree->ExpandAll();
    
    int loadedCount = static_cast<int>(m_sessions.size());
    int excludedCount = static_cast<int>(m_removedSessions.size());
    
    if (excludedCount > 0) {
        SetStatusText(wxString::Format(wxT("已加载 %d 个会话 (忽略了 %d 个已移除的会话)"), 
                                     loadedCount, excludedCount));
    } else {
        SetStatusText(wxString::Format(wxT("已加载 %d 个会话"), loadedCount));
    }
}

void MainFrame::OnRemoveSession(wxCommandEvent& event) {
    // 检查是否有选中的会话
    if (m_currentSessionId.IsEmpty()) {
        wxMessageBox(wxT("请先选择一个要移除的会话"), wxT("提示"), wxICON_INFORMATION, this);
        return;
    }
    
    // 获取当前选中的会话名称
    wxString sessionName = m_currentSessionId;
    
    // 确认是否要移除
    wxMessageDialog confirmDlg(
        this,
        wxString::Format(wxT("确定要从列表中移除会话\"%s\"吗？\n\n注意：会话文件夹不会被删除，您可以稍后重新添加此会话。"), sessionName),
        wxT("确认移除"),
        wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION
    );
    
    if (confirmDlg.ShowModal() != wxID_YES) {
        return;  // 用户取消
    }
    
    // 在会话列表中查找并移除
    auto it = std::find_if(m_sessions.begin(), m_sessions.end(),
                           [&](const SessionItem& session) { return session.name == sessionName; });
    
    if (it != m_sessions.end()) {
        // 保存会话路径以供显示
        wxString sessionPath = it->path;
        
        // 从列表中移除
        m_sessions.erase(it);
        
        // 将会话添加到已移除列表
        if (!IsSessionRemoved(sessionName)) {
            m_removedSessions.push_back(sessionName);
            SaveRemovedSessionsList();
        }
        
        // 清空当前会话信息
        m_currentSessionId = wxEmptyString;
        m_currentSessionPath = wxEmptyString;
        
        // 刷新会话树
        RefreshSessionTree();
        
        // 清空编辑区和批注
        m_transcriptionBubbleCtrl->Clear();
        if (m_annotationManager) {
            m_annotationManager->ClearAnnotations();
        }
        // 清空批注树和书签树
        if (m_annotationTree) {
            m_annotationTree->DeleteAllItems();
            m_annotationTree->AddRoot(wxT("批注列表"));
        }
        if (m_bookmarkTree) {
            m_bookmarkTree->DeleteAllItems();
            m_bookmarkTree->AddRoot(wxT("书签列表"));
        }
        
        // 更新状态栏
        SetStatusText(wxString::Format(wxT("已从列表中移除会话: %s (路径: %s)"), sessionName, sessionPath));
    } else {
        wxMessageBox(wxT("找不到指定的会话"), wxT("错误"), wxICON_ERROR, this);
    }
}

void MainFrame::OnAddExistingSession(wxCommandEvent& event) {
    // 获取会话根目录
    wxString sessionsDir = GetSessionsDirectory();
    if (sessionsDir.IsEmpty() || !wxDir::Exists(sessionsDir)) {
        wxMessageBox(wxT("无法访问会话根目录"), wxT("错误"), wxICON_ERROR, this);
        return;
    }
    
    // 创建目录选择对话框，从会话根目录开始
    wxDirDialog dlg(this, wxT("选择要添加的会话目录"), sessionsDir, 
                   wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    
    if (dlg.ShowModal() != wxID_OK) {
        return;  // 用户取消
    }
    
    wxString selectedPath = dlg.GetPath();
    
    // 检查所选目录是否在会话根目录内
    if (!selectedPath.StartsWith(sessionsDir)) {
        wxMessageBox(wxT("所选目录必须在会话目录内"), wxT("错误"), wxICON_ERROR, this);
        return;
    }
    
    // 获取会话名（目录名）
    wxFileName dirInfo(selectedPath);
    wxString sessionName = dirInfo.GetDirs().Last();
    
    // 检查该会话是否已在列表中
    for (const auto& session : m_sessions) {
        if (session.name == sessionName) {
            wxMessageBox(
                wxString::Format(wxT("会话\"%s\"已在列表中"), sessionName),
                wxT("提示"),
                wxICON_INFORMATION,
                this
            );
            return;
        }
    }
    
    // 检查目录是否是有效的会话目录（这里可以添加更多验证）
    if (!dirInfo.DirExists()) {
        wxMessageBox(wxT("所选目录不存在"), wxT("错误"), wxICON_ERROR, this);
        return;
    }
    
    // 检查访问权限
    if (!wxFileName::IsDirReadable(selectedPath)) {
        wxMessageBox(
            wxString::Format(wxT("没有权限访问目录: %s\n请检查目录权限设置。"), selectedPath),
            wxT("权限错误"),
            wxICON_ERROR,
            this
        );
        return;
    }
    
    // 创建新会话对象并添加到列表中
    SessionItem newSession;
    newSession.name = sessionName;
    newSession.path = selectedPath;
    
    // 尝试获取目录修改时间，失败则使用当前时间
    wxDateTime modTime;
    if (dirInfo.GetTimes(NULL, &modTime, NULL)) {
        newSession.creationTime = modTime;
    } else {
        newSession.creationTime = wxDateTime::Now();
        wxLogWarning(wxT("无法获取目录修改时间，使用当前时间: %s"), selectedPath);
    }
    
    newSession.isActive = false;
    
    // 添加到会话列表
    m_sessions.push_back(newSession);
    
    // 如果此会话曾被移除，从已移除列表中删除它
    auto it = std::find(m_removedSessions.begin(), m_removedSessions.end(), sessionName);
    if (it != m_removedSessions.end()) {
        m_removedSessions.erase(it);
        SaveRemovedSessionsList();
    }
    
    // 刷新会话树
    RefreshSessionTree();
    
    // 自动选择新添加的会话
    wxTreeItemId root = m_sessionTree->GetRootItem();
    wxTreeItemIdValue cookie;
    wxTreeItemId firstChild = m_sessionTree->GetFirstChild(root, cookie);
    
    if (firstChild.IsOk()) {
        bool found = false;
        wxTreeItemId currentItem = firstChild;
        
        // 查找并选择新添加的会话
        do {
            wxString itemText = m_sessionTree->GetItemText(currentItem);
            if (itemText == sessionName) {
                m_sessionTree->SelectItem(currentItem);
                found = true;
                break;
            }
            
            currentItem = m_sessionTree->GetNextSibling(currentItem);
        } while (currentItem.IsOk());
        
        // 如果没找到，至少选择第一个
        if (!found && firstChild.IsOk()) {
            m_sessionTree->SelectItem(firstChild);
        }
    }
    
    SetStatusText(wxString::Format(wxT("已添加现有会话: %s"), sessionName));
}

void MainFrame::RefreshSessionTree() {
    // 清空会话树
    m_sessionTree->DeleteAllItems();
    wxTreeItemId rootId = m_sessionTree->AddRoot(wxT("会话列表"));
    
    // 重新添加所有会话到树中
    for (const auto& session : m_sessions) {
        AddSessionToTree(session.name, session.path, session.creationTime, session.isActive);
    }
    
    // 展开树
    m_sessionTree->ExpandAll();
}

void MainFrame::AddSessionToTree(const wxString& name, const wxString& path, const wxDateTime& creationTime, bool isActive) {
    wxTreeItemId rootId = m_sessionTree->GetRootItem();
    if (!rootId.IsOk()) {
        return;
    }
    
    // 格式化显示文本
    wxString displayText;
    if (isActive) {
        displayText = wxString::Format(wxT("* %s"), name); // 活动会话前加星号
    } else {
        displayText = name;
    }
    
    // 添加到树
    wxTreeItemId itemId = m_sessionTree->AppendItem(rootId, displayText);
    
    // 选择第一个会话（如果树为空）
    if (m_sessionTree->GetChildrenCount(rootId) == 1) {
        m_sessionTree->SelectItem(itemId);
    }
}

// 实现会话树右键菜单处理函数
void MainFrame::OnSessionTreeContextMenu(wxTreeEvent& event) {
    wxTreeItemId item = event.GetItem();
    if (!item.IsOk() || item == m_sessionTree->GetRootItem()) {
        return;
    }
    
    // 当前选中的会话名称
    wxString sessionName = m_sessionTree->GetItemText(item);
    
    // 如果会话名称前面有星号（表示活动会话），去掉星号和空格
    if (sessionName.StartsWith(wxT("* "))) {
        sessionName = sessionName.Mid(2);
    }
    
    // 创建右键菜单
    wxMenu contextMenu;
    contextMenu.Append(ID_SessionTreeContext_Remove, wxT("从列表中移除"));
    
    // 绑定菜单事件
    contextMenu.Bind(wxEVT_COMMAND_MENU_SELECTED, 
        [this, sessionName](wxCommandEvent& event) {
            // 设置当前会话为右键点击的会话
            m_currentSessionId = sessionName;
            
            // 转发到移除会话处理函数
            wxCommandEvent removeEvent(wxEVT_COMMAND_MENU_SELECTED, ID_Menu_Remove_Session);
            this->OnRemoveSession(removeEvent);
        }, 
        ID_SessionTreeContext_Remove);
    
    // 直接使用鼠标的当前位置，这是最可靠的方法
    wxPoint mousePos = wxGetMousePosition();
    wxPoint clientPos = m_sessionTree->ScreenToClient(mousePos);
    
    // 显示右键菜单
    m_sessionTree->PopupMenu(&contextMenu, clientPos);
}

// 实现FunASR初始化函数
bool MainFrame::InitializeFunASR() {
    // 这是一个简单的模拟实现，实际应用中需要真正初始化语音识别引擎
    wxLogMessage(wxT("初始化FunASR语音识别引擎..."));
    
    // 假设配置成功
    m_isFunASRInitialized = true;
    m_asrHandle = (void*)1; // 仅作为非空指针的标记
    
    SetStatusText(wxT("语音识别引擎初始化完成"));
    return true;
}

// 实现切换批注显示函数
void MainFrame::ToggleAnnotationsDisplay() {
    m_showAnnotations = !m_showAnnotations;
    
    if (m_showAnnotations) {
        // 显示批注面板
        if (m_editorAnnotationSplitter->IsSplit()) {
            // 已经分割，不需要操作
        } else {
            // 需要重新分割
            m_editorAnnotationSplitter->SplitVertically(m_editorPanel, m_annotationPanel, -200);
        }
        SetStatusText(wxT("显示批注面板"));
    } else {
        // 隐藏批注面板
        if (m_editorAnnotationSplitter->IsSplit()) {
            m_editorAnnotationSplitter->Unsplit(m_annotationPanel);
        }
        SetStatusText(wxT("隐藏批注面板"));
    }
    
    // 更新工具栏按钮状态
    if (m_editorToolbar) {
        m_editorToolbar->ToggleTool(ID_Toolbar_Toggle_Annotations, m_showAnnotations);
    }
}

// 实现同步批注滚动位置函数
void MainFrame::SyncAnnotationScrollPosition(wxScrollWinEvent& event) {
    // 获取主编辑器的滚动位置
    int pos = m_transcriptionBubbleCtrl->GetScrollPos(wxVERTICAL);
    
    // 设置批注面板的滚动位置
    if (m_annotationTextCtrl) {
        m_annotationTextCtrl->SetScrollPos(wxVERTICAL, pos);
    }
    
    // 继续处理原始事件
    event.Skip();
}

// 实现刷新批注函数
void MainFrame::RefreshAnnotations() {
    if (!m_annotationManager || m_currentSessionId.IsEmpty() || !m_annotationTextCtrl) {
        return;
    }
    
    // 清空批注控件
    m_annotationTextCtrl->Clear();
    
    // 获取当前会话的所有批注
    std::vector<MeetAnt::Annotation*> annotations = 
        m_annotationManager->GetSessionAnnotations(m_currentSessionId);
    
    // 按时间戳排序
    std::sort(annotations.begin(), annotations.end(), 
        [](MeetAnt::Annotation* a, MeetAnt::Annotation* b) {
            return a->GetTimestamp() < b->GetTimestamp();
        });
    
    // 添加批注到显示区域
    for (auto* annotation : annotations) {
        // 添加时间戳
        MeetAnt::TimeStamp ts = annotation->GetTimestamp();
        wxString timeStr = wxString::Format(wxT("[%02d:%02d:%02d] "), 
                                           ts / 3600000, 
                                           (ts % 3600000) / 60000, 
                                           (ts % 60000) / 1000);
        
        wxRichTextAttr timeAttr;
        timeAttr.SetTextColour(wxColour(100, 100, 100));
        timeAttr.SetFontWeight(wxFONTWEIGHT_BOLD);
        
        m_annotationTextCtrl->BeginStyle(timeAttr);
        m_annotationTextCtrl->AppendText(timeStr);
        m_annotationTextCtrl->EndStyle();
        
        // 根据不同类型的批注添加不同的内容
        switch (annotation->GetType()) {
            case MeetAnt::AnnotationType::Note:
            {
                auto* note = static_cast<MeetAnt::NoteAnnotation*>(annotation);
                
                wxRichTextAttr titleAttr;
                titleAttr.SetTextColour(wxColour(0, 0, 150));
                titleAttr.SetFontWeight(wxFONTWEIGHT_BOLD);
                
                m_annotationTextCtrl->BeginStyle(titleAttr);
                m_annotationTextCtrl->AppendText(note->GetTitle() + wxT("\n"));
                m_annotationTextCtrl->EndStyle();
                
                m_annotationTextCtrl->AppendText(note->GetContent() + wxT("\n\n"));
                break;
            }
            case MeetAnt::AnnotationType::Bookmark:
            {
                auto* bookmark = static_cast<MeetAnt::BookmarkAnnotation*>(annotation);
                
                wxRichTextAttr labelAttr;
                labelAttr.SetTextColour(wxColour(150, 0, 0));
                labelAttr.SetFontWeight(wxFONTWEIGHT_BOLD);
                
                m_annotationTextCtrl->BeginStyle(labelAttr);
                m_annotationTextCtrl->AppendText(wxT("书签: ") + bookmark->GetLabel() + wxT("\n"));
                m_annotationTextCtrl->EndStyle();
                
                if (!bookmark->GetContent().IsEmpty()) {
                    m_annotationTextCtrl->AppendText(bookmark->GetContent() + wxT("\n"));
                }
                
                m_annotationTextCtrl->AppendText(wxT("\n"));
                break;
            }
            case MeetAnt::AnnotationType::Highlight:
            {
                auto* highlight = static_cast<MeetAnt::HighlightAnnotation*>(annotation);
                
                wxRichTextAttr highlightAttr;
                wxColour color = highlight->GetColor();
                highlightAttr.SetTextColour(color);
                highlightAttr.SetBackgroundColour(color.ChangeLightness(180));
                
                m_annotationTextCtrl->BeginStyle(highlightAttr);
                m_annotationTextCtrl->AppendText(wxT("高亮: ") + highlight->GetContent() + wxT("\n\n"));
                m_annotationTextCtrl->EndStyle();
                break;
            }
        }
    }
    
    // 自动滚动到顶部
}

// 实现保存当前会话函数
void MainFrame::SaveCurrentSession() {
    if (m_currentSessionPath.IsEmpty()) {
        wxLogWarning(wxT("没有活动的会话可以保存"));
        return;
    }
    
    // 保存转录文本
    wxString textContent = m_transcriptionBubbleCtrl->ExportAsText();
    wxString textFilePath = wxFileName(m_currentSessionPath, wxT("transcript.txt")).GetFullPath();
    
    wxFile textFile;
    if (!textFile.Open(textFilePath, wxFile::write)) {
        wxLogError(wxT("无法打开转录文件进行写入: %s"), textFilePath);
        return;
    }
    
    if (!textFile.Write(textContent)) {
        wxLogError(wxT("写入转录文件失败: %s"), textFilePath);
    }
    
    textFile.Close();
    
    // 保存批注
    if (m_annotationManager) {
        m_annotationManager->SaveAnnotations(m_currentSessionPath);
    }
    
    // 保存会话信息文件（可选）
    wxString infoFilePath = wxFileName(m_currentSessionPath, wxT("session.info")).GetFullPath();
    wxFile infoFile;
    if (!infoFile.Open(infoFilePath, wxFile::write)) {
        wxLogWarning(wxT("无法打开会话信息文件进行写入: %s"), infoFilePath);
    } else {
        wxDateTime now = wxDateTime::Now();
        wxString infoContent = wxString::Format(
            wxT("Session-Name: %s\nLast-Modified: %s\n"),
            m_currentSessionId,
            now.FormatISODate() + wxT(" ") + now.FormatISOTime()
        );
        
        if (!infoFile.Write(infoContent)) {
            wxLogWarning(wxT("写入会话信息文件失败: %s"), infoFilePath);
        }
        
        infoFile.Close();
    }
    
    SetStatusText(wxString::Format(wxT("会话已保存: %s"), m_currentSessionId));
}

// 实现创建会话目录函数
wxString MainFrame::CreateSessionDirectory(const wxString& sessionName) {
    // 获取会话根目录
    wxString sessionsDir = GetSessionsDirectory();
    if (sessionsDir.IsEmpty()) {
        wxLogError(wxT("无法获取会话根目录"));
        return wxEmptyString;
    }
    
    // 确保会话根目录存在
    if (!wxDir::Exists(sessionsDir)) {
        wxLogInfo(wxT("会话根目录不存在，尝试创建: %s"), sessionsDir);
        bool created = false;
        try {
            created = wxDir::Make(sessionsDir, wxS_DIR_DEFAULT);
        } catch (...) {
            wxLogError(wxT("创建会话根目录时发生异常: %s"), sessionsDir);
            return wxEmptyString;
        }
        
        if (!created) {
            wxLogError(wxT("无法创建会话根目录: %s (可能是权限问题)"), sessionsDir);
            return wxEmptyString;
        }
    }
    
    // 构建新会话的目录路径
    wxString sessionDirPath = wxFileName(sessionsDir, sessionName).GetFullPath();
    
    // 检查目录是否已存在
    bool dirExists = false;
    try {
        dirExists = wxDir::Exists(sessionDirPath);
    } catch (...) {
        wxLogWarning(wxT("检查会话目录是否存在时发生异常: %s"), sessionDirPath);
        // 我们将继续尝试创建目录
    }
    
    if (dirExists) {
        // 目录已存在 - 不需要尝试进行读写测试，这可能导致权限错误
        // 我们假设目录可用，由后续操作来处理可能的权限问题
        wxLogInfo(wxT("会话目录已存在: %s"), sessionDirPath);
        return sessionDirPath;
    } else {
        // 创建新目录
        bool created = false;
        try {
            created = wxDir::Make(sessionDirPath, wxS_DIR_DEFAULT);
        } catch (...) {
            wxLogError(wxT("创建会话目录时发生异常: %s"), sessionDirPath);
            return wxEmptyString;
        }
        
        if (!created) {
            wxLogError(wxT("无法创建会话目录: %s (可能是权限问题)"), sessionDirPath);
            return wxEmptyString;
        }
        
        wxLogInfo(wxT("成功创建会话目录: %s"), sessionDirPath);
        return sessionDirPath;
    }
}

// ==================== 音频录制功能实现 ====================

// 加载音频配置
bool MainFrame::LoadAudioConfig() {
    // 使用与ConfigDialog相同的配置文件路径
    wxString configPath;
    
#ifdef __WXMSW__
    // Windows 平台：使用 %USERPROFILE%\MeetAntConfig
    configPath = wxGetHomeDir() + wxT("\\MeetAntConfig");
#else
    // Linux/macOS 平台：使用 ~/.MeetAntConfig
    configPath = wxGetHomeDir() + wxT("/.MeetAntConfig");
#endif
    
    wxString configFilePath = configPath + wxFileName::GetPathSeparator() + wxT("config.json");
    
    if (!wxFile::Exists(configFilePath)) {
        // 使用默认配置
        m_sampleRate = 16000;
        m_audioDeviceIndex = Pa_GetDefaultInputDevice();
        m_silenceThreshold = 0.05f;
        m_systemAudioMode = false; // 默认使用麦克风录制
        m_captureType = 0; // WASAPI
        wxLogInfo(wxT("配置文件不存在，使用默认配置: %s"), configFilePath);
        return true;
    }
    
    try {
        wxFileInputStream input(configFilePath);
        if (!input.IsOk()) {
            wxLogWarning(wxT("无法打开配置文件: %s"), configFilePath);
            return false;
        }
        
        wxString jsonStr;
        char buffer[1024];
        while (!input.Eof()) {
            input.Read(buffer, sizeof(buffer));
            size_t bytesRead = input.LastRead();
            jsonStr += wxString(buffer, wxConvUTF8, bytesRead);
        }
        
        wxLogInfo(wxT("从配置文件加载: %s"), configFilePath);
        wxLogInfo(wxT("配置文件完整内容: %s"), jsonStr); // 显示完整内容用于调试
        
        // 简化的JSON解析 - 查找audio对象中的字段
        // 由于MainFrame不包含nlohmann/json，我们继续使用正则表达式，但修复匹配逻辑
        
        // 首先提取整个audio对象
        size_t audioStart = jsonStr.Find(wxT("\"audio\""));
        if (audioStart == wxNOT_FOUND) {
            wxLogWarning(wxT("未找到音频配置段"));
            // 使用默认配置
            m_sampleRate = 16000;
            m_audioDeviceIndex = Pa_GetDefaultInputDevice();
            m_silenceThreshold = 0.05f;
            m_systemAudioMode = false; // 默认使用麦克风录制
            m_captureType = 0;
            return true;
        }
        
        // 找到audio对象的开始和结束
        size_t braceStart = jsonStr.find(wxT("{"), audioStart);
        if (braceStart == wxNOT_FOUND) {
            wxLogWarning(wxT("音频配置格式错误"));
            return false;
        }
        
        // 简单的大括号匹配来找到audio对象的结束
        int braceCount = 1;
        size_t braceEnd = braceStart + 1;
        while (braceEnd < jsonStr.length() && braceCount > 0) {
            if (jsonStr[braceEnd] == wxT('{')) {
                braceCount++;
            } else if (jsonStr[braceEnd] == wxT('}')) {
                braceCount--;
            }
            braceEnd++;
        }
        
        wxString audioSection = jsonStr.Mid(braceStart, braceEnd - braceStart);
        wxLogInfo(wxT("提取的音频配置段: %s"), audioSection);
        
        // 解析各个字段
        wxRegEx deviceRegex(wxT("\"device\"\\s*:\\s*(\\d+)"));
        if (deviceRegex.Matches(audioSection)) {
            long deviceIndex;
            if (deviceRegex.GetMatch(audioSection, 1).ToLong(&deviceIndex)) {
                m_audioDeviceIndex = static_cast<int>(deviceIndex);
                wxLogInfo(wxT("加载设备索引: %d"), m_audioDeviceIndex);
            }
        }
        
        wxRegEx sampleRateRegex(wxT("\"sampleRate\"\\s*:\\s*(\\d+)"));
        if (sampleRateRegex.Matches(audioSection)) {
            long sampleRate;
            if (sampleRateRegex.GetMatch(audioSection, 1).ToLong(&sampleRate)) {
                m_sampleRate = static_cast<int>(sampleRate);
                wxLogInfo(wxT("加载采样率: %d"), m_sampleRate);
            }
        }
        
        wxRegEx systemAudioRegex(wxT("\"systemAudioEnabled\"\\s*:\\s*(true|false)"));
        if (systemAudioRegex.Matches(audioSection)) {
            wxString value = systemAudioRegex.GetMatch(audioSection, 1);
            m_systemAudioMode = (value == wxT("true"));
            wxLogInfo(wxT("从配置文件加载系统音频模式: %s"), m_systemAudioMode ? wxT("true") : wxT("false"));
        } else {
            wxLogWarning(wxT("未找到systemAudioEnabled字段，使用默认值false"));
            m_systemAudioMode = false; // 默认使用麦克风录制
        }
        
        wxRegEx captureRegex(wxT("\"captureType\"\\s*:\\s*(\\d+)"));
        if (captureRegex.Matches(audioSection)) {
            long captureType;
            if (captureRegex.GetMatch(audioSection, 1).ToLong(&captureType)) {
                m_captureType = static_cast<int>(captureType);
                wxLogInfo(wxT("加载捕获类型: %d"), m_captureType);
            }
        }
        
        wxRegEx thresholdRegex(wxT("\"silenceThreshold\"\\s*:\\s*([0-9]*\\.?[0-9]+)"));
        if (thresholdRegex.Matches(audioSection)) {
            double threshold;
            if (thresholdRegex.GetMatch(audioSection, 1).ToDouble(&threshold)) {
                m_silenceThreshold = static_cast<float>(threshold);
                wxLogInfo(wxT("加载静音阈值: %f"), m_silenceThreshold);
            }
        }
        
        wxLogInfo(wxT("配置加载完成 - 系统音频: %s, 设备: %d, 采样率: %d"), 
                 m_systemAudioMode ? wxT("启用") : wxT("禁用"), 
                 m_audioDeviceIndex, 
                 m_sampleRate);
        
        return true;
    } catch (...) {
        wxLogError(wxT("解析配置文件时发生异常"));
        // 使用默认配置
        m_sampleRate = 16000;
        m_audioDeviceIndex = Pa_GetDefaultInputDevice();
        m_silenceThreshold = 0.05f;
        m_systemAudioMode = false; // 默认使用麦克风录制
        m_captureType = 0;
        return true;
    }
}

// 初始化PortAudio
void MainFrame::InitializePortAudio() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        wxLogError(wxT("PortAudio初始化失败: %s"), wxString(Pa_GetErrorText(err), wxConvUTF8));
        return;
    }
    
#ifdef _WIN32
    // 检查COM是否已经初始化，而不是强制重新初始化
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        m_comInitialized = true;
        wxLogInfo(wxT("COM初始化成功"));
    } else if (hr == RPC_E_CHANGED_MODE) {
        // COM已经在不同模式下初始化，这是正常的
        m_comInitialized = false; // 我们没有初始化它，所以不应该清理它
        wxLogInfo(wxT("COM已在其他模式下初始化，继续使用现有初始化"));
    } else if (hr == S_FALSE) {
        // COM已经初始化，这是正常的
        m_comInitialized = false; // 我们没有初始化它，所以不应该清理它
        wxLogInfo(wxT("COM已经初始化，继续使用现有初始化"));
    } else {
        wxLogWarning(wxT("COM初始化失败: hr = 0x%08lx"), hr);
        m_comInitialized = false;
    }
#endif
    
    wxLogInfo(wxT("PortAudio初始化成功"));
}

// 关闭PortAudio
void MainFrame::ShutdownPortAudio() {
    // 停止录制
    if (m_isRecording) {
        StopAudioCapture();
    }
    
#ifdef _WIN32
    // 清理WASAPI资源
    ShutdownDirectWASAPILoopback();
    
    // 清理COM
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
#endif
    
    // 清理PortAudio
    Pa_Terminate();
    
    // 清理音频缓冲区
    if (m_audioBuffer) {
        delete[] m_audioBuffer;
        m_audioBuffer = nullptr;
    }
    
    // 清理定时器
    if (m_recordingTimer) {
        if (m_recordingTimer->IsRunning()) {
            m_recordingTimer->Stop();
        }
        delete m_recordingTimer;
        m_recordingTimer = nullptr;
    }
}

// PortAudio回调函数
static int PortAudioCallback(const void* inputBuffer, void* outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void* userData) {
    MainFrame* mainFrame = static_cast<MainFrame*>(userData);
    const float* input = static_cast<const float*>(inputBuffer);
    
    if (input && mainFrame) {
        mainFrame->OnAudioDataReceived(input, framesPerBuffer);
    }
    
    return paContinue;
}

// 初始化音频输入
bool MainFrame::InitializeAudioInput() {
    if (m_isAudioInitialized) {
        return true;
    }
    
    // 分配音频缓冲区
    if (!m_audioBuffer) {
        m_audioBuffer = new float[AUDIO_BUFFER_SIZE];
    }
    
    wxLogInfo(wxT("初始化音频输入 - 系统音频: %s, 设备: %d, 捕获类型: %d"), 
             m_systemAudioMode ? wxT("启用") : wxT("禁用"), 
             m_audioDeviceIndex, 
             m_captureType);
    
    if (m_systemAudioMode) {
#ifdef _WIN32
        if (m_captureType == 0) { // WASAPI - 使用Direct WASAPI
            wxLogInfo(wxT("使用Direct WASAPI Loopback模式"));
            return InitializeDirectWASAPILoopback(m_audioDeviceIndex, m_sampleRate);
        } else { // WDMKS或其他 - 使用PortAudio
            wxLogInfo(wxT("使用PortAudio WDMKS模式"));
            return InitializePortAudioCapture(true);
        }
#else
        wxMessageBox(wxT("系统内录功能仅在Windows上支持"), wxT("功能不支持"), wxOK | wxICON_WARNING, this);
        return false;
#endif
    } else {
        // 麦克风录制 - 使用PortAudio
        wxLogInfo(wxT("使用PortAudio麦克风模式"));
        return InitializePortAudioCapture(false);
    }
}

// 初始化PortAudio捕获
bool MainFrame::InitializePortAudioCapture(bool systemAudio) {
    PaStreamParameters inputParameters;
    PaError err;
    
    // 设置设备索引
    if (systemAudio) {
        // 系统内录模式：m_audioDeviceIndex 实际上是选定的输出设备索引（用于loopback）
        inputParameters.device = m_audioDeviceIndex;
        wxLogInfo(wxT("系统内录模式：使用输出设备索引 %d 进行loopback录制"), m_audioDeviceIndex);
    } else {
        // 麦克风模式：使用输入设备
        if (m_audioDeviceIndex < 0 || m_audioDeviceIndex >= Pa_GetDeviceCount()) {
            inputParameters.device = Pa_GetDefaultInputDevice();
        } else {
            inputParameters.device = m_audioDeviceIndex;
        }
        wxLogInfo(wxT("麦克风模式：使用输入设备索引 %d"), inputParameters.device);
    }
    
    if (inputParameters.device == paNoDevice) {
        wxLogError(wxT("未找到可用的音频设备"));
        return false;
    }
    
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(inputParameters.device);
    if (!deviceInfo) {
        wxLogError(wxT("无法获取音频设备信息"));
        return false;
    }
    
    const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
    wxLogInfo(wxT("选中设备: %s [%s], 最大输入通道: %d, 最大输出通道: %d"), 
             wxString::FromUTF8(deviceInfo->name),
             wxString::FromUTF8(hostApiInfo->name),
             deviceInfo->maxInputChannels,
             deviceInfo->maxOutputChannels);
    
    // 设置声道数
    if (systemAudio) {
        // 系统内录模式：基于输出设备的输出声道数
        inputParameters.channelCount = (deviceInfo->maxOutputChannels == 1) ? 1 : 2;
        wxLogInfo(wxT("系统内录模式：根据输出设备的输出声道数设置为 %d 声道"), inputParameters.channelCount);
    } else {
        // 麦克风模式：基于输入设备的输入声道数
        inputParameters.channelCount = (deviceInfo->maxInputChannels >= 1) ? 1 : 2;
        wxLogInfo(wxT("麦克风模式：根据输入设备的输入声道数设置为 %d 声道"), inputParameters.channelCount);
    }
    
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = deviceInfo->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;
    
    // WASAPI loopback 特定处理
#ifdef _WIN32
    PaWasapiStreamInfo wasapiInfo;
    if (systemAudio && hostApiInfo->type == paWASAPI) {
        // 设置WASAPI loopback参数
        memset(&wasapiInfo, 0, sizeof(PaWasapiStreamInfo));
        wasapiInfo.size = sizeof(PaWasapiStreamInfo);
        wasapiInfo.hostApiType = paWASAPI;
        wasapiInfo.version = 1;
        wasapiInfo.flags = AUDCLNT_STREAMFLAGS_LOOPBACK;
        wasapiInfo.streamCategory = 0;
        wasapiInfo.hDuration = NULL;
        inputParameters.hostApiSpecificStreamInfo = &wasapiInfo;
        
        wxLogInfo(wxT("为WASAPI输出设备 %d 设置了loopback标志"), inputParameters.device);
        
        // 检查格式支持
        PaStreamParameters testParams = inputParameters;
        testParams.channelCount = 2;
        PaError formatErr = Pa_IsFormatSupported(&testParams, nullptr, m_sampleRate);
        if (formatErr == paFormatIsSupported) {
            wxLogInfo(wxT("设备支持2声道 %d Hz loopback"), m_sampleRate);
        } else {
            wxLogWarning(wxT("设备不支持2声道 %d Hz loopback，错误: %s"), 
                        m_sampleRate, wxString::FromUTF8(Pa_GetErrorText(formatErr)));
            testParams.channelCount = 1;
            formatErr = Pa_IsFormatSupported(&testParams, nullptr, m_sampleRate);
            if (formatErr == paFormatIsSupported) {
                wxLogInfo(wxT("设备支持1声道 %d Hz loopback"), m_sampleRate);
                inputParameters.channelCount = 1;
            } else {
                wxLogError(wxT("设备不支持1声道 %d Hz loopback，错误: %s"), 
                          m_sampleRate, wxString::FromUTF8(Pa_GetErrorText(formatErr)));
            }
        }
    } else if (systemAudio && hostApiInfo->type != paWASAPI) {
        wxLogError(wxT("系统内录模式要求WASAPI设备，但选定设备是 %s"), 
                  wxString::FromUTF8(hostApiInfo->name));
        return false;
    }
#endif
    
    // 打开音频流
    err = Pa_OpenStream(&m_paStream,
                       &inputParameters,
                       nullptr, // 无输出
                       m_sampleRate,
                       AUDIO_BUFFER_SIZE,
                       paClipOff,
                       PortAudioCallback,
                       this);
    
    // 如果声道数错误，尝试其他声道数
    if (err == paInvalidChannelCount && systemAudio && inputParameters.channelCount == 2) {
        wxLogWarning(wxT("2声道失败，尝试1声道"));
        inputParameters.channelCount = 1;
        err = Pa_OpenStream(&m_paStream,
                           &inputParameters,
                           nullptr,
                           m_sampleRate,
                           AUDIO_BUFFER_SIZE,
                           paClipOff,
                           PortAudioCallback,
                           this);
    }
    
    if (err != paNoError) {
        wxLogError(wxT("打开PortAudio流失败: %s"), wxString(Pa_GetErrorText(err), wxConvUTF8));
        wxLogError(wxT("参数: 设备=%d, 声道=%d, 采样率=%d, hostApiSpecificStreamInfo=%p"), 
                  inputParameters.device, inputParameters.channelCount, m_sampleRate, 
                  inputParameters.hostApiSpecificStreamInfo);
        return false;
    }
    
    wxLogInfo(wxT("PortAudio流初始化成功，设备: %s, 声道: %d, 采样率: %d Hz"), 
             wxString::FromUTF8(deviceInfo->name), inputParameters.channelCount, m_sampleRate);
    
    m_isAudioInitialized = true;
    return true;
}

#ifdef _WIN32
// 初始化直接WASAPI循环录制
bool MainFrame::InitializeDirectWASAPILoopback(int paOutputDeviceIndex, int requestedSampleRate) {
    HRESULT hr;
    wxLogInfo(wxT("InitializeDirectWASAPILoopback: 开始初始化设备索引 %d, 采样率 %dHz"), paOutputDeviceIndex, requestedSampleRate);

    // 0. 获取PortAudio设备信息以找到设备名称
    if (paOutputDeviceIndex < 0 || paOutputDeviceIndex >= Pa_GetDeviceCount()) {
        wxLogError(wxT("DirectWASAPI: 无效的PortAudio输出设备索引: %d"), paOutputDeviceIndex);
        return false;
    }
    const PaDeviceInfo* paSelectedDevInfo = Pa_GetDeviceInfo(paOutputDeviceIndex);
    if (!paSelectedDevInfo) {
        wxLogError(wxT("DirectWASAPI: 无法获取设备索引 %d 的PaDeviceInfo"), paOutputDeviceIndex);
        return false;
    }
    
    // 从PortAudio名称中提取基础设备名称
    // ConfigDialog中的PopulateLoopbackDevices添加了" [WASAPI Output]"后缀
    wxString targetDeviceFullName = wxString::FromUTF8(paSelectedDevInfo->name);
    wxString targetDeviceBaseName = targetDeviceFullName;
    
    // 移除" [WASAPI Output]"后缀（如果存在）
    if (targetDeviceBaseName.EndsWith(wxT(" [WASAPI Output]"))) {
        targetDeviceBaseName = targetDeviceBaseName.Left(targetDeviceBaseName.length() - 16); // 16 = " [WASAPI Output]".length()
    }
    // 移除其他可能的API标识符
    else {
        int bracketPos = targetDeviceBaseName.Find(wxT(" ["));
        if (bracketPos != wxNOT_FOUND) {
            targetDeviceBaseName = targetDeviceBaseName.Left(bracketPos);
        }
    }
    
    wxLogInfo(wxT("DirectWASAPI: 目标回环设备名称: '%s' (完整: '%s', PA索引: %d)"), targetDeviceBaseName, targetDeviceFullName, paOutputDeviceIndex);

    // 1. COM应该已经在InitializePortAudio中初始化了，这里不再重新初始化
    wxLogInfo(wxT("DirectWASAPI: 使用现有的COM初始化"));

    // 2. 创建设备枚举器
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&m_pEnumerator);
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: CoCreateInstance(MMDeviceEnumerator)失败: hr = 0x%08lx"), hr);
        if (m_pEnumerator) {
            m_pEnumerator->Release();
            m_pEnumerator = nullptr;
        }
        return false;
    }

    // 3. 查找对应的IMMDevice
    IMMDeviceCollection *pRenderEndpoints = nullptr;
    hr = m_pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pRenderEndpoints);
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: EnumAudioEndpoints(eRender)失败: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }

    UINT endpointCount = 0;
    pRenderEndpoints->GetCount(&endpointCount);
    wxLogInfo(wxT("DirectWASAPI: 找到 %u 个活动渲染设备"), endpointCount);
    
    bool foundDevice = false;
    IMMDevice* defaultDevice = nullptr;
    
    // 首先尝试获取默认设备作为备选
    hr = m_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    if (SUCCEEDED(hr) && defaultDevice) {
        wxLogInfo(wxT("DirectWASAPI: 获取到默认渲染设备作为备选"));
    }
    
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
                wxLogInfo(wxT("DirectWASAPI: 检查设备 %u: '%s'"), i, currentDeviceSystemName);
                
                // 尝试多种匹配方式
                bool isMatch = false;
                
                // 1. 精确匹配
                if (targetDeviceBaseName == currentDeviceSystemName) {
                    isMatch = true;
                    wxLogInfo(wxT("DirectWASAPI: 精确匹配成功"));
                }
                // 2. 包含匹配
                else if (currentDeviceSystemName.Contains(targetDeviceBaseName) || 
                         targetDeviceBaseName.Contains(currentDeviceSystemName)) {
                    isMatch = true;
                    wxLogInfo(wxT("DirectWASAPI: 包含匹配成功"));
                }
                // 3. 检查是否包含loopback关键字（用于识别回环设备）
                else if (targetDeviceBaseName.Lower().Contains(wxT("loopback")) ||
                         currentDeviceSystemName.Lower().Contains(wxT("loopback"))) {
                    isMatch = true;
                    wxLogInfo(wxT("DirectWASAPI: Loopback关键字匹配成功"));
                }
                // 4. 如果目标是"主声音驱动程序"，尝试匹配常见的默认设备名称
                else if (targetDeviceBaseName == wxT("主声音驱动程序")) {
                    if (currentDeviceSystemName.Contains(wxT("扬声器")) || 
                        currentDeviceSystemName.Contains(wxT("Speakers")) ||
                        currentDeviceSystemName.Contains(wxT("耳机")) ||
                        currentDeviceSystemName.Contains(wxT("Headphones")) ||
                        currentDeviceSystemName.Contains(wxT("默认")) ||
                        currentDeviceSystemName.Contains(wxT("Default"))) {
                        isMatch = true;
                        wxLogInfo(wxT("DirectWASAPI: 主声音驱动程序模糊匹配成功"));
                    }
                }
                // 5. 特殊处理：如果目标设备名称很短或很通用，尝试更宽松的匹配
                else if (targetDeviceBaseName.length() <= 10) {
                    // 对于很短的设备名称，尝试部分匹配
                    wxString targetLower = targetDeviceBaseName.Lower();
                    wxString currentLower = currentDeviceSystemName.Lower();
                    if (currentLower.Contains(targetLower) || targetLower.Contains(currentLower)) {
                        isMatch = true;
                        wxLogInfo(wxT("DirectWASAPI: 短名称部分匹配成功"));
                    }
                }
                
                if (isMatch) {
                    m_pSelectedLoopbackDevice = pEndpoint;
                    m_pSelectedLoopbackDevice->AddRef();
                    wxLogInfo(wxT("DirectWASAPI: 找到匹配的渲染设备: '%s'"), currentDeviceSystemName);
                    foundDevice = true;
                }
            }
            PropVariantClear(&varName);
            pProps->Release();
        }
        pEndpoint->Release();
        if (foundDevice) break;
    }
    
    // 如果没有找到匹配的设备，使用默认设备
    if (!foundDevice && defaultDevice) {
        m_pSelectedLoopbackDevice = defaultDevice;
        m_pSelectedLoopbackDevice->AddRef();
        foundDevice = true;
        wxLogInfo(wxT("DirectWASAPI: 未找到精确匹配，使用默认渲染设备"));
    }
    
    pRenderEndpoints->Release();
    if (defaultDevice) {
        defaultDevice->Release();
    }

    if (!foundDevice || !m_pSelectedLoopbackDevice) {
        wxLogError(wxT("DirectWASAPI: 无法找到任何可用的渲染设备"));
        ShutdownDirectWASAPILoopback();
        return false;
    }

    // 4. 激活IAudioClient
    hr = m_pSelectedLoopbackDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_pAudioClient);
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: Activate(IAudioClient)失败: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }

    // 5. 获取混音格式
    hr = m_pAudioClient->GetMixFormat(&m_pWaveFormat);
    if (FAILED(hr) || !m_pWaveFormat) {
        wxLogError(wxT("DirectWASAPI: GetMixFormat失败: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }
    wxLogInfo(wxT("DirectWASAPI: 实际混音格式 - 采样率: %u, 声道: %u, 位深: %u"), 
             m_pWaveFormat->nSamplesPerSec, m_pWaveFormat->nChannels, m_pWaveFormat->wBitsPerSample);

    // 6. 初始化IAudioClient用于回环
    REFERENCE_TIME hnsBufferDuration = 300000; // 30ms缓冲区
    hr = m_pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        hnsBufferDuration,
        0,
        m_pWaveFormat,
        NULL);
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: Initialize失败: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }

    // 7. 获取IAudioCaptureClient
    hr = m_pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_pCaptureClient);
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: GetService(IAudioCaptureClient)失败: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }

    // 8. 启动流
    hr = m_pAudioClient->Start();
    if (FAILED(hr)) {
        wxLogError(wxT("DirectWASAPI: Start失败: hr = 0x%08lx"), hr);
        ShutdownDirectWASAPILoopback();
        return false;
    }

    m_bDirectWasapiLoopbackActive = true;
    m_isAudioInitialized = true;
    wxLogInfo(wxT("Direct WASAPI Loopback初始化成功，格式: %u Hz, %u 声道"), 
             m_pWaveFormat->nSamplesPerSec, m_pWaveFormat->nChannels);

    return true;
}

// 关闭直接WASAPI循环录制
void MainFrame::ShutdownDirectWASAPILoopback() {
    wxLogInfo(wxT("ShutdownDirectWASAPILoopback: 开始关闭"));
    
    if (m_pAudioClient) {
        m_pAudioClient->Stop();
    }
    
    if (m_pCaptureClient) {
        m_pCaptureClient->Release();
        m_pCaptureClient = nullptr;
    }
    
    if (m_pAudioClient) {
        m_pAudioClient->Release();
        m_pAudioClient = nullptr;
    }
    
    if (m_pWaveFormat) {
        CoTaskMemFree(m_pWaveFormat);
        m_pWaveFormat = nullptr;
    }
    
    if (m_pSelectedLoopbackDevice) {
        m_pSelectedLoopbackDevice->Release();
        m_pSelectedLoopbackDevice = nullptr;
    }
    
    if (m_pEnumerator) {
        m_pEnumerator->Release();
        m_pEnumerator = nullptr;
    }
    
    m_bDirectWasapiLoopbackActive = false;
    wxLogInfo(wxT("ShutdownDirectWASAPILoopback: 完成"));
}
#endif

// 开始音频捕获
void MainFrame::StartAudioCapture() {
    if (!m_isAudioInitialized) {
        wxLogError(wxT("音频未初始化，无法开始捕获"));
        return;
    }
    
#ifdef _WIN32
    if (m_bDirectWasapiLoopbackActive) {
        // Direct WASAPI模式已经在初始化时启动了流
        // 创建定时器来定期捕获音频数据
        if (!m_recordingTimer) {
            m_recordingTimer = new wxTimer(this);
        }
        
        // 绑定定时器事件
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            CaptureWASAPIAudio();
        });
        
        // 启动定时器，每10ms捕获一次
        m_recordingTimer->Start(10);
        wxLogInfo(wxT("Direct WASAPI音频捕获已启动，定时器间隔10ms"));
        return;
    }
#endif
    
    if (!m_paStream) {
        wxLogError(wxT("PortAudio流未初始化，无法开始捕获"));
        return;
    }
    
    PaError err = Pa_StartStream(m_paStream);
    if (err != paNoError) {
        wxLogError(wxT("启动PortAudio流失败: %s"), wxString(Pa_GetErrorText(err), wxConvUTF8));
        return;
    }
    
    wxLogInfo(wxT("PortAudio音频捕获已开始"));
}

// 停止音频捕获
void MainFrame::StopAudioCapture() {
#ifdef _WIN32
    if (m_bDirectWasapiLoopbackActive) {
        // 停止定时器
        if (m_recordingTimer && m_recordingTimer->IsRunning()) {
            m_recordingTimer->Stop();
        }
        
        // Direct WASAPI模式需要停止流
        if (m_pAudioClient) {
            m_pAudioClient->Stop();
            wxLogInfo(wxT("Direct WASAPI音频捕获已停止"));
        }
        return;
    }
#endif
    
    if (m_paStream && Pa_IsStreamActive(m_paStream)) {
        PaError err = Pa_StopStream(m_paStream);
        if (err != paNoError) {
            wxLogError(wxT("停止PortAudio流失败: %s"), wxString(Pa_GetErrorText(err), wxConvUTF8));
        } else {
            wxLogInfo(wxT("PortAudio音频捕获已停止"));
        }
    }
}

// 处理接收到的音频数据
void MainFrame::OnAudioDataReceived(const float* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) {
        return;
    }
    
    // 计算音量级别
    float volumeSum = 0.0f;
    for (size_t i = 0; i < bufferSize; i++) {
        volumeSum += buffer[i] * buffer[i];
    }
    float rmsLevel = sqrt(volumeSum / bufferSize);
    
    // 更新音量显示
    UpdateVolumeLevel(rmsLevel);
    
    // 保存音频数据到文件
    if (m_isRecording) {
        SaveAudioData(buffer, bufferSize);
    }
    
    // 处理音频数据用于语音识别
    ProcessAudioChunk(buffer, bufferSize);
}

// 更新音量级别显示
void MainFrame::UpdateVolumeLevel(float level) {
    // 将音量级别转换为分贝
    float dbLevel = 20.0f * log10f(level + 1e-10f); // 避免log(0)
    
    // 限制范围到 -60dB 到 0dB
    dbLevel = std::max(-60.0f, std::min(0.0f, dbLevel));
    
    // 更新状态栏显示音量 - 使用默认字段（索引0）
    wxString volumeText = wxString::Format(wxT("音量: %.1f dB"), dbLevel);
    
    // 安全地设置状态栏文本
    if (GetStatusBar() && GetStatusBar()->GetFieldsCount() > 0) {
        SetStatusText(volumeText, 0); // 使用第一个字段
    }
}

#ifdef _WIN32
// 捕获Direct WASAPI音频数据
void MainFrame::CaptureWASAPIAudio() {
    if (!m_bDirectWasapiLoopbackActive || !m_pCaptureClient) {
        return;
    }
    
    HRESULT hr;
    UINT32 packetLength = 0;
    
    // 获取可用的数据包大小
    hr = m_pCaptureClient->GetNextPacketSize(&packetLength);
    if (FAILED(hr)) {
        return;
    }
    
    while (packetLength != 0) {
        BYTE* pData;
        UINT32 numFramesAvailable;
        DWORD flags;
        
        // 获取缓冲区
        hr = m_pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
        if (FAILED(hr)) {
            break;
        }
        
        if (numFramesAvailable > 0 && pData) {
            // 处理音频数据
            ProcessWASAPIAudioData(pData, numFramesAvailable, m_pWaveFormat);
        }
        
        // 释放缓冲区
        hr = m_pCaptureClient->ReleaseBuffer(numFramesAvailable);
        if (FAILED(hr)) {
            break;
        }
        
        // 获取下一个数据包大小
        hr = m_pCaptureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) {
            break;
        }
    }
}

// 处理WASAPI音频数据
void MainFrame::ProcessWASAPIAudioData(const BYTE* pData, UINT32 numFrames, const WAVEFORMATEX* wfex) {
    if (!pData || numFrames == 0 || !wfex) {
        return;
    }
    
    // 转换为float格式并计算音量
    std::vector<float> floatBuffer;
    float volumeSum = 0.0f;
    
    wxLogDebug(wxT("WASAPI音频数据: %u 帧, %u 声道, %u 位, 采样率: %u"), 
              numFrames, wfex->nChannels, wfex->wBitsPerSample, wfex->nSamplesPerSec);
    
    if (wfex->wBitsPerSample == 16) {
        // 16位整数转float
        const int16_t* samples = reinterpret_cast<const int16_t*>(pData);
        UINT32 totalSamples = numFrames * wfex->nChannels;
        floatBuffer.resize(totalSamples);
        
        for (UINT32 i = 0; i < totalSamples; i++) {
            floatBuffer[i] = samples[i] / 32768.0f;
            volumeSum += floatBuffer[i] * floatBuffer[i];
        }
    } else if (wfex->wBitsPerSample == 32) {
        // 检查是否是IEEE float格式
        if (wfex->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || 
            (wfex->wFormatTag == WAVE_FORMAT_EXTENSIBLE && 
             reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfex)->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT)) {
            // 32位float
            const float* samples = reinterpret_cast<const float*>(pData);
            UINT32 totalSamples = numFrames * wfex->nChannels;
            floatBuffer.assign(samples, samples + totalSamples);
            
            for (UINT32 i = 0; i < totalSamples; i++) {
                volumeSum += floatBuffer[i] * floatBuffer[i];
            }
        } else {
            // 32位整数转float
            const int32_t* samples = reinterpret_cast<const int32_t*>(pData);
            UINT32 totalSamples = numFrames * wfex->nChannels;
            floatBuffer.resize(totalSamples);
            
            for (UINT32 i = 0; i < totalSamples; i++) {
                floatBuffer[i] = samples[i] / 2147483648.0f; // 2^31
                volumeSum += floatBuffer[i] * floatBuffer[i];
            }
        }
    } else {
        // 不支持的格式
        wxLogWarning(wxT("不支持的音频格式: %u 位"), wfex->wBitsPerSample);
        return;
    }
    
    // 计算RMS音量
    float rmsLevel = sqrt(volumeSum / floatBuffer.size());
    
    // 更新音量显示
    UpdateVolumeLevel(rmsLevel);
    
    // 保存音频数据到文件
    if (m_isRecording) {
        SaveAudioData(floatBuffer.data(), floatBuffer.size());
    }
    
    // 处理音频数据用于语音识别
    ProcessAudioChunk(floatBuffer.data(), floatBuffer.size());
}
#endif

// ==================== 音频保存功能实现 ====================

// 初始化音频录制
bool MainFrame::InitializeAudioRecording() {
    if (m_currentSessionPath.IsEmpty()) {
        wxLogError(wxT("没有活动会话，无法初始化音频录制"));
        return false;
    }
    
    // 根据当前音频模式设置实际格式参数
#ifdef _WIN32
    if (m_bDirectWasapiLoopbackActive && m_pWaveFormat) {
        // 使用WASAPI的实际格式
        m_actualSampleRate = m_pWaveFormat->nSamplesPerSec;
        m_actualChannels = m_pWaveFormat->nChannels;
        m_actualBitsPerSample = m_pWaveFormat->wBitsPerSample;
        wxLogInfo(wxT("使用WASAPI格式: %d Hz, %d 声道, %d 位"), 
                 m_actualSampleRate, m_actualChannels, m_actualBitsPerSample);
    } else
#endif
    {
        // 使用PortAudio配置的格式
        m_actualSampleRate = m_sampleRate;
        m_actualChannels = 1; // PortAudio通常配置为单声道
        m_actualBitsPerSample = 16; // 固定使用16位
        wxLogInfo(wxT("使用PortAudio格式: %d Hz, %d 声道, %d 位"), 
                 m_actualSampleRate, m_actualChannels, m_actualBitsPerSample);
    }
    
    // 创建音频文件路径
    m_currentAudioFilePath = CreateAudioFilePath();
    if (m_currentAudioFilePath.IsEmpty()) {
        wxLogError(wxT("无法创建音频文件路径"));
        return false;
    }
    
    // 初始化编码器
    if (m_isMP3Format) {
        if (!InitializeMP3Encoder()) {
            wxLogError(wxT("无法初始化MP3编码器"));
            return false;
        }
        wxLogInfo(wxT("MP3编码器初始化成功，比特率: %d kbps"), m_mp3Bitrate);
    } else {
        // WAV格式：创建音频文件并写入头部
        m_audioFile = new wxFile();
        if (!m_audioFile->Create(m_currentAudioFilePath, true)) {
            wxLogError(wxT("无法创建音频文件: %s"), m_currentAudioFilePath);
            delete m_audioFile;
            m_audioFile = nullptr;
            return false;
        }
        
        // 写入WAV文件头（使用实际格式参数）
        if (!WriteWAVHeader(*m_audioFile, m_actualSampleRate, m_actualChannels, m_actualBitsPerSample)) {
            wxLogError(wxT("无法写入WAV文件头"));
            m_audioFile->Close();
            delete m_audioFile;
            m_audioFile = nullptr;
            return false;
        }
    }
    
    // 重置计数器
    m_totalAudioFrames = 0;
    m_audioDataBuffer.clear();
    
    wxLogInfo(wxT("音频录制初始化成功，格式: %s，文件: %s"), 
             m_isMP3Format ? wxT("MP3") : wxT("WAV"), 
             m_currentAudioFilePath);
    return true;
}

// 开始音频录制
void MainFrame::StartAudioRecording() {
    if (!m_audioFile) {
        if (!InitializeAudioRecording()) {
            return;
        }
    }
    
    // 启动定时保存定时器
    if (m_audioSaveTimer && !m_audioSaveTimer->IsRunning()) {
        m_audioSaveTimer->Start(AUDIO_SAVE_INTERVAL);
        wxLogInfo(wxT("音频保存定时器已启动，间隔: %zu ms"), AUDIO_SAVE_INTERVAL);
    }
    
    // 显示音频格式信息
    wxString formatInfo = wxString::Format(
        wxT("音频录制格式: %d Hz, %d 声道, %d 位"),
        m_actualSampleRate, m_actualChannels, m_actualBitsPerSample);
    wxLogInfo(formatInfo);
    SetStatusText(formatInfo);
}

// 停止音频录制
void MainFrame::StopAudioRecording() {
    // 停止定时器
    if (m_audioSaveTimer && m_audioSaveTimer->IsRunning()) {
        m_audioSaveTimer->Stop();
    }
    
    // 刷新缓冲区
    FlushAudioBuffer();
    
    if (m_isMP3Format) {
        // MP3格式：完成编码
        if (!FinalizeMP3Encoding()) {
            wxLogError(wxT("MP3编码完成失败"));
        } else {
            wxLogInfo(wxT("MP3录制已停止，总帧数: %zu，文件: %s"), 
                     m_totalAudioFrames, m_currentAudioFilePath);
        }
        
        // 关闭MP3编码器
        ShutdownMP3Encoder();
    } else {
        // WAV格式：关闭文件
        if (m_audioFile) {
            // 更新WAV文件头
            UpdateWAVHeader(*m_audioFile, m_totalAudioFrames, m_actualSampleRate, m_actualChannels, m_actualBitsPerSample);
            
            m_audioFile->Close();
            delete m_audioFile;
            m_audioFile = nullptr;
            
            wxLogInfo(wxT("WAV录制已停止，总帧数: %zu，文件: %s"), 
                     m_totalAudioFrames, m_currentAudioFilePath);
        }
    }
    
    m_currentAudioFilePath.Clear();
}

// 保存音频数据
void MainFrame::SaveAudioData(const float* buffer, size_t bufferSize) {
    if (!buffer || bufferSize == 0) {
        return;
    }
    
    // 检查是否有有效的输出目标
    if (!m_isMP3Format && !m_audioFile) {
        return;
    }
    
    // 将float数据添加到缓冲区
    for (size_t i = 0; i < bufferSize; i++) {
        m_audioDataBuffer.push_back(buffer[i]);
    }
    
    // 更新总帧数（注意：bufferSize是样本数，需要除以声道数得到帧数）
    m_totalAudioFrames += bufferSize / m_actualChannels;
    
    // 如果缓冲区满了，写入文件
    if (m_audioDataBuffer.size() >= AUDIO_BUFFER_FRAMES) {
        FlushAudioBuffer();
    }
}

// 刷新音频缓冲区
void MainFrame::FlushAudioBuffer() {
    if (m_audioDataBuffer.empty()) {
        return;
    }
    
    // 检查是否有有效的输出目标
    if (!m_isMP3Format && !m_audioFile) {
        return;
    }
    
    if (m_isMP3Format) {
        // MP3格式：编码音频数据
        if (!EncodeToMP3(m_audioDataBuffer.data(), m_audioDataBuffer.size())) {
            wxLogError(wxT("MP3编码失败"));
        } else {
            wxLogDebug(wxT("已编码 %zu 个音频样本到MP3"), m_audioDataBuffer.size());
        }
    } else {
        // WAV格式：直接写入文件
        // 根据实际位深度转换数据
        if (m_actualBitsPerSample == 16) {
            // 转换为16位整数
            std::vector<int16_t> int16Buffer;
            int16Buffer.reserve(m_audioDataBuffer.size());
            
            for (float sample : m_audioDataBuffer) {
                // 限制范围到[-1.0, 1.0]
                sample = std::max(-1.0f, std::min(1.0f, sample));
                // 转换为16位整数
                int16_t intSample = static_cast<int16_t>(sample * 32767.0f);
                int16Buffer.push_back(intSample);
            }
            
            // 写入文件
            size_t bytesToWrite = int16Buffer.size() * sizeof(int16_t);
            size_t bytesWritten = m_audioFile->Write(int16Buffer.data(), bytesToWrite);
            
            if (bytesWritten != bytesToWrite) {
                wxLogError(wxT("音频数据写入不完整: %zu/%zu 字节"), bytesWritten, bytesToWrite);
            }
            
            wxLogDebug(wxT("已写入 %zu 字节16位音频数据"), bytesWritten);
        } else if (m_actualBitsPerSample == 32) {
            // 直接写入32位float数据
            size_t bytesToWrite = m_audioDataBuffer.size() * sizeof(float);
            size_t bytesWritten = m_audioFile->Write(m_audioDataBuffer.data(), bytesToWrite);
            
            if (bytesWritten != bytesToWrite) {
                wxLogError(wxT("音频数据写入不完整: %zu/%zu 字节"), bytesWritten, bytesToWrite);
            }
            
            wxLogDebug(wxT("已写入 %zu 字节32位音频数据"), bytesWritten);
        } else {
            wxLogError(wxT("不支持的位深度: %d"), m_actualBitsPerSample);
        }
    }
    
    // 清空缓冲区
    m_audioDataBuffer.clear();
}

// 音频保存定时器事件处理
void MainFrame::OnAudioSaveTimer(wxTimerEvent& event) {
    FlushAudioBuffer();
}

// 创建音频文件路径
wxString MainFrame::CreateAudioFilePath() const {
    if (m_currentSessionPath.IsEmpty()) {
        return wxEmptyString;
    }
    
    // 使用当前时间戳创建文件名
    wxDateTime now = wxDateTime::Now();
    wxString fileName = wxString::Format(wxT("audio_%s%s"), 
                                       now.Format(wxT("%Y%m%d_%H%M%S")),
                                       GetAudioFileExtension());
    
    return wxFileName(m_currentSessionPath, fileName).GetFullPath();
}

// 写入WAV文件头
bool MainFrame::WriteWAVHeader(wxFile& file, int sampleRate, int channels, int bitsPerSample) {
    // WAV文件头结构
    struct WAVHeader {
        char riff[4];           // "RIFF"
        uint32_t fileSize;      // 文件大小 - 8
        char wave[4];           // "WAVE"
        char fmt[4];            // "fmt "
        uint32_t fmtSize;       // fmt块大小 (16)
        uint16_t audioFormat;   // 音频格式 (1 = PCM, 3 = IEEE float)
        uint16_t numChannels;   // 声道数
        uint32_t sampleRate;    // 采样率
        uint32_t byteRate;      // 字节率
        uint16_t blockAlign;    // 块对齐
        uint16_t bitsPerSample; // 位深度
        char data[4];           // "data"
        uint32_t dataSize;      // 数据大小
    };
    
    WAVHeader header;
    memcpy(header.riff, "RIFF", 4);
    header.fileSize = 0; // 稍后更新
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    header.fmtSize = 16;
    
    // 根据位深度设置音频格式
    if (bitsPerSample == 32) {
        header.audioFormat = 3; // IEEE float
    } else {
        header.audioFormat = 1; // PCM
    }
    
    header.numChannels = channels;
    header.sampleRate = sampleRate;
    header.bitsPerSample = bitsPerSample;
    header.byteRate = sampleRate * channels * bitsPerSample / 8;
    header.blockAlign = channels * bitsPerSample / 8;
    memcpy(header.data, "data", 4);
    header.dataSize = 0; // 稍后更新
    
    size_t bytesWritten = file.Write(&header, sizeof(header));
    bool success = bytesWritten == sizeof(header);
    
    if (success) {
        wxLogInfo(wxT("WAV文件头写入成功: %d Hz, %d 声道, %d 位, 格式=%d"), 
                 sampleRate, channels, bitsPerSample, header.audioFormat);
    }
    
    return success;
}

// 更新WAV文件头
bool MainFrame::UpdateWAVHeader(wxFile& file, size_t totalFrames, int sampleRate, int channels, int bitsPerSample) {
    if (!file.IsOpened()) {
        return false;
    }
    
    // 计算数据大小
    uint32_t dataSize = totalFrames * channels * bitsPerSample / 8;
    uint32_t fileSize = dataSize + 36; // WAV头大小 - 8
    
    // 更新文件大小
    file.Seek(4);
    if (file.Write(&fileSize, sizeof(fileSize)) != sizeof(fileSize)) {
        return false;
    }
    
    // 更新数据大小
    file.Seek(40);
    if (file.Write(&dataSize, sizeof(dataSize)) != sizeof(dataSize)) {
        return false;
    }
    
    return true;
}

// ==================== MP3编码功能实现 ====================

// 加载音频格式配置
void MainFrame::LoadAudioFormatConfig() {
    // 使用与ConfigDialog相同的配置文件路径
    wxString configPath;
    
#ifdef __WXMSW__
    configPath = wxGetHomeDir() + wxT("\\MeetAntConfig");
#else
    configPath = wxGetHomeDir() + wxT("/.MeetAntConfig");
#endif
    
    wxString configFilePath = configPath + wxFileName::GetPathSeparator() + wxT("config.json");
    
    if (!wxFile::Exists(configFilePath)) {
        // 使用默认配置：MP3 192kbps
        m_audioFormat = AudioFormat::MP3_192;
        m_isMP3Format = true;
        m_mp3Bitrate = 192;
        wxLogInfo(wxT("配置文件不存在，使用默认音频格式: MP3 192kbps"));
        return;
    }
    
    try {
        wxFileInputStream input(configFilePath);
        if (!input.IsOk()) {
            wxLogWarning(wxT("无法打开配置文件: %s"), configFilePath);
            return;
        }
        
        wxString jsonStr;
        char buffer[1024];
        while (!input.Eof()) {
            input.Read(buffer, sizeof(buffer));
            size_t bytesRead = input.LastRead();
            jsonStr += wxString(buffer, wxConvUTF8, bytesRead);
        }
        
        // 查找音频格式配置
        wxRegEx formatRegex(wxT("\"audioFormat\"\\s*:\\s*\"([^\"]+)\""));
        if (formatRegex.Matches(jsonStr)) {
            wxString formatStr = formatRegex.GetMatch(jsonStr, 1);
            
            if (formatStr == wxT("MP3_128")) {
                m_audioFormat = AudioFormat::MP3_128;
                m_isMP3Format = true;
                m_mp3Bitrate = 128;
            } else if (formatStr == wxT("MP3_192")) {
                m_audioFormat = AudioFormat::MP3_192;
                m_isMP3Format = true;
                m_mp3Bitrate = 192;
            } else if (formatStr == wxT("MP3_320")) {
                m_audioFormat = AudioFormat::MP3_320;
                m_isMP3Format = true;
                m_mp3Bitrate = 320;
            } else if (formatStr == wxT("WAV_PCM16")) {
                m_audioFormat = AudioFormat::WAV_PCM16;
                m_isMP3Format = false;
                m_mp3Bitrate = 0;
            } else if (formatStr == wxT("WAV_PCM32")) {
                m_audioFormat = AudioFormat::WAV_PCM32;
                m_isMP3Format = false;
                m_mp3Bitrate = 0;
            } else {
                // 默认使用MP3 192kbps
                m_audioFormat = AudioFormat::MP3_192;
                m_isMP3Format = true;
                m_mp3Bitrate = 192;
            }
            
            wxLogInfo(wxT("从配置文件加载音频格式: %s"), formatStr);
        } else {
            // 默认使用MP3 192kbps
            m_audioFormat = AudioFormat::MP3_192;
            m_isMP3Format = true;
            m_mp3Bitrate = 192;
            wxLogInfo(wxT("配置文件中未找到音频格式设置，使用默认: MP3 192kbps"));
        }
        
    } catch (...) {
        wxLogError(wxT("解析音频格式配置时发生异常"));
        // 使用默认配置
        m_audioFormat = AudioFormat::MP3_192;
        m_isMP3Format = true;
        m_mp3Bitrate = 192;
    }
}

// 获取音频文件扩展名
wxString MainFrame::GetAudioFileExtension() const {
    switch (m_audioFormat) {
        case AudioFormat::MP3_128:
        case AudioFormat::MP3_192:
        case AudioFormat::MP3_320:
            return wxT(".mp3");
        case AudioFormat::WAV_PCM16:
        case AudioFormat::WAV_PCM32:
            return wxT(".wav");
        case AudioFormat::OGG_VORBIS:
            return wxT(".ogg");
        default:
            return wxT(".mp3");
    }
}

// 初始化MP3编码器
bool MainFrame::InitializeMP3Encoder() {
    if (!m_isMP3Format) {
        return true; // WAV格式不需要编码器
    }
    
    // 这里使用简化的实现，实际项目中可以集成LAME库
    // 目前我们将音频数据缓存，然后在录制结束时转换为MP3
    
    m_mp3Buffer.clear();
    m_mp3Buffer.reserve(1024 * 1024); // 预分配1MB缓冲区
    
    wxLogInfo(wxT("MP3编码器初始化成功，比特率: %d kbps"), m_mp3Bitrate);
    return true;
}

// 关闭MP3编码器
void MainFrame::ShutdownMP3Encoder() {
    if (m_lameEncoder) {
        // 在实际实现中，这里应该调用LAME的清理函数
        m_lameEncoder = nullptr;
    }
    
    m_mp3Buffer.clear();
    wxLogInfo(wxT("MP3编码器已关闭"));
}

// 编码到MP3
bool MainFrame::EncodeToMP3(const float* buffer, size_t bufferSize) {
    if (!m_isMP3Format || !buffer || bufferSize == 0) {
        return false;
    }
    
    // 简化实现：将float数据转换为16位整数并存储
    // 实际项目中应该直接调用LAME编码
    
    for (size_t i = 0; i < bufferSize; i++) {
        float sample = std::max(-1.0f, std::min(1.0f, buffer[i]));
        int16_t intSample = static_cast<int16_t>(sample * 32767.0f);
        
        // 将16位整数转换为字节并添加到缓冲区
        m_mp3Buffer.push_back(static_cast<unsigned char>(intSample & 0xFF));
        m_mp3Buffer.push_back(static_cast<unsigned char>((intSample >> 8) & 0xFF));
    }
    
    return true;
}

// 完成MP3编码
bool MainFrame::FinalizeMP3Encoding() {
    if (!m_isMP3Format) {
        return true;
    }
    
    // 简化实现：使用外部工具转换WAV到MP3
    // 首先创建临时WAV文件
    wxString tempWavPath = m_currentAudioFilePath + wxT(".temp.wav");
    
    // 创建临时WAV文件
    wxFile tempWavFile;
    if (!tempWavFile.Create(tempWavPath, true)) {
        wxLogError(wxT("无法创建临时WAV文件: %s"), tempWavPath);
        return false;
    }
    
    // 写入WAV头
    if (!WriteWAVHeader(tempWavFile, m_actualSampleRate, m_actualChannels, 16)) {
        wxLogError(wxT("无法写入临时WAV文件头"));
        tempWavFile.Close();
        return false;
    }
    
    // 写入音频数据
    if (!m_mp3Buffer.empty()) {
        size_t bytesWritten = tempWavFile.Write(m_mp3Buffer.data(), m_mp3Buffer.size());
        if (bytesWritten != m_mp3Buffer.size()) {
            wxLogError(wxT("写入临时WAV文件数据失败"));
        }
    }
    
    // 更新WAV头
    size_t totalFrames = m_mp3Buffer.size() / (m_actualChannels * 2); // 16位 = 2字节
    UpdateWAVHeader(tempWavFile, totalFrames, m_actualSampleRate, m_actualChannels, 16);
    tempWavFile.Close();
    
    // 使用FFmpeg转换WAV到MP3
    wxString ffmpegCmd = wxString::Format(
        wxT("ffmpeg -i \"%s\" -codec:a libmp3lame -b:a %dk \"%s\" -y"),
        tempWavPath, m_mp3Bitrate, m_currentAudioFilePath);
    
    wxLogInfo(wxT("开始MP3转换: %s"), ffmpegCmd);
    
    // 执行转换命令
    int result = wxExecute(ffmpegCmd, wxEXEC_SYNC);
    
    // 删除临时文件
    if (wxFileExists(tempWavPath)) {
        wxRemoveFile(tempWavPath);
    }
    
    if (result == 0) {
        wxLogInfo(wxT("MP3转换成功: %s"), m_currentAudioFilePath);
        return true;
    } else {
        wxLogError(wxT("MP3转换失败，错误代码: %d"), result);
        
        // 如果FFmpeg转换失败，尝试使用LAME
        wxString lameCmd = wxString::Format(
            wxT("lame -b %d \"%s\" \"%s\""),
            m_mp3Bitrate, tempWavPath, m_currentAudioFilePath);
        
        wxLogInfo(wxT("尝试使用LAME转换: %s"), lameCmd);
        result = wxExecute(lameCmd, wxEXEC_SYNC);
        
        if (wxFileExists(tempWavPath)) {
            wxRemoveFile(tempWavPath);
        }
        
        if (result == 0) {
            wxLogInfo(wxT("LAME转换成功: %s"), m_currentAudioFilePath);
            return true;
        } else {
            wxLogError(wxT("LAME转换也失败，错误代码: %d"), result);
            wxLogError(wxT("请确保系统中安装了FFmpeg或LAME"));
            return false;
        }
    }
}

// ==================== AI对话功能实现 ====================

// 加载AI配置
bool MainFrame::LoadAIConfig() {
    // 使用与ConfigDialog相同的配置文件路径
    wxString configPath;
    
#ifdef __WXMSW__
    configPath = wxGetHomeDir() + wxT("\\MeetAntConfig");
#else
    configPath = wxGetHomeDir() + wxT("/.MeetAntConfig");
#endif
    
    wxString configFilePath = configPath + wxFileName::GetPathSeparator() + wxT("config.json");
    
    wxLogInfo(wxT("尝试加载AI配置文件: %s"), configFilePath);
    
    if (!wxFile::Exists(configFilePath)) {
        // 使用默认配置
        if (!m_aiConfig) {
            m_aiConfig = new AIConfig();
        }
        m_aiConfig->apiKey = wxT("");
        m_aiConfig->endpointURL = wxT("https://api.openai.com/v1/chat/completions");
        m_aiConfig->modelName = wxT("gpt-3.5-turbo");
        m_aiConfig->temperature = 0.7;
        m_aiConfig->maxTokens = 1000;
        m_aiConfig->promptTemplate = wxT("你是一个会议助手，请帮助用户处理会议相关的问题。");
        
        wxLogInfo(wxT("AI配置文件不存在，使用默认配置"));
        return true;
    }
    
    try {
        wxFileInputStream input(configFilePath);
        if (!input.IsOk()) {
            wxLogWarning(wxT("无法打开AI配置文件: %s"), configFilePath);
            return false;
        }
        
        wxString jsonStr;
        char buffer[1024];
        while (!input.Eof()) {
            input.Read(buffer, sizeof(buffer));
            size_t bytesRead = input.LastRead();
            jsonStr += wxString(buffer, wxConvUTF8, bytesRead);
        }
        
        wxLogInfo(wxT("配置文件内容长度: %zu 字符"), jsonStr.length());
        wxLogInfo(wxT("配置文件前200字符: %s"), jsonStr.Left(200));
        
        // 创建AI配置对象
        if (!m_aiConfig) {
            m_aiConfig = new AIConfig();
        }
        
        // 设置默认值
        m_aiConfig->apiKey = wxT("");
        m_aiConfig->endpointURL = wxT("https://api.openai.com/v1/chat/completions");
        m_aiConfig->modelName = wxT("gpt-3.5-turbo");
        m_aiConfig->temperature = 0.7;
        m_aiConfig->maxTokens = 1000;
        m_aiConfig->promptTemplate = wxT("你是一个会议助手，请帮助用户处理会议相关的问题。");
        
        // 使用nlohmann/json解析配置文件
        std::string jsonStrUtf8 = jsonStr.ToUTF8().data();
        nlohmann::json configJson = nlohmann::json::parse(jsonStrUtf8);
        
        // 查找AI配置段
        nlohmann::json aiConfig;
        if (configJson.contains("llm") && configJson["llm"].is_object()) {
            aiConfig = configJson["llm"];
            wxLogInfo(wxT("找到 'llm' 配置段"));
        } else {
            // 如果没有ai对象，直接在根级别查找
            aiConfig = configJson;
            wxLogInfo(wxT("在根级别查找AI配置字段"));
        }
        
        // 解析各个字段
        if (aiConfig.contains("apiKey") && aiConfig["apiKey"].is_string()) {
            m_aiConfig->apiKey = wxString::FromUTF8(aiConfig["apiKey"].get<std::string>().c_str());
            wxLogInfo(wxT("找到API密钥配置"));
        } else {
            wxLogInfo(wxT("未找到API密钥配置"));
        }
        
        if (aiConfig.contains("endpointURL") && aiConfig["endpointURL"].is_string()) {
            m_aiConfig->endpointURL = wxString::FromUTF8(aiConfig["endpointURL"].get<std::string>().c_str());
            wxLogInfo(wxT("找到端点URL配置: %s"), m_aiConfig->endpointURL);
        }
        
        if (aiConfig.contains("modelName") && aiConfig["modelName"].is_string()) {
            m_aiConfig->modelName = wxString::FromUTF8(aiConfig["modelName"].get<std::string>().c_str());
            wxLogInfo(wxT("找到模型名称配置: %s"), m_aiConfig->modelName);
        }
        
        if (aiConfig.contains("temperature") && aiConfig["temperature"].is_number()) {
            m_aiConfig->temperature = aiConfig["temperature"].get<double>();
            wxLogInfo(wxT("找到温度配置: %f"), m_aiConfig->temperature);
        }
        
        if (aiConfig.contains("maxTokens") && aiConfig["maxTokens"].is_number_integer()) {
            m_aiConfig->maxTokens = aiConfig["maxTokens"].get<int>();
            wxLogInfo(wxT("找到最大令牌数配置: %d"), m_aiConfig->maxTokens);
        }
        
        if (aiConfig.contains("promptTemplate") && aiConfig["promptTemplate"].is_string()) {
            m_aiConfig->promptTemplate = wxString::FromUTF8(aiConfig["promptTemplate"].get<std::string>().c_str());
            wxLogInfo(wxT("找到提示模板配置"));
        }
        
        wxLogInfo(wxT("AI配置加载完成"));
        return true;
        
    } catch (const nlohmann::json::exception& e) {
        wxLogError(wxT("解析AI配置文件时发生JSON异常: %s"), wxString::FromUTF8(e.what()));
        return false;
    } catch (...) {
        wxLogError(wxT("解析AI配置文件时发生未知异常"));
        return false;
    }
}

// 发送AI消息
void MainFrame::SendAIMessage(const wxString& message) {
    if (!m_aiConfig) {
        wxLogError(wxT("AI配置未加载"));
        return;
    }
    
    // 在聊天历史中显示用户消息
    m_aiChatHistoryCtrl->AppendText(wxString::Format(wxT("用户: %s\n"), message));
    
    // 发送请求
    SendAIRequest(message);
}

// 发送AI请求
void MainFrame::SendAIRequest(const wxString& userInput) {
    wxLogInfo(wxT("开始发送AI请求，用户输入: %s"), userInput);
    
    if (!m_aiConfig || m_aiConfig->apiKey.IsEmpty()) {
        wxLogWarning(wxT("API密钥为空或AI配置未加载"));
        m_aiChatHistoryCtrl->AppendText(wxT("AI: 请先配置API密钥\n"));
        return;
    }
    
    if (m_isAIRequestActive) {
        wxLogWarning(wxT("已有活跃的AI请求"));
        m_aiChatHistoryCtrl->AppendText(wxT("AI: 正在处理上一个请求，请稍候...\n"));
        return;
    }
    
    // 重置响应开始标志
    m_aiResponseStarted = false;
    m_streamingBuffer.Clear();  // 清空流式缓冲区
    
    wxLogInfo(wxT("使用配置 - 端点: %s, 模型: %s"), m_aiConfig->endpointURL, m_aiConfig->modelName);
    
    // 使用nlohmann/json构建请求
    try {
        nlohmann::json requestJson;
        
        // 安全地转换wxString到UTF-8 std::string
        std::string modelNameUtf8 = m_aiConfig->modelName.ToUTF8().data();
        std::string promptTemplateUtf8 = m_aiConfig->promptTemplate.ToUTF8().data();
        std::string userInputUtf8 = userInput.ToUTF8().data();
        
        requestJson["model"] = modelNameUtf8;
        requestJson["temperature"] = m_aiConfig->temperature;
        requestJson["max_tokens"] = m_aiConfig->maxTokens;
        requestJson["stream"] = true;
        
        // 构建messages数组
        nlohmann::json messages = nlohmann::json::array();
        
        // 系统消息
        nlohmann::json systemMessage;
        systemMessage["role"] = "system";
        systemMessage["content"] = promptTemplateUtf8;
        messages.push_back(systemMessage);
        
        // 用户消息
        nlohmann::json userMessage;
        userMessage["role"] = "user";
        userMessage["content"] = userInputUtf8;
        messages.push_back(userMessage);
        
        requestJson["messages"] = messages;
        
        // 转换为字符串
        std::string requestBodyStd = requestJson.dump();
        wxString requestBody = wxString::FromUTF8(requestBodyStd.c_str());
        
        wxLogInfo(wxT("请求体: %s"), requestBody);
        
        // 使用改进的 SSEClient 发送 POST 请求
        if (!m_sseClient) {
            m_sseClient = std::make_unique<SSEClient>(this);
        }
        
        // 断开之前的连接（如果有）
        if (m_sseClient->IsConnected()) {
            m_sseClient->Disconnect();
        }
        
        // 设置请求头
        std::map<wxString, wxString> headers;
        headers[wxT("Content-Type")] = wxT("application/json");
        headers[wxT("Authorization")] = wxT("Bearer ") + m_aiConfig->apiKey;
        
        // 使用 POST 方法连接
        wxLogInfo(wxT("使用 SSEClient 发送 POST 请求到: %s"), m_aiConfig->endpointURL);
        
        if (!m_sseClient->ConnectWithPost(m_aiConfig->endpointURL, requestBody, headers)) {
            wxLogError(wxT("SSEClient 连接失败"));
            m_aiChatHistoryCtrl->AppendText(wxT("AI: 无法建立连接\n"));
            return;
        }
        
        m_isAIRequestActive = true;
        
        // 启动超时定时器
        if (!m_aiTimeoutTimer) {
            m_aiTimeoutTimer = new wxTimer(this);
        }
        m_aiTimeoutTimer->Start(30000, wxTIMER_ONE_SHOT); // 30秒超时
        wxLogInfo(wxT("超时定时器已启动"));
        
        m_aiChatHistoryCtrl->AppendText(wxT("AI: 正在思考...\n"));
        
    } catch (const nlohmann::json::exception& e) {
        wxLogError(wxT("构建AI请求JSON时发生异常: %s"), wxString::FromUTF8(e.what()));
        m_aiChatHistoryCtrl->AppendText(wxT("AI: 请求构建失败\n"));
        return;
    } catch (const std::exception& e) {
        wxLogError(wxT("发送AI请求时发生异常: %s"), wxString::FromUTF8(e.what()));
        m_aiChatHistoryCtrl->AppendText(wxT("AI: 请求发送失败\n"));
        return;
    } catch (...) {
        wxLogError(wxT("发送AI请求时发生未知异常"));
        m_aiChatHistoryCtrl->AppendText(wxT("AI: 请求发送失败\n"));
        return;
    }
}

// 处理AI Web请求状态变化
void MainFrame::OnAIWebRequestStateChanged(wxWebRequestEvent& event) {
    wxLogInfo(wxT("AI请求状态变化: %d"), static_cast<int>(event.GetState()));
    
    switch (event.GetState()) {
        case wxWebRequest::State_Completed:
            wxLogInfo(wxT("AI请求完成，状态码: %d"), event.GetResponse().GetStatus());
            m_isAIRequestActive = false;
            StopAITimeoutTimer();
            if (event.GetResponse().GetStatus() == 200) {
                wxLogInfo(wxT("AI请求成功完成"));
                
                // 总是尝试读取完整响应数据
                try {
                    wxString responseData;
                    wxInputStream* responseStream = event.GetResponse().GetStream();
                    if (responseStream && responseStream->IsOk()) {
                        // 读取所有响应数据
                        char buffer[4096];
                        while (!responseStream->Eof()) {
                            responseStream->Read(buffer, sizeof(buffer));
                            size_t bytesRead = responseStream->LastRead();
                            if (bytesRead > 0) {
                                wxString chunk = wxString::FromUTF8(buffer, bytesRead);
                                responseData += chunk;
                            }
                        }
                        
                        wxLogInfo(wxT("获取到完整响应数据，长度: %zu"), responseData.length());
                        wxLogInfo(wxT("响应数据前500字符: %s"), responseData.Left(500));
                        
                        if (!responseData.IsEmpty()) {
                            // 如果还没有开始显示AI响应，先显示AI前缀
                            if (!m_aiResponseStarted) {
                                // 清除"正在思考..."消息
                                wxString currentText = m_aiChatHistoryCtrl->GetValue();
                                if (currentText.EndsWith(wxT("AI: 正在思考...\n"))) {
                                    // 移除最后的"正在思考..."行
                                    int lastNewline = currentText.rfind('\n', currentText.length() - 2);
                                    if (lastNewline != wxNOT_FOUND) {
                                        m_aiChatHistoryCtrl->SetValue(currentText.Left(lastNewline + 1));
                                    }
                                }
                                
                                m_aiChatHistoryCtrl->AppendText(wxT("AI: "));
                                m_aiResponseStarted = true;
                            }
                            
                            // 处理完整的响应数据
                            ProcessStreamingSSEData(responseData);
                            
                            // 确保响应结束时有换行
                            if (!responseData.EndsWith(wxT("\n"))) {
                                m_aiChatHistoryCtrl->AppendText(wxT("\n"));
                            }
                        } else {
                            wxLogWarning(wxT("响应数据为空"));
                            m_aiChatHistoryCtrl->AppendText(wxT("AI: 收到空响应\n"));
                        }
                    } else {
                        wxLogError(wxT("无法获取响应流"));
                        m_aiChatHistoryCtrl->AppendText(wxT("AI: 无法读取响应数据\n"));
                    }
                } catch (const std::exception& e) {
                    wxLogError(wxT("处理响应数据时发生异常: %s"), wxString::FromUTF8(e.what()));
                    m_aiChatHistoryCtrl->AppendText(wxT("AI: 响应处理失败\n"));
                } catch (...) {
                    wxLogError(wxT("处理响应数据时发生未知异常"));
                    m_aiChatHistoryCtrl->AppendText(wxT("AI: 响应处理失败\n"));
                }
            } else {
                wxLogError(wxT("AI请求失败，状态码: %d"), event.GetResponse().GetStatus());
                m_aiChatHistoryCtrl->AppendText(
                    wxString::Format(wxT("AI: 请求失败 (状态码: %d)\n"), 
                                   event.GetResponse().GetStatus()));
            }
            break;
            
        case wxWebRequest::State_Failed:
            wxLogError(wxT("AI请求失败"));
            m_isAIRequestActive = false;
            StopAITimeoutTimer();
            m_aiChatHistoryCtrl->AppendText(wxT("AI: 请求失败\n"));
            break;
            
        case wxWebRequest::State_Cancelled:
            wxLogWarning(wxT("AI请求被取消"));
            m_isAIRequestActive = false;
            StopAITimeoutTimer();
            m_aiChatHistoryCtrl->AppendText(wxT("AI: 请求已取消\n"));
            break;
            
        case wxWebRequest::State_Active:
            wxLogInfo(wxT("AI请求激活"));
            break;
            
        default:
            wxLogInfo(wxT("AI请求状态: %d"), static_cast<int>(event.GetState()));
            break;
    }
}

// 处理AI Web请求数据接收
void MainFrame::OnAIWebRequestDataReceived(wxWebRequestEvent& event) {
    wxLogInfo(wxT("收到AI响应数据事件"));
    
    // 获取数据缓冲区和大小
    const void* dataBuffer = event.GetDataBuffer();
    size_t dataSize = event.GetDataSize();
    
    wxLogInfo(wxT("数据大小: %zu 字节"), dataSize);
    
    if (dataBuffer && dataSize > 0) {
        // 将数据转换为字符串
        wxString data = wxString::FromUTF8(static_cast<const char*>(dataBuffer), dataSize);
        
        wxLogInfo(wxT("接收到的数据片段: %s"), data.Left(200)); // 只显示前200字符
        
        // 实时处理流式数据片段
        ProcessStreamingSSEData(data);
    } else {
        wxLogWarning(wxT("收到空数据或无效数据缓冲区"));
    }
}

// 处理SSE数据
void MainFrame::ProcessSSEData(const wxString& data) {
    wxLogInfo(wxT("MainFrame::ProcessSSEData 处理SSE数据，长度: %zu"), data.length());
    
    // 立即停止超时定时器，因为我们已经收到响应
    StopAITimeoutTimer();
    
    // 如果还没有开始显示AI响应，先显示AI前缀
    if (!m_aiResponseStarted) {
        // 清除"正在思考..."消息
        wxString currentText = m_aiChatHistoryCtrl->GetValue();
        if (currentText.EndsWith(wxT("AI: 正在思考...\n"))) {
            // 移除最后的"正在思考..."行
            int lastNewline = currentText.rfind('\n', currentText.length() - 2);
            if (lastNewline != wxNOT_FOUND) {
                m_aiChatHistoryCtrl->SetValue(currentText.Left(lastNewline + 1));
            }
        }
        
        m_aiChatHistoryCtrl->AppendText(wxT("AI: "));
        m_aiResponseStarted = true;
    }
    
    // 检查是否是非流式响应（直接的JSON响应）
    if (data.StartsWith(wxT("{"))) {
        wxLogInfo(wxT("检测到非流式JSON响应"));
        
        try {
            // 使用nlohmann/json解析非流式响应
            std::string dataUtf8 = data.ToUTF8().data();
            nlohmann::json jsonObj = nlohmann::json::parse(dataUtf8);
            
            if (jsonObj.contains("choices") && jsonObj["choices"].is_array() && !jsonObj["choices"].empty()) {
                auto& choice = jsonObj["choices"][0];
                if (choice.contains("message") && choice["message"].contains("content")) {
                    wxString content = wxString::FromUTF8(choice["message"]["content"].get<std::string>().c_str());
                    
                    wxLogInfo(wxT("从非流式响应提取到内容: %s"), content);
                    
                    // 添加到聊天历史
                    m_aiChatHistoryCtrl->AppendText(content + wxT("\n"));
                    return;
                }
            }
        } catch (const nlohmann::json::exception& e) {
            wxLogWarning(wxT("解析非流式JSON响应失败: %s"), wxString::FromUTF8(e.what()));
        }
    }
    
    // 处理流式SSE响应
    wxStringTokenizer tokenizer(data, wxT("\n"));
    
    int lineCount = 0;
    bool hasContent = false;
    
    while (tokenizer.HasMoreTokens()) {
        wxString line = tokenizer.GetNextToken().Trim();
        lineCount++;
        
        wxLogDebug(wxT("处理第%d行: %s"), lineCount, line.Left(100));
        
        // 跳过空行和长度标识符行（纯数字的行）
        if (line.IsEmpty()) {
            continue;
        }
        
        // 检查是否是长度标识符（十六进制数字）
        bool isLengthLine = true;
        for (size_t i = 0; i < line.length(); i++) {
            wxChar c = line[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                isLengthLine = false;
                break;
            }
        }
        
        if (isLengthLine && line.length() <= 8) {
            wxLogDebug(wxT("跳过长度标识符行: %s"), line);
            continue;
        }
        
        if (line.StartsWith(wxT("data: "))) {
            wxString jsonData = line.Mid(6); // 去掉 "data: " 前缀
            
            wxLogInfo(wxT("找到数据行: %s"), jsonData);
            
            if (jsonData == wxT("[DONE]")) {
                // 流结束
                wxLogInfo(wxT("SSE流结束"));
                if (hasContent) {
                    m_aiChatHistoryCtrl->AppendText(wxT("\n"));
                }
                return;
            }
            
            try {
                // 使用nlohmann/json解析流式响应
                std::string jsonDataUtf8 = jsonData.ToUTF8().data();
                nlohmann::json jsonObj = nlohmann::json::parse(jsonDataUtf8);
                
                wxString content;
                bool foundContent = false;
                
                // 尝试解析 choices[0].delta.content (流式响应)
                if (jsonObj.contains("choices") && jsonObj["choices"].is_array() && !jsonObj["choices"].empty()) {
                    auto& choice = jsonObj["choices"][0];
                    if (choice.contains("delta") && choice["delta"].contains("content")) {
                        content = wxString::FromUTF8(choice["delta"]["content"].get<std::string>().c_str());
                        foundContent = true;
                        wxLogInfo(wxT("从delta提取到内容: %s"), content);
                    }
                }
                
                // 如果没有找到delta.content，尝试直接的content字段（兼容性）
                if (!foundContent && jsonObj.contains("content")) {
                    content = wxString::FromUTF8(jsonObj["content"].get<std::string>().c_str());
                    foundContent = true;
                    wxLogInfo(wxT("从简单content提取到内容: %s"), content);
                }
                
                if (foundContent && !content.IsEmpty()) {
                    // 添加到聊天历史
                    m_aiChatHistoryCtrl->AppendText(content);
                    hasContent = true;
                } else {
                    wxLogDebug(wxT("未找到content字段在JSON中: %s"), jsonData);
                }
                
            } catch (const nlohmann::json::exception& e) {
                wxLogDebug(wxT("解析JSON失败: %s, 数据: %s"), wxString::FromUTF8(e.what()), jsonData);
            }
        }
    }
    
    // 如果没有找到任何内容，显示错误信息
    if (!hasContent) {
        wxLogWarning(wxT("未从SSE数据中提取到任何内容"));
        m_aiChatHistoryCtrl->AppendText(wxT("抱歉，无法解析AI响应\n"));
    } else {
        // 如果有内容但没有明确的结束标记，添加换行
        m_aiChatHistoryCtrl->AppendText(wxT("\n"));
    }
    
    wxLogInfo(wxT("SSE数据处理完成，共处理%d行，找到内容: %s"), lineCount, hasContent ? wxT("是") : wxT("否"));
}

// 处理流式SSE数据（实时处理）
void MainFrame::ProcessStreamingSSEData(const wxString& data) {
    wxLogInfo(wxT("处理流式SSE数据片段，长度: %zu"), data.length());
    
    // 将新数据添加到缓冲区
    m_streamingBuffer += data;
    
    // 处理缓冲区中的所有完整行
    wxStringTokenizer tokenizer(m_streamingBuffer, wxT("\n"));
    wxString remainingData;
    
    // 收集所有完整的行
    wxArrayString completeLines;
    wxString lastToken;
    
    while (tokenizer.HasMoreTokens()) {
        lastToken = tokenizer.GetNextToken();
        completeLines.Add(lastToken);
    }
    
    // 检查最后一个token是否是完整的行（原始数据以\n结尾）
    if (!m_streamingBuffer.IsEmpty() && !m_streamingBuffer.EndsWith(wxT("\n"))) {
        // 最后一行不完整，保存为剩余数据
        if (!completeLines.IsEmpty()) {
            remainingData = completeLines.Last();
            completeLines.RemoveAt(completeLines.GetCount() - 1);
        }
    }
    
    // 处理所有完整的行
    for (const wxString& line : completeLines) {
        wxString trimmedLine = line;
        trimmedLine.Trim();
        ProcessSingleSSELine(trimmedLine);
    }
    
    // 更新缓冲区只保留不完整的数据
    m_streamingBuffer = remainingData;
    
    wxLogDebug(wxT("处理了 %zu 行，流式缓冲区剩余数据长度: %zu"), completeLines.GetCount(), m_streamingBuffer.length());
}

// 处理单个SSE行
void MainFrame::ProcessSingleSSELine(const wxString& line) {
    wxLogDebug(wxT("处理SSE行: %s"), line.Left(100));
    
    // 跳过空行和长度标识符行
    if (line.IsEmpty()) {
        return;
    }
    
    // 检查是否是长度标识符（十六进制数字）
    bool isLengthLine = true;
    for (size_t i = 0; i < line.length(); i++) {
        wxChar c = line[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            isLengthLine = false;
            break;
        }
    }
    
    if (isLengthLine && line.length() <= 8) {
        wxLogDebug(wxT("跳过长度标识符行: %s"), line);
        return;
    }
    
    if (line.StartsWith(wxT("data: "))) {
        wxString jsonData = line.Mid(6); // 去掉 "data: " 前缀
        
        wxLogInfo(wxT("找到流式数据行: %s"), jsonData.Left(200));
        
        if (jsonData == wxT("[DONE]")) {
            // 流结束
            wxLogInfo(wxT("SSE流结束"));
            return;
        }
        
        try {
            // 使用nlohmann/json解析流式响应
            std::string jsonDataUtf8 = jsonData.ToUTF8().data();
            nlohmann::json jsonObj = nlohmann::json::parse(jsonDataUtf8);
            
            wxString content;
            bool foundContent = false;
            
            // 尝试解析 choices[0].delta.content (流式响应)
            if (jsonObj.contains("choices") && jsonObj["choices"].is_array() && !jsonObj["choices"].empty()) {
                auto& choice = jsonObj["choices"][0];
                if (choice.contains("delta") && choice["delta"].contains("content")) {
                    auto contentValue = choice["delta"]["content"];
                    if (contentValue.is_string()) {
                        content = wxString::FromUTF8(contentValue.get<std::string>().c_str());
                        foundContent = true;
                        wxLogInfo(wxT("从流式delta提取到内容: '%s'"), content);
                    }
                }
                // 如果没有delta.content，尝试message.content（非流式响应）
                else if (choice.contains("message") && choice["message"].contains("content")) {
                    auto contentValue = choice["message"]["content"];
                    if (contentValue.is_string()) {
                        content = wxString::FromUTF8(contentValue.get<std::string>().c_str());
                        foundContent = true;
                        wxLogInfo(wxT("从message提取到内容: '%s'"), content);
                    }
                }
            }
            
            // 如果没有找到choices结构，尝试直接的content字段（兼容性）
            if (!foundContent && jsonObj.contains("content")) {
                auto contentValue = jsonObj["content"];
                if (contentValue.is_string()) {
                    content = wxString::FromUTF8(contentValue.get<std::string>().c_str());
                    foundContent = true;
                    wxLogInfo(wxT("从简单content提取到内容: '%s'"), content);
                }
            }
            
            if (foundContent && !content.IsEmpty()) {
                // 确保AI响应已经开始
                if (!m_aiResponseStarted) {
                    // 清除"正在思考..."消息
                    wxString currentText = m_aiChatHistoryCtrl->GetValue();
                    if (currentText.EndsWith(wxT("AI: 正在思考...\n"))) {
                        // 移除最后的"正在思考..."行
                        int lastNewline = currentText.rfind('\n', currentText.length() - 2);
                        if (lastNewline != wxNOT_FOUND) {
                            m_aiChatHistoryCtrl->SetValue(currentText.Left(lastNewline + 1));
                        }
                    }
                    
                    m_aiChatHistoryCtrl->AppendText(wxT("AI: "));
                    m_aiResponseStarted = true;
                }
                
                // 实时添加到聊天历史
                m_aiChatHistoryCtrl->AppendText(content);
                
                // 强制刷新UI以实现实时显示效果
                m_aiChatHistoryCtrl->Update();
                wxTheApp->Yield(true);
            } else {
                wxLogDebug(wxT("未找到content字段在流式JSON中: %s"), jsonData.Left(100));
            }
            
        } catch (const nlohmann::json::exception& e) {
            wxLogDebug(wxT("解析流式JSON失败: %s, 数据: %s"), wxString::FromUTF8(e.what()), jsonData.Left(100));
        }
    }
}

// AI请求超时处理
void MainFrame::OnAIRequestTimeout(wxTimerEvent& event) {
    wxLogWarning(wxT("AI请求超时"));
    if (m_isAIRequestActive) {
        wxLogInfo(wxT("取消活跃的AI请求"));
        if (m_aiWebRequest.IsOk()) {
            m_aiWebRequest.Cancel();
        }
        m_isAIRequestActive = false;
        m_aiChatHistoryCtrl->AppendText(wxT("AI: 请求超时\n"));
    } else {
        wxLogInfo(wxT("超时时没有活跃的AI请求"));
    }
}

// 停止AI超时定时器
void MainFrame::StopAITimeoutTimer() {
    if (m_aiTimeoutTimer && m_aiTimeoutTimer->IsRunning()) {
        wxLogInfo(wxT("停止AI超时定时器"));
        m_aiTimeoutTimer->Stop();
    }
}

// 添加AI消息到聊天历史
void MainFrame::AppendAIMessage(const wxString& sender, const wxString& message, const wxColour& color) {
    wxTextAttr attr;
    attr.SetTextColour(color);
    
    m_aiChatHistoryCtrl->SetDefaultStyle(attr);
    m_aiChatHistoryCtrl->AppendText(wxString::Format(wxT("%s: %s\n"), sender, message));
    
    // 重置样式
    attr.SetTextColour(*wxBLACK);
    m_aiChatHistoryCtrl->SetDefaultStyle(attr);
}

// ==================== SSE 事件处理实现 ====================

// SSE 连接打开事件
void MainFrame::OnSSEOpen(wxThreadEvent& event) {
    wxLogInfo(wxT("MainFrame::OnSSEOpen 被调用"));
    // 连接建立时，"正在思考..."消息已经显示，不需要额外操作
}

// SSE 消息事件
void MainFrame::OnSSEMessage(wxThreadEvent& event) {
    wxLogInfo(wxT("MainFrame::OnSSEMessage 被调用"));
    wxLogInfo(wxT("当前线程ID: %lu, 主线程ID: %lu"), 
              wxThread::GetCurrentId(), wxThread::GetMainId());
    wxLogInfo(wxT("事件对象: %p, 事件类型: %d"), &event, event.GetEventType());
    
    try {
        SSEEventData eventData = event.GetPayload<SSEEventData>();
        wxLogInfo(wxT("SSE 消息数据: '%s'"), eventData.data);
        
        // 检查是否有多行数据
        if (eventData.data.Contains(wxT("\n"))) {
            wxLogInfo(wxT("SSE 消息包含多行，使用 ProcessSSEData 处理"));
            ProcessSSEData(eventData.data);
        } else {
            wxLogInfo(wxT("SSE 消息是单行，使用 ProcessSSEMessage 处理"));
            ProcessSSEMessage(eventData.data);
        }
    } catch (const std::exception& e) {
        wxLogError(wxT("处理 SSE 消息时发生异常: %s"), wxString::FromUTF8(e.what()));
    }
}

// SSE 错误事件
void MainFrame::OnSSEError(wxThreadEvent& event) {
    try {
        SSEEventData eventData = event.GetPayload<SSEEventData>();
        wxLogError(wxT("SSE 错误: %s"), eventData.data);
        
        m_isAIRequestActive = false;
        StopAITimeoutTimer();
        
        // 如果还没有显示任何响应，显示错误信息
        if (!m_aiResponseStarted) {
            // 清除"正在思考..."消息
            wxString currentText = m_aiChatHistoryCtrl->GetValue();
            if (currentText.EndsWith(wxT("AI: 正在思考...\n"))) {
                int lastNewline = currentText.rfind('\n', currentText.length() - 2);
                if (lastNewline != wxNOT_FOUND) {
                    m_aiChatHistoryCtrl->SetValue(currentText.Left(lastNewline + 1));
                }
            }
            
            m_aiChatHistoryCtrl->AppendText(wxT("AI: 请求失败 - ") + eventData.data + wxT("\n"));
        }
    } catch (const std::exception& e) {
        wxLogError(wxT("处理 SSE 错误事件时发生异常: %s"), wxString::FromUTF8(e.what()));
    }
}

// SSE 连接关闭事件
void MainFrame::OnSSEClose(wxThreadEvent& event) {
    wxLogInfo(wxT("MainFrame::OnSSEClose 被调用"));
    
    // 重置AI请求状态
    m_isAIRequestActive = false;
    
    // 停止超时定时器
    StopAITimeoutTimer();
    
    // 确保响应结束时有换行
    if (m_aiResponseStarted) {
        wxString currentText = m_aiChatHistoryCtrl->GetValue();
        if (!currentText.EndsWith(wxT("\n"))) {
            m_aiChatHistoryCtrl->AppendText(wxT("\n"));
        }
        // 重置响应开始标志
        m_aiResponseStarted = false;
    }
    
    wxLogInfo(wxT("SSE 连接关闭处理完成"));
}

// 处理 SSE 消息
void MainFrame::ProcessSSEMessage(const wxString& message) {
    wxLogDebug(wxT("MainFrame::ProcessSSEMessage 处理 SSE 消息: %s"), message.Left(200));
    
    // 立即停止超时定时器
    StopAITimeoutTimer();
    
    // 如果还没有开始显示AI响应，先显示AI前缀
    if (!m_aiResponseStarted) {
        // 清除"正在思考..."消息
        wxString currentText = m_aiChatHistoryCtrl->GetValue();
        if (currentText.EndsWith(wxT("AI: 正在思考...\n"))) {
            int lastNewline = currentText.rfind('\n', currentText.length() - 2);
            if (lastNewline != wxNOT_FOUND) {
                m_aiChatHistoryCtrl->SetValue(currentText.Left(lastNewline + 1));
            }
        }
        
        m_aiChatHistoryCtrl->AppendText(wxT("AI: "));
        m_aiResponseStarted = true;
    }
    
    // 处理消息内容
    if (message == wxT("[DONE]")) {
        // 流结束标记
        wxLogInfo(wxT("SSE 流结束"));
        return;
    }
    
    try {
        // 解析 JSON 消息
        std::string messageUtf8 = message.ToUTF8().data();
        nlohmann::json jsonObj = nlohmann::json::parse(messageUtf8);
        
        wxString content;
        bool foundContent = false;
        
        // 尝试解析 choices[0].delta.content (流式响应)
        if (jsonObj.contains("choices") && jsonObj["choices"].is_array() && !jsonObj["choices"].empty()) {
            auto& choice = jsonObj["choices"][0];
            if (choice.contains("delta") && choice["delta"].contains("content")) {
                auto contentValue = choice["delta"]["content"];
                if (contentValue.is_string()) {
                    content = wxString::FromUTF8(contentValue.get<std::string>().c_str());
                    foundContent = true;
                }
            }
        }
        
        if (foundContent && !content.IsEmpty()) {
            // 实时添加到聊天历史
            m_aiChatHistoryCtrl->AppendText(content);
            
            // 强制刷新UI以实现实时显示效果
            m_aiChatHistoryCtrl->Update();
            wxTheApp->Yield(true);
        }
        
    } catch (const nlohmann::json::exception& e) {
        wxLogDebug(wxT("解析 SSE JSON 消息失败: %s"), wxString::FromUTF8(e.what()));
    }
}

// 新增：处理转录消息点击事件
void MainFrame::OnTranscriptionMessageClicked(wxCommandEvent& event) {
    m_selectedTranscriptionMessageId = event.GetInt();
    SetStatusText(wxString::Format(wxT("选中消息 ID: %d"), m_selectedTranscriptionMessageId));
}

// 新增：处理转录消息右键点击事件
void MainFrame::OnTranscriptionMessageRightClicked(wxCommandEvent& event) {
    m_selectedTranscriptionMessageId = event.GetInt();
    wxMouseEvent* mouseEvent = static_cast<wxMouseEvent*>(event.GetClientData());
    
    // 创建右键菜单
    wxMenu contextMenu;
    contextMenu.Append(ID_Context_AddNote, wxT("添加批注"));
    contextMenu.Append(ID_Context_Highlight, wxT("高亮"));
    contextMenu.Append(ID_Context_SetSpeaker, wxT("设置发言人"));
    contextMenu.AppendSeparator();
    contextMenu.Append(ID_Context_Copy, wxT("复制文本"));
    
    // 显示菜单
    PopupMenu(&contextMenu);
}


void MainFrame::OnAISend(wxCommandEvent& event)
{
    wxString userInput = m_aiUserInputCtrl->GetValue();
    if (userInput.IsEmpty()) {
        return;
    }
    
    // 添加测试功能
    if (userInput.Lower() == wxT("test")) {
        userInput = wxT("请回复'测试成功'");
        wxLogInfo(wxT("使用测试消息"));
    }
    
    m_aiChatHistoryCtrl->AppendText(wxString::Format(wxT("用户: %s\n"), userInput));
    m_aiUserInputCtrl->Clear();

    // Send the user input to the AI
    SendAIRequest(userInput);
}


void MainFrame::OnRecordToggle(wxCommandEvent& event)
{
    if (!m_isRecording) {
        // 开始录制前重新加载配置
        LoadAudioConfig();
        
        // 重置音频初始化状态，强制重新初始化
        m_isAudioInitialized = false;
        if (m_paStream) {
            Pa_CloseStream(m_paStream);
            m_paStream = nullptr;
        }
        
        // 开始录制
        if (InitializeAudioInput()) {
            StartAudioCapture();
            StartAudioRecording(); // 开始音频保存
            m_isRecording = true;
            m_recordingStartTime = wxDateTime::Now();  // 记录录音开始时间
            m_recordButton->SetLabel(wxT("正在录制..."));
            m_recordButton->SetBackgroundColour(*wxRED);
            SetStatusText(wxString::Format(wxT("录制开始... (系统音频: %s)"), 
                                         m_systemAudioMode ? wxT("启用") : wxT("禁用")));
        } else {
            wxMessageBox(wxT("无法初始化音频输入设备，请检查音频配置。"), 
                        wxT("录制失败"), wxOK | wxICON_ERROR, this);
            SetStatusText(wxT("录制初始化失败"));
        }
    } else {
        // 停止录制
        StopAudioCapture();
        StopAudioRecording(); // 停止音频保存
        m_isRecording = false;
        m_recordButton->SetLabel(wxT("开始录制"));
        m_recordButton->SetBackgroundColour(wxNullColour);
        SetStatusText(wxT("录制停止"));
        
        // 保存当前会话
        SaveCurrentSession();
    }
    m_recordButton->Refresh();
    UpdateTaskBarIconState();
}

void MainFrame::OnShowConfigDialog(wxCommandEvent& event)
{
    ConfigDialog dlg(this, wxID_ANY, wxT("MeetAnt 设置"));
    // dlg.LoadSettings(); // Hypothetical method to load current settings into dialog
    if (dlg.ShowModal() == wxID_OK) {
        // dlg.ApplySettings(); // Hypothetical method to apply settings from dialog
        SetStatusText(wxT("设置已更新 (模拟)"));
        
        // Reload AI configuration
        LoadAIConfig();
    } else {
        SetStatusText(wxT("设置未更改"));
    }
}
void MainFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox(wxT("MeetAnt 会议助手\n版本 1.0.0\n一个简单的会议记录和转录工具。"),
                 wxT("关于 MeetAnt"), wxOK | wxICON_INFORMATION, this);
}
void MainFrame::OnExit(wxCommandEvent& event)
{
    // TODO: Implement exit logic, e.g., prompt to save unsaved changes
    Close(true); // Close the frame
}
void MainFrame::ProcessAudioChunk(const float* buffer, size_t bufferSize) {
    if (!m_isFunASRInitialized && !InitializeFunASR()) {
        wxLogError(wxT("无法处理音频，FunASR未初始化"));
        return;
    }
    
    // 在实际实现中，这里应该将音频数据发送到FunASR进行处理
    // 本地模式：示例：funasr_process_audio(m_asrHandle, buffer, bufferSize, ...);
    // 云端模式：这里应该向服务器发送音频数据
    
    // 由于是模拟，每隔一段时间生成一个虚拟识别结果
    static int frameCount = 0;
    frameCount++;
    
    // 每30帧（假设约1秒）生成一个识别结果
    if (frameCount % 30 == 0) {
        wxDateTime now = wxDateTime::Now();
        wxString simulatedResult = wxString::Format(wxT("这是在%s模拟的识别结果 %d"), 
                                                  m_isLocalMode ? wxT("本地") : wxT("云端"), 
                                                  frameCount / 30);
        
        // 模拟非最终和最终结果
        bool isFinal = (frameCount % 90 == 0); // 每3个结果一个最终结果
        
        // 在UI线程中处理结果
        wxCommandEvent resultEvent(wxEVT_COMMAND_TEXT_UPDATED, ID_ASRResult);
        resultEvent.SetString(simulatedResult);
        resultEvent.SetInt(isFinal ? 1 : 0);
        GetEventHandler()->AddPendingEvent(resultEvent);
    }
}
void MainFrame::HandleRecognitionResult(const wxString& text, bool isFinal) {
    // 这个方法应该在UI线程中处理识别结果
    
    if (!isFinal) {
        // 非最终结果，可以显示为临时文本
        SetStatusText(wxString::Format(wxT("正在识别: %s"), text));
    } else {
        // 最终结果，添加到转录
        wxRichTextAttr timeAttr;
        timeAttr.SetTextColour(wxColour(100, 100, 100));
        timeAttr.SetFontWeight(wxFONTWEIGHT_BOLD);
        
        wxDateTime now = wxDateTime::Now();
        
        // 添加时间戳
        // m_transcriptionTextCtrl->BeginStyle(timeAttr);
        // m_transcriptionTextCtrl->AppendText(wxString::Format(wxT("[%s] "), now.FormatTime()));
        // m_transcriptionTextCtrl->EndStyle();
        
        // 添加识别文本
        // m_transcriptionTextCtrl->AppendText(text + wxT("\n"));
        
        SetStatusText(wxString::Format(wxT("识别文本: %s"), text.Left(30)));
        
        // 自动保存会话
        if (!m_currentSessionPath.IsEmpty()) {
            SaveCurrentSession();
        }
    }
}

// 添加测试转录数据的方法
void MainFrame::AddTestTranscriptionData() {
    // 模拟一段会议对话
    wxDateTime baseTime = wxDateTime::Now();
    baseTime.Subtract(wxTimeSpan::Minutes(10)); // 从10分钟前开始
    
    // 记录开始时间
    wxDateTime startTime = baseTime;
    m_recordingStartTime = startTime;  // 设置录音开始时间
    
    // 添加一系列对话消息
    m_transcriptionBubbleCtrl->AddMessage(wxT("张经理"), 
        wxT("大家好，今天我们开始讨论新产品的发布计划。首先请李总介绍一下市场调研的情况。"), 
        baseTime);
    m_playbackControlBar->AddTimeMarker((baseTime - startTime).GetMilliseconds().ToLong(), wxT("张经理"));
    
    baseTime.Add(wxTimeSpan::Seconds(30));
    m_transcriptionBubbleCtrl->AddMessage(wxT("李总"), 
        wxT("谢谢张经理。根据我们最近的市场调研，目标用户群体主要集中在25-40岁的职场人士，他们对产品的便携性和易用性有较高要求。"), 
        baseTime);
    m_playbackControlBar->AddTimeMarker((baseTime - startTime).GetMilliseconds().ToLong(), wxT("李总"));
    
    baseTime.Add(wxTimeSpan::Seconds(45));
    m_transcriptionBubbleCtrl->AddMessage(wxT("李总"), 
        wxT("调研数据显示，有73%的受访者表示愿意为高质量的产品支付溢价，这给了我们很大的信心。"), 
        baseTime);
    
    baseTime.Add(wxTimeSpan::Seconds(20));
    m_transcriptionBubbleCtrl->AddMessage(wxT("王工"), 
        wxT("从技术角度来说，我们的产品在性能上已经达到了行业领先水平。特别是在续航和稳定性方面，比竞品有明显优势。"), 
        baseTime);
    m_playbackControlBar->AddTimeMarker((baseTime - startTime).GetMilliseconds().ToLong(), wxT("王工"));
    
    baseTime.Add(wxTimeSpan::Seconds(35));
    m_transcriptionBubbleCtrl->AddMessage(wxT("张经理"), 
        wxT("很好。那么关于定价策略，财务部有什么建议吗？"), 
        baseTime);
    m_playbackControlBar->AddTimeMarker((baseTime - startTime).GetMilliseconds().ToLong(), wxT("张经理"));
    
    baseTime.Add(wxTimeSpan::Seconds(25));
    m_transcriptionBubbleCtrl->AddMessage(wxT("赵会计"), 
        wxT("根据成本核算和市场定位，我建议定价在2999-3499元之间。这样既能保证合理的利润率，又具有市场竞争力。"), 
        baseTime);
    m_playbackControlBar->AddTimeMarker((baseTime - startTime).GetMilliseconds().ToLong(), wxT("赵会计"));
    
    baseTime.Add(wxTimeSpan::Seconds(40));
    m_transcriptionBubbleCtrl->AddMessage(wxT("刘经理"), 
        wxT("营销方面，我们计划采用线上线下结合的方式。线上主要通过社交媒体和KOL合作，线下则在主要城市的商场设置体验店。"), 
        baseTime);
    m_playbackControlBar->AddTimeMarker((baseTime - startTime).GetMilliseconds().ToLong(), wxT("刘经理"));
    
    baseTime.Add(wxTimeSpan::Seconds(30));
    m_transcriptionBubbleCtrl->AddMessage(wxT("张经理"), 
        wxT("听起来计划很完善。大家还有什么需要补充的吗？"), 
        baseTime);
    
    baseTime.Add(wxTimeSpan::Seconds(15));
    m_transcriptionBubbleCtrl->AddMessage(wxT("李总"), 
        wxT("我建议我们在正式发布前，先做一个小规模的测试发布，收集用户反馈。"), 
        baseTime);
    m_playbackControlBar->AddTimeMarker((baseTime - startTime).GetMilliseconds().ToLong(), wxT("李总"));
    
    baseTime.Add(wxTimeSpan::Seconds(20));
    m_transcriptionBubbleCtrl->AddMessage(wxT("张经理"), 
        wxT("好主意。那我们就按这个方向推进。下周三之前，各部门提交详细的执行计划。今天的会议就到这里，谢谢大家！"), 
        baseTime);
    
    // 设置总时长
    wxTimeSpan totalDuration = baseTime - startTime;
    long totalDurationMs = totalDuration.GetMilliseconds().ToLong();
    m_playbackControlBar->SetDuration(totalDurationMs);
    
    // 设置一些发言人的颜色
    m_transcriptionBubbleCtrl->SetSpeakerColor(wxT("张经理"), wxColour(65, 105, 225));  // 皇家蓝
    m_transcriptionBubbleCtrl->SetSpeakerColor(wxT("李总"), wxColour(220, 20, 60));     // 深红色
    m_transcriptionBubbleCtrl->SetSpeakerColor(wxT("王工"), wxColour(34, 139, 34));     // 森林绿
    m_transcriptionBubbleCtrl->SetSpeakerColor(wxT("赵会计"), wxColour(255, 140, 0));   // 深橙色
    m_transcriptionBubbleCtrl->SetSpeakerColor(wxT("刘经理"), wxColour(138, 43, 226));  // 蓝紫色
    
    wxLogInfo(wxT("已添加测试转录数据"));
}

// 播放控制事件处理函数
void MainFrame::OnPlaybackPositionChanged(wxCommandEvent& event) {
    int positionMs = event.GetInt();
    
    // 根据播放位置滚动到对应的消息
    const auto& messages = m_transcriptionBubbleCtrl->GetMessages();
    for (const auto& msg : messages) {
        // 假设消息的时间戳是从录音开始的毫秒数
        long msgTimeMs = (msg.timestamp.GetTicks() - m_recordingStartTime.GetTicks()) * 1000;
        
        // 找到最接近当前播放位置的消息
        if (msgTimeMs <= positionMs && msgTimeMs + 5000 > positionMs) { // 5秒窗口
            m_transcriptionBubbleCtrl->ScrollToMessage(msg.messageId);
            break;
        }
    }
}

void MainFrame::OnPlaybackStateChanged(wxCommandEvent& event) {
    int state = event.GetInt();
    
    if (state == 1) { // 播放
        SetStatusText(wxT("正在播放录音"));
    } else if (state == 0) { // 暂停
        SetStatusText(wxT("播放已暂停"));
    } else if (state == -1) { // 停止
        SetStatusText(wxT("播放已停止"));
    }
}