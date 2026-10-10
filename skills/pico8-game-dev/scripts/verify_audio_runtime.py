#!/usr/bin/env python3
"""Decode actual workshop carts and synthesize PCM using the pinned fake-08 source.
Development-only audio data/playback checks; does not claim subjective listening quality.
"""
import argparse
import subprocess
import tempfile
import wave
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
PIN='814991a2571ad3970e386cef48f3b148aa1c27b9'
DRIVER=r'''
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <cmath>
#include "cart.h"
#include "Audio.h"
int main(int argc,char**argv){
 assert(argc==4);
 std::ifstream in(argv[1],std::ios::binary);
 std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),{});
 Cart cart(data.data(),data.size());assert(cart.LoadError.empty());
 PicoRam ram={};ram.Reset();std::memcpy(ram.data,cart.CartRom.data,sizeof(cart.CartRom.data));
 assert(ram.sfx[14].notes[0].getKey()==36 && ram.sfx[14].notes[0].getVolume()==3);
 assert(ram.songs[6].getStart()==1 && ram.songs[6].getLoop()==1 && ram.songs[6].getSfx0()==14 && ram.songs[6].getSfx3()>=64);
 Audio audio(&ram);
 for(int track:{0,6}){
  audio.resetAudioState();audio.api_music(track,0,track==0?7:3);
  std::vector<int16_t> pcm(22050*(track==0?30:12));
  audio.FillMonoAudioBuffer(pcm.data(),0,pcm.size());
  double energy=0;int peak=0;
  for(int16_t n:pcm){energy+=double(n)*n;peak=std::max(peak,int(std::abs(int(n))));}
  assert(energy>0 && peak>100 && peak<32767);
  assert(audio.getCurrentMusic()>=0 && audio.getMusicPatternCount()>=2);
  std::ofstream out(std::string(argv[2])+(track==0?"/kajam-maze.pcm":"/gentle-loop.pcm"),std::ios::binary);
  out.write((char*)pcm.data(),pcm.size()*sizeof(int16_t));out.close();
  std::cout<<"PASS track "<<track<<" RMS="<<std::sqrt(energy/pcm.size())<<" peak="<<peak<<" pattern="<<audio.getCurrentMusic()<<" count="<<audio.getMusicPatternCount()<<"\n";
  audio.api_sfx(0,3,0,0);std::vector<int16_t> tail(22050/5);audio.FillMonoAudioBuffer(tail.data(),0,tail.size());
  assert(audio.getCurrentMusic()>=0); // Reserved music voices were not replaced.
  audio.api_music(-1,200,0);tail.resize(22050);audio.FillMonoAudioBuffer(tail.data(),0,tail.size());assert(audio.getCurrentMusic()==-1);
 }
 for(int id=0;id<=5;id++){
  audio.resetAudioState();audio.api_sfx(id,3,0,0);std::vector<int16_t> pcm(22050);audio.FillMonoAudioBuffer(pcm.data(),0,pcm.size());
  double energy=0;for(int16_t n:pcm)energy+=double(n)*n;assert(energy>0);assert(audio.getCurrentMusic()==-1);
 }
 std::ifstream customIn(argv[3],std::ios::binary);data.assign(std::istreambuf_iterator<char>(customIn),{});
 Cart custom(data.data(),data.size());assert(custom.LoadError.empty());
 assert(custom.CartRom.SfxData[16].notes[0].getKey()==40 && custom.CartRom.SongData[8].getSfx0()==16);
 std::memcpy(ram.data,custom.CartRom.data,sizeof(custom.CartRom.data));audio.resetAudioState();audio.api_music(8,0,3);
 std::vector<int16_t> pcm(22050*10);audio.FillMonoAudioBuffer(pcm.data(),0,pcm.size());
 assert(audio.getCurrentMusic()==8 && audio.getMusicPatternCount()>=2);
 std::cout<<"PASS six event SFX, music/SFX channel coexistence, fade-out stop, custom note decoding and pattern 8 looping\n";
}
'''
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'build/workshop-fake08')
    parser.add_argument('--fixtures',type=Path,default=ROOT/'build/pocketjs-port/workshop-audio-verification')
    args=parser.parse_args()
    pin=subprocess.check_output(['git','-C',str(args.source),'rev-parse','HEAD'],text=True).strip()
    if pin!=PIN: raise SystemExit('Use workshop pinned fake-08 '+PIN)
    output=args.fixtures.resolve();output.mkdir(exist_ok=True,parents=True)
    with tempfile.TemporaryDirectory(prefix='pico8-audio-') as tmp:
        temp=Path(tmp);(temp/'verify.cpp').write_text(DRIVER);binary=temp/'verify'
        sources=[args.source/'source'/s for s in ['cart.cpp','Audio.cpp','synth.cpp','filter.cpp','mathhelpers.cpp','filehelpers.cpp','stringToDataHelpers.cpp','emojiconversion.cpp']]
        sources+=[args.source/'libs/lodepng/lodepng.cpp',args.source/'test/nooplogger.cpp']
        include=['source','libs/z8lua','libs/lodepng','libs/utf8-util','libs/miniz','libs/simpleini']
        result=subprocess.run(['clang++','-std=c++17','-O1','-Wno-deprecated','-Wno-deprecated-declarations',*[('-I'+str(args.source/p)) for p in include],str(temp/'verify.cpp'),*[str(p) for p in sources],'-o',str(binary)],capture_output=True,text=True,timeout=120)
        if result.returncode: raise SystemExit(result.stderr)
        subprocess.run([str(binary),str(output/'audio-fixture.p8'),str(output),str(output/'custom-audio-fixture.p8')],check=True,timeout=30)
    for name in ['kajam-maze','gentle-loop']:
        pcm=(output/(name+'.pcm')).read_bytes()
        with wave.open(str(output/(name+'.wav')),'wb') as out:
            out.setnchannels(1);out.setsampwidth(2);out.setframerate(22050);out.writeframes(pcm)
        (output/(name+'.pcm')).unlink()
    print('Development audio checks only; PCM previews:',output)
if __name__=='__main__':main()
