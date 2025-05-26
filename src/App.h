#ifndef MEETANT_APP_H
#define MEETANT_APP_H

#include <wx/wx.h>
#include <random>

class MeetAntApp : public wxApp {
public:
    virtual bool OnInit() override;
    
    // 生成指定范围内的随机数
    int GetRandomNumber(int min, int max);

private:
    std::mt19937 m_randomEngine;
};

#endif // MEETANT_APP_H 