#include "Track.h"
#include <math.h>
namespace Mantis {
Sequencer gSequencer;
void Sequencer::clear() { for (auto &t : _tracks) { t.count=0; t.recording=false; t.muted=false; t.solo=false; } }
bool Sequencer::add(TrackId id, uint32_t tick, uint16_t duration, uint8_t note, uint8_t velocity, uint8_t instrument, uint8_t flags) {
  TrackState &t = _tracks[id];
  if (t.count >= MAX_EVENTS) return false;
  NoteEvent e{tick,duration,note,velocity,instrument,flags};
  uint16_t p=t.count;
  while(p>0 && t.events[p-1].tick>tick){t.events[p]=t.events[p-1];--p;}
  t.events[p]=e; ++t.count; return true;
}
void Sequencer::removeLastRecorded(TrackId id,uint32_t startTick,uint32_t endTick){
  TrackState &t=_tracks[id]; uint16_t w=0;
  for(uint16_t i=0;i<t.count;i++){bool in=startTick<=endTick?(t.events[i].tick>=startTick&&t.events[i].tick<endTick):(t.events[i].tick>=startTick||t.events[i].tick<endTick); if(!in)t.events[w++]=t.events[i];}
  t.count=w;
}
uint32_t Sequencer::snap(uint32_t tick,uint32_t loopTicks)const{if(!_quantize)return tick%loopTicks; uint32_t q=_quantize; uint32_t s=((tick+q/2)/q)*q; return s>=loopTicks?0:s;}
bool Sequencer::anySolo()const{for(auto&t:_tracks)if(t.solo)return true;return false;}
}
