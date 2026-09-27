#include "AudioEngine.h"
#include "../seq/Transport.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

namespace Mantis {
AudioEngine gAudio;
static constexpr float PI2=6.28318530718f;

int16_t* AudioEngine::allocSamples(size_t samples){
  size_t bytes=samples*sizeof(int16_t);
  int16_t* p=(int16_t*)heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!p)p=(int16_t*)heap_caps_malloc(bytes,MALLOC_CAP_8BIT);
  if(p)memset(p,0,bytes); return p;
}

float AudioEngine::midiHz(uint8_t note)const{return 440.0f*powf(2.0f,((int)note-69)/12.0f);}

void AudioEngine::begin(){
  if(!_sdReady){SPI.begin(18,38,23,4);_sdReady=SD.begin(4,SPI,25000000);}
  ensureProjectDirs();
  const char* files[]={"/MANTIS/SAMPLES/hat_closed.raw","/MANTIS/SAMPLES/hat_open.raw","/MANTIS/SAMPLES/kick.raw","/MANTIS/SAMPLES/snare.raw"};
  for(uint8_t i=0;i<4;i++){_drum[i]=allocSamples(DRUM_SAMPLES);_drumPlay[i]=allocSamples(DRUM_SAMPLES);if(_drum[i]){makeDrum(i);_drumLen[i]=DRUM_SAMPLES;if(_sdReady&&SD.exists(files[i])){File f=SD.open(files[i],FILE_READ);if(f){size_t n=min<size_t>(f.size(),DRUM_SAMPLES*2);n&=~1u;f.read((uint8_t*)_drum[i],n);_drumLen[i]=n/2;f.close();}}}}
  for(uint8_t i=0;i<VOICES;i++){_voice[i]=allocSamples(SYNTH_SAMPLES);_voiceLen[i]=0;}
  if(M5.Speaker.isEnabled()){M5.Speaker.begin();M5.Speaker.setVolume(_master);}
}

void AudioEngine::ensureProjectDirs(){if(!_sdReady)return;if(!SD.exists("/MANTIS"))SD.mkdir("/MANTIS");if(!SD.exists("/MANTIS/PROJECTS"))SD.mkdir("/MANTIS/PROJECTS");if(!SD.exists("/MANTIS/SAMPLES"))SD.mkdir("/MANTIS/SAMPLES");}

void AudioEngine::makeDrum(uint8_t pad){
  if(!_drum[pad])return; float sr=SAMPLE_RATE;
  for(uint32_t i=0;i<DRUM_SAMPLES;i++){
    float t=(float)i/sr, x=0;
    if(pad==2){float env=expf(-t*7.0f);float f=150.0f*expf(-t*32.0f)+45.0f; x=sinf(PI2*f*t)*env; x+=0.25f*sinf(PI2*2400*t)*expf(-t*45.0f);x+=0.05f*((float)esp_random()/4294967295.0f*2-1)*expf(-t*80.0f);}
    else if(pad==3){float body=sinf(PI2*190*t)*expf(-t*30.0f);float n=((float)esp_random()/4294967295.0f*2-1)*expf(-t*30.0f);x=0.35f*body+0.75f*n;}
    else {float env=pad==0?expf(-t*95.0f):expf(-t*28.0f);float n=((float)esp_random()/4294967295.0f*2-1);float metal=sinf(PI2*5500*t)+0.7f*sinf(PI2*7100*t)+0.45f*sinf(PI2*8300*t);x=(0.65f*n+0.35f*metal)*env;}
    float fade=(i<48)?(float)i/48.0f:1.0f;_drum[pad][i]=(int16_t)(constrain(x*28000.0f*fade,-32767.0f,32767.0f));
  }
}

bool AudioEngine::pickVoice(uint8_t &slot){for(uint8_t i=0;i<VOICES;i++){uint8_t ch=SYNTH_CHANNEL_BASE+i;if(!M5.Speaker.isPlaying(ch)){slot=i;return true;}}slot=0;return false;}

void AudioEngine::makeVoice(uint8_t slot,uint8_t note,uint8_t velocity,uint8_t instrument,TrackId track,uint16_t duration){
  if(!_voice[slot])return;uint32_t n=min<uint32_t>(SYNTH_SAMPLES,max<uint32_t>(256,(uint32_t)((uint64_t)duration*60000ULL/(uint32_t)DEFAULT_BPM/PPQ*SAMPLE_RATE/1000)));float hz=midiHz(note),amp=(velocity/127.0f)*0.48f*(_volume[track]/255.0f);float sr=SAMPLE_RATE;
  if(track==TRACK_BASS){n=min<uint32_t>(n,SAMPLE_RATE*650/1000);for(uint32_t i=0;i<n;i++){float t=i/sr;float env=expf(-t*(instrument==2?3.5f:8.0f));float phase=PI2*hz*t;float x=instrument==0?sinf(phase)+0.18f*sinf(phase*2):instrument==1?((fmodf(hz*t,1.0f)<0.5f)?1.0f:-1.0f):tanhf(1.6f*sinf(phase));float sub=sinf(PI2*(hz*0.5f)*t)*0.45f;x=(x*0.6f+sub)*env;_voice[slot][i]=(int16_t)(x*32767.0f*amp);}}
  else {for(uint32_t i=0;i<n;i++){float t=i/sr;float a=(t<0.008f?t/0.008f:expf(-t*(4.0f+instrument*2.0f)));float phase=PI2*hz*t;float x=sinf(phase);if(instrument==1)x=2.0f*(phase/PI2-floorf(phase/PI2+0.5f));else if(instrument==2)x=sinf(phase)+0.35f*sinf(phase*2)+0.2f*sinf(phase*3);x=tanhf(x*1.4f)*a;_voice[slot][i]=(int16_t)(x*32767.0f*amp);}}
  _voiceLen[slot]=n;
}

void AudioEngine::hapticBass(float energy){static float level=0;static uint32_t until=0;uint32_t now=millis();if(energy>level)level=energy;until=max(until,now+85);if(now<until){float e=level*level;float decay=(float)(until-now)/85.0f;uint8_t v=(uint8_t)constrain(12.0f+e*225.0f*decay,0.0f,255.0f);M5.Power.setVibration(v);}else{level=0;M5.Power.setVibration(0);}}

void AudioEngine::triggerDrum(uint8_t pad,uint8_t velocity){if(pad>3)return;if(!M5.Speaker.isEnabled())M5.Speaker.begin();M5.Speaker.setVolume(_master);if(_drum[pad]&&_drumLen[pad]){float v=(velocity/127.0f)*(_volume[TRACK_DRUM]/255.0f);if(_drumPlay[pad]){for(uint32_t i=0;i<_drumLen[pad];i++)_drumPlay[pad][i]=(int16_t)((float)_drum[pad][i]*v);M5.Speaker.playRaw(_drumPlay[pad],_drumLen[pad],SAMPLE_RATE,false,1,DRUM_CHANNEL_BASE+pad,false);}else M5.Speaker.playRaw(_drum[pad],_drumLen[pad],SAMPLE_RATE,false,1,DRUM_CHANNEL_BASE+pad,false);}if(pad==2)hapticBass(velocity/127.0f);else if(pad==3)M5.Power.setVibration(65);}

void AudioEngine::triggerNote(TrackId track,uint8_t note,uint8_t velocity,uint8_t instrument,uint16_t duration){uint8_t slot;if(!pickVoice(slot))return;makeVoice(slot,note,velocity,instrument,track,duration);M5.Speaker.playRaw(_voice[slot],_voiceLen[slot],SAMPLE_RATE,false,1,SYNTH_CHANNEL_BASE+slot,false);if(track==TRACK_BASS)hapticBass((velocity/127.0f)*0.45f);}

void AudioEngine::update(uint32_t tick,bool transportPlaying){
  static uint32_t lastTick=UINT32_MAX; if(!transportPlaying)return;uint32_t len=gTransport.loopTicks();if(!len)return;
  uint32_t prev=lastTick==UINT32_MAX?tick:lastTick;
  for(uint8_t ti=0;ti<3;ti++){TrackState&t=gSequencer.track((TrackId)ti);if(t.muted||(gSequencer.anySolo()&&!t.solo))continue;for(uint16_t i=0;i<t.count;i++){uint32_t et=t.events[i].tick;if(et==tick||((prev>tick)&&(et<=tick||et>prev))){if(et==tick||et>prev)triggerNote((TrackId)ti,t.events[i].note,t.events[i].velocity,t.events[i].instrument,t.events[i].duration);}}}
  if(lastTick!=UINT32_MAX&&tick<prev){/* loop boundary; events at zero will fire on next update */}
  lastTick=tick;
}

void AudioEngine::stopAll(){for(uint8_t c=0;c<8;c++)M5.Speaker.stop(c);M5.Power.setVibration(0);}
void AudioEngine::setTrackVolume(TrackId t,uint8_t v){_volume[t]=v;}
void AudioEngine::saveDrumSample(uint8_t pad){ if(!_sdReady||pad>3||!_drum[pad]||!_drumLen[pad])return; const char* n[]={("/MANTIS/SAMPLES/hat_closed.raw"),("/MANTIS/SAMPLES/hat_open.raw"),("/MANTIS/SAMPLES/kick.raw"),("/MANTIS/SAMPLES/snare.raw")}; if(SD.exists(n[pad]))SD.remove(n[pad]); File f=SD.open(n[pad],FILE_WRITE); if(f){f.write((uint8_t*)_drum[pad],_drumLen[pad]*2);f.close();}}
void AudioEngine::setMaster(uint8_t v){_master=v;if(M5.Speaker.isEnabled())M5.Speaker.setVolume(v);}

bool AudioEngine::beginDrumSampleRecord(uint8_t pad){if(pad>3||!_drum[pad])return false;stopAll();if(M5.Speaker.isEnabled())M5.Speaker.end();if(!M5.Mic.isEnabled())M5.Mic.begin();_drumRecPad=pad;_drumRecActive=true;_drumRecStart=millis();_drumRecSamples=0;memset(_drum[pad],0,DRUM_SAMPLES*sizeof(int16_t));return true;}
uint16_t AudioEngine::drumRecordElapsedMs()const{return _drumRecActive?(uint16_t)min<uint32_t>(DRUM_MAX_MS,millis()-_drumRecStart):0;}
bool AudioEngine::updateDrumSampleRecord(){if(!_drumRecActive)return false;uint32_t target=min<uint32_t>(DRUM_SAMPLES,((uint64_t)(millis()-_drumRecStart)*SAMPLE_RATE)/1000);if(target>_drumRecSamples){uint32_t want=min<uint32_t>(target-_drumRecSamples,512);if(M5.Mic.record(_drum[_drumRecPad]+_drumRecSamples,want,SAMPLE_RATE))_drumRecSamples+=want;}if(_drumRecSamples>=DRUM_SAMPLES||millis()-_drumRecStart>=DRUM_MAX_MS){_drumRecActive=false;M5.Mic.end();M5.Speaker.begin();M5.Speaker.setVolume(_master);_drumLen[_drumRecPad]=_drumRecSamples;saveDrumSample(_drumRecPad);return true;}return false;}

void AudioEngine::saveWav(const char*path,const int16_t*data,uint32_t samples,uint32_t rate){if(!_sdReady)return;File f=SD.open(path,FILE_WRITE);if(!f)return;struct H{char riff[4];uint32_t size;char wave[4];char fmt[4];uint32_t fmts;uint16_t fmtid;uint16_t ch;uint32_t sr;uint32_t br;uint16_t ba;uint16_t bits;char dat[4];uint32_t ds;}h{};memcpy(h.riff,"RIFF",4);memcpy(h.wave,"WAVE",4);memcpy(h.fmt,"fmt ",4);memcpy(h.dat,"data",4);h.fmts=16;h.fmtid=1;h.ch=1;h.sr=rate;h.bits=16;h.ba=2;h.br=rate*2;h.ds=samples*2;h.size=36+h.ds;f.write((uint8_t*)&h,sizeof(h));f.write((uint8_t*)data,h.ds);f.close();}

bool AudioEngine::beginAudioRecord(){if(!_audioClip){_audioClip=allocSamples(AUDIO_MAX_SAMPLES);if(!_audioClip)return false;}stopAll();if(M5.Speaker.isEnabled())M5.Speaker.end();if(!M5.Mic.isEnabled())M5.Mic.begin();_audioRecActive=true;_audioRecStart=millis();_audioRecSamples=0;return true;}
void AudioEngine::finishAudioRecord(){if(!_audioRecActive)return;_audioRecActive=false;M5.Mic.end();M5.Speaker.begin();M5.Speaker.setVolume(_master);if(_audioRecSamples>0)saveWav("/MANTIS/PROJECTS/LAST_AUDIO.wav",_audioClip,_audioRecSamples,SAMPLE_RATE);}
bool AudioEngine::updateAudioRecord(){if(!_audioRecActive)return false;uint32_t elapsed=millis()-_audioRecStart;uint32_t target=min<uint32_t>(AUDIO_MAX_SAMPLES,((uint64_t)elapsed*SAMPLE_RATE)/1000);if(target>_audioRecSamples){uint32_t want=min<uint32_t>(target-_audioRecSamples,512);if(M5.Mic.record(_audioClip+_audioRecSamples,want,SAMPLE_RATE))_audioRecSamples+=want;}if(_audioRecSamples>=AUDIO_MAX_SAMPLES){finishAudioRecord();return true;}return false;}

bool AudioEngine::loadWav(const char*path){if(!_sdReady)return false;File f=SD.open(path,FILE_READ);if(!f)return false;uint8_t h[44];if(f.read(h,44)!=44||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4)){f.close();return false;}uint32_t rate=*(uint32_t*)(h+24);uint16_t ch=*(uint16_t*)(h+22),bits=*(uint16_t*)(h+34);uint32_t data=*(uint32_t*)(h+40);if(ch!=1||bits!=16||rate<8000||rate>48000||data/2>AUDIO_MAX_SAMPLES){f.close();return false;}if(!_audioClip)_audioClip=allocSamples(AUDIO_MAX_SAMPLES);if(!_audioClip){f.close();return false;}size_t n=f.read((uint8_t*)_audioClip,data);f.close();_audioClipSamples=n/2;_audioClipRate=rate;return _audioClipSamples>0;}
bool AudioEngine::loadAudioClip(const char*path){return loadWav(path);}
void AudioEngine::playAudioClip(){if(hasAudioClip()){if(!M5.Speaker.isEnabled())M5.Speaker.begin();M5.Speaker.playRaw(_audioClip,_audioClipSamples,_audioClipRate,false,1,AUDIO_CHANNEL,false);}}
void AudioEngine::stopAudioClip(){M5.Speaker.stop(AUDIO_CHANNEL);}
}
