#include "BookmarkDialog.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace MeetAnt {

wxBEGIN_EVENT_TABLE(BookmarkDialog, wxDialog)
    EVT_BUTTON(wxID_OK, BookmarkDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, BookmarkDialog::OnCancel)
wxEND_EVENT_TABLE()

BookmarkDialog::BookmarkDialog(wxWindow* parent, 
                             const wxString& title,
                             const wxString& initialLabel,
                             const wxString& initialDescription)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE) {
    
    CreateControls();
    
    // 设置初始值
    if (!initialLabel.IsEmpty()) {
        m_labelTextCtrl->SetValue(initialLabel);
    }
    
    if (!initialDescription.IsEmpty()) {
        m_descriptionTextCtrl->SetValue(initialDescription);
    }
    
    // 设置对话框大小和位置
    Fit();
    Centre();
}

void BookmarkDialog::CreateControls() {
    // 创建主布局
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // 书签标签
    wxBoxSizer* labelSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* labelText = new wxStaticText(this, wxID_ANY, wxT("书签标签:"));
    m_labelTextCtrl = new wxTextCtrl(this, wxID_ANY);
    
    labelSizer->Add(labelText, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    labelSizer->Add(m_labelTextCtrl, 1, wxEXPAND | wxALL, 5);
    
    mainSizer->Add(labelSizer, 0, wxEXPAND | wxALL, 5);
    
    // 书签描述
    wxStaticText* descriptionLabel = new wxStaticText(this, wxID_ANY, wxT("描述:"));
    m_descriptionTextCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                         wxDefaultPosition, wxSize(-1, 60),
                                         wxTE_MULTILINE);
    
    mainSizer->Add(descriptionLabel, 0, wxALL, 5);
    mainSizer->Add(m_descriptionTextCtrl, 0, wxEXPAND | wxALL, 5);
    
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
}

wxString BookmarkDialog::GetBookmarkLabel() const {
    return m_labelTextCtrl->GetValue();
}

wxString BookmarkDialog::GetBookmarkDescription() const {
    return m_descriptionTextCtrl->GetValue();
}

void BookmarkDialog::OnOK(wxCommandEvent& event) {
    // 可以在这里添加输入验证
    if (m_labelTextCtrl->GetValue().IsEmpty()) {
        wxMessageBox(wxT("请输入书签标签"), wxT("错误"), wxICON_ERROR, this);
        m_labelTextCtrl->SetFocus();
        return;
    }
    
    EndModal(wxID_OK);
}

void BookmarkDialog::OnCancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}

} // namespace MeetAnt 