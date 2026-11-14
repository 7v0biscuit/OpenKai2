// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <sstream>

namespace openkai2::launcher::ui {

struct Cell { int x, y, width, height; };

// Restrict a canvas scroll copy to both the bitmap and the visible client.
// Tiny/minimized windows and DPI rounding must never produce a negative byte count.
struct ScrollCopy {
    int source, destination, rows;
    ScrollCopy(int canvasHeight,int sourceTop,int destinationTop,int requestedRows,int clientHeight)
        : source(std::clamp(sourceTop,0,std::max(0,canvasHeight))),
          destination(std::clamp(destinationTop,0,std::max(0,canvasHeight))),
          rows(std::max(0,std::min({requestedRows,canvasHeight-source,canvasHeight-destination,
                                   clientHeight-destination}))) {}
};

inline bool matchesTerms(const std::wstring& text,const std::wstring& query) {
    std::wistringstream words(query);std::wstring word;
    while(words>>word) {
        if(word==L"open" || word==L"go" || word==L"to" || word==L"change")continue;
        if(text.find(word)==std::wstring::npos)return false;
    }
    return true;
}

struct SettingsLayout {
    static constexpr int top=224, padding=24, gap=16, header=68, row=72, fieldHeight=40;
    int left, width, columns, column;
    std::array<int,4> sections{};
    static constexpr std::array<int,4> fields{7,6,7,6};

    explicit SettingsLayout(int contentWidth,int contentLeft=36) : left(contentLeft),width(contentWidth),
        columns(contentWidth<1040 ? 2 : 3),column((contentWidth-2*padding-(columns-1)*gap)/columns) {
        filter(L"");
    }
    int rows(int count) const {return (count+columns-1)/columns;}
    int bottom(int y,int count) const {return y+header+rows(count)*row+12;}
    Cell field(int y,int index) const {
        return {left+padding+(index%columns)*(column+gap),y+header+(index/columns)*row+20,column,fieldHeight};
    }
    Cell label(int y,int index) const {auto c=field(y,index);c.y-=20;c.height=16;return c;}
    void filter(const std::wstring& query) {
        static const wchar_t* words[]={
            L"display window mode resolution aspect ratio frame rate cap fps vsync audio volume motion smoothing interpolation high refresh",
            L"graphics renderer anti aliasing texture filtering upscaling presets fsr internal resolution",
            L"game cpu difficulty hd textures music randomizer ps2 load times counter run ahead input lag latency",
            L"launcher theme accent colors tabs start page appearance discord activity privacy animation reduced motion help hints tooltips"};
        int next=top;
        for(size_t i=0;i<sections.size();++i) {
            const bool match=matchesTerms(words[i],query);
            sections[i]=match ? next : -1;
            if(match)next=bottom(next,fields[i])+20;
        }
    }
    int height() const {
        int result=680;
        for(size_t i=0;i<sections.size();++i)
            if(sections[i]>=0)result=std::max(result,bottom(sections[i],fields[i])+56);
        return result;
    }
};

struct DockLayout {
    int cell, left, top;
    DockLayout(int width,int height,int count) : cell(std::min(112,(width-88)/std::max(1,count))),
        left((width-cell*count)/2),top(height-112) {}
};

}
