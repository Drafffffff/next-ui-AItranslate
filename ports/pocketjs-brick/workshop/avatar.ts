import {BotEngine,type BotFrame} from './third-party/bloub/engine.ts';
import type {StateId} from './third-party/bloub/states.ts';
export type AvatarState='idle'|'listening'|'thinking'|'working'|'done'|'error';
export const AVATAR_STATES:Record<AvatarState,StateId>={idle:'idle',listening:'wide',thinking:'orbit',working:'orbit',done:'wink',error:'alert'};
export function avatarState(voice:string,running:boolean,tools:number,outcome:AvatarState):AvatarState {
 if(voice==='recording')return 'listening';
 if(voice==='processing')return 'thinking';
 if(running)return tools?'working':'thinking';
 return outcome;
}
/** Original time-driven morphing; render at 24Hz, never move layout or block input. */
export function createAvatar(reducedMotion=false){
 const engine=new BotEngine(),still=new BotEngine();
 let last:AvatarState='idle',visual:StateId='idle',orbitStart=0,lastSample=-Infinity,start:number|undefined;
 return {state:()=>last,staticFrame:()=>still.sample(0),tick(state:AvatarState,nowMs:number):BotFrame|undefined{
  if(start===undefined)start=nowMs;
  const now=Math.max(0,(nowMs-start)/1000),changed=state!==last;
  if(changed){last=state;const next=AVATAR_STATES[state];if(next!==visual){visual=next;engine.setState(next,now);if(next==='orbit')orbitStart=now;}}
  if(!changed&&now-lastSample<1/24)return;
  if(reducedMotion){if(!changed&&lastSample!==-Infinity)return;engine.reset(AVATAR_STATES[state],0);lastSample=now;return engine.sample(1);}
  // The upstream orbit is a 3.4s sequence, not an indefinite spinner. Re-enter
  // through its actual current pose to repeat it smoothly during long requests.
  // Thinking -> tool execution uses the same visual and does not restart it.
  if(visual==='orbit'&&now-orbitStart>=3.4){engine.setState('idle',now);engine.setState('orbit',now);orbitStart=now;}
  lastSample=now;return engine.sample(now);
 }};
}
