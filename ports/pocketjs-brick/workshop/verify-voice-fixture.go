// QA only: synthetic ASR result for native IPC + controller lifecycle tests.
// This does not exercise microphone capture or the remote ASR service.
package main
import("bufio";"encoding/json";"fmt";"net";"os";"sync")
func main(){if len(os.Args)!=2{panic("socket required")};l,e:=net.Listen("unix",os.Args[1]);if e!=nil{panic(e)};defer l.Close();var mu sync.Mutex;session:=0;state:="idle";text:=""
for{c,e:=l.Accept();if e!=nil{return};go func(){defer c.Close();op,_:=bufio.NewReader(c).ReadString('\n');mu.Lock();defer mu.Unlock();switch op{case "start\n":session++;state="recording";text="";fmt.Fprintln(os.Stderr,"VOICE_FIXTURE start");case "stop\n":state="result";text="继续修改这个游戏，把玩家移动速度加快。";fmt.Fprintln(os.Stderr,"VOICE_FIXTURE stop");case "cancel\n":session++;state="idle";text=""};json.NewEncoder(c).Encode(map[string]interface{}{"state":state,"session":session,"text":text,"rms":3500,"seconds":0.5})}()}}
