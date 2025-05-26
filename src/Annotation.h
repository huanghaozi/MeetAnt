#ifndef MEETANT_ANNOTATION_H
#define MEETANT_ANNOTATION_H

#include <wx/wx.h>
#include <wx/datetime.h>
#include <wx/colour.h>
#include <string>
#include <vector>
#include <memory>

namespace MeetAnt {

// 时间戳类型 (以毫秒为单位)
typedef long long TimeStamp;

// 批注类型
enum class AnnotationType {
    Highlight,  // 文本高亮
    Note,       // 批注注释
    Bookmark    // 书签
};

// 基本批注类
class Annotation {
public:
    Annotation(const wxString& sessionId, TimeStamp timestamp, const wxString& content = wxEmptyString);
    virtual ~Annotation() = default;
    
    // 获取基本属性
    wxString GetSessionId() const { return m_sessionId; }
    TimeStamp GetTimestamp() const { return m_timestamp; }
    wxString GetContent() const { return m_content; }
    wxDateTime GetCreationTime() const { return m_creationTime; }
    
    // 设置内容
    void SetContent(const wxString& content) { m_content = content; }
    void SetParsedCreationTime(const wxDateTime& time) { m_creationTime = time; }
    
    // 批注类型
    virtual AnnotationType GetType() const = 0;
    
    // 转换为显示字符串
    virtual wxString ToString() const;
    
    // 序列化和反序列化 (用于保存/加载)
    virtual wxString Serialize() const;
    static std::unique_ptr<Annotation> Deserialize(const wxString& data);

protected:
    wxString m_sessionId;    // 所属会话ID
    TimeStamp m_timestamp;   // 关联的音频时间戳
    wxString m_content;      // 批注内容
    wxDateTime m_creationTime; // 创建时间
};

// 高亮批注
class HighlightAnnotation : public Annotation {
public:
    HighlightAnnotation(const wxString& sessionId, TimeStamp timestamp, 
                        const wxString& content, const wxColour& color);
    
    virtual AnnotationType GetType() const override { return AnnotationType::Highlight; }
    wxColour GetColor() const { return m_color; }
    void SetColor(const wxColour& color) { m_color = color; }
    
    virtual wxString ToString() const override;
    virtual wxString Serialize() const override;
    
private:
    wxColour m_color;  // 高亮颜色
};

// 批注批注
class NoteAnnotation : public Annotation {
public:
    NoteAnnotation(const wxString& sessionId, TimeStamp timestamp, 
                  const wxString& content, const wxString& title = wxEmptyString);
    
    virtual AnnotationType GetType() const override { return AnnotationType::Note; }
    wxString GetTitle() const { return m_title; }
    void SetTitle(const wxString& title) { m_title = title; }
    
    virtual wxString ToString() const override;
    virtual wxString Serialize() const override;
    
private:
    wxString m_title;  // 批注标题
};

// 书签批注
class BookmarkAnnotation : public Annotation {
public:
    BookmarkAnnotation(const wxString& sessionId, TimeStamp timestamp, 
                      const wxString& content, const wxString& label = wxEmptyString);
    
    virtual AnnotationType GetType() const override { return AnnotationType::Bookmark; }
    wxString GetLabel() const { return m_label; }
    void SetLabel(const wxString& label) { m_label = label; }
    
    virtual wxString ToString() const override;
    virtual wxString Serialize() const override;
    
private:
    wxString m_label;  // 书签标签
};

// 批注管理器
class AnnotationManager {
public:
    AnnotationManager();
    ~AnnotationManager();
    
    // 添加批注
    void AddAnnotation(std::unique_ptr<Annotation> annotation);
    
    // 获取特定会话的所有批注
    std::vector<Annotation*> GetSessionAnnotations(const wxString& sessionId);
    
    // 获取特定类型的批注
    std::vector<Annotation*> GetAnnotationsByType(const wxString& sessionId, AnnotationType type);
    
    // 获取所有书签
    std::vector<BookmarkAnnotation*> GetAllBookmarks();
    
    // 获取特定时间范围内的批注
    std::vector<Annotation*> GetAnnotationsByTimeRange(const wxString& sessionId, 
                                                     TimeStamp startTime, 
                                                     TimeStamp endTime);
    
    // 删除批注
    bool RemoveAnnotation(Annotation* annotation);
    
    // 保存和加载批注 (到特定会话目录)
    bool SaveAnnotations(const wxString& sessionPath);
    bool LoadAnnotations(const wxString& sessionPath);
    
    // 清除所有批注
    void ClearAnnotations();
    
private:
    std::vector<std::unique_ptr<Annotation>> m_annotations;
};

} // namespace MeetAnt

#endif // MEETANT_ANNOTATION_H 