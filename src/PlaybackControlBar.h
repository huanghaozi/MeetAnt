#ifndef PLAYBACK_CONTROL_BAR_H
#define PLAYBACK_CONTROL_BAR_H

#include <wx/wx.h>
#include <wx/slider.h>
#include <wx/timer.h>
#include <vector>

// 播放控制条控件
class PlaybackControlBar : public wxPanel {
public:
    PlaybackControlBar(wxWindow* parent, wxWindowID id = wxID_ANY,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxDefaultSize);
    
    virtual ~PlaybackControlBar();
    
    // 设置总时长（毫秒）
    void SetDuration(int durationMs);
    
    // 设置当前播放位置（毫秒）
    void SetPosition(int positionMs);
    
    // 获取当前播放位置（毫秒）
    int GetPosition() const { return m_currentPosition; }
    
    // 播放控制
    void Play();
    void Pause();
    void Stop();
    bool IsPlaying() const { return m_isPlaying; }
    
    // 添加时间标记（例如发言人切换时刻）
    void AddTimeMarker(int timeMs, const wxString& label);
    void ClearTimeMarkers();
    
    // 设置播放速度
    void SetPlaybackSpeed(double speed) { m_playbackSpeed = speed; }
    double GetPlaybackSpeed() const { return m_playbackSpeed; }
    
protected:
    // 事件处理
    void OnPlayPauseButton(wxCommandEvent& event);
    void OnSliderChange(wxCommandEvent& event);
    void OnTimer(wxTimerEvent& event);
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouse(wxMouseEvent& event);
    void OnMarkerPaint(wxPaintEvent& event);
    
    // 组件创建和布局
    void CreateComponents();
    void LayoutComponents();
    
    // 更新时间显示
    void UpdateTimeDisplay();
    
    // 格式化时间
    wxString FormatTime(int milliseconds) const;
    
    // 绘制时间标记
    void DrawTimeMarkers(wxDC& dc);
    
private:
    // 控件
    wxButton* m_playPauseButton;
    wxSlider* m_timeSlider;
    wxStaticText* m_currentTimeLabel;
    wxStaticText* m_totalTimeLabel;
    wxPanel* m_markerPanel;  // 用于显示时间标记
    
    // 播放状态
    bool m_isPlaying;
    int m_duration;          // 总时长（毫秒）
    int m_currentPosition;   // 当前位置（毫秒）
    double m_playbackSpeed;  // 播放速度
    
    // 定时器
    wxTimer* m_updateTimer;
    
    // 时间标记
    struct TimeMarker {
        int timeMs;
        wxString label;
        wxColour color;
    };
    std::vector<TimeMarker> m_timeMarkers;
    
    // ID定义
    enum {
        ID_PlayPauseButton = 20000,
        ID_TimeSlider,
        ID_UpdateTimer
    };
    
    wxDECLARE_EVENT_TABLE();
};

// 自定义事件
wxDECLARE_EVENT(wxEVT_PLAYBACK_POSITION_CHANGED, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_PLAYBACK_STATE_CHANGED, wxCommandEvent);

#endif // PLAYBACK_CONTROL_BAR_H 