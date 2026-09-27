#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <math.h>
#include <string.h>
#include "Config.h"
#include "seq/Transport.h"
#include "seq/Track.h"
#include "audio/AudioEngine.h"
#include "storage/ProjectStore.h"
#include "ui/StudioUI.h"

using namespace Mantis;

static uint32_t lastServiceTick=0;
static bool playbackPrimed=false;
static bool loopRecording=false;
static TrackId recordingTrack=TRACK_DRUM;
static uint32_t recordStartTick=0;
static bool pendingSampleRecord=false;
static int pendingSamplePad=-1;
static bool pendingAudioRecord=false;
static bool physicalBLong=false;
static uint32_t bDown=0;
static uint32_t lastInput=0;
static bool projectDirty=false;
static uint32_t projectDirtyAt=0;
static bool audioEventPlayed=false;
static uint32_t audioRecordTick=0;
static int touchHoldPad=-1;
static uint32_t touchHoldStart=0;

static void click(uint8_t beat){
  if(!M5.Speaker.isEnabled()) M5.Speaker.begin();
  M5.Speaker.setVolume(150);
  M5.Speaker.tone(beat==0?1320.0f:880.0f, beat==0?65:40, 6, true);
}

static void startLoopRecord(TrackId t){
  recordingTrack=t;
  loopRecording=true;
  gSequencer.track(t).recording=true;
  recordStartTick=gTransport.tick();
  if(!gTransport.playing()){
    gTransport.startCountIn(1);
    audioEventPlayed=false;
    gUI.flash("GET READY",700);
  }
}

static void stopLoopRecord(){
  loopRecording=false;
  gSequencer.track(recordingTrack).recording=false;
  gUI.flash("TAKE SAVED",600);
}

static void startSampleRecord(int pad){
  if(pad<0||pad>3)return;
  if(gTransport.playing())gTransport.stop();
  pendingSampleRecord=true;pendingSamplePad=pad;
  gTransport.startCountIn(1);
  gUI.flash("SAMPLE",500);
}

static void startAudioRecord(){
  if(gTransport.playing())gTransport.stop();
  pendingAudioRecord=true;audioRecordTick=gTransport.tick();
  gTransport.startCountIn(1);
  gUI.flash("AUDIO",500);
}

static void markDirty(){projectDirty=true;projectDirtyAt=millis();}

static void addEvent(TrackId track,uint8_t note,uint8_t vel,uint8_t inst,uint16_t duration){
  if(!loopRecording||recordingTrack!=track)return;
  uint32_t t=gTransport.tick();
  t=gSequencer.snap(t,gTransport.loopTicks());
  if(gSequencer.add(track,t,duration,note,vel,inst,0))markDirty();
}

static void performDrum(int pad){
  gAudio.triggerDrum(pad,120);
  addEvent(TRACK_DRUM,(uint8_t)pad,120,0,PPQ/4);
}

static void performLead(int index){
  static const uint8_t roots[8]={60,62,64,65,67,69,71,72};
  static const uint8_t thirds[8]={64,65,67,69,71,72,74,76};
  static const uint8_t fifths[8]={67,69,71,72,74,76,79,79};
  uint8_t r=roots[index], notes[4]={r,thirds[index],fifths[index],(uint8_t)(r+12)};
  if(gUI.leadArp()){
    for(int i=0;i<4;i++){gAudio.triggerNote(TRACK_LEAD,notes[i],112,gUI.leadVoice(),PPQ/4);if(loopRecording&&recordingTrack==TRACK_LEAD){uint32_t t=(gTransport.tick()+i*(PPQ/4))%gTransport.loopTicks();t=gSequencer.snap(t,gTransport.loopTicks());if(gSequencer.add(TRACK_LEAD,t,PPQ/4,notes[i],112,gUI.leadVoice()))markDirty();}}
  }else{
    for(int i=0;i<3;i++){gAudio.triggerNote(TRACK_LEAD,notes[i],112,gUI.leadVoice(),PPQ);if(loopRecording&&recordingTrack==TRACK_LEAD)addEvent(TRACK_LEAD,notes[i],112,gUI.leadVoice(),PPQ);}
  }
}

static void performBass(int index){
  static const uint8_t notes[8]={36,38,40,41,43,45,47,48};
  uint8_t n=notes[index];gAudio.triggerNote(TRACK_BASS,n,120,gUI.bassVoice(),PPQ);
  addEvent(TRACK_BASS,n,120,gUI.bassVoice(),PPQ);
}

static void servicePlayback(){
  if(!gTransport.playing()){playbackPrimed=false;return;}
  uint32_t now=gTransport.tick();uint32_t len=gTransport.loopTicks();
  if(!playbackPrimed){lastServiceTick=(now+len-1)%len;playbackPrimed=true;}
  uint32_t prev=lastServiceTick;
  for(uint8_t ti=0;ti<3;ti++){
    TrackId id=(TrackId)ti;TrackState&t=gSequencer.track(id);if(t.muted||(gSequencer.anySolo()&&!t.solo))continue;
    for(uint16_t i=0;i<t.count;i++){
      uint32_t e=t.events[i].tick;
      bool hit = prev<now ? (e>prev&&e<=now) : (prev>now ? (e>prev||e<=now) : (e==now));
      if(hit)gAudio.triggerNote(id,t.events[i].note,t.events[i].velocity,t.events[i].instrument,t.events[i].duration);
    }
  }
  lastServiceTick=now;
  if(!audioEventPlayed && gAudio.hasAudioClip() && gSequencer.track(TRACK_AUDIO).count>0){
    uint32_t e=gSequencer.track(TRACK_AUDIO).events[0].tick;
    if((prev<now && e>prev && e<=now) || (prev>now && (e>prev||e<=now)) || (!playbackPrimed)){ gAudio.playAudioClip(); audioEventPlayed=true; }
  }
  if(now<prev) audioEventPlayed=false;
}

static void serviceCountIn(){
  static uint32_t seenSerial=0; if(!gTransport.countInActive()){seenSerial=0;return;}
  uint32_t serial=(gTransport.countInNumber());
  if(serial!=seenSerial){seenSerial=serial;uint8_t beat=(uint8_t)((gTransport.countInNumber()+1)%BEATS_PER_BAR);click(beat);}
  if(!gTransport.countInActive()){
    if(pendingSampleRecord){pendingSampleRecord=false;gAudio.beginDrumSampleRecord((uint8_t)pendingSamplePad);pendingSamplePad=-1;}
    if(pendingAudioRecord){pendingAudioRecord=false;gAudio.beginAudioRecord();}
  }
}

static void drawCountInFlash(){
  // The persistent transport marker and per-screen count-in are drawn by StudioUI.
}

static void handleTouch(){
  auto td=M5.Touch.getDetail();
  if(td.wasPressed()){
    gUI.touch(td.x,td.y,true,false);
    if(gUI.screen()==SCREEN_DRUM && td.y>=36&&td.y<194){int id=(td.y-36)/79*2+(td.x/160);if(id>=0&&id<4){touchHoldPad=id;touchHoldStart=millis();performDrum(id);}}
  }
  if(td.isPressed() && touchHoldPad>=0 && gUI.screen()==SCREEN_DRUM && millis()-touchHoldStart>1100){startSampleRecord(touchHoldPad);touchHoldPad=-1;}
  if(td.wasReleased())touchHoldPad=-1;
}

static void handlePhysical(){
  M5.update();
  if(M5.BtnB.isPressed()){
    if(!bDown)bDown=millis();
    if(!physicalBLong && millis()-bDown>700){physicalBLong=true;
      if(gUI.screen()==SCREEN_DRUM){int p=0;startSampleRecord(p);} else if(gUI.screen()==SCREEN_AUDIO){startAudioRecord();}
    }
  }
  if(M5.BtnB.wasReleased()){
    if(!physicalBLong && millis()-lastInput>160){
      if(gUI.screen()==SCREEN_AUDIO){if(gAudio.audioRecording()){gAudio.finishAudioRecord();gSequencer.track(TRACK_AUDIO).count=0;gSequencer.add(TRACK_AUDIO,audioRecordTick,PPQ,0,127,0);markDirty();}else startAudioRecord();}
      else if(gUI.screen()==SCREEN_DRUM||gUI.screen()==SCREEN_LEAD||gUI.screen()==SCREEN_BASS){
        if(loopRecording)stopLoopRecord();else {TrackId t=(gUI.screen()==SCREEN_DRUM?TRACK_DRUM:gUI.screen()==SCREEN_LEAD?TRACK_LEAD:TRACK_BASS);startLoopRecord(t);}
      } else {if(gTransport.playing())gTransport.stop();else gTransport.play();}
      lastInput=millis();
    }
    bDown=0;physicalBLong=false;
  }
  if(millis()-lastInput>180){
    if(M5.BtnA.wasPressed()){gUI.setScreen((Screen)((gUI.screen()+SCREEN_COUNT-1)%SCREEN_COUNT));lastInput=millis();}
    if(M5.BtnC.wasPressed()){gUI.setScreen((Screen)((gUI.screen()+1)%SCREEN_COUNT));lastInput=millis();}
  }
}

static void processUiActions(){
  if(gUI.consumePlayToggle()){if(gUI.screen()==SCREEN_AUDIO){gAudio.playAudioClip();}else if(gTransport.playing())gTransport.stop();else gTransport.play();}
  int p=gUI.consumePad();if(p>=0)performDrum(p);
  int l=gUI.consumeLead();if(l>=0)performLead(l);
  int b=gUI.consumeBass();if(b>=0)performBass(b);
  if(gUI.consumeRecord()){
    TrackId t=gUI.screen()==SCREEN_DRUM?TRACK_DRUM:gUI.screen()==SCREEN_LEAD?TRACK_LEAD:TRACK_BASS;
    if(loopRecording)stopLoopRecord();else startLoopRecord(t);
  }
  if(gUI.consumeAudioRecord()){if(!gAudio.audioRecording())startAudioRecord();}
  if(gUI.consumeStopRecord()){if(gAudio.audioRecording()){gAudio.finishAudioRecord();gSequencer.track(TRACK_AUDIO).count=0;gSequencer.add(TRACK_AUDIO,audioRecordTick,PPQ,0,127,0);markDirty();}else gAudio.stopAudioClip();}
  if(gUI.consumeQuantize()){uint16_t q=gSequencer.quantize();if(q==PPQ/4)q=PPQ/2;else if(q==PPQ/2)q=PPQ/8;else if(q==PPQ/8)q=0;else q=PPQ/4;gSequencer.setQuantize(q);markDirty();}
  if(gUI.consumeVoice()){if(gUI.screen()==SCREEN_LEAD)gUI.cycleLeadVoice();else if(gUI.screen()==SCREEN_BASS)gUI.cycleBassVoice();}
  if(gUI.consumeMute()){TrackState&t=gSequencer.track((TrackId)gUI.selectedTrack());t.muted=!t.muted;markDirty();}
  if(gUI.consumeSolo()){TrackState&t=gSequencer.track((TrackId)gUI.selectedTrack());t.solo=!t.solo;markDirty();}
}

void setup(){
  auto cfg=M5.config();cfg.internal_imu=true;cfg.internal_mic=true;cfg.internal_spk=true;M5.begin(cfg);M5.Display.setRotation(1);
  gUI.begin();gTransport.begin();gSequencer.clear();gProject.begin();gAudio.begin();
  float savedBpm=DEFAULT_BPM;uint8_t savedBars=DEFAULT_BARS,savedLead=0,savedBass=0;uint16_t savedQ=PPQ/4;bool savedArp=true;String audioPath;
  if(gProject.load("LAST",savedBpm,savedBars,savedQ,audioPath,savedLead,savedBass,savedArp)){gTransport.setBpm(savedBpm);gTransport.setBars(savedBars);gSequencer.setQuantize(savedQ);gUI.setLeadVoice(savedLead);gUI.setBassVoice(savedBass);gUI.setLeadArp(savedArp);if(audioPath.length())gAudio.loadAudioClip(audioPath.c_str());}
  if(M5.Imu.isEnabled())M5.Imu.loadOffsetFromNVS();
  M5.Speaker.setVolume(190);gUI.flash("MANTIS STUDIO",900);delay(180);
}

void loop(){
  M5.update();
  handlePhysical();
  handleTouch();
  processUiActions();

  if(gAudio.drumRecording()){
    gAudio.updateDrumSampleRecord();
  } else if(gAudio.audioRecording()){
    bool done=gAudio.updateAudioRecord();
    if(done){uint32_t t=audioRecordTick;gSequencer.track(TRACK_AUDIO).count=0;gSequencer.add(TRACK_AUDIO,t,PPQ,0,127,0);markDirty();}
  }

  gTransport.update();
  if(gTransport.beatPulse()&&gTransport.playing()){
    // The visual beat marker is always present; the audible click is intentionally reserved for count-in.
  }
  serviceCountIn();
  servicePlayback();

  if(projectDirty && millis()-projectDirtyAt>1800){String ap=gAudio.hasAudioClip()?"/MANTIS/PROJECTS/LAST_AUDIO.wav":"";gProject.save("LAST",gTransport.bpm(),gTransport.bars(),gSequencer.quantize(),ap,gUI.leadVoice(),gUI.bassVoice(),gUI.leadArp());projectDirty=false;}
  gUI.draw(gUI.screen());
  drawCountInFlash();
  M5.delay(4);
}
