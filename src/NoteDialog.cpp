#include "NoteDialog.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace MeetAnt {

wxBEGIN_EVENT_TABLE(NoteDialog, wxDialog)
    EVT_BUTTON(wxID_OK, NoteDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, NoteDialog::OnCancel)
wxEND_EVENT_TABLE()

NoteDialog::NoteDialog(wxWindow* parent, 
                     const wxString& title,
                     const wxString& initialTitle,
                     const wxString& initialContent)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    
    CreateControls();
    
    // 设置初始值
    if (!initialTitle.IsEmpty()) {
        m_titleTextCtrl->SetValue(initialTitle);
    }
    
    if (!initialContent.IsEmpty()) {
        m_contentTextCtrl->SetValue(initialContent);
    }
    
    // 设置对话框大小和位置
    SetMinSize(wxSize(400, 300));
    Centre();
}

void NoteDialog::CreateControls() {
    // 创建主布局
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // 批注标题
    wxBoxSizer* titleSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* titleLabel = new wxStaticText(this, wxID_ANY, wxT("标题:"));
    m_titleTextCtrl = new wxTextCtrl(this, wxID_ANY);
    
    titleSizer->Add(titleLabel, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    titleSizer->Add(m_titleTextCtrl, 1, wxEXPAND | wxALL, 5);
    
    mainSizer->Add(titleSizer, 0, wxEXPAND | wxALL, 5);
    
    // 批注内容
    wxStaticText* contentLabel = new wxStaticText(this, wxID_ANY, wxT("内容:"));
    m_contentTextCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE);
    
    mainSizer->Add(contentLabel, 0, wxALL, 5);
    mainSizer->Add(m_contentTextCtrl, 1, wxEXPAND | wxALL, 5);
    
    // 按钮
    wxStdDialogButtonSizer* buttonSizer = new wxStdDialogButtonSizer();
    
    wxButton* okButton = new wxButton(this, wxID_OK);
    buttonSizer->AddButton(okButton);
    
    wxButton* cancelButton = new wxButton(this, wxID_CANCEL);
    buttonSizer->AddButton(cancelButton);
    
    buttonSizer->Realize();
    
    mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, 10);
    
    // 设置布局
    SetSizer(mainSizer);
    Layout();
    Fit();
}

wxString NoteDialog::GetNoteTitle() const {
    return m_titleTextCtrl->GetValue();
}

wxString NoteDialog::GetNoteContent() const {
    return m_contentTextCtrl->GetValue();
}

void NoteDialog::OnOK(wxCommandEvent& event) {
    // 可以在这里添加输入验证
    EndModal(wxID_OK);
}

void NoteDialog::OnCancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}

} // namespace MeetAnt 