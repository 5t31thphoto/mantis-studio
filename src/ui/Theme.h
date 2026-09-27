#pragma once
#include <M5Unified.h>
#include "../Config.h"
namespace Mantis { namespace UI {
inline uint16_t rgb(uint8_t r,uint8_t g,uint8_t b){return M5.Display.color565(r,g,b);}
constexpr uint16_t TEAL=0x03B9, PLUM=0x500B, LIME=0x07E0, BG=0x18C3, PANEL=0x2126, WHITE=0xFFFF, DIM=0x8410;
inline void text(const char*s,int x,int y,uint16_t c=WHITE,uint8_t sz=1){M5.Display.setTextSize(sz);M5.Display.setTextColor(c,BG);M5.Display.setCursor(x,y);M5.Display.print(s);}
inline void panel(int x,int y,int w,int h,uint16_t c=PANEL){M5.Display.fillRoundRect(x,y,w,h,8,c);M5.Display.drawRoundRect(x,y,w,h,8,TEAL);}
}}
