#include "StudioUI.h"
#include "Theme.h"
#include <math.h>
namespace Mantis {
StudioUI gUI;
using namespace UI;
static const char* names[]={"TIMELINE","DRUM","LEAD","BASS","AUDIO","MIX"};
const char* StudioUI::screenName(Screen s)const{return names[(int)s];}
void StudioUI::begin(){M5.Display.setRotation(1);M5.Display.setTextDatum(TL_DATUM);}
void StudioUI::labelCentered(const char*s,int x,int y,int w,int h,uint16_t c,uint8_t sz){M5.Display.setTextSize(sz);int tw=M5.Display.textWidth(s);int th=M5.Display.fontHeight();M5.Display.setTextColor(c,BG);M5.Display.drawString(s,x+(w-tw)/2,y+(h-th)/2);}
void StudioUI::topBar(){
  M5.Display.fillRect(0,0,W,31,BG);
  if(gTransport.playing()){ uint16_t bc=gTransport.beatPulse()?LIME:TEAL; M5.Display.drawRect(0,0,W-1,H-1,bc); M5.Display.drawRect(1,1,W-3,H-3,gTransport.beat()==0?PLUM:bc); }M5.Display.drawFastHLine(0,30,W,TEAL);
  M5.Display.setTextSize(1);M5.Display.setTextColor(LIME,BG);M5.Display.setCursor(7,7);M5.Display.print("MANTIS");
  M5.Display.setTextColor(TEAL,BG);M5.Display.setCursor(60,7);M5.Display.print(screenName(_screen));
  char b[16];snprintf(b,sizeof(b),"%03d",(int)gTransport.bpm());M5.Display.setTextColor(WHITE,BG);M5.Display.setCursor(182,7);M5.Display.print(b);M5.Display.print(" BPM");
  uint16_t pulse=gTransport.playing()?(gTransport.beatPulse()?LIME:TEAL):DIM;M5.Display.fillCircle(297,15,gTransport.playing()?8:5,pulse);
  if(gTransport.playing()){M5.Display.drawCircle(297,15,11,PLUM);M5.Display.drawCircle(297,15,14,TEAL);}
  M5.Display.setTextColor(WHITE,BG);M5.Display.setCursor(309,8);M5.Display.print(gTransport.playing()?">":"|");
}
void StudioUI::navBar(){M5.Display.fillRect(0,219,W,21,0x1082);M5.Display.drawFastHLine(0,219,W,PLUM);M5.Display.setTextSize(1);M5.Display.setTextColor(TEAL,0x1082);M5.Display.setCursor(8,225);M5.Display.print("A <");M5.Display.setCursor(147,225);M5.Display.print("B");M5.Display.setCursor(278,225);M5.Display.print("> C");}
void StudioUI::beatGrid(int x,int y,int w,int h,TrackId id){
  M5.Display.drawRect(x,y,w,h,0x39A7);for(int i=1;i<16;i++){int xx=x+(i*w)/16;M5.Display.drawFastVLine(xx,y,(i%4==0?h: h/3),i%4==0?0x52AA:0x2965);}const TrackState&t=gSequencer.track(id);for(uint16_t i=0;i<t.count;i++){int xx=x+(int)((uint64_t)t.events[i].tick*w/gTransport.loopTicks());int bh=max(3,(int)t.events[i].velocity/12);M5.Display.fillRect(xx,y+h-bh,3,bh,id==TRACK_DRUM?LIME:id==TRACK_LEAD?TEAL:PLUM);}if(gTransport.playing()){int xx=x+(int)((uint64_t)gTransport.tick()*w/gTransport.loopTicks());M5.Display.fillRect(xx,y,2,h,LIME);}}
void StudioUI::timeline(){
  labelCentered("LOOP",8,37,70,24,TEAL,1);labelCentered(gTransport.playing()?"PLAY":"STOP",235,37,70,24,gTransport.playing()?LIME:WHITE,1);
  const char* n[]={"DRUM","LEAD","BASS","AUDIO"};TrackId ids[]={TRACK_DRUM,TRACK_LEAD,TRACK_BASS,TRACK_AUDIO};for(int i=0;i<4;i++){M5.Display.setTextColor(i==_selectedTrack?LIME:WHITE,BG);M5.Display.setCursor(8,69+i*34);M5.Display.print(n[i]);beatGrid(58,65+i*34,250,27,ids[i]);}
  char q[24];snprintf(q,sizeof(q),"GRID %s",gSequencer.quantize()==0?"FREE":gSequencer.quantize()==PPQ/2?"1/8":gSequencer.quantize()==PPQ/4?"1/16":gSequencer.quantize()==PPQ/8?"1/32":"FREE");labelCentered(q,8,201,110,15,TEAL,1);labelCentered("A/C screens",118,201,90,15,DIM,1);labelCentered("B PLAY/STOP",208,201,104,15,WHITE,1);
}
void StudioUI::drum(){
  const char* p[] = {"CH","OH","KICK","SNARE"};for(int i=0;i<4;i++){int x=(i%2)*158+3,y=36+(i/2)*79;uint16_t c=i==2?PLUM:i==3?TEAL:0x31A6;M5.Display.fillRoundRect(x,y,154,74,8,c);M5.Display.drawRoundRect(x,y,154,74,8,LIME);labelCentered(p[i],x,y,154,74,WHITE,2);}
  M5.Display.fillRoundRect(4,194,92,22,6,0x2929);labelCentered("REC LOOP",4,194,92,22,gTransport.playing()?LIME:WHITE,1);M5.Display.fillRoundRect(101,194,100,22,6,0x2929);labelCentered(gSequencer.quantize()==PPQ/4?"GRID 1/16":gSequencer.quantize()==PPQ/8?"GRID 1/32":gSequencer.quantize()==PPQ/2?"GRID 1/8":"GRID FREE",101,194,100,22,TEAL,1);M5.Display.fillRoundRect(206,194,110,22,6,0x2929);labelCentered("HOLD = SAMPLE",206,194,110,22,DIM,1);
  if(gAudio.drumRecording()){M5.Display.fillRoundRect(70,77,180,65,10,PLUM);char r[24];snprintf(r,sizeof(r),"SAMPLE %u ms",(unsigned)gAudio.drumRecordElapsedMs());labelCentered(r,70,83,180,28,LIME,1);labelCentered("RECORDING",70,111,180,24,WHITE,1);}
  if(gTransport.countInActive()){M5.Display.fillRect(0,31,W,163,BG);char b[8];snprintf(b,sizeof(b),"%d",gTransport.countInNumber());labelCentered(b,0,50,W,110,LIME,5);labelCentered("COUNT IN",0,165,W,20,TEAL,1);}
}
void StudioUI::lead(){
  const char* chords[]={"I","ii","iii","IV","V","vi","vii","VIII"};for(int i=0;i<8;i++){int x=(i%4)*79+3,y=39+(i/4)*73;M5.Display.fillRoundRect(x,y,74,68,7,(i==0||i==3||i==4)?TEAL:PLUM);labelCentered(chords[i],x,y,74,68,WHITE,2);}char v[30];snprintf(v,sizeof(v),"VOICE %d   ARP %s",_leadVoice+1,_leadArp?"ON":"OFF");labelCentered(v,4,184,312,30,LIME,1);if(gTransport.countInActive()){M5.Display.fillRect(0,31,W,153,BG);char b[8];snprintf(b,sizeof(b),"%d",gTransport.countInNumber());labelCentered(b,0,55,W,90,LIME,5);}}
void StudioUI::bass(){
  const char* notes[]={"C","D","E","F","G","A","B","C'"};for(int i=0;i<8;i++){int x=(i%4)*79+3,y=39+(i/4)*73;M5.Display.fillRoundRect(x,y,74,68,7,(i%2)?PLUM:TEAL);labelCentered(notes[i],x,y,74,68,WHITE,2);}char v[30];const char* vv[]={"ELECTRO","CHIP","DUB"};snprintf(v,sizeof(v),"%s   GRID %s",vv[_bassVoice],gSequencer.quantize()==PPQ/4?"1/16":gSequencer.quantize()==PPQ/2?"1/8":gSequencer.quantize()==PPQ/8?"1/32":"FREE");labelCentered(v,4,184,312,30,LIME,1);if(gTransport.countInActive()){M5.Display.fillRect(0,31,W,153,BG);char b[8];snprintf(b,sizeof(b),"%d",gTransport.countInNumber());labelCentered(b,0,55,W,90,LIME,5);}}
void audio(){
  M5.Display.fillRoundRect(10,42,300,100,10,PANEL);labelCentered(gAudio.audioRecording()?"RECORDING":"AUDIO TRACK",10,49,300,28,gAudio.audioRecording()?LIME:TEAL,2);
  uint32_t s=gAudio.audioRecording()?gAudio.audioRecordSamples():gAudio.audioClipSamples();int w=(int)min<uint32_t>(290,(s*290ULL)/Mantis::AUDIO_MAX_SAMPLES);M5.Display.fillRect(15,91,w,32,LIME);M5.Display.drawRect(15,91,290,32,TEAL);char b[40];snprintf(b,sizeof(b),"%0.1fs / 30s",s/(float)SAMPLE_RATE);labelCentered(b,15,96,290,22,0x0000,1);
  labelCentered("REC",18,157,82,38,WHITE,2);labelCentered("PLAY",119,157,82,38,TEAL,2);labelCentered("STOP",220,157,82,38,WHITE,2);labelCentered("B = RECORD / STOP",10,198,300,15,DIM,1);
  if(gTransport.countInActive()){M5.Display.fillRect(0,31,W,163,BG);char c[8];snprintf(c,sizeof(c),"%d",gTransport.countInNumber());labelCentered(c,0,55,W,90,LIME,5);labelCentered("AUDIO PRE-ROLL",0,155,W,20,TEAL,1);}
}
void StudioUI::mix(){
  const char*n[]={"DRUM","LEAD","BASS","AUDIO"};for(int i=0;i<4;i++){int y=42+i*37;M5.Display.setTextColor(i==_selectedTrack?LIME:WHITE,BG);M5.Display.setCursor(8,y+6);M5.Display.print(n[i]);int x=68,w=176;M5.Display.drawRoundRect(x,y,w,22,5,TEAL);int fw=(gAudio.trackVolume((TrackId)i)*w)/255;M5.Display.fillRoundRect(x,y,fw,22,5,i==0?TEAL:i==1?PLUM:LIME);labelCentered(gSequencer.track((TrackId)i).muted?"M":"",252,y,25,22,LIME,1);labelCentered(gSequencer.track((TrackId)i).solo?"S":"",278,y,25,22,TEAL,1);}labelCentered("MUTE",235,196,35,16,DIM,1);labelCentered("SOLO",278,196,35,16,DIM,1);}
void StudioUI::draw(Screen screen){_screen=screen;M5.Display.fillScreen(BG);topBar();switch(screen){case SCREEN_TIMELINE:timeline();break;case SCREEN_DRUM:drum();break;case SCREEN_LEAD:lead();break;case SCREEN_BASS:bass();break;case SCREEN_AUDIO:audio();break;case SCREEN_MIX:mix();break;default:break;}navBar();if(_flashUntil>millis()){M5.Display.fillRoundRect(75,82,170,58,10,PLUM);labelCentered(_flash.c_str(),75,82,170,58,LIME,1);}}
void StudioUI::flash(const char*msg,uint16_t ms){_flash=msg;_flashUntil=millis()+ms;}

bool StudioUI::touch(uint16_t x,uint16_t y,bool pressed,bool released){if(!pressed)return false;if(y<31&&x<55){_playToggle=true;return true;}if(y>219){if(x<105){_screen=(Screen)((_screen+SCREEN_COUNT-1)%SCREEN_COUNT);}else if(x>215){_screen=(Screen)((_screen+1)%SCREEN_COUNT);}return true;}
switch(_screen){case SCREEN_DRUM:if(y>=36&&y<194){int c=x/160,r=(y-36)/79;if(c<2&&r<2){_pad=r*2+c;return true;}}if(y>=194){if(x<100)_record=true;else if(x<205)_quantize=true;}break;
case SCREEN_LEAD:if(y>=39&&y<185){int c=x/79,r=(y-39)/73;if(c<4&&r<2){_lead=r*4+c;return true;}}if(y>=184&&x<155)_voice=true;else if(y>=184)_record=true;break;
case SCREEN_BASS:if(y>=39&&y<185){int c=x/79,r=(y-39)/73;if(c<4&&r<2){_bass=r*4+c;return true;}}if(y>=184&&x<155)_voice=true;else if(y>=184)_record=true;break;
case SCREEN_AUDIO:if(y>=157&&y<197){if(x<110)_audioRecord=true;else if(x<215)_playToggle=true;else _stopRecord=true;}break;
case SCREEN_MIX:for(int i=0;i<4;i++){int yy=42+i*37;if(y>=yy&&y<yy+22){if(x<60)_selectedTrack=i;else if(x>=235&&x<270)_mute=true;else if(x>=270)_solo=true;else if(x>=68&&x<244){uint8_t v=(uint8_t)constrain((x-68)*255/176,0,255);gAudio.setTrackVolume((TrackId)i,v);}return true;}}break;
default:break;}return false;}
}
