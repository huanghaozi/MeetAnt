#ifndef TRANSCRIPTION_BUBBLE_CTRL_H
#define TRANSCRIPTION_BUBBLE_CTRL_H

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/datetime.h>
#include <vector>
#include <map>
#include <memory>

// 转录消息结构
struct TranscriptionMessage {
    wxString speakerName;      // 发言人名称
    wxString content;          // 发言内容
    wxDateTime timestamp;      // 时间戳
    wxColour speakerColor;     // 发言人对应的颜色
    bool isHighlighted;        // 是否高亮
    int messageId;             // 消息ID，用于定位和引用
    
    TranscriptionMessage() : isHighlighted(false), messageId(0) {}
};

// 自定义转录气泡控件
class TranscriptionBubbleCtrl : public wxScrolledWindow {
public:
    TranscriptionBubbleCtrl(wxWindow* parent, wxWindowID id = wxID_ANY,
                           const wxPoint& pos = wxDefaultPosition,
                           const wxSize& size = wxDefaultSize,
                           long style = wxVSCROLL | wxHSCROLL);
    
    virtual ~TranscriptionBubbleCtrl();
    
    // 添加新的转录消息
    void AddMessage(const wxString& speaker, const wxString& content, 
                   const wxDateTime& timestamp = wxDateTime::Now());
    
    // 清空所有消息
    void Clear();
    
    // 设置发言人颜色
    void SetSpeakerColor(const wxString& speaker, const wxColour& color);
    
    // 获取发言人颜色
    wxColour GetSpeakerColor(const wxString& speaker) const;
    
    // 高亮指定消息
    void HighlightMessage(int messageId, bool highlight = true);
    
    // 搜索文本
    std::vector<int> SearchText(const wxString& searchText, bool caseSensitive = false);
    
    // 滚动到指定消息
    void ScrollToMessage(int messageId);
    
    // 获取所有消息
    const std::vector<TranscriptionMessage>& GetMessages() const { return m_messages; }
    
    // 设置是否显示时间戳
    void ShowTimestamps(bool show) { m_showTimestamps = show; Refresh(); }
    
    // 设置是否自动滚动到底部
    void SetAutoScroll(bool autoScroll) { m_autoScroll = autoScroll; }
    
    // 导出为文本
    wxString ExportAsText() const;
    
protected:
    // 绘制事件处理
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseRightDown(wxMouseEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    
    // 计算布局
    void CalculateLayout();
    
    // 绘制单个消息气泡
    void DrawMessageBubble(wxDC* dc, const TranscriptionMessage& msg, 
                          const wxRect& bubbleRect, bool isHovered);
    
    // 计算文本大小
    wxSize CalculateTextSize(wxDC& dc, const wxString& text, int maxWidth);
    
    // 获取鼠标位置对应的消息索引
    int GetMessageAtPoint(const wxPoint& pt) const;
    
    // 生成发言人默认颜色
    wxColour GenerateSpeakerColor(const wxString& speaker);
    
private:
    // 消息数据
    std::vector<TranscriptionMessage> m_messages;
    
    // 发言人颜色映射
    std::map<wxString, wxColour> m_speakerColors;
    
    // 消息布局信息
    struct MessageLayout {
        wxRect bubbleRect;      // 气泡矩形
        wxRect textRect;        // 文本矩形
        wxRect avatarRect;      // 头像矩形
        wxRect timestampRect;   // 时间戳矩形
        int messageId;          // 对应的消息ID
    };
    std::vector<MessageLayout> m_layouts;
    
    // 控件设置
    bool m_showTimestamps;      // 是否显示时间戳
    bool m_autoScroll;          // 是否自动滚动
    int m_bubbleMargin;         // 气泡边距
    int m_bubblePadding;        // 气泡内边距
    int m_maxBubbleWidth;       // 最大气泡宽度
    int m_avatarSize;           // 头像大小
    
    // 交互状态
    int m_hoveredMessage;       // 鼠标悬停的消息索引
    int m_selectedMessage;      // 选中的消息索引
    
    // 颜色方案
    wxColour m_backgroundColor;
    wxColour m_bubbleColorLight;
    wxColour m_bubbleColorDark;
    wxColour m_textColor;
    wxColour m_timestampColor;
    wxColour m_highlightColor;
    
    // 字体设置
    wxFont m_messageFont;
    wxFont m_speakerFont;
    wxFont m_timestampFont;
    
    // 搜索结果
    std::vector<int> m_searchResults;
    int m_currentSearchIndex;
    
    // 消息ID计数器
    int m_nextMessageId;
    
    // 虚拟高度（用于滚动）
    int m_virtualHeight;
    
    // 默认颜色列表
    static const std::vector<wxColour> s_defaultColors;
    
    wxDECLARE_EVENT_TABLE();
};

// 自定义事件
wxDECLARE_EVENT(wxEVT_TRANSCRIPTION_MESSAGE_CLICKED, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_TRANSCRIPTION_MESSAGE_RIGHT_CLICKED, wxCommandEvent);

#endif // TRANSCRIPTION_BUBBLE_CTRL_H 