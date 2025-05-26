#include "SSEClient.h"
#include <wx/log.h>
#include <sstream>
#include <regex>
#include <curl/curl.h>

// 全局初始化辅助类
class CurlGlobalInit {
public:
    CurlGlobalInit() {
        CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
        if (res != CURLE_OK) {
            wxLogError(wxT("curl_global_init 失败: %s"), wxString::FromUTF8(curl_easy_strerror(res)));
        }
    }
    ~CurlGlobalInit() {
        curl_global_cleanup();
    }
};

// 静态全局初始化对象
static CurlGlobalInit s_curlInit;

// 定义事件
wxDEFINE_EVENT(wxEVT_SSE_MESSAGE, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_SSE_ERROR, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_SSE_OPEN, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_SSE_CLOSE, wxThreadEvent);

// SSE 工作线程实现
class SSEClient::SSEWorkerThread : public wxThread {
public:
    SSEWorkerThread(SSEClient* client, const wxString& url, 
                   const std::map<wxString, wxString>& headers)
        : wxThread(wxTHREAD_JOINABLE)
        , m_client(client)
        , m_url(url)
        , m_headers(headers)
        , m_shouldStop(false)
        , m_curl(nullptr) {
    }
    
    virtual ~SSEWorkerThread() {
        Stop();
    }
    
    void Stop() {
        m_shouldStop = true;
        // 如果 curl 正在运行，中断它
        if (m_curl) {
            // 这会导致 curl_easy_perform 返回 CURLE_ABORTED_BY_CALLBACK
            // 注意：这个操作不是线程安全的，但在实践中通常没问题
            // 更安全的方法是使用 curl_multi_* API
        }
    }
    
protected:
    virtual ExitCode Entry() override {
        CURL* curl = nullptr;
        CURLcode res;
        
        try {
            // 初始化 libcurl
            curl = curl_easy_init();
            if (!curl) {
                SendErrorEvent(wxT("无法初始化 libcurl"));
                return (ExitCode)1;
            }
            
            m_curl = curl; // 保存 curl 句柄以便 Stop() 使用
            
            // 设置 URL
            std::string urlUtf8 = m_url.ToUTF8().data();
            curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.c_str());
            
            // 设置请求方法和请求体
            if (m_client->m_requestBody && !m_client->m_requestBody->empty()) {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, m_client->m_requestBody->c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)m_client->m_requestBody->size());
            }
            
            // 设置请求头
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Accept: text/event-stream");
            headers = curl_slist_append(headers, "Cache-Control: no-cache");
            headers = curl_slist_append(headers, "Connection: keep-alive");
            
            for (const auto& header : m_headers) {
                std::string headerStr = header.first.ToUTF8().data() + std::string(": ") + 
                                       header.second.ToUTF8().data();
                headers = curl_slist_append(headers, headerStr.c_str());
            }
            
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            
            // 设置超时（秒）
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); // 0 = 无超时，SSE 需要长连接
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)m_client->m_timeout);
            
            // 设置低速限制，防止连接被过早断开
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);  // 1 字节/秒
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);  // 60 秒
            
            // 设置写入回调
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
            
            // 设置进度回调（用于检查是否应该停止）
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
            
            // 禁用信号（多线程环境下更安全）
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            
            // 跟随重定向
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
            
            // SSL 选项
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
            
            // 在开发环境中，如果SSL验证有问题，可以暂时禁用（生产环境不建议）
            // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            
            // 启用详细输出（调试用）
            if (wxLog::GetVerbose()) {
                curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
                // 设置调试回调
                curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, CurlDebugCallback);
                curl_easy_setopt(curl, CURLOPT_DEBUGDATA, this);
            }
            
            // 设置用户代理
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "MeetAnt SSEClient/1.0");
            
            // 强制使用 HTTP/1.1
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
            
            // 设置期望的 Transfer-Encoding
            curl_easy_setopt(curl, CURLOPT_TRANSFER_ENCODING, 1L);
            
            // 设置错误缓冲区
            char errbuf[CURL_ERROR_SIZE];
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
            errbuf[0] = 0;
            
            // 发送连接成功事件
            SendOpenEvent();
            
            // 执行请求
            res = curl_easy_perform(curl);
            
            if (res != CURLE_OK && res != CURLE_ABORTED_BY_CALLBACK) {
                if (!m_shouldStop) {
                    wxString errorMsg = wxString::Format(wxT("libcurl 错误: %s"), 
                                                      wxString::FromUTF8(curl_easy_strerror(res)));
                    if (strlen(errbuf) > 0) {
                        errorMsg += wxT(" - ") + wxString::FromUTF8(errbuf);
                    }
                    
                    // 获取HTTP响应码
                    long httpCode = 0;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
                    if (httpCode > 0) {
                        errorMsg += wxString::Format(wxT(" (HTTP %ld)"), httpCode);
                    }
                    
                    SendErrorEvent(errorMsg);
                }
            } else if (res == CURLE_OK) {
                // 正常完成（服务器关闭了连接）
            }
            
            // 清理
            if (headers) {
                curl_slist_free_all(headers);
            }
            
            curl_easy_cleanup(curl);
            m_curl = nullptr;
            
        } catch (const std::exception& e) {
            SendErrorEvent(wxString::Format(wxT("异常: %s"), wxString::FromUTF8(e.what())));
            if (curl) {
                curl_easy_cleanup(curl);
                m_curl = nullptr;
            }
        } catch (...) {
            SendErrorEvent(wxT("未知异常"));
            if (curl) {
                curl_easy_cleanup(curl);
                m_curl = nullptr;
            }
        }
        
        // 发送关闭事件
        SendCloseEvent();
        
        return (ExitCode)0;
    }
    
private:
    SSEClient* m_client;
    wxString m_url;
    std::map<wxString, wxString> m_headers;
    bool m_shouldStop;
    std::string m_buffer;  // 用于存储未完成的行
    CURL* m_curl;          // 保存 curl 句柄
    
    // libcurl 写入回调
    static size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        SSEWorkerThread* thread = static_cast<SSEWorkerThread*>(userdata);
        size_t realsize = size * nmemb;
        
        if (thread->m_shouldStop || thread->TestDestroy()) {
            return 0; // 返回 0 会导致 curl 中止传输
        }
        
        // 处理接收到的数据
        thread->ProcessData(std::string(ptr, realsize));
        return realsize;
    }
    
    // libcurl 调试回调
    static int CurlDebugCallback(CURL* handle, curl_infotype type, char* data, size_t size, void* userdata) {
        // 只在真正需要调试时输出
        if (!wxLog::GetVerbose()) {
            return 0;
        }
        
        wxString prefix;
        switch (type) {
            case CURLINFO_TEXT:
                prefix = wxT("SSEClient [TEXT]: ");
                break;
            case CURLINFO_HEADER_IN:
                prefix = wxT("SSEClient [HEADER_IN]: ");
                break;
            case CURLINFO_HEADER_OUT:
                prefix = wxT("SSEClient [HEADER_OUT]: ");
                break;
            case CURLINFO_DATA_IN:
                // 数据输入通常太多，跳过
                return 0;
            case CURLINFO_DATA_OUT:
                // 数据输出通常太多，跳过
                return 0;
            default:
                return 0;
        }
        
        wxString info = wxString::FromUTF8(data, size);
        info.Trim();
        if (!info.IsEmpty()) {
            wxLogInfo(wxT("%s%s"), prefix, info);
        }
        return 0;
    }
    
    // libcurl 进度回调（用于检查是否应该停止）
    static int CurlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                                   curl_off_t ultotal, curl_off_t ulnow) {
        SSEWorkerThread* thread = static_cast<SSEWorkerThread*>(clientp);
        
        // 返回非 0 值会导致 curl 中止传输
        if (thread->m_shouldStop || thread->TestDestroy()) {
            return 1;
        }
        
        return 0; // 继续
    }
    
    void ProcessData(const std::string& data) {
        m_buffer += data;
        
        // 处理完整的行
        size_t pos = 0;
        while ((pos = m_buffer.find('\n')) != std::string::npos) {
            std::string line = m_buffer.substr(0, pos);
            m_buffer.erase(0, pos + 1);
            
            // 移除可能的 \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
            // 跳过可能的 chunked 编码长度行（纯十六进制数字）
            bool isChunkSize = true;
            if (!line.empty() && line.length() <= 8) {
                for (char c : line) {
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                        isChunkSize = false;
                        break;
                    }
                }
                if (isChunkSize) {
                    continue;
                }
            }
            
            ProcessLine(line);
        }
    }
    
    void ProcessLine(const std::string& line) {
        if (line.empty()) {
            // 空行，SSE 事件分隔符
            return;
        }
        
        // 忽略注释
        if (line[0] == ':') {
            return;
        }
        
        // 解析字段
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            return;
        }
        
        std::string field = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);
        
        // 移除值开头的空格
        if (!value.empty() && value[0] == ' ') {
            value.erase(0, 1);
        }
        
        // 对于流式输出，每个 data 行都立即发送
        if (field == "data") {
            SSEEventData eventData;
            eventData.type = SSEEventType::Message;
            eventData.data = wxString::FromUTF8(value.c_str());
            
            SendMessageEvent(eventData);
            
        } else if (field == "event") {
            // 处理自定义事件类型
            wxString eventName = wxString::FromUTF8(value.c_str());
            
        } else if (field == "id") {
            // 处理事件ID
            wxString eventId = wxString::FromUTF8(value.c_str());
            
        } else if (field == "retry") {
            try {
                int retry = std::stoi(value);
                if (retry > 0) {
                    m_client->SetReconnectInterval(retry);
                }
            } catch (...) {
                // 忽略无效的重试值
            }
        }
    }
    
    void SendMessageEvent(const SSEEventData& data) {
        wxThreadEvent* event = new wxThreadEvent(wxEVT_SSE_MESSAGE);
        event->SetPayload(data);
        // 直接发送到最终目标（MainFrame）
        if (m_client->m_parent) {
            wxQueueEvent(m_client->m_parent, event);
        } else {
            delete event;
            wxLogError(wxT("SSEClient: 无法发送消息事件，parent为空"));
        }
    }
    
    void SendErrorEvent(const wxString& error) {
        SSEEventData data;
        data.type = SSEEventType::Error;
        data.data = error;
        
        wxThreadEvent* event = new wxThreadEvent(wxEVT_SSE_ERROR);
        event->SetPayload(data);
        // 直接发送到最终目标（MainFrame）
        if (m_client->m_parent) {
            wxQueueEvent(m_client->m_parent, event);
        } else {
            delete event;
        }
    }
    
    void SendOpenEvent() {
        SSEEventData data;
        data.type = SSEEventType::Open;
        
        wxThreadEvent* event = new wxThreadEvent(wxEVT_SSE_OPEN);
        event->SetPayload(data);
        // 直接发送到最终目标（MainFrame）
        if (m_client->m_parent) {
            wxQueueEvent(m_client->m_parent, event);
        } else {
            delete event;
        }
    }
    
    void SendCloseEvent() {
        SSEEventData data;
        data.type = SSEEventType::Close;
        
        wxThreadEvent* event = new wxThreadEvent(wxEVT_SSE_CLOSE);
        event->SetPayload(data);
        // 直接发送到最终目标（MainFrame）
        if (m_client->m_parent) {
            wxQueueEvent(m_client->m_parent, event);
        } else {
            delete event;
        }
    }
};

// SSEClient 实现
wxBEGIN_EVENT_TABLE(SSEClient, wxEvtHandler)
    EVT_THREAD(wxEVT_SSE_MESSAGE, SSEClient::OnWorkerEvent)
    EVT_THREAD(wxEVT_SSE_OPEN, SSEClient::OnWorkerEvent)
    EVT_THREAD(wxEVT_SSE_CLOSE, SSEClient::OnWorkerEvent)
    EVT_THREAD(wxEVT_SSE_ERROR, SSEClient::OnWorkerEvent)
wxEND_EVENT_TABLE()

SSEClient::SSEClient(wxEvtHandler* parent)
    : m_parent(parent) {
}

SSEClient::~SSEClient() {
    Disconnect();
}

bool SSEClient::Connect(const wxString& url, const std::map<wxString, wxString>& headers) {
    if (m_connected) {
        wxLogWarning(wxT("SSEClient: 已经连接"));
        return false;
    }
    
    m_url = url;
    m_headers = headers;
    m_requestBody.reset();  // 清空请求体（GET 请求）
    
    // 创建并启动工作线程
    m_workerThread = std::make_unique<SSEWorkerThread>(this, m_url, m_headers);
    
    if (m_workerThread->Run() != wxTHREAD_NO_ERROR) {
        wxLogError(wxT("SSEClient: 无法启动工作线程"));
        m_workerThread.reset();
        return false;
    }
    
    m_connected = true;
    return true;
}

bool SSEClient::ConnectWithPost(const wxString& url, 
                               const wxString& requestBody,
                               const std::map<wxString, wxString>& headers) {
    if (m_connected) {
        wxLogWarning(wxT("SSEClient: 已经连接"));
        return false;
    }
    
    m_url = url;
    m_headers = headers;
    m_requestBody = std::make_shared<std::string>(requestBody.ToUTF8().data());
    
    // 创建并启动工作线程
    m_workerThread = std::make_unique<SSEWorkerThread>(this, m_url, m_headers);
    
    if (m_workerThread->Run() != wxTHREAD_NO_ERROR) {
        wxLogError(wxT("SSEClient: 无法启动工作线程"));
        m_workerThread.reset();
        return false;
    }
    
    m_connected = true;
    return true;
}

void SSEClient::Disconnect() {
    if (!m_connected || !m_workerThread) {
        return;
    }
    
    m_workerThread->Stop();
    m_workerThread->Wait();
    m_workerThread.reset();
    
    m_connected = false;
}

bool SSEClient::IsConnected() const {
    return m_connected;
}

void SSEClient::OnWorkerEvent(wxThreadEvent& event) {
    wxEventType eventType = event.GetEventType();
    
    // 转发事件给父窗口
    if (m_parent) {
        // 创建新的事件副本并转发
        // 使用 wxQueueEvent 而不是 ProcessEvent，确保跨线程事件传递正确
        wxThreadEvent* newEvent = new wxThreadEvent(eventType);
        newEvent->SetEventObject(this);
        
        // 复制事件数据
        try {
            SSEEventData eventData = event.GetPayload<SSEEventData>();
            newEvent->SetPayload(eventData);
        } catch (...) {
            wxLogError(wxT("SSEClient::OnWorkerEvent 复制事件数据失败"));
        }
        
        // 使用 wxQueueEvent 发送事件，它会负责管理事件的生命周期
        wxQueueEvent(m_parent, newEvent);
    } else {
        wxLogError(wxT("SSEClient::OnWorkerEvent m_parent 为空，无法转发事件"));
    }
    
    // 更新连接状态
    if (event.GetEventType() == wxEVT_SSE_CLOSE || 
        event.GetEventType() == wxEVT_SSE_ERROR) {
        m_connected = false;
        
        // 处理自动重连
        if (m_autoReconnect && event.GetEventType() == wxEVT_SSE_ERROR) {
            wxLogInfo(wxT("SSEClient: 将在 %d 毫秒后尝试重连"), m_reconnectInterval);
            // TODO: 实现自动重连逻辑
            // 可以使用 wxTimer 来延迟重连
        }
    }
} 