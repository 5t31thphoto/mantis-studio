#pragma once
#include <Arduino.h>
#include <SD.h>
#include "../seq/Track.h"

namespace Mantis {
class ProjectStore {
public:
  bool begin();
  bool save(const char* name,float bpm,uint8_t bars,uint16_t quantize,const char* audioPath,uint8_t leadVoice=0,uint8_t bassVoice=0,bool leadArp=true);
  bool load(const char* name,float &bpm,uint8_t &bars,uint16_t &quantize,String &audioPath,uint8_t &leadVoice,uint8_t &bassVoice,bool &leadArp);
  bool exists(const char* name);
  bool sdReady() const { return _ready; }
  const char* current() const { return _current; }
  void setCurrent(const char* name);
private:
  bool _ready=false;
  char _current[32]="UNTITLED";
  String pathFor(const char* name) const;
};
extern ProjectStore gProject;
}
