#pragma once
#include <M5Unified.h>
#include "../Config.h"
#include "../seq/Transport.h"
#include "../seq/Track.h"
#include "../audio/AudioEngine.h"
#include "../storage/ProjectStore.h"

namespace Mantis {
enum Screen : uint8_t { SCREEN_TIMELINE=0, SCREEN_DRUM, SCREEN_LEAD, SCREEN_BASS, SCREEN_AUDIO, SCREEN_MIX, SCREEN_COUNT };
class StudioUI {
public:
  void begin();
  void draw(Screen screen);
  void setScreen(Screen s){_screen=s;}
  Screen screen()const{return _screen;}
  void flash(const char*msg,uint16_t ms=700);
  bool touch(uint16_t x,uint16_t y,bool pressed,bool released);
  const char* screenName(Screen s)const;
  bool consumePlayToggle(){bool v=_playToggle;_playToggle=false;return v;}
  bool consumeRecord(){bool v=_record;_record=false;return v;}
  int consumePad(){int v=_pad;_pad=-1;return v;}
  int consumeLead(){int v=_lead;_lead=-1;return v;}
  int consumeBass(){int v=_bass;_bass=-1;return v;}
  bool consumeAudioRecord(){bool v=_audioRecord;_audioRecord=false;return v;}
  bool consumeStopRecord(){bool v=_stopRecord;_stopRecord=false;return v;}
  bool consumeQuantize(){bool v=_quantize;_quantize=false;return v;}
  bool consumeVoice(){bool v=_voice;_voice=false;return v;}
  bool consumeMute(){bool v=_mute;_mute=false;return v;}
  bool consumeSolo(){bool v=_solo;_solo=false;return v;}
  bool consumeProjectSave(){bool v=_save;_save=false;return v;}
  bool consumeProjectLoad(){bool v=_load;_load=false;return v;}
  int selectedTrack()const{return _selectedTrack;}
  void selectTrack(int t){_selectedTrack=constrain(t,0,3);}
  uint8_t leadVoice()const{return _leadVoice;}
  uint8_t bassVoice()const{return _bassVoice;}
  bool leadArp()const{return _leadArp;}
  void cycleLeadVoice(){_leadVoice=(_leadVoice+1)%3;}
  void cycleBassVoice(){_bassVoice=(_bassVoice+1)%3;}
  void toggleLeadArp(){_leadArp=!_leadArp;}
  void setLeadVoice(uint8_t v){_leadVoice=v%3;}
  void setBassVoice(uint8_t v){_bassVoice=v%3;}
  void setLeadArp(bool v){_leadArp=v;}
private:
  Screen _screen=SCREEN_TIMELINE;
  int _selectedTrack=0;
  uint8_t _leadVoice=0,_bassVoice=0;
  bool _leadArp=true;
  bool _playToggle=false,_record=false,_audioRecord=false,_stopRecord=false,_quantize=false,_voice=false,_mute=false,_solo=false,_save=false,_load=false;
  int _pad=-1,_lead=-1,_bass=-1;
  String _flash;uint32_t _flashUntil=0;
  void topBar();
  void navBar();
  void beatGrid(int x,int y,int w,int h,TrackId id);
  void timeline();void drum();void lead();void bass();void audio();void mix();
  void labelCentered(const char*s,int x,int y,int w,int h,uint16_t c,uint8_t sz=1);
};
extern StudioUI gUI;
}
