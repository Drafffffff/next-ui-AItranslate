/* PocketJS renderer for bloub BotFrame. Geometry/morphing stays in the pinned
 * MIT TypeScript engine. Bounded 2x supersampled software raster, two textures.
 * Only engine-generated M/L/C/A/Z paths and affine eye matrices are accepted.
 */
#define AV_SIZE 64
#define AV_GRID (AV_SIZE*2)
#define AV_POINTS 1024
#define AV_SCALE ((double)AV_GRID/316.0)
typedef struct {double x,y;int move;} AvPoint;
typedef struct {AvPoint p[AV_POINTS];int count;} AvPath;
static unsigned char av_pixels[AV_GRID*AV_GRID*4],av_body[AV_GRID*AV_GRID];
static float av_eyes[AV_GRID*AV_GRID];
static int32_t av_handles[2]={-1,-1};
static unsigned av_frames;
static double av_num(JSContext *ctx,JSValueConst object,const char *name,double fallback){
 JSValue value=JS_GetPropertyStr(ctx,object,name);double n=fallback;
 if(JS_IsNumber(value)&&JS_ToFloat64(ctx,&n,value))n=fallback;
 JS_FreeValue(ctx,value);return isfinite(n)?n:fallback;
}
static double av_next(const char **cursor,int *ok){
 while(**cursor==' '||**cursor==',')(*cursor)++;
 char *end;double n=strtod(*cursor,&end);
 if(end==*cursor||!isfinite(n)||fabs(n)>10000){*ok=0;return 0;}
 *cursor=end;return n;
}
static int av_add(AvPath *path,double x,double y,int move,const double *matrix){
 if(path->count>=AV_POINTS)return 0;
 double tx=matrix[0]*x+matrix[2]*y+matrix[4],ty=matrix[1]*x+matrix[3]*y+matrix[5];
 path->p[path->count++]=(AvPoint){(tx+158)*AV_SCALE,(ty+158)*AV_SCALE,move};return 1;
}
static int av_path(JSContext *ctx,JSValueConst obj,const char *field,const double *matrix,AvPath *path){
 path->count=0;JSValue value=JS_GetPropertyStr(ctx,obj,field);
 if(!JS_IsString(value)){JS_FreeValue(ctx,value);return 0;}
 const char *text=JS_ToCString(ctx,value);JS_FreeValue(ctx,value);if(!text)return 0;
 const char *p=text;int ok=strlen(text)<20000;double x=0,y=0;
 while(ok&&*p){
  if(*p==' '||*p==','){p++;continue;}
  char cmd=*p++;if(cmd=='Z')continue;
  if(cmd=='M'||cmd=='L'){x=av_next(&p,&ok);y=av_next(&p,&ok);ok=ok&&av_add(path,x,y,cmd=='M',matrix);}
  else if(cmd=='C'){
   double a=av_next(&p,&ok),b=av_next(&p,&ok),c=av_next(&p,&ok),d=av_next(&p,&ok),ex=av_next(&p,&ok),ey=av_next(&p,&ok);
   for(int k=1;ok&&k<=8;k++){double t=k/8.0,u=1-t;ok=av_add(path,u*u*u*x+3*u*u*t*a+3*u*t*t*c+t*t*t*ex,u*u*u*y+3*u*u*t*b+3*u*t*t*d+t*t*t*ey,0,matrix);}x=ex;y=ey;
  }else if(cmd=='A'){
   double rx=fabs(av_next(&p,&ok)),ry=fabs(av_next(&p,&ok)),rot=av_next(&p,&ok),large=av_next(&p,&ok),sweep=av_next(&p,&ok),ex=av_next(&p,&ok),ey=av_next(&p,&ok);
   // Upstream capsule arcs are axis-aligned quarter circles before eye transform.
   if(rot!=0||large!=0||sweep!=1){ok=0;break;}
   if(rx<.001||ry<.001){ok=ok&&av_add(path,ex,ey,0,matrix);x=ex;y=ey;continue;}
   double dx=(x-ex)/2,dy=(y-ey)/2,q=dx*dx/(rx*rx)+dy*dy/(ry*ry);
   if(q>1){double s=sqrt(q);rx*=s;ry*=s;}
   double denom=rx*rx*dy*dy+ry*ry*dx*dx;
   double factor=denom>0?sqrt(fmax(0,(rx*rx*ry*ry-denom)/denom)):0;
   double cx=(x+ex)/2+factor*rx*dy/ry,cy=(y+ey)/2-factor*ry*dx/rx;
   double first=atan2((y-cy)/ry,(x-cx)/rx),last=atan2((ey-cy)/ry,(ex-cx)/rx),span=last-first;
   if(span<0)span+=6.283185307179586;
   for(int k=1;ok&&k<=8;k++)ok=av_add(path,cx+rx*cos(first+span*k/8),cy+ry*sin(first+span*k/8),0,matrix);
   x=ex;y=ey;
  }else ok=0;
 }
 JS_FreeCString(ctx,text);if(!ok)path->count=0;return ok&&path->count>2;
}
static int av_compare(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static void av_mask(const AvPath *path,unsigned char *mask){
 memset(mask,0,AV_GRID*AV_GRID);if(path->count<3)return;
 for(int y=0;y<AV_GRID;y++){
  double scan=y+.5,cuts[AV_POINTS];int n=0;
  for(int i=0;i<path->count;i++){AvPoint a=path->p[i],b=path->p[(i+1)%path->count];
   if((a.y<=scan&&b.y>scan)||(b.y<=scan&&a.y>scan))cuts[n++]=a.x+(scan-a.y)*(b.x-a.x)/(b.y-a.y);
  }
  qsort(cuts,n,sizeof(double),av_compare);
  for(int i=0;i+1<n;i+=2){int lo=(int)ceil(fmax(0,fmin(AV_GRID,cuts[i]-.5))),hi=(int)ceil(fmax(0,fmin(AV_GRID,cuts[i+1]-.5)));for(int x=lo;x<hi;x++)mask[y*AV_GRID+x]=1;}
 }
}
static void av_paint(int x,int y,uint32_t color,double opacity){
 if(x<0||x>=AV_GRID||y<0||y>=AV_GRID)return;
 double alpha=fmax(0,fmin(1,opacity))*(color&255)/255.0,inv=1-alpha;unsigned i=(y*AV_GRID+x)*4;
 av_pixels[i]=(unsigned char)((color>>24)*alpha+av_pixels[i]*inv+.5);
 av_pixels[i+1]=(unsigned char)(((color>>16)&255)*alpha+av_pixels[i+1]*inv+.5);
 av_pixels[i+2]=(unsigned char)(((color>>8)&255)*alpha+av_pixels[i+2]*inv+.5);
 av_pixels[i+3]=(unsigned char)(255*alpha+av_pixels[i+3]*inv+.5);
}
static void av_circle(double x,double y,double radius,uint32_t color,double opacity,int hole){
 x=(x+158)*AV_SCALE;y=(y+158)*AV_SCALE;radius*=AV_SCALE;
 int left=(int)fmax(0,floor(x-radius)),right=(int)fmin(AV_GRID,ceil(x+radius)),top=(int)fmax(0,floor(y-radius)),bottom=(int)fmin(AV_GRID,ceil(y+radius));
 for(int py=top;py<bottom;py++)for(int px=left;px<right;px++)if((px+.5-x)*(px+.5-x)+(py+.5-y)*(py+.5-y)<=radius*radius){if(hole)av_eyes[py*AV_GRID+px]=1;else av_paint(px,py,color,opacity);}
}
static void av_stroke(const AvPath *path,double width,uint32_t color,double opacity){
 double r=width*AV_SCALE/2;
 for(int i=1;i<path->count;i++){
  AvPoint a=path->p[i-1],b=path->p[i];if(b.move)continue;
  double dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;
  int left=(int)fmax(0,floor(fmin(a.x,b.x)-r)),right=(int)fmin(AV_GRID,ceil(fmax(a.x,b.x)+r)),top=(int)fmax(0,floor(fmin(a.y,b.y)-r)),bottom=(int)fmin(AV_GRID,ceil(fmax(a.y,b.y)+r));
  for(int y=top;y<bottom;y++)for(int x=left;x<right;x++){double t=len?fmax(0,fmin(1,((x+.5-a.x)*dx+(y+.5-a.y)*dy)/len)):0,ex=x+.5-a.x-t*dx,ey=y+.5-a.y-t*dy;if(ex*ex+ey*ey<=r*r)av_paint(x,y,color,opacity);}
 }
}
static const double av_identity[6]={1,0,0,1,0,0};
static void av_arcs(JSContext *ctx,JSValueConst frame,const char *side,uint32_t color){
 JSValue list=JS_GetPropertyStr(ctx,frame,"arcs");unsigned n=(unsigned)av_num(ctx,list,"length",0);if(n>8)n=8;
 for(unsigned i=0;i<n;i++){JSValue arc=JS_GetPropertyUint32(ctx,list,i);AvPath path;if(av_path(ctx,arc,side,av_identity,&path))av_stroke(&path,av_num(ctx,arc,"width",0),color,av_num(ctx,arc,"opacity",1));JS_FreeValue(ctx,arc);}JS_FreeValue(ctx,list);
}
static void av_dots(JSContext *ctx,JSValueConst frame,uint32_t color){
 JSValue list=JS_GetPropertyStr(ctx,frame,"dots");unsigned n=(unsigned)av_num(ctx,list,"length",0);if(n>64)n=64;
 for(unsigned i=0;i<n;i++){
  JSValue dot=JS_GetPropertyUint32(ctx,list,i);double x=av_num(ctx,dot,"x",0),y=av_num(ctx,dot,"y",0),alpha=av_num(ctx,dot,"opacity",1)*av_num(ctx,dot,"depth",1),angle=av_num(ctx,dot,"rot",0)*3.141592653589793/180;
  double matrix[6]={100*cos(angle),100*sin(angle),-100*sin(angle),100*cos(angle),x,y};AvPath path;
  if(av_path(ctx,dot,"d",matrix,&path)){unsigned char mask[AV_GRID*AV_GRID];av_mask(&path,mask);for(int j=0;j<AV_GRID*AV_GRID;j++)if(mask[j])av_paint(j%AV_GRID,j/AV_GRID,color,alpha);}
  else av_circle(x,y,av_num(ctx,dot,"r",0),color,alpha,0);
  JS_FreeValue(ctx,dot);
 }JS_FreeValue(ctx,list);
}
static JSValue workshop_avatar(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
 (void)self;uint32_t ink,detail,paper;
 if(argc<4||!JS_IsObject(argv[0])||!color_value(ctx,argv[1],&ink)||!color_value(ctx,argv[2],&detail)||!color_value(ctx,argv[3],&paper))return JS_NewInt32(ctx,-1);
 int slot=argc>4&&JS_ToBool(ctx,argv[4])?0:1;if(slot==0&&av_handles[0]>=0)return JS_NewInt32(ctx,av_handles[0]);
 memset(av_pixels,0,sizeof(av_pixels));memset(av_eyes,0,sizeof(av_eyes));
 AvPath body;if(!av_path(ctx,argv[0],"bodyPath",av_identity,&body))return JS_NewInt32(ctx,-1);av_mask(&body,av_body);
 JSValue eyes=JS_GetPropertyStr(ctx,argv[0],"eyes");unsigned count=(unsigned)av_num(ctx,eyes,"length",0);if(count>2)count=2;
 for(unsigned i=0;i<count;i++){
  JSValue eye=JS_GetPropertyUint32(ctx,eyes,i),value=JS_GetPropertyStr(ctx,eye,"matrix");const char *text=JS_ToCString(ctx,value);JS_FreeValue(ctx,value);
  double matrix[6];int valid=text&&sscanf(text,"matrix(%lf,%lf,%lf,%lf,%lf,%lf)",&matrix[0],&matrix[1],&matrix[2],&matrix[3],&matrix[4],&matrix[5])==6;
  if(text)JS_FreeCString(ctx,text);
  for(int k=0;valid&&k<6;k++)if(!isfinite(matrix[k])||fabs(matrix[k])>1000)valid=0;
  AvPath path;if(valid&&av_path(ctx,eye,"d",matrix,&path)){unsigned char mask[AV_GRID*AV_GRID];av_mask(&path,mask);double alpha=av_num(ctx,eye,"alpha",1);for(int j=0;j<AV_GRID*AV_GRID;j++)if(mask[j])av_eyes[j]=fmax(av_eyes[j],alpha);}
  JS_FreeValue(ctx,eye);
 }JS_FreeValue(ctx,eyes);
 JSValue notch=JS_GetPropertyStr(ctx,argv[0],"notch");if(JS_IsObject(notch))av_circle(av_num(ctx,notch,"x",0),av_num(ctx,notch,"y",0),av_num(ctx,notch,"r",0),paper,1,1);JS_FreeValue(ctx,notch);
 av_arcs(ctx,argv[0],"back",detail);
 JSValue behind=JS_GetPropertyStr(ctx,argv[0],"dotsBehind");int dots_behind=JS_ToBool(ctx,behind);JS_FreeValue(ctx,behind);if(dots_behind)av_dots(ctx,argv[0],ink);
 double alpha=av_num(ctx,argv[0],"bodyAlpha",1);
 for(int j=0;j<AV_GRID*AV_GRID;j++)if(av_body[j]){
  double hole=fmax(0,fmin(1,av_eyes[j]));uint32_t color=0;
  for(int k=0;k<4;k++){int shift=24-k*8;unsigned a=(ink>>shift)&255,b=(paper>>shift)&255;color|=(uint32_t)(a*(1-hole)+b*hole+.5)<<shift;}
  av_paint(j%AV_GRID,j/AV_GRID,color,alpha);
 }
 if(!dots_behind)av_dots(ctx,argv[0],ink);
 JSValue notif=JS_GetPropertyStr(ctx,argv[0],"notif");if(JS_IsObject(notif))av_circle(av_num(ctx,notif,"x",0),av_num(ctx,notif,"y",0),av_num(ctx,notif,"r",0),detail,1,0);JS_FreeValue(ctx,notif);
 av_arcs(ctx,argv[0],"front",detail);
 unsigned char pixels[AV_SIZE*AV_SIZE*4];
 for(int y=0;y<AV_SIZE;y++)for(int x=0;x<AV_SIZE;x++){
  unsigned sums[4]={0};for(int sy=0;sy<2;sy++)for(int sx=0;sx<2;sx++){unsigned i=((y*2+sy)*AV_GRID+x*2+sx)*4;for(int k=0;k<4;k++)sums[k]+=av_pixels[i+k];}
  unsigned i=(y*AV_SIZE+x)*4,a=(sums[3]+2)/4;pixels[i+3]=a;for(int k=0;k<3;k++)pixels[i+k]=a?(unsigned char)fmin(255,(sums[k]*255.0)/(4*a)):0;
 }
 int32_t handle=ui_upload_texture(pixels,sizeof(pixels),AV_SIZE,AV_SIZE,3);
 if(handle>=0){if(av_handles[slot]>=0)ui_free_texture(av_handles[slot]);av_handles[slot]=handle;av_frames++;}
 return JS_NewInt32(ctx,handle);
}
static JSValue workshop_avatar_stats(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
 (void)self;(void)argc;(void)argv;return JS_NewInt32(ctx,av_frames);
}
static void workshop_avatar_boot(JSContext *ctx,JSValue info){
 av_handles[0]=av_handles[1]=-1;av_frames=0;
 JS_SetPropertyStr(ctx,info,"avatar",JS_NewCFunction(ctx,workshop_avatar,"avatar",5));
 JS_SetPropertyStr(ctx,info,"avatarFrames",JS_NewCFunction(ctx,workshop_avatar_stats,"avatarFrames",0));
}
static void workshop_avatar_shutdown(void){for(int i=0;i<2;i++){if(av_handles[i]>=0)ui_free_texture(av_handles[i]);av_handles[i]=-1;}}
