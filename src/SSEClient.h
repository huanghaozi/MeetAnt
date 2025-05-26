#ifndef SSECLIENT_H
#define SSECLIENT_H

#include <wx/wx.h>
#include <wx/event.h>
#include <wx/thread.h>
#include <wx/eventfilter.h>
#include <memory>
#include <functional>
#include <string>
#include <map>

// SSE 事件类型
enum class SSEEventType {
    Message,    // 普通消息
    Error,      // 错误
    Open,       // 连接打开
    Close       // 连接关闭
};

// SSE 事件数据
struct SSEEventData {
    SSEEventType type;
    wxString eventName;  // SSE 事件名称（如果有）
    wxString data;       // 事件数据
    wxString id;         // 事件ID（如果有）
    int retry = -1;      // 重连时间（毫秒）
};

// 声明自定义事件
wxDECLARE_EVENT(wxEVT_SSE_MESSAGE, wxThreadEvent);
wxDECLARE_EVENT(wxEVT_SSE_ERROR, wxThreadEvent);
wxDECLARE_EVENT(wxEVT_SSE_OPEN, wxThreadEvent);
wxDECLARE_EVENT(wxEVT_SSE_CLOSE, wxThreadEvent);

// SSE 客户端类
class SSEClient : public wxEvtHandler {
public:
    SSEClient(wxEvtHandler* parent = nullptr);
    virtual ~SSEClient();
    
    // 连接到 SSE 端点
    bool Connect(const wxString& url, const std::map<wxString, wxString>& headers = {});
    
    // 使用 POST 方法连接到 SSE 端点
    bool ConnectWithPost(const wxString& url, 
                        const wxString& requestBody,
                        const std::map<wxString, wxString>& headers = {});
    
    // 断开连接
    void Disconnect();
    
    // 是否已连接
    bool IsConnected() const;
    
    // 设置重连间隔（毫秒）
    void SetReconnectInterval(int interval) { m_reconnectInterval = interval; }
    
    // 设置是否自动重连
    void SetAutoReconnect(bool autoReconnect) { m_autoReconnect = autoReconnect; }
    
    // 设置连接超时（秒）
    void SetTimeout(int timeout) { m_timeout = timeout; }
    
protected:
    // 内部实现类
    class SSEWorkerThread;
    friend class SSEWorkerThread;  // 允许工作线程访问私有成员
    
private:
    wxEvtHandler* m_parent;
    std::unique_ptr<SSEWorkerThread> m_workerThread;
    wxString m_url;
    std::map<wxString, wxString> m_headers;
    std::shared_ptr<std::string> m_requestBody;  // 请求体（POST 请求使用）
    int m_reconnectInterval = 3000;  // 默认3秒重连
    bool m_autoReconnect = true;
    int m_timeout = 30;  // 默认30秒超时
    bool m_connected = false;
    
    // 处理来自工作线程的事件
    void OnWorkerEvent(wxThreadEvent& event);
    
    wxDECLARE_EVENT_TABLE();
};

#endif // SSECLIENT_H 