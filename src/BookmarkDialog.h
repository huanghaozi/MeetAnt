#ifndef MEETANT_BOOKMARKDIALOG_H
#define MEETANT_BOOKMARKDIALOG_H

#include <wx/wx.h>

namespace MeetAnt {

// 书签对话框 - 用于创建和编辑书签
class BookmarkDialog : public wxDialog {
public:
    BookmarkDialog(wxWindow* parent, 
                 const wxString& title = wxT("添加书签"), 
                 const wxString& initialLabel = wxEmptyString,
                 const wxString& initialDescription = wxEmptyString);
    
    // 获取用户输入
    wxString GetBookmarkLabel() const;
    wxString GetBookmarkDescription() const;
    
private:
    void CreateControls();
    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);
    
    wxTextCtrl* m_labelTextCtrl;
    wxTextCtrl* m_descriptionTextCtrl;
    
    wxDECLARE_EVENT_TABLE();
};

} // namespace MeetAnt

#endif // MEETANT_BOOKMARKDIALOG_H 