#include "ProjectStore.h"
#include <string.h>
namespace Mantis {
ProjectStore gProject;
struct __attribute__((packed)) ProjectHeader { char magic[8]; uint16_t version; float bpm; uint8_t bars; uint16_t quantize; uint16_t counts[TRACK_COUNT]; char audio[64]; uint8_t leadVoice; uint8_t bassVoice; uint8_t leadArp; };
bool ProjectStore::begin(){_ready=SD.begin(4,SPI,25000000);if(_ready){if(!SD.exists("/MANTIS"))SD.mkdir("/MANTIS");if(!SD.exists("/MANTIS/PROJECTS"))SD.mkdir("/MANTIS/PROJECTS");}return _ready;}
String ProjectStore::pathFor(const char*name)const{String n=name;n.replace("/","_");return String("/MANTIS/PROJECTS/")+n+".mst";}
bool ProjectStore::exists(const char*name){return _ready&&SD.exists(pathFor(name));}
void ProjectStore::setCurrent(const char*name){strncpy(_current,name,sizeof(_current)-1);_current[sizeof(_current)-1]=0;}
bool ProjectStore::save(const char*name,float bpm,uint8_t bars,uint16_t quantize,const char*audioPath,uint8_t leadVoice,uint8_t bassVoice,bool leadArp){if(!_ready)return false;String p=pathFor(name);if(SD.exists(p))SD.remove(p);File f=SD.open(p,FILE_WRITE);if(!f)return false;ProjectHeader h{};memcpy(h.magic,"MNTSTUD1",8);h.version=1;h.bpm=bpm;h.bars=bars;h.quantize=quantize;for(int i=0;i<TRACK_COUNT;i++)h.counts[i]=gSequencer.track((TrackId)i).count;strncpy(h.audio,audioPath?audioPath:"",sizeof(h.audio)-1);h.leadVoice=leadVoice;h.bassVoice=bassVoice;h.leadArp=leadArp?1:0;f.write((uint8_t*)&h,sizeof(h));for(int i=0;i<TRACK_COUNT;i++){auto&t=gSequencer.track((TrackId)i);f.write((uint8_t*)t.events,sizeof(NoteEvent)*t.count);}f.close();setCurrent(name);return true;}
bool ProjectStore::load(const char*name,float&bpm,uint8_t&bars,uint16_t&q,String&audioPath,uint8_t&leadVoice,uint8_t&bassVoice,bool&leadArp){if(!_ready)return false;File f=SD.open(pathFor(name),FILE_READ);if(!f)return false;ProjectHeader h{};if(f.read((uint8_t*)&h,sizeof(h))!=sizeof(h)||memcmp(h.magic,"MNTSTUD1",8)){f.close();return false;}gSequencer.clear();bpm=h.bpm;bars=h.bars;q=h.quantize;audioPath=String(h.audio);leadVoice=h.leadVoice;bassVoice=h.bassVoice;leadArp=h.leadArp!=0;for(int i=0;i<TRACK_COUNT;i++){auto&t=gSequencer.track((TrackId)i);uint16_t n=min<uint16_t>(h.counts[i],MAX_EVENTS);t.count=n;if(n)f.read((uint8_t*)t.events,sizeof(NoteEvent)*n);}f.close();setCurrent(name);return true;}
}
