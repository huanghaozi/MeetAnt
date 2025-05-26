#include "App.h"
#include "MainFrame.h"
#include <wx/image.h>
#include <wx/log.h>
#include <ctime>
#include <portaudio.h>
#include <wx/wx.h>

bool MeetAntApp::OnInit() {
    if (!wxApp::OnInit())
        return false;

    // 初始化随机数生成器
    m_randomEngine.seed(static_cast<unsigned int>(time(nullptr)));

    // 设置日志级别
#ifdef NDEBUG
    // Release模式：只显示错误和警告
    wxLog::SetLogLevel(wxLOG_Warning);
    // 禁用调试和信息日志显示到窗口的弹窗
    wxLog* oldLogger = wxLog::SetActiveTarget(new wxLogStderr());
    delete oldLogger;
#else
    // Debug模式：显示所有级别的日志，但不弹窗
    wxLog::SetLogLevel(wxLOG_Debug);
    // 使用标准的GUI日志但将其重定向到stderr以避免弹窗
    wxLog* oldLogger = wxLog::SetActiveTarget(new wxLogStderr());
    delete oldLogger;
#endif
    const PaVersionInfo* versionInfo = Pa_GetVersionInfo();
    if (versionInfo) {
        wxLogDebug(wxT("PortAudio version: %s"), wxString::FromUTF8(versionInfo->versionText));
    } else {
        wxLogDebug(wxT("Could not get PortAudio version info."));
    }
    Pa_Initialize();

    // 添加 PNG 图片处理器
    wxImage::AddHandler(new wxPNGHandler);

    // 创建主窗口
    MainFrame *frame = new MainFrame(wxT("MeetAnt 会议助手"), wxPoint(50, 50), wxSize(1000, 700));
    frame->Show(true);
    SetTopWindow(frame);


    return true;
}

// 生成指定范围内的随机数
int MeetAntApp::GetRandomNumber(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(m_randomEngine);
} 