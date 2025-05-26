#ifndef MEETANT_NOTEDIALOG_H
#define MEETANT_NOTEDIALOG_H

#include <wx/wx.h>
#include <wx/datetime.h>

namespace MeetAnt {

// 批注对话框 - 用于创建和编辑批注批注
class NoteDialog : public wxDialog {
public:
    NoteDialog(wxWindow* parent, 
               const wxString& title = wxT("添加批注"), 
               const wxString& initialTitle = wxEmptyString,
               const wxString& initialContent = wxEmptyString);
    
    // 获取用户输入的标题和内容
    wxString GetNoteTitle() const;
    wxString GetNoteContent() const;
    
private:
    void CreateControls();
    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    
    wxTextCtrl* m_titleTextCtrl;
    wxTextCtrl* m_contentTextCtrl;
    
    wxDECLARE_EVENT_TABLE();
};

} // namespace MeetAnt

#endif // MEETANT_NOTEDIALOG_H 