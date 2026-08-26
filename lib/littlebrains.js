/* =====================================================================
   Little Brains — the learning sidecar, as a standalone library.
   mathProfile "lb2": every number this file produces comes from the
   IEEE-754 spec-exact operation set (+ - * / sqrt fround imul) plus the
   software transcendentals below — NO Math.tanh/exp/sin/cos/log — so
   results are bit-identical across JS engines, OSes, and the C port
   (compiled with -ffp-contract=off). See FINDINGS.md 29/32/36.

   u  = controller.step(err)
   u += brain.step(ctx, u, true)     // FEL: the teacher is the reflex

   API:
     new LittleBrains.Brain(cfg)     cfg: {nCtx, nOut, dt, authority,
        n=64, cap=n (growth off unless cap>n), seed=1, rule, gate,
        tauElig, leak, rbCap, tauOut, osc, mix, mixPC, sparsifier,
        kFrac, alpha0}
     .step(ctx, teacher, learn) -> Float64Array(nOut)  (the correction)
     .read(ctx) == .step(ctx, 0-vector, false)
     .outcome(cost)     non-negative, lower better ("the metric you
                        already have") — feeds the harm watchdog
     .save() -> Uint8Array   opaque versioned blob (slow layer + stats).
                        The CALLER stores it: a file, localStorage,
                        EEPROM — the library never touches storage.
     .load(bytes)       restore from a blob (same wiring cfg required)
     .health(), .meanAlpha(), .reset(), .mergeFrom(other)

   Memory is honest: n=16 allocates 16-unit arrays (about 2 KB at
   float64). Growth needs an explicit cap > n.
   ===================================================================== */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.LittleBrains = factory();
})(typeof self !== 'undefined' ? self : this, function () {
'use strict';

var VERSION = '0.9.0';
var MATH_PROFILE = 'lb2';
var FAN = 4;

/* ---- deterministic math (spec-exact ops only) ----------------------- */
function mulberry32(a){return function(){a|=0;a=a+0x6D2B79F5|0;
  var t=Math.imul(a^a>>>15,1|a);t=t+Math.imul(t^t>>>7,61|t)^t;
  return((t^t>>>14)>>>0)/4294967296}}
/* gaussian-ish by Irwin-Hall(12): sum of 12 uniforms - 6. Mean 0, var 1,
   support [-6,6]. Replaces Box-Muller (log/cos are not spec-exact). */
function gauss12(rng){var s=0;
  for(var i=0;i<12;i++)s+=rng();
  return s-6}
/* tanh: clamped [7/6] continued-fraction rational (max err ~1e-7 before
   the clamp; activations do not care). Arithmetic only. */
function softTanh(x){
  if(x>4.97)return 1;
  if(x<-4.97)return -1;
  var x2=x*x;
  var p=x*(135135+x2*(17325+x2*(378+x2)));
  var q=135135+x2*(62370+x2*(3150+x2*28));
  return p/q}
/* exp: 2^k * 2^f split; f in [-0.5,0.5], degree-6 minimax-ish poly on
   e^(f*ln2). Rel err ~1e-10 over the range this library uses. */
var LN2=0.6931471805599453, INV_LN2=1.4426950408889634;
function softExp(x){
  if(x<-745)return 0;
  if(x>709)return 1/0;
  var y=x*INV_LN2;
  var k=y>=0?(y+0.5)|0:-((-y+0.5)|0);       // round to nearest int
  var f=(x-k*LN2);                           // |f| <= ln2/2
  var p=1+f*(1+f*(0.5+f*(0.16666666666666666
        +f*(0.041666666666666664+f*(0.008333333333333333
        +f*0.001388888888888889)))));
  /* 2^k by exact doubling/halving (k is small in practice) */
  var s=1;
  if(k>0){while(k>=30){s*=1073741824;k-=30}s*=(1<<k)}
  else if(k<0){k=-k;while(k>=30){s/=1073741824;k-=30}s/=(1<<k)}
  return p*s}
function softLog(x){ /* only used at init/save; newton on softExp */
  if(!(x>0))return -1/0;
  var k=0,v=x;
  while(v>1.5){v*=0.5;k++}
  while(v<0.75){v*=2;k--}
  var t=(v-1)/(v+1),t2=t*t;                  // atanh series, |t|<0.2
  var l=2*t*(1+t2*(0.3333333333333333+t2*(0.2+t2*(0.14285714285714285
        +t2*0.1111111111111111))));
  return l+k*LN2}
/* sin/cos with phase pre-wrapped to [-pi,pi]: degree-9/8 polys. */
var PI=3.141592653589793, TWO_PI=6.283185307179586;
function wrapPi(x){
  while(x>PI)x-=TWO_PI;
  while(x<-PI)x+=TWO_PI;
  return x}
function softSin(x){ /* x already in [-pi,pi] */
  if(x>1.5707963267948966)x=PI-x;
  else if(x<-1.5707963267948966)x=-PI-x;
  var x2=x*x;
  return x*(1+x2*(-0.16666666666666666+x2*(0.008333333333333333
    +x2*(-0.0001984126984126984+x2*0.0000027557319223985893))))}
function softCos(x){return softSin(wrapPi(x+1.5707963267948966))}

/* ---- the brain ------------------------------------------------------ */
function Brain(cfg){
  if(!cfg||!(cfg.nCtx>0)||!(cfg.nOut>0)||!(cfg.dt>0)||!(cfg.authority>0))
    throw new Error('LittleBrains: cfg needs nCtx, nOut, dt, authority');
  this.nCtxIn=cfg.nCtx|0;this.nOut=cfg.nOut|0;this.dt=+cfg.dt;
  this.clamp=0.5*cfg.authority;
  this.Wbox=+cfg.authority;                 // projection bound per weight
  this.n0=(cfg.n||cfg.n0||64)|0;
  this.cap=Math.max(this.n0,(cfg.cap||this.n0)|0);   // honest memory
  this.n=this.n0;
  this.tauGolgi=0.1;
  this.episodic=this.dt>=this.tauGolgi;
  this.sparsifier=cfg.sparsifier||'golgi';  // 'kwta' kept for study
  this.boundES=cfg.bound==='es';
  this.nOsc=cfg.osc!==undefined?cfg.osc|0:(this.episodic?0:3);
  this.nCtx=this.nCtxIn+2*this.nOsc;
  this.td=softExp(-this.dt/(cfg.tauElig||0.3));
  this.leakF=cfg.leak!==undefined?+cfg.leak:0.9995;
  this.muS=0.005;
  this.aNorm=1-softExp(-0.2*this.dt);
  this.tauOut=cfg.tauOut!==undefined?+cfg.tauOut:Math.max(0.05,2.5*this.dt);
  this.gateTau=5.0;this.gateOn=cfg.gate!==false;
  this.metaTheta=0.01;this.metaTau=1e4;this.alpha0=cfg.alpha0||0.05;
  this.logAlpha0=softLog(this.alpha0);
  this.ruleNLMS=cfg.rule?cfg.rule==='nlms':this.episodic;
  this.muF=0.5;
  this.mixOn=cfg.mix!==undefined?!!cfg.mix:false;   // finding 27: off
  this.mixPC=!!cfg.mixPC;
  this.seed=(cfg.seed||1)|0;
  this.rng=mulberry32(this.seed);
  var C=this.cap,J=this.nOut;
  this.idx=new Int32Array(C*FAN);this.wIn=new Float64Array(C*FAN);
  this.biasArr=new Float64Array(C);
  for(var i=0;i<C;i++)this.seedUnit(i);
  this.mu=new Float64Array(this.nCtx);
  this.varr=new Float64Array(this.nCtx);
  for(i=0;i<this.nCtx;i++)this.varr[i]=1;
  this.wf=[];this.ws=[];this.beta=[];this.hTr=[];this.vN=[];
  this.energy=[];this.cnt=[];
  for(var j=0;j<J;j++){
    this.wf.push(new Float64Array(C));this.ws.push(new Float64Array(C));
    var b=new Float64Array(C);
    for(i=0;i<C;i++)b[i]=this.logAlpha0;
    this.beta.push(b);
    this.hTr.push(new Float64Array(C));this.vN.push(new Float64Array(C));
    this.energy.push(new Float64Array(C));
    this.cnt.push(new Float64Array(C));
  }
  this.mixA=new Float64Array(J);
  this.mixDenom=new Float64Array(J);
  for(j=0;j<J;j++)this.mixDenom[j]=1e-2;
  this._lamJ=new Float64Array(J);this._diffJ=new Float64Array(J);
  this.prevS=new Float64Array(C);
  this.elig=new Float64Array(C);this.sAct=new Float64Array(C);
  this.lowTime=new Float64Array(C);
  this.out=new Float64Array(J);this.raw=new Float64Array(J);
  this.corr=new Float64Array(J);this.xn=new Float64Array(this.nCtx);
  this.gRU=new Float64Array(J);this.gRR=new Float64Array(J);
  this.gUU=new Float64Array(J);this.uu0=null;
  this.alpha=new Float64Array(J);
  if(!this.gateOn)for(j=0;j<J;j++)this.alpha[j]=1;
  this.oPre=null;this.oNow=null;this.oPreN=0;this.oN=0;this.oCap=1;
  this.harmRatio=1.5;
  this.oscPh=new Float64Array(this.nOsc);
  this.oscW=new Float64Array(this.nOsc);
  for(var o=0;o<this.nOsc;o++){
    var w0=TWO_PI*0.3;for(var p=0;p<o;p++)w0*=3;
    this.oscW[o]=w0}
  this.oscK=1.5;this.tMag=0.1;
  this.t=0;this.stepI=0;this.active=0;this.nanEvents=0;this.mass=0;
  this.decAcc=new Float64Array(J);this.decN=0;
  this.decEvery=Math.max(1,Math.round(0.1/this.dt));
  this.errHist=[];this.normHist=[];this.lastGrowth=-1e9;
  this.checkEvery=Math.max(1,Math.round(1.0/this.dt));
  this.events=[];
  this.rbCap=cfg.rbCap!==undefined?cfg.rbCap|0:(this.episodic?256:0);
  this.rbN=0;this.rbI=0;
  if(this.rbCap){
    this.rbCtx=new Float64Array(this.rbCap*this.nCtxIn);
    this.rbTot=new Float64Array(this.rbCap*J);
    this.rbMask=new Uint8Array(this.rbCap*J);
  }
  this.wcCap=8;this.wcN=0;this.wcI=0;
  this.wcCtx=new Float64Array(this.wcCap*this.nCtx);
  this.sTmp=new Float64Array(C);this.xnTmp=new Float64Array(this.nCtx);
  this.kFrac=cfg.kFrac||(this.episodic?0.25:0.08);
  this.kwta=Math.max(4,Math.round(this.kFrac*this.n0));
  this._topBuf=new Float64Array(0);
  this._fam=undefined;this._famStep=-1;
}

Brain.prototype.seedUnit=function(i){var r=this.rng,o=i*FAN;
  for(var k=0;k<FAN;k++){this.idx[o+k]=(r()*this.nCtx)|0;
    this.wIn[o+k]=gauss12(r)*0.5}          // /sqrt(FAN)=0.5 exactly
  this.biasArr[i]=gauss12(r)*0.5};

Brain.prototype.reset=function(){var J=this.nOut;
  for(var j=0;j<J;j++){this.wf[j].fill(0);this.ws[j].fill(0);
    this.beta[j].fill(this.logAlpha0);this.hTr[j].fill(0);
    this.vN[j].fill(0);this.energy[j].fill(0);this.cnt[j].fill(0)}
  this.mixA.fill(0);this.mixDenom.fill(1e-2);this.prevS.fill(0);
  this.elig.fill(0);this.out.fill(0);this.n=this.n0;
  this.gRU.fill(0);this.gRR.fill(0);this.gUU.fill(0);this.uu0=null;
  this.alpha.fill(this.gateOn?0:1);this.t=0;this.stepI=0;this.mass=0;
  this.oPre=null;this.oNow=null;this.oPreN=0;this.oN=0;this.oCap=1;
  this.errHist=[];this.normHist=[];this.lastGrowth=-1e9;
  this.lowTime.fill(0);this.decAcc.fill(0);this.decN=0;
  this.rbN=0;this.rbI=0;this.wcN=0;this.wcI=0;
  this._fam=undefined;this._famStep=-1};

/* ---- basis ---------------------------------------------------------- */
Brain.prototype._activate=function(xn){
  var n=this.n,i;
  for(i=0;i<n;i++){var o=i*FAN;
    var z=this.biasArr[i]
      +this.wIn[o]*xn[this.idx[o]]+this.wIn[o+1]*xn[this.idx[o+1]]
      +this.wIn[o+2]*xn[this.idx[o+2]]+this.wIn[o+3]*xn[this.idx[o+3]];
    this.sTmp[i]=softTanh(z)}
  var active=0,s;
  if(this.sparsifier==='golgi'){
    /* exact Golgi fixed point (finding 30): self-tuning density, fixed
       iteration count (WCET-friendly), one sparsifier for both regimes */
    var lo=0,hi=1;
    for(var it=0;it<24;it++){
      var mid=(lo+hi)/2,ssum=0;
      for(i=0;i<n;i++){var v=this.sTmp[i]-mid;if(v>0)ssum+=v}
      if(ssum*0.3>mid)lo=mid;else hi=mid}
    var inh=(lo+hi)/2;
    for(i=0;i<n;i++){s=this.sTmp[i]-inh;
      if(s<0)s=0;else if(s>0)active++;
      this.sAct[i]=s}
  }else{
    /* deterministic k-WTA (study option) */
    var k=this.kwta;
    var top=this._topBuf.length>=k+1?this._topBuf
      :(this._topBuf=new Float64Array(k+1));
    var filled=0;
    for(i=0;i<n;i++){
      var g=this.sTmp[i];
      if(filled<k+1){var p=filled++;top[p]=g;
        while(p>0&&top[p]<top[p-1]){var tp=top[p];top[p]=top[p-1];
          top[p-1]=tp;p--}
      }else if(g>top[0]){
        top[0]=g;var q=0;
        while(q<k&&top[q]>top[q+1]){var tq=top[q];top[q]=top[q+1];
          top[q+1]=tq;q++}
      }
    }
    var theta=Math.max(0,filled>k?top[0]:0);
    for(i=0;i<n;i++){s=this.sTmp[i]-theta;
      if(s<0)s=0;else if(s>0)active++;
      this.sAct[i]=s}
  }
  this.active=active/Math.max(n,1)};

/* ---- main step ------------------------------------------------------ */
Brain.prototype.step=function(ctx,teacher,learn){
  var tch=(typeof teacher==='number')?[teacher]:teacher;
  var J=this.nOut,n=this.n,a=this.aNorm,xn=this.xn,i,j;
  for(var o=0;o<this.nOsc;o++)
    this.oscPh[o]=wrapPi(this.oscPh[o]+this.oscW[o]*this.dt);
  if(learn&&this.nOsc){
    var f=0;for(j=0;j<J;j++)f+=tch[j];f/=J;
    this.tMag+=0.005*((f<0?-f:f)-this.tMag);
    var F=f/(this.tMag+1e-6);
    for(o=0;o<this.nOsc;o++){
      var d=this.oscK*F*softSin(this.oscPh[o])*this.dt;
      this.oscPh[o]=wrapPi(this.oscPh[o]-d);this.oscW[o]-=d;
      var lo2=TWO_PI*0.05,hi2=0.8*PI/this.dt;
      if(this.oscW[o]<lo2)this.oscW[o]=lo2;
      if(this.oscW[o]>hi2)this.oscW[o]=hi2;
    }
  }
  for(var c=0;c<this.nCtxIn;c++){
    if(learn){this.mu[c]+=a*(ctx[c]-this.mu[c]);
      var dv=ctx[c]-this.mu[c];this.varr[c]+=a*(dv*dv-this.varr[c])}
    xn[c]=(ctx[c]-this.mu[c])/Math.sqrt(this.varr[c]+1e-8)}
  for(o=0;o<this.nOsc;o++){
    xn[this.nCtxIn+2*o]=softSin(this.oscPh[o]);
    xn[this.nCtxIn+2*o+1]=softCos(this.oscPh[o])}
  this._activate(xn);
  for(j=0;j<J;j++){
    var yf=0,ys=0,wfj=this.wf[j],wsj=this.ws[j],sA=this.sAct;
    for(i=0;i<n;i++){var sv=sA[i];if(sv!==0){yf+=wfj[i]*sv;ys+=wsj[i]*sv}}
    var lam=this.mixOn?1/(1+softExp(-this.mixA[j])):1;
    var r=this.mixOn?lam*yf+(1-lam)*ys:yf+ys;
    if(r>this.clamp)r=this.clamp;else if(r<-this.clamp)r=-this.clamp;
    this.raw[j]=r;
    this._lamJ[j]=lam;this._diffJ[j]=yf-ys;
    if(this.tauOut>this.dt)this.out[j]+=(r-this.out[j])*(this.dt/this.tauOut);
    else this.out[j]=r;
    if(learn&&this.mixOn){
      var diff=yf-ys;
      this.mixDenom[j]+=0.01*(diff*diff-this.mixDenom[j]);
      this.mixA[j]+=0.5*tch[j]*diff*lam*(1-lam)/(this.mixDenom[j]+1e-6);
      if(this.mixA[j]>4)this.mixA[j]=4;
      if(this.mixA[j]<-4)this.mixA[j]=-4;
    }
  }
  if(!this.gateOn)for(j=0;j<J;j++)this.alpha[j]=1;
  if(learn&&this.gateOn){
    var ga=Math.min(0.15,this.dt/this.gateTau);
    for(j=0;j<J;j++){
      this.gRU[j]+=ga*(this.raw[j]*tch[j]-this.gRU[j]);
      this.gRR[j]+=ga*(this.raw[j]*this.raw[j]-this.gRR[j]);
      this.gUU[j]+=ga*(tch[j]*tch[j]-this.gUU[j])}
    if(this.uu0===null&&this.t>2*this.gateTau)
      this.uu0=Float64Array.from(this.gUU);
    for(j=0;j<J;j++){
      var rho=this.gRU[j]/Math.sqrt(this.gRR[j]*this.gUU[j]+1e-12);
      var harm=this.uu0!==null&&this.gUU[j]>0.7*this.uu0[j]&&rho<0.3;
      if(harm)this.alpha[j]*=Math.max(0,1-this.dt/2);
      else{var up=(rho-0.15)/0.45;
        if(up<0)up=0;else if(up>1)up=1;
        this.alpha[j]=Math.min(1,this.alpha[j]+(this.dt/8)*up)}
    }
  }
  if(learn){
    var td=this.td,e=this.elig,sA2=this.sAct;
    for(i=0;i<n;i++)e[i]=e[i]*td+sA2[i];
    var bad=false;
    for(j=0;j<J;j++){
      var dj=tch[j];
      if(dj===0)continue;
      var djF=dj,djS=dj;
      if(this.mixOn&&this.mixPC){
        djF=dj-(1-this._lamJ[j])*this._diffJ[j];
        djS=dj+this._lamJ[j]*this._diffJ[j];
      }
      var wf2=this.wf[j],ws2=this.ws[j],bj=this.beta[j],
          hj=this.hTr[j],vj=this.vN[j],Ej=this.energy[j],cj=this.cnt[j];
      var ep=1e-8;
      for(i=0;i<n;i++){var x0=e[i];if(x0!==0)ep+=x0*x0}
      var scale=1,nlmsStep=0;
      if(this.ruleNLMS){
        if(this._famStep!==this.stepI){
          var dot=0,na=1e-12,nb=1e-12;
          for(i=0;i<n;i++){var av=sA2[i],bv=this.prevS[i];
            dot+=av*bv;na+=av*av;nb+=bv*bv}
          var fm=dot/Math.sqrt(na*nb);
          if(fm<0)fm=0;else if(fm>1)fm=1;
          this._fam=fm;
          this.prevS.set(sA2.subarray(0,n));
          this._famStep=this.stepI;
        }
        nlmsStep=(0.2+0.8*this._fam)*this.muF/ep;
      }else{
        /* Autostep with the deliberate transient normalization (f.26) */
        var M=0;
        for(i=0;i<n;i++){
          var x=e[i];if(x===0)continue;
          var dxh=djF*x*hj[i],adx=dxh<0?-dxh:dxh;
          var al0=softExp(bj[i]);
          var vv=vj[i];
          var vn=vv+(1/this.metaTau)*al0*x*x*(adx-vv);
          vj[i]=adx>vn?adx:vn;
          if(vj[i]>1e-12){
            bj[i]+=this.metaTheta*dxh/vj[i];
            if(bj[i]>1)bj[i]=1;else if(bj[i]<-12)bj[i]=-12;
          }
          M+=softExp(bj[i])*x*(this.boundES?sA2[i]:x);
        }
        scale=M>1?1/M:1;
      }
      for(i=0;i<n;i++){
        var x2=e[i];if(x2===0)continue;
        var al;
        if(this.ruleNLMS)al=nlmsStep;
        else{al=softExp(bj[i])*scale;
          var hd=1-al*x2*x2;if(hd<0)hd=0;
          hj[i]=hj[i]*hd+al*djF*x2}
        wf2[i]+=al*djF*x2;
        ws2[i]+=(this.ruleNLMS?(0.2+0.8*this._fam):1)*(this.muS/ep)*djS*x2;
        var s2=sA2[i]*sA2[i];
        if(s2>0){Ej[i]=Math.min(Ej[i]+s2,1e4);cj[i]+=s2}
        if(wf2[i]>this.Wbox)wf2[i]=this.Wbox;
        else if(wf2[i]<-this.Wbox)wf2[i]=-this.Wbox;
        if(ws2[i]>this.Wbox)ws2[i]=this.Wbox;
        else if(ws2[i]<-this.Wbox)ws2[i]=-this.Wbox;
        if(!isFinite(wf2[i])||!isFinite(ws2[i]))bad=true;
      }
      this.mass+=1;
    }
    var lk=this.leakF;
    if(lk<1)for(j=0;j<J;j++){
      var wl=this.wf[j];
      for(i=0;i<n;i++)wl[i]*=lk;
    }
    if(bad){this.reset();this.nanEvents++;
      this.events.push([this.t,'NaN guard tripped — memory zeroed'])}
    var dmax=0;
    for(j=0;j<J;j++){var ab=tch[j]<0?-tch[j]:tch[j];if(ab>dmax)dmax=ab}
    if(dmax>3*this.tMag&&dmax>1e-3){
      var wo=this.wcI*this.nCtx;
      for(c=0;c<this.nCtx;c++)this.wcCtx[wo+c]=xn[c];
      this.wcI=(this.wcI+1)%this.wcCap;
      if(this.wcN<this.wcCap)this.wcN++;
    }
    if(this.rbCap){
      var co=this.rbI*this.nCtxIn,to=this.rbI*J;
      for(c=0;c<this.nCtxIn;c++)this.rbCtx[co+c]=ctx[c];
      for(j=0;j<J;j++){
        this.rbTot[to+j]=this.alpha[j]*this.out[j]+tch[j];
        this.rbMask[to+j]=tch[j]!==0?1:0}
      this.rbI=(this.rbI+1)%this.rbCap;
      if(this.rbN<this.rbCap)this.rbN++;
      this._replay();
    }
    this._housekeeping(tch);
  }
  this.t+=this.dt;this.stepI++;
  if(this.oCap<1)for(j=0;j<J;j++)
    if(this.alpha[j]>this.oCap)this.alpha[j]=this.oCap;
  for(j=0;j<J;j++)this.corr[j]=this.alpha[j]*this.out[j];
  return this.corr};

Brain.prototype.read=function(ctx){
  var z=Brain._zeros&&Brain._zeros.length===this.nOut?Brain._zeros
    :(Brain._zeros=new Float64Array(this.nOut));
  return this.step(ctx,z,false)};

/* ---- outcome watchdog (finding 31) --------------------------------- */
Brain.prototype.outcome=function(c){
  if(!(c>=0)||!isFinite(c))return;
  if(this.oPreN<8||this.meanAlpha()<0.5){
    this.oPreN++;
    this.oPre=this.oPre===null?c
      :this.oPre+(c-this.oPre)/Math.min(this.oPreN,8);
    if(this.meanAlpha()<0.5)return;
  }
  this.oNow=this.oNow===null?c:this.oNow+0.1*(c-this.oNow);
  this.oN++;
  var anchor=this.oPre>1e-9?this.oPre:1e-9;
  if(this.oN>=4&&this.oNow>this.harmRatio*anchor){
    this.oCap=Math.max(0.05,this.oCap*0.7);
    this.events.push([this.t,'outcome watchdog: cost '+this.oNow
      +' vs pre-engagement '+anchor+' — authority capped at '+this.oCap]);
  }else if(this.oNow<1.15*anchor){
    this.oCap=Math.min(1,this.oCap*1.03+0.005);
  }};

Brain.prototype.noveltyBonus=function(j){
  var u=0,cj=this.cnt[j],sA=this.sAct,n=this.n;
  for(var i=0;i<n;i++){var s=sA[i];if(s!==0)u+=s*s/(1+cj[i])}
  return Math.sqrt(u)};

Brain.prototype._forward=function(ctx,sOut){
  for(var c=0;c<this.nCtxIn;c++)
    this.xnTmp[c]=(ctx[c]-this.mu[c])/Math.sqrt(this.varr[c]+1e-8);
  for(var o=0;o<this.nOsc;o++){
    this.xnTmp[this.nCtxIn+2*o]=softSin(this.oscPh[o]);
    this.xnTmp[this.nCtxIn+2*o+1]=softCos(this.oscPh[o])}
  var saveS=this.sAct;this.sAct=sOut;
  var saveA=this.active;
  this._activate(this.xnTmp);
  this.sAct=saveS;this.active=saveA};

Brain.prototype._replay=function(){
  if(this.rbN<8)return;
  var n=this.n,s=this.sTmp2||(this.sTmp2=new Float64Array(this.cap));
  for(var k=0;k<4;k++){
    var pick=(this.rng()*this.rbN)|0;
    var co=pick*this.nCtxIn,to=pick*this.nOut;
    this._forward(this.rbCtx.subarray(co,co+this.nCtxIn),s);
    var ep2=1e-8;
    for(var i=0;i<n;i++)ep2+=s[i]*s[i];
    for(var j=0;j<this.nOut;j++){
      if(!this.rbMask[to+j])continue;
      var pred=0,wfj=this.wf[j],wsj=this.ws[j];
      for(i=0;i<n;i++)pred+=(wfj[i]+wsj[i])*s[i];
      var f=0.2*(this.rbTot[to+j]-pred)/ep2;
      for(i=0;i<n;i++)wfj[i]+=f*s[i];
    }
  }};

Brain.prototype._housekeeping=function(tch){
  for(var j=0;j<this.nOut;j++)this.decAcc[j]+=tch[j];
  this.decN++;
  if(this.decN>=this.decEvery){
    var row=[];
    for(j=0;j<this.nOut;j++)row.push(this.decAcc[j]/this.decN);
    this.errHist.push(row);
    if(this.errHist.length>300)this.errHist.shift();
    this.decAcc.fill(0);this.decN=0}
  if(this.stepI%this.checkEvery!==0)return;
  this.normHist.push(this.fastNorm());
  if(this.normHist.length>11)this.normHist.shift();
  this._maybePrune();this._maybeGrow()};

Brain.prototype._residStats=function(){
  if(this.errHist.length<100)return[0,0];
  var e=this.errHist,N=e.length,J=this.nOut;
  var ms=0,r,j;
  for(var q=0;q<N;q++){r=e[q];for(j=0;j<J;j++)ms+=r[j]*r[j]}
  var rms=Math.sqrt(ms/N);
  var rhoMax=0;
  for(j=0;j<J;j++){
    var m=0;for(q=0;q<N;q++)m+=e[q][j];m/=N;
    var den=1e-12;
    for(q=0;q<N;q++){var dd=e[q][j]-m;den+=dd*dd}
    for(var lag=1;lag<=5;lag++){var num=0;
      for(q=lag;q<N;q++)num+=(e[q][j]-m)*(e[q-lag][j]-m);
      var rr=num/den;if(rr<0)rr=-rr;
      if(rr>rhoMax)rhoMax=rr}}
  return[rms,rhoMax]};

Brain.prototype._converged=function(){
  if(this.normHist.length<11)return false;
  var now=this.normHist[10],ago=this.normHist[0];
  var dl=now-ago;if(dl<0)dl=-dl;
  return dl<0.01*Math.max(now,1e-9)};

Brain.prototype._maybeGrow=function(){
  if(this.episodic||this.n>=this.cap)return;
  if(this.uu0===null&&this.gateOn)return;
  if(this.t-this.lastGrowth<20)return;
  if(!this._converged())return;
  var st=this._residStats(),rms=st[0],rho=st[1];
  var rms0=0;
  if(this.uu0)for(var j=0;j<this.nOut;j++)rms0+=this.uu0[j];
  rms0=Math.sqrt(rms0);
  if(rms<0.35*rms0||rho<0.15)return;
  var nNew=Math.min(Math.max((this.n*0.2)|0,4),this.cap-this.n);
  this.n+=nNew;this.lastGrowth=this.t;
  this.kwta=Math.max(4,Math.round(this.kFrac*this.n));
  this.events.push([this.t,'brain grew '+(this.n-nNew)+' -> '+this.n])};

Brain.prototype._maybePrune=function(){
  var mean=0,n=this.n,i,j,w;
  for(i=0;i<n;i++){w=0;
    for(j=0;j<this.nOut;j++){
      var a=this.wf[j][i];if(a<0)a=-a;
      var b=this.ws[j][i];if(b<0)b=-b;
      w+=a+b}
    mean+=w}
  mean/=n;if(mean<1e-9)return;
  var dead=0;
  for(i=0;i<n;i++){w=0;
    for(j=0;j<this.nOut;j++){
      var a2=this.wf[j][i];if(a2<0)a2=-a2;
      var b2=this.ws[j][i];if(b2<0)b2=-b2;
      w+=a2+b2}
    if(w<0.02*mean){this.lowTime[i]+=1;
      if(this.lowTime[i]>60){this._recycle(i);dead++}}
    else this.lowTime[i]=0}
  if(dead>0)this.events.push([this.t,'recycled '+dead+' dead units'])};

Brain.prototype._recycle=function(i){
  this.lowTime[i]=0;this.elig[i]=0;
  for(var j=0;j<this.nOut;j++){this.wf[j][i]=0;this.ws[j][i]=0;
    this.beta[j][i]=this.logAlpha0;this.hTr[j][i]=0;
    this.vN[j][i]=0;this.energy[j][i]=0}
  if(this.wcN>0&&this.rng()<0.5){
    var pick=((this.rng()*this.wcN)|0)*this.nCtx;
    var o=i*FAN,used=[];
    for(var k=0;k<FAN;k++){
      var bi=0,bv=-1;
      for(var c=0;c<this.nCtx;c++){
        if(used.indexOf(c)>=0)continue;
        var v=this.wcCtx[pick+c];if(v<0)v=-v;
        if(v>bv){bv=v;bi=c}}
      used.push(bi);
      this.idx[o+k]=bi;
      this.wIn[o+k]=(this.wcCtx[pick+bi]>=0?1:-1)*(0.5+0.5*this.rng())*0.5;
    }
    var z=0;
    for(k=0;k<FAN;k++)z+=this.wIn[o+k]*this.wcCtx[pick+this.idx[o+k]];
    this.biasArr[i]=-z+0.3;
  }else this.seedUnit(i)};

/* ---- fleet ---------------------------------------------------------- */
Brain.prototype.mergeFrom=function(other){
  var n=Math.min(this.n,other.n);
  for(var j=0;j<this.nOut;j++){
    var wa=this.ws[j],wb=other.ws[j],Ea=this.energy[j],Eb=other.energy[j];
    for(var i=0;i<n;i++){
      var tot=Ea[i]+Eb[i];
      if(tot>1e-9)wa[i]=(Ea[i]*wa[i]+Eb[i]*wb[i])/tot;
      Ea[i]=Math.max(Ea[i],Eb[i]);
    }
  }};

/* ---- telemetry ------------------------------------------------------ */
Brain.prototype.fastNorm=function(){var s=0;
  for(var j=0;j<this.nOut;j++){var w=this.wf[j];
    for(var i=0;i<this.n;i++)s+=w[i]*w[i]}
  return Math.sqrt(s)};
Brain.prototype.slowNormPer=function(i){var s=0;
  for(var j=0;j<this.nOut;j++){var v=this.ws[j][i];s+=v<0?-v:v}
  return s};
Brain.prototype.meanAlpha=function(){var s=0;
  for(var j=0;j<this.nOut;j++)s+=this.alpha[j];
  return s/this.nOut};
Brain.prototype.health=function(){var clamp=0,tot=0,sn=0;
  for(var j=0;j<this.nOut;j++){var wf=this.wf[j],ws=this.ws[j];
    for(var i=0;i<this.n;i++){tot++;sn+=ws[i]*ws[i];
      var a=wf[i]<0?-wf[i]:wf[i],b=ws[i]<0?-ws[i]:ws[i];
      if(a>=0.98*this.Wbox||b>=0.98*this.Wbox)clamp++}}
  return{wf:this.fastNorm(),ws:Math.sqrt(sn),
    clampFrac:clamp/Math.max(tot,1),oCap:this.oCap}};

/* ---- persistence: opaque blob, caller owns the storage -------------- */
/* layout (little-endian): magic 'LB02' | u8 profile=2 | u8 flags
   (bit0 = fast layer included) | u16 n | u16 nCtxIn | u8 nOut | u8 FAN |
   i32 seed | f64 dt | f64 mass | mu[nCtx] f64 | var[nCtx] f64 |
   ws[J][n] f64 | E[J][n] f32 | (wf[J][n] f64 if flag) | crc32 */
function crc32(bytes,end){
  var c,crc=0xFFFFFFFF;
  for(var i=0;i<end;i++){
    c=(crc^bytes[i])&0xFF;
    for(var k=0;k<8;k++)c=c&1?(c>>>1)^0xEDB88320:c>>>1;
    crc=(crc>>>8)^c;
  }
  return(crc^0xFFFFFFFF)>>>0}

Brain.prototype.save=function(includeFast){
  var J=this.nOut,n=this.n,nc=this.nCtx;
  var sz=4+1+1+2+2+1+1+4+8+8+nc*8*2+J*n*8+J*n*4
        +(includeFast?J*n*8:0)+4;
  var buf=new ArrayBuffer(sz),dv=new DataView(buf),
      u8=new Uint8Array(buf),p=0;
  u8[p++]=0x4C;u8[p++]=0x42;u8[p++]=0x30;u8[p++]=0x32;  // 'LB02'
  u8[p++]=2;u8[p++]=includeFast?1:0;
  dv.setUint16(p,n,true);p+=2;
  dv.setUint16(p,this.nCtxIn,true);p+=2;
  u8[p++]=J;u8[p++]=FAN;
  dv.setInt32(p,this.seed,true);p+=4;
  dv.setFloat64(p,this.dt,true);p+=8;
  dv.setFloat64(p,this.mass,true);p+=8;
  var i,j;
  for(i=0;i<nc;i++){dv.setFloat64(p,this.mu[i],true);p+=8}
  for(i=0;i<nc;i++){dv.setFloat64(p,this.varr[i],true);p+=8}
  for(j=0;j<J;j++)for(i=0;i<n;i++){dv.setFloat64(p,this.ws[j][i],true);p+=8}
  for(j=0;j<J;j++)for(i=0;i<n;i++){
    dv.setFloat32(p,Math.fround(this.energy[j][i]),true);p+=4}
  if(includeFast)
    for(j=0;j<J;j++)for(i=0;i<n;i++){
      dv.setFloat64(p,this.wf[j][i],true);p+=8}
  dv.setUint32(p,crc32(u8,p),true);p+=4;
  return u8};

Brain.prototype.load=function(bytes){
  var u8=bytes instanceof Uint8Array?bytes:new Uint8Array(bytes);
  var dv=new DataView(u8.buffer,u8.byteOffset,u8.byteLength),p=0;
  if(u8.length<40||u8[0]!==0x4C||u8[1]!==0x42||u8[2]!==0x30||u8[3]!==0x32)
    throw new Error('LittleBrains.load: not an LB02 blob');
  p=4;
  var profile=u8[p++],flags=u8[p++];
  if(profile!==2)throw new Error('LittleBrains.load: mathProfile mismatch');
  var n=dv.getUint16(p,true);p+=2;
  var nci=dv.getUint16(p,true);p+=2;
  var J=u8[p++],fan=u8[p++];
  var seed=dv.getInt32(p,true);p+=4;
  var dt=dv.getFloat64(p,true);p+=8;
  if(n>this.cap||nci!==this.nCtxIn||J!==this.nOut||fan!==FAN)
    throw new Error('LittleBrains.load: wiring mismatch (blob n='+n
      +' nCtx='+nci+' nOut='+J+'; brain cap='+this.cap
      +' nCtx='+this.nCtxIn+' nOut='+this.nOut+')');
  if(seed!==this.seed)
    throw new Error('LittleBrains.load: basis seed mismatch (blob '
      +seed+', brain '+this.seed+') — weights are coordinates of the '
      +'basis; a different seed means a different coordinate system');
  var expect=4+1+1+2+2+1+1+4+8+8+this.nCtx*8*2+J*n*8+J*n*4
        +((flags&1)?J*n*8:0)+4;
  if(u8.length!==expect)
    throw new Error('LittleBrains.load: size mismatch (got '+u8.length
      +', expected '+expect+') — oscillator/config difference?');
  if(dv.getUint32(u8.length-4,true)!==crc32(u8,u8.length-4))
    throw new Error('LittleBrains.load: CRC mismatch (corrupt blob)');
  this.mass=dv.getFloat64(p,true);p+=8;
  var nc=this.nCtx,i,j;
  for(i=0;i<nc;i++){this.mu[i]=dv.getFloat64(p,true);p+=8}
  for(i=0;i<nc;i++){this.varr[i]=dv.getFloat64(p,true);p+=8}
  this.n=n;
  for(j=0;j<J;j++)for(i=0;i<n;i++){this.ws[j][i]=dv.getFloat64(p,true);p+=8}
  for(j=0;j<J;j++)for(i=0;i<n;i++){this.energy[j][i]=dv.getFloat32(p,true);p+=4}
  if(flags&1)
    for(j=0;j<J;j++)for(i=0;i<n;i++){this.wf[j][i]=dv.getFloat64(p,true);p+=8}
  return this};

return{Brain:Brain,VERSION:VERSION,MATH_PROFILE:MATH_PROFILE,FAN:FAN,
  _softmath:{tanh:softTanh,exp:softExp,log:softLog,sin:softSin,
    cos:softCos,gauss12:gauss12,mulberry32:mulberry32,crc32:crc32}};
});
