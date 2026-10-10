// Actual pinned fake-08 VM, graphics, input and audio; host I/O is headless.
// One process per replay: upstream globals must never be shared across workers.
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <cmath>
#include <chrono>
#include <algorithm>
#include "vm.h"
#include "stubhost.h"
#include "nibblehelpers.h"
#include "lodepng.h"
static std::string quoted(const std::string& text){
 std::string out="\"";for(unsigned char c:text){if(c=='"'||c=='\\')out+='\\';if(c=='\n'){out+="\\n";continue;}if(c<32){out+=' ';continue;}out+=c;}return out+'"';
}
static std::string read(const std::string& path){std::ifstream f(path,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
static bool capture(Vm& vm,StubHost& host,const std::string& path){
 std::vector<unsigned char> rgba(128*128*4);auto palette=host.GetPaletteColors();auto map=vm.GetScreenPaletteMap();auto fb=vm.GetPicoInteralFb();
 for(int y=0;y<128;y++)for(int x=0;x<128;x++){auto c=palette[map[getPixelNibble(x,y,fb)]%144];int i=(y*128+x)*4;rgba[i]=c.Red;rgba[i+1]=c.Green;rgba[i+2]=c.Blue;rgba[i+3]=255;}
 return lodepng_encode32_file(path.c_str(),rgba.data(),128,128)==0;
}
int main(int argc,char**argv){
 if(argc!=4)return 2;
 StubHost host;host.setUpPaletteColors();Vm vm(&host);
 auto cart=read(argv[1]);std::string out=argv[3];int fps=30;
 auto boot=[&](){
  host.stubInput(0,0);
  if(!vm.LoadCart((const unsigned char*)cart.data(),cart.size(),false))return false;
  vm.vm_run();vm.api_srand(fix32(42));
  if(!vm.Step())return false;
  // Upstream reports a fixed 60 Hz host tick rate. Inspect the initialized
  // sandbox callback instead of inferring the cart rate from that host value.
  fps=vm.ExecuteLua("function __workshop_eval_update60() return type(_update60)=='function' end","__workshop_eval_update60")?60:30;
  return true;
 };
 if(!boot()){std::cout<<"{\"kind\":\"load\",\"ok\":false,\"error\":"<<quoted(vm.GetBiosError())<<"}\n";return 1;}
 std::cout<<"{\"kind\":\"load\",\"ok\":true,\"fps\":"<<fps<<"}\n";
 std::ifstream commands(argv[2]);std::string line;uint8_t previous=0;int total=0;double maxms=0;
 std::vector<int16_t> audio;
 while(std::getline(commands,line)){
  std::istringstream command(line);std::string op;command>>op;
  if(op=="frames"){
   int count,mask;std::string name;command>>count>>mask>>name;
   if(count<1||count>10000||mask<0||mask>63||name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_")!=std::string::npos)return 2;
   for(int i=0;i<count;i++){
    host.stubInput((uint8_t)mask&~previous,mask);previous=mask;
    auto begin=std::chrono::steady_clock::now();bool good=true;
    // fake-08 ticks its coroutine at 60 Hz; 30 Hz carts yield an extra tick.
    // A replay frame means one game update/draw cycle, not one coroutine tick.
    int ticks=fps==30?2:1;
    for(int tick=0;tick<ticks&&good;tick++)good=vm.Step();
    maxms=std::max(maxms,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
    if(!good){std::cout<<"{\"kind\":\"runtime\",\"ok\":false,\"frame\":"<<total<<",\"error\":"<<quoted(vm.GetBiosError())<<"}\n";return 1;}
    total++;
    // Stereo signed 16-bit, 22050 Hz; real emulator audio synthesis advances
    // with each VM frame rather than synthesising a static soundtrack afterward.
    int samples=22050/fps;std::vector<int16_t> chunk(samples*2);vm.FillAudioBuffer(chunk.data(),0,samples);audio.insert(audio.end(),chunk.begin(),chunk.end());
   }
   bool good=capture(vm,host,out+"/"+name+".png");
   std::cout<<"{\"kind\":\"capture\",\"ok\":"<<(good?"true":"false")<<",\"frame\":"<<total<<",\"name\":"<<quoted(name)<<"}\n";
   if(!good)return 1;
  }else if(op=="assert"){
   std::string path,name;command>>path>>name;
   // P8 assert() draws an error and stops the cart; it does not raise a Lua
   // exception. ExecuteLua success alone therefore cannot validate a rule.
   // Temporarily install a strict test assertion, always restore the cart API
   // even after a script error, and read an explicit result through a callback.
   bool ready=vm.ExecuteLua("__workshop_eval_saved_assert=assert __workshop_eval_assert_failed=false assert=function(cond,msg) if not cond then __workshop_eval_assert_failed=true end end","");
   bool body=ready&&std::ifstream(path).good()&&vm.ExecuteLua(read(path),"");
   bool passed=vm.ExecuteLua("assert=__workshop_eval_saved_assert function __workshop_eval_assert_ok() return not __workshop_eval_assert_failed end","__workshop_eval_assert_ok");
   bool good=ready&&body&&passed;
   vm.ExecuteLua("__workshop_eval_saved_assert=nil __workshop_eval_assert_failed=nil __workshop_eval_assert_ok=nil","");
   std::cout<<"{\"kind\":\"assert\",\"name\":"<<quoted(name)<<",\"ok\":"<<(good?"true":"false")<<"}\n";
  }else if(op=="reload"){
   previous=0;if(!boot())return 1;
  }else if(!op.empty())return 2;
 }
 double energy=0;int peak=0;for(int16_t x:audio){energy+=double(x)*x;peak=std::max(peak,std::abs(int(x)));}
 std::ofstream pcm(out+"/audio.pcm",std::ios::binary);pcm.write((char*)audio.data(),audio.size()*sizeof(int16_t));
 std::cout<<"{\"kind\":\"summary\",\"ok\":true,\"frames\":"<<total<<",\"frame_max_ms\":"<<maxms<<",\"audio_rms\":"<<(audio.empty()?0:std::sqrt(energy/audio.size()))<<",\"audio_peak\":"<<peak<<",\"seed_boundary\":\"before_original_init\"}\n";
}
