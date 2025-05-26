#include "Annotation.h"
#include <wx/file.h>
#include <wx/tokenzr.h>
#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
#include <algorithm>
#include <sstream>

namespace MeetAnt {

// --------- Annotation 基类实现 ---------
Annotation::Annotation(const wxString& sessionId, TimeStamp timestamp, const wxString& content)
    : m_sessionId(sessionId), m_timestamp(timestamp), m_content(content) {
    m_creationTime = wxDateTime::Now();
}

wxString Annotation::ToString() const {
    // 默认转换为字符串格式: [时间戳] 内容
    // 时间格式化为 HH:MM:SS
    int hours = m_timestamp / 3600000;
    int minutes = (m_timestamp % 3600000) / 60000;
    int seconds = (m_timestamp % 60000) / 1000;
    
    return wxString::Format(wxT("[%02d:%02d:%02d] %s"), 
                           hours, minutes, seconds, 
                           m_content);
}

wxString Annotation::Serialize() const {
    // 基本序列化格式：会话ID|时间戳|创建时间|内容
    return wxString::Format(wxT("%s|%lld|%s|%s"),
                           m_sessionId,
                           m_timestamp,
                           m_creationTime.Format(wxT("%Y-%m-%d %H:%M:%S")),
                           m_content);
}

std::unique_ptr<Annotation> Annotation::Deserialize(const wxString& data) {
    // 这个基类方法将根据数据格式确定具体批注类型，然后调用相应的构造函数
    // 实际解析应在 AnnotationManager::LoadAnnotations 中根据前缀调用各派生类的特定解析方法
    // 或者在这里实现一个工厂，但由于派生类需要不同的参数，放在Manager中逐层解析更清晰
    wxStringTokenizer tokenizer(data, wxT("|"));
    if (tokenizer.CountTokens() < 1) return nullptr;

    wxString typePrefix = tokenizer.GetNextToken();
    // 剩余的 data 部分是派生类序列化内容，或者说，是基类序列化 + 派生类特定序列化
    // 但我们这里不能简单地传递 data.Mid(2) 因为基类的 Serialize 并不包含类型前缀

    // 由于每个派生类的 Serialize 方法格式不同，且都以类型前缀开头
    // 真正的反序列化逻辑最好放在 AnnotationManager::LoadAnnotations 中
    // 或者每个派生类提供一个静态的 Deserialize 方法
    // 例如：
    // if (typePrefix == wxT("H")) {
    //     return HighlightAnnotation::DeserializeFromString(data); // 假设有此方法
    // } else if (typePrefix == wxT("N")) {
    //     return NoteAnnotation::DeserializeFromString(data);
    // } else if (typePrefix == wxT("B")) {
    //     return BookmarkAnnotation::DeserializeFromString(data);
    // }
    return nullptr; // 应该由 AnnotationManager 处理
}

// --------- HighlightAnnotation 实现 ---------
HighlightAnnotation::HighlightAnnotation(const wxString& sessionId, TimeStamp timestamp, 
                                       const wxString& content, const wxColour& color)
    : Annotation(sessionId, timestamp, content), m_color(color) {
}

wxString HighlightAnnotation::ToString() const {
    return wxString::Format(wxT("[高亮] %s"), Annotation::ToString());
}

wxString HighlightAnnotation::Serialize() const {
    // 继承基类序列化并添加颜色信息: H|BaseSerialized|R,G,B
    return wxString::Format(wxT("H|%s|%s"), 
                           Annotation::Serialize(),
                           m_color.GetAsString(wxC2S_CSS_SYNTAX)); // 使用CSS格式 "rgb(r,g,b)"
}

// --------- NoteAnnotation 实现 ---------
NoteAnnotation::NoteAnnotation(const wxString& sessionId, TimeStamp timestamp, 
                             const wxString& content, const wxString& title)
    : Annotation(sessionId, timestamp, content), m_title(title) {
}

wxString NoteAnnotation::ToString() const {
    if (!m_title.IsEmpty()) {
        return wxString::Format(wxT("[批注: %s] %s"), m_title, Annotation::ToString());
    } else {
        return wxString::Format(wxT("[批注] %s"), Annotation::ToString());
    }
}

wxString NoteAnnotation::Serialize() const {
    // 继承基类序列化并添加标题信息: N|BaseSerialized|Title
    return wxString::Format(wxT("N|%s|%s"), 
                           Annotation::Serialize(),
                           m_title);
}

// --------- BookmarkAnnotation 实现 ---------
BookmarkAnnotation::BookmarkAnnotation(const wxString& sessionId, TimeStamp timestamp, 
                                     const wxString& content, const wxString& label)
    : Annotation(sessionId, timestamp, content), m_label(label) {
}

wxString BookmarkAnnotation::ToString() const {
    if (!m_label.IsEmpty()) {
        return wxString::Format(wxT("[书签: %s] %s"), m_label, Annotation::ToString());
    } else {
        return wxString::Format(wxT("[书签] %s"), Annotation::ToString());
    }
}

wxString BookmarkAnnotation::Serialize() const {
    // 继承基类序列化并添加标签信息: B|BaseSerialized|Label
    return wxString::Format(wxT("B|%s|%s"), 
                           Annotation::Serialize(),
                           m_label);
}

// --------- AnnotationManager 实现 ---------
AnnotationManager::AnnotationManager() {
}

AnnotationManager::~AnnotationManager() {
    ClearAnnotations();
}

void AnnotationManager::AddAnnotation(std::unique_ptr<Annotation> annotation) {
    m_annotations.push_back(std::move(annotation));
}

std::vector<Annotation*> AnnotationManager::GetSessionAnnotations(const wxString& sessionId) {
    std::vector<Annotation*> result;
    for (auto& annotation : m_annotations) {
        if (annotation->GetSessionId() == sessionId) {
            result.push_back(annotation.get());
        }
    }
    return result;
}

std::vector<Annotation*> AnnotationManager::GetAnnotationsByType(const wxString& sessionId, AnnotationType type) {
    std::vector<Annotation*> result;
    for (auto& annotation : m_annotations) {
        if (annotation->GetSessionId() == sessionId && annotation->GetType() == type) {
            result.push_back(annotation.get());
        }
    }
    return result;
}

std::vector<BookmarkAnnotation*> AnnotationManager::GetAllBookmarks() {
    std::vector<BookmarkAnnotation*> result;
    for (auto& annotation : m_annotations) {
        if (annotation->GetType() == AnnotationType::Bookmark) {
            result.push_back(static_cast<BookmarkAnnotation*>(annotation.get()));
        }
    }
    return result;
}

std::vector<Annotation*> AnnotationManager::GetAnnotationsByTimeRange(const wxString& sessionId, 
                                                                   TimeStamp startTime, 
                                                                   TimeStamp endTime) {
    std::vector<Annotation*> result;
    for (auto& annotation : m_annotations) {
        if (annotation->GetSessionId() == sessionId) {
            TimeStamp ts = annotation->GetTimestamp();
            if (ts >= startTime && ts <= endTime) {
                result.push_back(annotation.get());
            }
        }
    }
    return result;
}

bool AnnotationManager::RemoveAnnotation(Annotation* annotation) {
    auto it = std::find_if(m_annotations.begin(), m_annotations.end(), 
                          [annotation](const std::unique_ptr<Annotation>& ptr) {
                              return ptr.get() == annotation;
                          });
    
    if (it != m_annotations.end()) {
        m_annotations.erase(it);
        return true;
    }
    return false;
}

bool AnnotationManager::SaveAnnotations(const wxString& sessionPath) {
    wxString annotationsPath = wxFileName(sessionPath, wxT("annotations.dat")).GetFullPath();
    wxFile file;
    
    if (!file.Create(annotationsPath, true) || !file.Open(annotationsPath, wxFile::write)) {
        return false;
    }
    
    for (auto& annotation : m_annotations) {
        wxString line = annotation->Serialize() + wxT("\n");
        file.Write(line);
    }
    
    file.Close();
    return true;
}

bool AnnotationManager::LoadAnnotations(const wxString& sessionPath) {
    wxString annotationsPath = wxFileName(sessionPath, wxT("annotations.dat")).GetFullPath();
    
    if (!wxFileExists(annotationsPath)) {
        return false; // No annotations file, not an error, just nothing to load.
    }
    
    wxTextFile file; // Use wxTextFile for easier line-by-line reading
    if (!file.Open(annotationsPath)) {
        wxLogError(wxT("Failed to open annotations file: %s"), annotationsPath);
        return false;
    }
    
    ClearAnnotations(); // Clear existing annotations before loading new ones
    
    for (wxString line = file.GetFirstLine(); !file.Eof(); line = file.GetNextLine()) {
        if (line.IsEmpty() || line.StartsWith(wxT("#"))) { // Skip empty lines or comments
            continue;
        }

        // TypePrefix|SessionID|Timestamp|CreationTimeStr|Content|SpecificData
        wxStringTokenizer tokenizer(line, wxT('|'));
        if (tokenizer.CountTokens() < 6) { // Minimum parts: Prefix|SID|TS|CTS|Content|Specific
            wxLogWarning(wxT("Skipping malformed annotation line (not enough primary tokens): %s"), line);
            continue;
        }

        wxString typePrefix = tokenizer.GetNextToken();
        wxString sessionId = tokenizer.GetNextToken();
        wxString timestampStr = tokenizer.GetNextToken();
        wxString creationTimeStr = tokenizer.GetNextToken();
        wxString content = tokenizer.GetNextToken(); // This will be the base content
        // SpecificData will be the rest of the tokens, joined if necessary, or the last token.

        long long timestampVal;
        if (!timestampStr.ToLongLong(&timestampVal)) {
            wxLogWarning(wxT("Skipping annotation line (bad timestamp): %s"), line);
            continue;
        }

        wxDateTime creationTime;
        if (!creationTime.ParseFormat(creationTimeStr, wxT("%Y-%m-%d %H:%M:%S"))) {
             wxLogWarning(wxT("Skipping annotation line (bad creation time format): %s"), line);
            continue;
        }
        
        // Reconstruct specificData if it was tokenized due to containing pipes.
        // This is still a simplification. Ideally, content and specific data fields that
        // might contain the delimiter should be robustly escaped/unescaped or a different format (JSON) used.
        wxString specificData = wxT("");
        if (tokenizer.HasMoreTokens()){
            specificData = tokenizer.GetNextToken();
            while(tokenizer.HasMoreTokens()){ // if specific data itself has pipes for some complex future case
                 specificData << wxT("|") << tokenizer.GetNextToken();
            }
        } else if (typePrefix == wxT("H") || typePrefix == wxT("N") || typePrefix == wxT("B")){
            // If there are exactly 6 tokens (Prefix|SID|TS|CTS|Content|Specific) and Specific should not be empty.
            wxLogWarning(wxT("Skipping annotation line (missing specific data like color, title, or label): %s"), line);
            continue;
        }
        // If specificData is truly optional for some types, the above check needs adjustment.

        std::unique_ptr<Annotation> annotation = nullptr;

        if (typePrefix == wxT("H")) {
            wxColour color;
            if (!color.Set(specificData)) { // specificData is colorStr for Highlight
                 wxLogWarning(wxT("Skipping Highlight annotation line (bad color string '%s'): %s"), specificData, line);
                continue;
            }
            annotation = std::make_unique<HighlightAnnotation>(sessionId, timestampVal, content, color);

        } else if (typePrefix == wxT("N")) {
            // specificData is title for Note
            annotation = std::make_unique<NoteAnnotation>(sessionId, timestampVal, content, specificData);

        } else if (typePrefix == wxT("B")) {
            // specificData is label for Bookmark
            annotation = std::make_unique<BookmarkAnnotation>(sessionId, timestampVal, content, specificData);
        } else {
            wxLogWarning(wxT("Unknown annotation type prefix: '%s' in line: %s"), typePrefix, line);
        }

        if (annotation) {
            annotation->SetParsedCreationTime(creationTime); // Use the new public setter
            AddAnnotation(std::move(annotation));
        }
    }
    
    file.Close();
    return true;
}

void AnnotationManager::ClearAnnotations() {
    m_annotations.clear();
}

} // namespace MeetAnt 