#include "TranscriptionBubbleCtrl.h"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <algorithm>
#include <cmath>

// 定义事件
wxDEFINE_EVENT(wxEVT_TRANSCRIPTION_MESSAGE_CLICKED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_TRANSCRIPTION_MESSAGE_RIGHT_CLICKED, wxCommandEvent);

// 事件表
wxBEGIN_EVENT_TABLE(TranscriptionBubbleCtrl, wxScrolledWindow)
    EVT_PAINT(TranscriptionBubbleCtrl::OnPaint)
    EVT_SIZE(TranscriptionBubbleCtrl::OnSize)
    EVT_MOUSEWHEEL(TranscriptionBubbleCtrl::OnMouseWheel)
    EVT_LEFT_DOWN(TranscriptionBubbleCtrl::OnMouseLeftDown)
    EVT_RIGHT_DOWN(TranscriptionBubbleCtrl::OnMouseRightDown)
    EVT_MOTION(TranscriptionBubbleCtrl::OnMouseMotion)
    EVT_ERASE_BACKGROUND(TranscriptionBubbleCtrl::OnEraseBackground)
wxEND_EVENT_TABLE()

// 默认颜色列表
const std::vector<wxColour> TranscriptionBubbleCtrl::s_defaultColors = {
    wxColour(255, 179, 102),  // 橙色
    wxColour(102, 204, 255),  // 浅蓝色
    wxColour(153, 255, 153),  // 浅绿色
    wxColour(255, 153, 204),  // 粉色
    wxColour(204, 153, 255),  // 紫色
    wxColour(255, 255, 153),  // 浅黄色
    wxColour(153, 204, 255),  // 天蓝色
    wxColour(255, 204, 153)   // 浅橙色
};

TranscriptionBubbleCtrl::TranscriptionBubbleCtrl(wxWindow* parent, wxWindowID id,
                                               const wxPoint& pos, const wxSize& size,
                                               long style)
    : wxScrolledWindow(parent, id, pos, size, style | wxFULL_REPAINT_ON_RESIZE),
      m_showTimestamps(true),
      m_autoScroll(true),
      m_bubbleMargin(10),
      m_bubblePadding(12),
      m_maxBubbleWidth(400),
      m_avatarSize(40),
      m_hoveredMessage(-1),
      m_selectedMessage(-1),
      m_currentSearchIndex(-1),
      m_nextMessageId(1),
      m_virtualHeight(0)
{
    // 设置背景色
    m_backgroundColor = wxColour(255, 255, 255);
    SetBackgroundColour(m_backgroundColor);
    
    // 设置颜色方案
    m_bubbleColorLight = wxColour(230, 240, 250);
    m_bubbleColorDark = wxColour(200, 220, 240);
    m_textColor = wxColour(30, 30, 30);
    m_timestampColor = wxColour(120, 120, 120);
    m_highlightColor = wxColour(255, 235, 153);
    
    // 设置字体
    wxFont defaultFont = GetFont();
    m_messageFont = wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_speakerFont = wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    m_timestampFont = wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    
    // 设置滚动速率
    SetScrollRate(10, 10);
    
    // 启用双缓冲
    SetBackgroundStyle(wxBG_STYLE_PAINT);
}

TranscriptionBubbleCtrl::~TranscriptionBubbleCtrl() {
    // 清理资源
}

void TranscriptionBubbleCtrl::AddMessage(const wxString& speaker, const wxString& content,
                                        const wxDateTime& timestamp) {
    TranscriptionMessage msg;
    msg.speakerName = speaker;
    msg.content = content;
    msg.timestamp = timestamp;
    msg.messageId = m_nextMessageId++;
    
    // 获取或生成发言人颜色
    msg.speakerColor = GetSpeakerColor(speaker);
    
    m_messages.push_back(msg);
    
    // 重新计算布局
    CalculateLayout();
    
    // 如果启用了自动滚动，滚动到底部
    if (m_autoScroll) {
        ScrollToMessage(msg.messageId);
    }
    
    Refresh();
}

void TranscriptionBubbleCtrl::Clear() {
    m_messages.clear();
    m_layouts.clear();
    m_virtualHeight = 0;
    m_hoveredMessage = -1;
    m_selectedMessage = -1;
    m_searchResults.clear();
    m_currentSearchIndex = -1;
    
    SetVirtualSize(GetClientSize().GetWidth(), 0);
    Refresh();
}

void TranscriptionBubbleCtrl::SetSpeakerColor(const wxString& speaker, const wxColour& color) {
    m_speakerColors[speaker] = color;
    
    // 更新现有消息的颜色
    for (auto& msg : m_messages) {
        if (msg.speakerName == speaker) {
            msg.speakerColor = color;
        }
    }
    
    Refresh();
}

wxColour TranscriptionBubbleCtrl::GetSpeakerColor(const wxString& speaker) const {
    auto it = m_speakerColors.find(speaker);
    if (it != m_speakerColors.end()) {
        return it->second;
    }
    
    // 生成默认颜色
    return const_cast<TranscriptionBubbleCtrl*>(this)->GenerateSpeakerColor(speaker);
}

wxColour TranscriptionBubbleCtrl::GenerateSpeakerColor(const wxString& speaker) {
    // 基于发言人名称的哈希值选择颜色
    size_t hash = std::hash<std::wstring>{}(speaker.ToStdWstring());
    size_t colorIndex = hash % s_defaultColors.size();
    
    wxColour color = s_defaultColors[colorIndex];
    m_speakerColors[speaker] = color;
    
    return color;
}

void TranscriptionBubbleCtrl::HighlightMessage(int messageId, bool highlight) {
    for (auto& msg : m_messages) {
        if (msg.messageId == messageId) {
            msg.isHighlighted = highlight;
            Refresh();
            break;
        }
    }
}

std::vector<int> TranscriptionBubbleCtrl::SearchText(const wxString& searchText, bool caseSensitive) {
    m_searchResults.clear();
    
    if (searchText.IsEmpty()) {
        return m_searchResults;
    }
    
    wxString searchStr = caseSensitive ? searchText : searchText.Lower();
    
    for (const auto& msg : m_messages) {
        wxString content = caseSensitive ? msg.content : msg.content.Lower();
        if (content.Contains(searchStr)) {
            m_searchResults.push_back(msg.messageId);
        }
    }
    
    return m_searchResults;
}

void TranscriptionBubbleCtrl::ScrollToMessage(int messageId) {
    for (const auto& layout : m_layouts) {
        if (layout.messageId == messageId) {
            int y = layout.bubbleRect.GetTop();
            Scroll(-1, y / 10);  // 除以滚动速率
            break;
        }
    }
}

wxString TranscriptionBubbleCtrl::ExportAsText() const {
    wxString text;
    
    for (const auto& msg : m_messages) {
        text += wxString::Format(wxT("[%s] %s: %s\n"),
                               msg.timestamp.Format(wxT("%H:%M:%S")),
                               msg.speakerName,
                               msg.content);
    }
    
    return text;
}

void TranscriptionBubbleCtrl::OnPaint(wxPaintEvent& event) {
    wxAutoBufferedPaintDC dc(this);
    DoPrepareDC(dc);
    
    // 清除背景
    dc.SetBackground(wxBrush(m_backgroundColor));
    dc.Clear();
    
    // 获取视图区域
    wxRect viewRect = GetUpdateRegion().GetBox();
    int viewX, viewY;
    CalcUnscrolledPosition(viewRect.x, viewRect.y, &viewX, &viewY);
    viewRect.x = viewX;
    viewRect.y = viewY;
    
    // 绘制每个消息气泡
    for (size_t i = 0; i < m_layouts.size(); ++i) {
        const MessageLayout& layout = m_layouts[i];
        
        // 只绘制可见的消息
        if (layout.bubbleRect.Intersects(viewRect)) {
            const TranscriptionMessage& msg = m_messages[i];
            bool isHovered = (m_hoveredMessage == static_cast<int>(i));
            DrawMessageBubble(&dc, msg, layout.bubbleRect, isHovered);
        }
    }
}

void TranscriptionBubbleCtrl::DrawMessageBubble(wxDC* dc, const TranscriptionMessage& msg,
                                               const wxRect& bubbleRect, bool isHovered) {
    // 设置抗锯齿
    wxGraphicsContext* gc = nullptr;
    
    // 尝试根据DC类型创建图形上下文
    if (dc->IsKindOf(CLASSINFO(wxPaintDC))) {
        wxPaintDC* paintDC = static_cast<wxPaintDC*>(dc);
        gc = wxGraphicsContext::Create(*paintDC);
    } else if (dc->IsKindOf(CLASSINFO(wxMemoryDC))) {
        wxMemoryDC* memDC = static_cast<wxMemoryDC*>(dc);
        gc = wxGraphicsContext::Create(*memDC);
    } else if (dc->IsKindOf(CLASSINFO(wxWindowDC))) {
        wxWindowDC* winDC = static_cast<wxWindowDC*>(dc);
        gc = wxGraphicsContext::Create(*winDC);
    }
    
    if (!gc) {
        // 如果无法创建图形上下文，使用普通DC
        // 绘制简单矩形气泡
        dc->SetBrush(wxBrush(msg.isHighlighted ? m_highlightColor : wxColour(245, 255, 245)));
        dc->SetPen(wxPen(wxColour(220, 220, 220), 1));
        dc->DrawRoundedRectangle(bubbleRect, 5);
        
        // 绘制发言人和时间
        dc->SetFont(m_speakerFont);
        dc->SetTextForeground(wxColour(100, 100, 100));
        wxString speakerTime = wxString::Format(wxT("%s %s"), 
                                              msg.speakerName, 
                                              msg.timestamp.Format(wxT("%H:%M")));
        dc->DrawText(speakerTime, bubbleRect.x - 120, bubbleRect.y + 5);
        
        // 绘制内容
        dc->SetFont(m_messageFont);
        dc->SetTextForeground(m_textColor);
        
        wxRect contentRect(bubbleRect.x + m_bubblePadding,
                          bubbleRect.y + m_bubblePadding,
                          bubbleRect.width - m_bubblePadding * 2,
                          bubbleRect.height - m_bubblePadding * 2);
        
        // 自动换行绘制文本
        wxString content = msg.content;
        int lineHeight = dc->GetCharHeight();
        int y = contentRect.y;
        
        while (!content.IsEmpty() && y < contentRect.GetBottom()) {
            wxString line;
            int lineWidth = 0;
            size_t pos = 0;
            
            while (pos < content.length()) {
                wxString ch = content.Mid(pos, 1);
                int charWidth = dc->GetTextExtent(ch).x;
                
                if (lineWidth + charWidth > contentRect.width) {
                    break;
                }
                
                line += ch;
                lineWidth += charWidth;
                pos++;
            }
            
            if (!line.IsEmpty()) {
                dc->DrawText(line, contentRect.x, y);
                y += lineHeight;
                content = content.Mid(pos);
            } else {
                break;
            }
        }
        return;
    }
    
    // 使用图形上下文绘制更精美的效果
    // 绘制气泡背景 - 类似微信的样式
    wxColour bubbleColor = msg.isHighlighted ? m_highlightColor : wxColour(245, 255, 245);
    if (isHovered) {
        bubbleColor = bubbleColor.ChangeLightness(98);
    }
    
    gc->SetBrush(gc->CreateBrush(wxBrush(bubbleColor)));
    gc->SetPen(gc->CreatePen(wxPen(wxColour(220, 220, 220), 1)));
    
    // 创建圆角矩形路径
    wxGraphicsPath path = gc->CreatePath();
    double radius = 5.0;
    double x = bubbleRect.x;
    double y = bubbleRect.y;
    double w = bubbleRect.width;
    double h = bubbleRect.height;
    
    path.MoveToPoint(x + radius, y);
    path.AddLineToPoint(x + w - radius, y);
    path.AddArc(x + w - radius, y + radius, radius, -M_PI/2, 0, true);
    path.AddLineToPoint(x + w, y + h - radius);
    path.AddArc(x + w - radius, y + h - radius, radius, 0, M_PI/2, true);
    path.AddLineToPoint(x + radius, y + h);
    path.AddArc(x + radius, y + h - radius, radius, M_PI/2, M_PI, true);
    path.AddLineToPoint(x, y + radius);
    path.AddArc(x + radius, y + radius, radius, M_PI, 3*M_PI/2, true);
    path.CloseSubpath();
    
    gc->DrawPath(path);
    
    // 在气泡左侧绘制发言人名称和时间
    gc->SetFont(m_speakerFont, wxColour(100, 100, 100));
    wxString speakerTime = wxString::Format(wxT("%s %s"), 
                                          msg.speakerName, 
                                          msg.timestamp.Format(wxT("%H:%M")));
    
    double textWidth, textHeight;
    gc->GetTextExtent(speakerTime, &textWidth, &textHeight);
    gc->DrawText(speakerTime, bubbleRect.x - textWidth - 15, bubbleRect.y + 5);
    
    // 绘制内容
    gc->SetFont(m_messageFont, m_textColor);
    
    wxRect contentRect(bubbleRect.x + m_bubblePadding,
                      bubbleRect.y + m_bubblePadding,
                      bubbleRect.width - m_bubblePadding * 2,
                      bubbleRect.height - m_bubblePadding * 2);
    
    // 自动换行绘制文本
    wxString content = msg.content;
    double lineHeight = 0;
    double tempWidth = 0;
    gc->GetTextExtent(wxT("测试"), &tempWidth, &lineHeight);
    double currentY = contentRect.y;
    
    while (!content.IsEmpty() && currentY < contentRect.GetBottom()) {
        wxString line;
        double lineWidth = 0;
        size_t pos = 0;
        
        while (pos < content.length()) {
            wxString ch = content.Mid(pos, 1);
            double charWidth = 0;
            double charHeight = 0;
            gc->GetTextExtent(ch, &charWidth, &charHeight);
            
            if (lineWidth + charWidth > contentRect.width) {
                break;
            }
            
            line += ch;
            lineWidth += charWidth;
            pos++;
        }
        
        if (!line.IsEmpty()) {
            gc->DrawText(line, contentRect.x, currentY);
            currentY += lineHeight;
            content = content.Mid(pos);
        } else {
            break;
        }
    }
    
    delete gc;
}

void TranscriptionBubbleCtrl::CalculateLayout() {
    m_layouts.clear();
    
    wxClientDC dc(this);
    int clientWidth = GetClientSize().GetWidth();
    int y = m_bubbleMargin;
    
    // 留出左侧空间用于显示发言人和时间
    int leftMargin = 150;
    
    for (size_t i = 0; i < m_messages.size(); ++i) {
        const TranscriptionMessage& msg = m_messages[i];
        MessageLayout layout;
        layout.messageId = msg.messageId;
        
        // 计算内容区域的宽度
        int contentAreaWidth = std::min(m_maxBubbleWidth, clientWidth - leftMargin - m_bubbleMargin * 2);
        
        // 计算内容高度
        dc.SetFont(m_messageFont);
        wxSize contentSize = CalculateTextSize(dc, msg.content, contentAreaWidth - m_bubblePadding * 2);
        
        // 计算实际需要的高度
        int bubbleHeight = contentSize.y + m_bubblePadding * 2;
        
        // 确保最小高度
        bubbleHeight = std::max(bubbleHeight, 40);
        
        // 设置气泡矩形
        layout.bubbleRect = wxRect(leftMargin, y,
                                 contentAreaWidth,
                                 bubbleHeight);
        
        m_layouts.push_back(layout);
        y += bubbleHeight + m_bubbleMargin;
    }
    
    m_virtualHeight = y;
    SetVirtualSize(clientWidth, m_virtualHeight);
}

wxSize TranscriptionBubbleCtrl::CalculateTextSize(wxDC& dc, const wxString& text, int maxWidth) {
    int width = 0;
    int height = 0;
    int lineHeight = dc.GetCharHeight();
    
    wxString remaining = text;
    
    while (!remaining.IsEmpty()) {
        wxString line;
        int lineWidth = 0;
        size_t pos = 0;
        
        // 找到适合当前行的文本
        while (pos < remaining.length()) {
            wxString ch = remaining.Mid(pos, 1);
            int charWidth = dc.GetTextExtent(ch).x;
            
            if (lineWidth + charWidth > maxWidth) {
                // 如果是中文或其他可以断开的字符，可以在此处断开
                if (pos > 0) {
                    break;
                }
                // 如果第一个字符就超宽，至少要包含这个字符
                pos++;
                break;
            }
            
            line += ch;
            lineWidth += charWidth;
            pos++;
        }
        
        if (!line.IsEmpty()) {
            width = std::max(width, lineWidth);
            height += lineHeight;
            remaining = remaining.Mid(pos);
        } else if (pos > 0) {
            // 处理单个字符超宽的情况
            remaining = remaining.Mid(1);
            height += lineHeight;
        } else {
            break;
        }
    }
    
    return wxSize(width, height);
}

void TranscriptionBubbleCtrl::OnSize(wxSizeEvent& event) {
    CalculateLayout();
    event.Skip();
}

void TranscriptionBubbleCtrl::OnMouseWheel(wxMouseEvent& event) {
    // 处理鼠标滚轮事件
    int rotation = event.GetWheelRotation();
    int delta = event.GetWheelDelta();
    int lines = -rotation / delta * event.GetLinesPerAction();
    
    if (event.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL) {
        int x, y;
        GetViewStart(&x, &y);
        Scroll(x, y + lines);
    }
}

void TranscriptionBubbleCtrl::OnMouseLeftDown(wxMouseEvent& event) {
    int messageIndex = GetMessageAtPoint(event.GetPosition());
    if (messageIndex >= 0) {
        m_selectedMessage = messageIndex;
        
        // 发送点击事件
        wxCommandEvent clickEvent(wxEVT_TRANSCRIPTION_MESSAGE_CLICKED, GetId());
        clickEvent.SetEventObject(this);
        clickEvent.SetInt(m_messages[messageIndex].messageId);
        ProcessWindowEvent(clickEvent);
        
        Refresh();
    }
}

void TranscriptionBubbleCtrl::OnMouseRightDown(wxMouseEvent& event) {
    int messageIndex = GetMessageAtPoint(event.GetPosition());
    if (messageIndex >= 0) {
        // 发送右键点击事件
        wxCommandEvent clickEvent(wxEVT_TRANSCRIPTION_MESSAGE_RIGHT_CLICKED, GetId());
        clickEvent.SetEventObject(this);
        clickEvent.SetInt(m_messages[messageIndex].messageId);
        clickEvent.SetClientData(&event);
        ProcessWindowEvent(clickEvent);
    }
}

void TranscriptionBubbleCtrl::OnMouseMotion(wxMouseEvent& event) {
    int messageIndex = GetMessageAtPoint(event.GetPosition());
    if (messageIndex != m_hoveredMessage) {
        m_hoveredMessage = messageIndex;
        Refresh();
    }
    
    // 设置光标
    if (messageIndex >= 0) {
        SetCursor(wxCursor(wxCURSOR_HAND));
    } else {
        SetCursor(wxNullCursor);
    }
}

void TranscriptionBubbleCtrl::OnEraseBackground(wxEraseEvent& event) {
    // 不处理，避免闪烁
}

int TranscriptionBubbleCtrl::GetMessageAtPoint(const wxPoint& pt) const {
    // 转换为未滚动的坐标
    int x, y;
    const_cast<TranscriptionBubbleCtrl*>(this)->CalcUnscrolledPosition(pt.x, pt.y, &x, &y);
    wxPoint unscrolledPt(x, y);
    
    // 查找包含该点的消息
    for (size_t i = 0; i < m_layouts.size(); ++i) {
        if (m_layouts[i].bubbleRect.Contains(unscrolledPt)) {
            return static_cast<int>(i);
        }
    }
    
    return -1;
} 