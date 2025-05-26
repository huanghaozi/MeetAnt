#include "MainFrame.h"
#include "NoteDialog.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/dcbuffer.h>

// 自定义批注弹出窗口的事件ID
enum {
    ID_NotePopup_Close = wxID_HIGHEST + 500,
    ID_NotePopup_Edit
};

wxBEGIN_EVENT_TABLE(NotePopup, wxPopupWindow)
    EVT_BUTTON(ID_NotePopup_Close, NotePopup::OnClose)
    EVT_BUTTON(ID_NotePopup_Edit, NotePopup::OnEdit)
    EVT_LEFT_DOWN(NotePopup::OnLeftDown)
    EVT_MOUSE_CAPTURE_LOST(NotePopup::OnCaptureLost)
wxEND_EVENT_TABLE()

NotePopup::NotePopup(wxWindow* parent, MeetAnt::NoteAnnotation* note)
    : wxPopupWindow(parent, wxBORDER_SIMPLE), m_note(note), m_isDragging(false) {
    
    SetBackgroundColour(wxColour(255, 255, 204)); // 淡黄色背景
    CreateControls();
    
    if (note) {
        SetContent(note->GetTitle(), note->GetContent());
    }
    
    // 设置初始大小
    SetSize(wxSize(200, -1));
    Fit();
}

NotePopup::~NotePopup() {
    // 无需手动删除控件，会由框架自动处理
}

void NotePopup::CreateControls() {
    m_mainPanel = new wxPanel(this, wxID_ANY);
    m_mainPanel->SetBackgroundColour(wxColour(255, 255, 204));
    
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // 标题行
    wxBoxSizer* titleSizer = new wxBoxSizer(wxHORIZONTAL);
    
    // 批注标题
    m_titleText = new wxStaticText(m_mainPanel, wxID_ANY, wxT("批注标题"), 
                                 wxDefaultPosition, wxDefaultSize, 
                                 wxST_ELLIPSIZE_END);
    m_titleText->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    titleSizer->Add(m_titleText, 1, wxEXPAND | wxALL, 5);
    
    // 关闭按钮
    m_closeButton = new wxButton(m_mainPanel, ID_NotePopup_Close, wxT("×"), 
                               wxDefaultPosition, wxSize(20, 20), wxBU_EXACTFIT);
    titleSizer->Add(m_closeButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    
    mainSizer->Add(titleSizer, 0, wxEXPAND);
    
    // 批注内容
    m_contentText = new wxStaticText(m_mainPanel, wxID_ANY, wxT("批注内容..."), 
                                   wxDefaultPosition, wxDefaultSize, 
                                   wxST_NO_AUTORESIZE);
    mainSizer->Add(m_contentText, 1, wxEXPAND | wxALL, 5);
    
    // 编辑按钮
    m_editButton = new wxButton(m_mainPanel, ID_NotePopup_Edit, wxT("编辑"), 
                              wxDefaultPosition, wxDefaultSize);
    mainSizer->Add(m_editButton, 0, wxALIGN_RIGHT | wxALL, 5);
    
    m_mainPanel->SetSizer(mainSizer);
    
    // 创建批注的外层布局，添加外边框
    wxBoxSizer* popupSizer = new wxBoxSizer(wxVERTICAL);
    popupSizer->Add(m_mainPanel, 1, wxEXPAND);
    
    SetSizer(popupSizer);
    Layout();
}

void NotePopup::SetPosition(const wxPoint& pos) {
    Move(pos);
}

void NotePopup::SetContent(const wxString& title, const wxString& content) {
    m_titleText->SetLabel(title.IsEmpty() ? wxT("未命名批注") : title);
    m_contentText->SetLabel(content);
    
    // 确保内容不会太宽，限制在200像素以内
    m_contentText->Wrap(200);
    
    // 调整弹窗大小
    Layout();
    Fit();
}

void NotePopup::OnClose(wxCommandEvent& event) {
    // 关闭弹窗
    Show(false);
    
    // 通知MainFrame移除此弹窗
    wxCommandEvent closeEvent(wxEVT_COMMAND_BUTTON_CLICKED, ID_NotePopup_Close);
    closeEvent.SetEventObject(this);
    GetParent()->GetEventHandler()->ProcessEvent(closeEvent);
}

void NotePopup::OnEdit(wxCommandEvent& event) {
    if (!m_note) {
        return;
    }
    
    // 创建批注编辑对话框
    MeetAnt::NoteDialog dlg(GetParent(), wxT("编辑批注"), 
                           m_note->GetTitle(), m_note->GetContent());
    
    if (dlg.ShowModal() == wxID_OK) {
        wxString newTitle = dlg.GetNoteTitle();
        wxString newContent = dlg.GetNoteContent();
        
        // 更新批注内容
        m_note->SetTitle(newTitle);
        m_note->SetContent(newContent);
        
        // 更新显示
        SetContent(newTitle, newContent);
    }
}

void NotePopup::OnLeftDown(wxMouseEvent& event) {
    // 开始拖拽
    m_dragStartPos = ClientToScreen(event.GetPosition());
    m_isDragging = true;
    
    // 捕获鼠标以跟踪鼠标移动
    CaptureMouse();
    
    // 将事件传递给父对象，以便继续处理
    event.Skip();
}

void NotePopup::OnCaptureLost(wxMouseCaptureLostEvent& event) {
    if (m_isDragging && HasCapture()) {
        ReleaseMouse();
    }
    m_isDragging = false;
} 