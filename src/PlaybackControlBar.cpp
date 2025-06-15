#include "PlaybackControlBar.h"
#include <wx/dcbuffer.h>

// 定义事件
wxDEFINE_EVENT(wxEVT_PLAYBACK_POSITION_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_PLAYBACK_STATE_CHANGED, wxCommandEvent);

// 事件表
wxBEGIN_EVENT_TABLE(PlaybackControlBar, wxPanel)
    EVT_BUTTON(ID_PlayPauseButton, PlaybackControlBar::OnPlayPauseButton)
    EVT_SLIDER(ID_TimeSlider, PlaybackControlBar::OnSliderChange)
    EVT_TIMER(ID_UpdateTimer, PlaybackControlBar::OnTimer)
    EVT_PAINT(PlaybackControlBar::OnPaint)
    EVT_SIZE(PlaybackControlBar::OnSize)
    EVT_MOUSE_EVENTS(PlaybackControlBar::OnMouse)
wxEND_EVENT_TABLE()

PlaybackControlBar::PlaybackControlBar(wxWindow* parent, wxWindowID id,
                                     const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, id, pos, size),
      m_isPlaying(false),
      m_duration(0),
      m_currentPosition(0),
      m_playbackSpeed(1.0),
      m_markerPanel(nullptr),
      m_playPauseButton(nullptr),
      m_currentTimeLabel(nullptr),
      m_timeSlider(nullptr),
      m_totalTimeLabel(nullptr),
      m_updateTimer(nullptr)
{
    SetBackgroundColour(wxColour(240, 240, 240));
    SetMinSize(wxSize(300, 80));
    
    // 延迟创建组件，避免在构造函数中出现问题
    CallAfter([this]() {
        CreateComponents();
        LayoutComponents();
    });
    
    // 创建定时器
    m_updateTimer = new wxTimer(this, ID_UpdateTimer);
}

void PlaybackControlBar::CreateComponents() {
    // 创建时间标记面板
    m_markerPanel = new wxPanel(this, wxID_ANY);
    if (m_markerPanel) {
        m_markerPanel->SetBackgroundColour(wxColour(255, 255, 255));
        m_markerPanel->Bind(wxEVT_PAINT, &PlaybackControlBar::OnMarkerPaint, this);
    }
    
    // 创建播放/暂停按钮
    m_playPauseButton = new wxButton(this, ID_PlayPauseButton, wxT("▶"));
    if (m_playPauseButton) {
        m_playPauseButton->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    }
    
    // 创建当前时间标签
    m_currentTimeLabel = new wxStaticText(this, wxID_ANY, wxT("00:00"));
    if (m_currentTimeLabel) {
        m_currentTimeLabel->SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    }
    
    // 创建时间滑块
    m_timeSlider = new wxSlider(this, ID_TimeSlider, 0, 0, 100);
    
    // 创建总时间标签
    m_totalTimeLabel = new wxStaticText(this, wxID_ANY, wxT("00:00"));
    if (m_totalTimeLabel) {
        m_totalTimeLabel->SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    }
}

void PlaybackControlBar::LayoutComponents() {
    // 使用手动布局而不是sizer，避免潜在的sizer问题
    wxSize clientSize = GetClientSize();
    int panelWidth = clientSize.GetWidth();
    
    if (panelWidth < 300) {
        panelWidth = 300;
    }
    
    // 布局时间标记面板
    if (m_markerPanel) {
        m_markerPanel->SetSize(10, 5, panelWidth - 20, 30);
    }
    
    // 布局控制组件
    int controlY = 40;
    int currentX = 10;
    
    if (m_playPauseButton) {
        m_playPauseButton->SetPosition(wxPoint(currentX, controlY));
        currentX += 50;
    }
    
    if (m_currentTimeLabel) {
        m_currentTimeLabel->SetPosition(wxPoint(currentX, controlY + 3));
        currentX += 60;
    }
    
    if (m_timeSlider) {
        int sliderWidth = panelWidth - currentX - 70;
        if (sliderWidth < 100) sliderWidth = 100;
        m_timeSlider->SetSize(currentX, controlY, sliderWidth, 25);
        currentX += sliderWidth + 10;
    }
    
    if (m_totalTimeLabel) {
        m_totalTimeLabel->SetPosition(wxPoint(currentX, controlY + 3));
    }
}

PlaybackControlBar::~PlaybackControlBar() {
    if (m_updateTimer) {
        if (m_updateTimer->IsRunning()) {
            m_updateTimer->Stop();
        }
        delete m_updateTimer;
    }
}

void PlaybackControlBar::SetDuration(int durationMs) {
    m_duration = durationMs;
    
    // 更新滑块范围
    if (m_timeSlider && m_duration > 0) {
        m_timeSlider->SetMax(m_duration / 1000); // 转换为秒
    }
    
    // 更新总时间显示
    if (m_totalTimeLabel) {
        m_totalTimeLabel->SetLabel(FormatTime(m_duration));
    }
    
    // 刷新显示
    Refresh();
}

void PlaybackControlBar::SetPosition(int positionMs) {
    m_currentPosition = positionMs;
    
    // 更新滑块位置
    if (m_timeSlider && !m_timeSlider->HasCapture()) { // 避免用户拖动时更新
        m_timeSlider->SetValue(m_currentPosition / 1000); // 转换为秒
    }
    
    // 更新时间显示
    UpdateTimeDisplay();
    
    // 发送位置变化事件
    wxCommandEvent event(wxEVT_PLAYBACK_POSITION_CHANGED, GetId());
    event.SetEventObject(this);
    event.SetInt(m_currentPosition);
    ProcessWindowEvent(event);
}

void PlaybackControlBar::Play() {
    if (!m_isPlaying) {
        m_isPlaying = true;
        if (m_playPauseButton) {
            m_playPauseButton->SetLabel(wxT("❚❚")); // 暂停符号
        }
        
        // 启动定时器
        if (m_updateTimer) {
            m_updateTimer->Start(100); // 每100毫秒更新一次
        }
        
        // 发送状态变化事件
        wxCommandEvent event(wxEVT_PLAYBACK_STATE_CHANGED, GetId());
        event.SetEventObject(this);
        event.SetInt(1); // 1表示播放
        ProcessWindowEvent(event);
    }
}

void PlaybackControlBar::Pause() {
    if (m_isPlaying) {
        m_isPlaying = false;
        if (m_playPauseButton) {
            m_playPauseButton->SetLabel(wxT("▶")); // 播放符号
        }
        
        // 停止定时器
        if (m_updateTimer) {
            m_updateTimer->Stop();
        }
        
        // 发送状态变化事件
        wxCommandEvent event(wxEVT_PLAYBACK_STATE_CHANGED, GetId());
        event.SetEventObject(this);
        event.SetInt(0); // 0表示暂停
        ProcessWindowEvent(event);
    }
}

void PlaybackControlBar::Stop() {
    m_isPlaying = false;
    m_currentPosition = 0;
    if (m_playPauseButton) {
        m_playPauseButton->SetLabel(wxT("▶"));
    }
    
    // 停止定时器
    if (m_updateTimer) {
        m_updateTimer->Stop();
    }
    
    // 重置位置
    SetPosition(0);
    
    // 发送状态变化事件
    wxCommandEvent event(wxEVT_PLAYBACK_STATE_CHANGED, GetId());
    event.SetEventObject(this);
    event.SetInt(-1); // -1表示停止
    ProcessWindowEvent(event);
}

void PlaybackControlBar::AddTimeMarker(int timeMs, const wxString& label) {
    TimeMarker marker;
    marker.timeMs = timeMs;
    marker.label = label;
    
    // 生成颜色（可以根据需要改进）
    int colorIndex = m_timeMarkers.size() % 6;
    wxColour colors[] = {
        wxColour(255, 100, 100),  // 红
        wxColour(100, 255, 100),  // 绿
        wxColour(100, 100, 255),  // 蓝
        wxColour(255, 255, 100),  // 黄
        wxColour(255, 100, 255),  // 紫
        wxColour(100, 255, 255)   // 青
    };
    marker.color = colors[colorIndex];
    
    m_timeMarkers.push_back(marker);
    if (m_markerPanel) {
        m_markerPanel->Refresh();
    }
}

void PlaybackControlBar::ClearTimeMarkers() {
    m_timeMarkers.clear();
    if (m_markerPanel) {
        m_markerPanel->Refresh();
    }
}

void PlaybackControlBar::OnPlayPauseButton(wxCommandEvent& event) {
    if (m_isPlaying) {
        Pause();
    } else {
        Play();
    }
}

void PlaybackControlBar::OnSliderChange(wxCommandEvent& event) {
    // 用户拖动滑块时更新位置
    if (m_timeSlider) {
        int newPosition = m_timeSlider->GetValue() * 1000; // 转换为毫秒
        SetPosition(newPosition);
    }
}

void PlaybackControlBar::OnTimer(wxTimerEvent& event) {
    if (m_isPlaying) {
        // 更新播放位置
        m_currentPosition += static_cast<int>(100 * m_playbackSpeed); // 100ms * 播放速度
        
        // 检查是否到达结尾
        if (m_currentPosition >= m_duration) {
            Stop();
        } else {
            SetPosition(m_currentPosition);
        }
    }
}

void PlaybackControlBar::UpdateTimeDisplay() {
    if (m_currentTimeLabel) {
        m_currentTimeLabel->SetLabel(FormatTime(m_currentPosition));
    }
}

wxString PlaybackControlBar::FormatTime(int milliseconds) const {
    int totalSeconds = milliseconds / 1000;
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;
    
    if (hours > 0) {
        return wxString::Format(wxT("%02d:%02d:%02d"), hours, minutes, seconds);
    } else {
        return wxString::Format(wxT("%02d:%02d"), minutes, seconds);
    }
}

void PlaybackControlBar::DrawTimeMarkers(wxDC& dc) {
    if (m_duration <= 0) return;
    
    wxSize panelSize = m_markerPanel->GetClientSize();
    int width = panelSize.GetWidth();
    int height = panelSize.GetHeight();
    
    // 绘制背景
    dc.SetBrush(wxBrush(wxColour(250, 250, 250)));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, width, height);
    
    // 绘制时间轴线
    dc.SetPen(wxPen(wxColour(200, 200, 200), 1));
    dc.DrawLine(0, height/2, width, height/2);
    
    // 绘制时间标记
    for (const auto& marker : m_timeMarkers) {
        int x = static_cast<int>((static_cast<double>(marker.timeMs) / m_duration) * width);
        
        // 绘制标记线
        dc.SetPen(wxPen(marker.color, 2));
        dc.DrawLine(x, 0, x, height);
        
        // 绘制标签（如果空间足够）
        if (!marker.label.IsEmpty()) {
            dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            dc.SetTextForeground(marker.color);
            
            wxSize textSize = dc.GetTextExtent(marker.label);
            int textX = x - textSize.GetWidth() / 2;
            if (textX >= 0 && textX + textSize.GetWidth() <= width) {
                dc.DrawText(marker.label, textX, 2);
            }
        }
    }
    
    // 绘制当前播放位置
    if (m_currentPosition > 0 && m_currentPosition <= m_duration) {
        int posX = static_cast<int>((static_cast<double>(m_currentPosition) / m_duration) * width);
        dc.SetPen(wxPen(wxColour(255, 0, 0), 2));
        dc.DrawLine(posX, 0, posX, height);
    }
}

void PlaybackControlBar::OnPaint(wxPaintEvent& event) {
    wxPaintDC dc(this);
    // 可以在这里添加自定义绘制
}

void PlaybackControlBar::OnSize(wxSizeEvent& event) {
    LayoutComponents();
    Refresh();
    event.Skip();
}

void PlaybackControlBar::OnMouse(wxMouseEvent& event) {
    // 处理鼠标事件（如需要）
    event.Skip();
}

void PlaybackControlBar::OnMarkerPaint(wxPaintEvent& event) {
    if (m_markerPanel) {
        wxPaintDC dc(m_markerPanel);
        DrawTimeMarkers(dc);
    }
} 