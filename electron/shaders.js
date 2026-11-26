// GLSL for the three passes: feedback (warp previous frame + draw new layers),
// particles (additive points), composite (kaleidoscope, bloom, colour, to screen).
const SH = {};

SH.vert = `#version 300 es
void main(){ vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2)); gl_Position=vec4(p*2.-1.,0.,1.); }`;

const COMMON = `#version 300 es
precision highp float;
#define TAU 6.28318530718
uniform sampler2D uAudio;
uniform vec2 uRes;
uniform float uTime,uAT,uBass,uMid,uTreb,uBeat,uBeatAge,uDt60,palShift;
vec3 pal(float t){ return .5+.5*cos(TAU*(t+palShift+vec3(0.,.33,.67))); }
float spec(float x){ return texture(uAudio,vec2(x,.25)).r; }
float wav(float x){ return texture(uAudio,vec2(x,.75)).r-.5; }
mat2 R(float a){ float c=cos(a),s=sin(a); return mat2(c,-s,s,c); }
`;

SH.feedback = COMMON + `
uniform sampler2D uPrev;
uniform float zoom,rot,swirl,warp,warpF,ripple,decay,hue,wave,line,ring,orbs,plasma,tunnel,flower,flowerK,bars,shock,web;
out vec4 o;
vec3 hueRot(vec3 c,float a){ const vec3 k=vec3(.57735); float ca=cos(a),sa=sin(a); return c*ca+cross(k,c)*sa+k*dot(k,c)*(1.-ca); }
float glow(float d,float w){ float x=d/w; return exp(-x*x*.5)+.12/(1.+x*x*.6); }
void main(){
  vec2 p=(gl_FragCoord.xy-.5*uRes)/uRes.y;
  float r=length(p), a=atan(p.y,p.x);
  float u=a/TAU+.5, um=abs(u*2.-1.);

  // ---- warp the previous frame ----
  vec2 q=p;
  q*=1.-(zoom*(1.+1.5*uBass)+.012*uBeat*sign(zoom))*uDt60;
  q=R((rot+swirl*(.5-r))*(1.+uMid)*uDt60)*q;
  float t=uTime*.4;
  q+=warp*.0035*uDt60*vec2(
      sin(q.y*warpF*3.+t)+.5*sin(q.y*warpF*7.-t*1.7+q.x*2.),
      cos(q.x*warpF*3.3-t*.8)+.5*cos(q.x*warpF*6.1+t*1.3+q.y*2.));
  q+=ripple*.004*uDt60*(p/(r+1e-4))*sin(r*24.-uAT*3.)*(.5+uBass);
  vec3 c=texture(uPrev,q*vec2(uRes.y/uRes.x,1.)+.5).rgb;
  c=max(hueRot(c,hue*uDt60),0.);
  c*=pow(decay,uDt60);
  c-=min(c.r,min(c.g,c.b))*.05*uDt60; // bleed white haze back into colour

  // ---- new layers ----
  vec3 L=vec3(0.), F=vec3(0.);
  if(wave>0.){ // circular oscilloscope
    float R0=.24+.09*uBass;
    float d=abs(r-(R0+wav(um)*.3));
    L+=wave*pal(u+uTime*.07)*glow(d,.004)*1.4;
    d=abs(r-(R0*.45-wav(1.-um)*.15));
    L+=wave*pal(u+uTime*.07+.5)*glow(d,.003)*.8;
  }
  if(line>0.){ // drifting oscilloscope lines
    for(int i=0;i<2;i++){
      float fi=float(i);
      vec2 pl=R(.35*sin(uTime*.13+fi*1.9)+fi*1.5708*step(.5,sin(uTime*.05)))*p;
      float x=clamp(pl.x*(uRes.y/uRes.x)*.9+.5,0.,1.);
      float d=abs(pl.y-wav(x)*.6-(fi-.5)*.3*sin(uTime*.21));
      L+=line*pal(x*.6+uTime*.05+fi*.4)*glow(d,.0035)*1.3;
    }
  }
  if(ring>0.){ // radial spectrum
    float n=56.;
    float id=floor(um*n)/n, f=fract(um*n);
    float s=spec(id*.82+.01);
    float R1=.17+.05*uBass;
    float in_=step(R1,r)*smoothstep(R1+s*.33+.006,R1+s*.33,r)*smoothstep(.08,.2,f)*smoothstep(.92,.8,f);
    F+=ring*pal(id*.9+uTime*.05)*in_*(.3+.6*s);
  }
  if(bars>0.){ // mirrored spectrum bars top and bottom
    float x=gl_FragCoord.x/uRes.x, xm=abs(x*2.-1.);
    float n=44., id=floor(xm*n)/n, f=fract(xm*n);
    float s=spec(id*.85+.01);
    float in_=smoothstep(.5-s*.34-.004,.5-s*.34,abs(p.y))*smoothstep(.1,.22,f)*smoothstep(.9,.78,f);
    F+=bars*pal(id*.7+uTime*.04+.2)*in_*(.3+.6*s);
  }
  if(orbs>0.){ // band-reactive orbiting lights
    for(int i=0;i<7;i++){
      float fi=float(i);
      vec2 c0=vec2(sin(uAT*.31*(1.+fi*.17)+fi*1.7),cos(uAT*.27*(1.+fi*.13)+fi*2.3))*(.2+.2*sin(uTime*.1+fi));
      vec2 d=p-c0;
      float e=spec(.04+.12*fi);
      L+=orbs*pal(fi/7.+uTime*.03)*exp(-dot(d,d)/(.0004+.004*e*e))*(.35+e);
    }
  }
  if(tunnel>0.){ // polar grid rushing outward
    float tz=.3/(r+.06)+uAT*.5;
    float g=pow(abs(sin(tz*5.)),34.)+pow(abs(sin(u*TAU*8.+.4*sin(tz))),44.)*.7;
    L+=tunnel*pal(tz*.08+u*.2)*g*smoothstep(.02,.2,r)*(.35+1.2*uTreb+.5*uBeat);
  }
  if(flower>0.){ // breathing rose curves
    for(int i=0;i<2;i++){
      float s=float(i)*2.-1.;
      float rf=(.2+.13*uBass+.06*float(i))*(.6+.4*cos(flowerK*a+s*uTime*.6+wav(um)*2.));
      L+=flower*pal(u*2.+uTime*.1+.3+.25*float(i))*glow(abs(r-rf),.0035)*1.3;
    }
  }
  if(web>0.){ // interference lattice
    vec2 g=R(uTime*.07)*p*(7.+2.*sin(uTime*.2));
    float v=abs(sin(g.x)+sin(g.y)+sin((g.x+g.y)*.7+uAT));
    L+=web*pal(v*.3+r+uTime*.05)*glow(v,.05)*(.25+.9*uMid);
  }
  if(shock>0.){ // beat shockwave
    float d=abs(r-uBeatAge*1.3-.03);
    L+=shock*pal(uTime*.21+.1)*glow(d,.005)*exp(-uBeatAge*3.5)*2.5;
  }
  c=min(c,1.); L=clamp(L*.85,0.,1.);
  c=1.-(1.-c)*(1.-L);
  c=max(c,min(F,.85)); // screen blend keeps the buffer in range
  if(plasma>0.){ // slow colour fog that the warp smears around
    float v=sin(p.x*5.+uTime)+sin(p.y*6.-uTime*1.2)+sin((p.x+p.y)*4.+uAT)+sin(r*9.-uAT*1.5);
    c=mix(c,pal(v*.14+uTime*.03)*(.2+.9*uMid+.4*uBass),clamp(plasma*.05*uDt60,0.,1.));
  }
  o=vec4(clamp(c,0.,1.),1.);
}`;

SH.pvert = `#version 300 es
precision highp float;
#define TAU 6.28318530718
uniform sampler2D uAudio;
uniform vec2 uRes;
uniform float uTime,uAT,uBass,uTreb,uBeat,palShift,uN,pStar,pPhyl;
out vec3 vCol;
float h(float n){ return fract(sin(n*12.9898)*43758.5453); }
vec3 pal(float t){ return .5+.5*cos(TAU*(t+palShift+vec3(0.,.33,.67))); }
void main(){
  float id=float(gl_VertexID), f=id/uN;
  // starburst
  float ang=h(id)*TAU+uAT*.08*(h(id+3.)-.5);
  float ph=fract(h(id+7.)+uAT*(.02+.05*h(id+11.)));
  vec2 pa=ph*ph*1.3*vec2(cos(ang),sin(ang));
  // phyllotaxis driven by the waveform
  float w=texture(uAudio,vec2(f,.75)).r-.5;
  float a2=id*2.39996323+uTime*.15;
  vec2 pb=sqrt(f)*.62*(1.+w*1.6+.15*uBass)*vec2(cos(a2),sin(a2));
  float m=pPhyl/(pStar+pPhyl+1e-4);
  vec2 pos=mix(pa,pb,m);
  gl_Position=vec4(pos*vec2(uRes.y/uRes.x,1.)*2.,0.,1.);
  float sz=mix(1.+3.5*ph*h(id+5.),1.6+2.*abs(w)*4.,m);
  gl_PointSize=(sz+2.*uBeat)*uRes.y/720.;
  float br=mix(ph*(.4+1.5*uTreb),.5+.8*abs(w)*4.,m);
  vCol=pal(mix(h(id+2.),f*2.,m)+uTime*.05)*br*max(pStar,pPhyl);
}`;
SH.pfrag = `#version 300 es
precision highp float;
in vec3 vCol; out vec4 o;
void main(){ vec2 d=gl_PointCoord-.5; float a=smoothstep(.5,.0,length(d)); o=vec4(vCol*a*a,1.); }`;

SH.composite = COMMON + `
uniform sampler2D uTex;
uniform float kal,kalN,chroma,palMix,exposure;
out vec4 o;
vec2 toUV(vec2 p){ return p*vec2(uRes.y/uRes.x,1.)+.5; }
vec3 sampleC(vec2 p,float ca){
  vec2 dir=p*ca;
  return vec3(texture(uTex,toUV(p+dir)).r,texture(uTex,toUV(p)).g,texture(uTex,toUV(p-dir)).b);
}
void main(){
  vec2 p=(gl_FragCoord.xy-.5*uRes)/uRes.y;
  float r=length(p);
  vec2 pk=p;
  if(kal>0.){
    float seg=TAU/kalN;
    float a=atan(p.y,p.x)+uTime*.05;
    a=abs(mod(a,seg)-seg*.5)+uTime*.03;
    pk=r*vec2(cos(a),sin(a));
  }
  float ca=chroma*(.006+.02*uBeat)*(.3+r);
  vec3 c=sampleC(p,ca);
  if(kal>0.) c=mix(c,sampleC(pk,ca),kal);
  // bloom from the mip chain
  vec2 uvb=toUV(mix(p,pk,step(.5,kal)));
  vec3 b=textureLod(uTex,uvb,2.5).rgb*.5+textureLod(uTex,uvb,4.5).rgb*.4+textureLod(uTex,uvb,6.5).rgb*.3;
  c+=b*(.3+.35*uBeat);
  // psychedelic recolour by luminance
  float l=dot(c,vec3(.299,.587,.114));
  c=mix(c,pal(l*.9+r*.4-uTime*.06)*l*1.5,palMix);
  c=1.-exp(-c*c*1.9*exposure*(1.+.25*uBeat));
  c=mix(vec3(dot(c,vec3(.333))),c,1.25);
  c*=1.-.45*smoothstep(.45,1.05,r);
  c=pow(max(c,0.),vec3(.9));
  float n=fract(sin(dot(gl_FragCoord.xy+uTime,vec2(12.9898,78.233)))*43758.5453);
  o=vec4(c+(n-.5)/255.,1.);
}`;

// Scene = a set of weights; scenes cross-fade by interpolating every number.
const BASE = { zoom:.02, rot:0, swirl:0, warp:0, warpF:1, ripple:0, decay:.95, hue:.01,
  wave:0, line:0, ring:0, orbs:0, plasma:0, tunnel:0, flower:0, flowerK:5, bars:0, shock:0, web:0,
  pStar:0, pPhyl:0, kal:0, kalN:6, chroma:.3, palMix:0, palShift:0, exposure:1.1 };
const SCENES = [
  ['Event Horizon',   { zoom:.03, rot:.004, swirl:.03, warp:.4, decay:.962, hue:.02, wave:1, shock:1, pStar:1, orbs:.6, chroma:.6 }],
  ['Mandala Engine',  { zoom:.014, rot:.01, warp:.7, warpF:2, decay:.95, ring:1, flower:1, flowerK:5, orbs:1, kal:1, kalN:8, palMix:.3, palShift:.15, pPhyl:.5 }],
  ['Liquid Aurora',   { zoom:.006, rot:-.002, warp:2.2, warpF:1.6, decay:.985, hue:.012, plasma:1, line:1, orbs:.5, palMix:.45, palShift:.4, chroma:.2 }],
  ['Neon Tunnel',     { zoom:.05, swirl:.06, decay:.93, tunnel:1, wave:.7, pStar:1, shock:1, chroma:1, palShift:.55, hue:.03 }],
  ['Spectrum Bloom',  { zoom:.022, rot:.003, ripple:1.2, decay:.955, ring:1, bars:1, pPhyl:1, shock:.6, palShift:.7, hue:.015 }],
  ['Kaleido Fever',   { zoom:.02, rot:-.012, warp:1.2, warpF:2.4, decay:.96, plasma:.7, line:1, orbs:1, web:.6, kal:1, kalN:12, palMix:.75, palShift:.25, chroma:.5 }],
  ['Phyllotaxis',     { zoom:-.012, rot:.02, swirl:-.03, decay:.955, pPhyl:1, flower:.8, flowerK:7, shock:1, bars:.5, palShift:.85, hue:.025 }],
  ['Ripple Tank',     { zoom:.008, ripple:2.4, warp:.5, decay:.975, hue:.03, wave:1, orbs:1, web:.5, palShift:.05, palMix:.25, chroma:.4 }],
  ['Hyperflower',     { zoom:.03, swirl:-.05, warp:.6, decay:.95, flower:1, flowerK:3, tunnel:.45, ring:.6, pStar:.7, kal:1, kalN:6, palShift:.33, palMix:.2 }],
  ['Meltdown',        { zoom:.016, rot:.008, swirl:.04, warp:3, warpF:2.2, ripple:1, decay:.972, hue:.04, wave:.8, line:.8, ring:.6, orbs:1, plasma:.6, web:.7, flower:.6, flowerK:4, shock:1, pStar:.8, pPhyl:.6, kal:1, kalN:4, palMix:1, palShift:.6, chroma:1, exposure:.95 }],
].map(([name, p]) => ({ name, p: Object.assign({}, BASE, p) }));
