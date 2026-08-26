/* lb16 — integer-only Little Brains core for 8-bit-class MCUs.
 * No float, no division inside per-weight loops, no recursion.
 * Fixed-point map:
 *   ctx, s_act, w_in       Q15  (+-1)
 *   bias, wf, out, teacher Q12  (+-8)
 *   ws                     Q27 in int32 (+-16, fine-grained: the slow
 *                          layer must not stall at 1 LSB — V2-FEATURES
 *                          hazard, solved by 32-bit accumulation)
 *   elig                   Q12
 * The only divisions are one per learning step (NLMS normalization)
 * and one at init. */
#include "lb16.h"

/* ---- tanh LUT: 33 entries over z in [0,8) Q12, Q15 out, lerp ------- */
static const int16_t TANH_LUT[33]={
      0,  8025, 15142, 20812, 24955, 27796, 29659, 30846,
  31588, 32047, 32328, 32500, 32605, 32669, 32707, 32731,
  32745, 32754, 32759, 32762, 32764, 32765, 32766, 32766,
  32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767};
/* tanh(k*0.25)*32767 for k=0..32; linear interp between entries. */
static int16_t q_tanh(int32_t z_q12){
  uint8_t neg=0;
  if(z_q12<0){neg=1;z_q12=-z_q12;}
  if(z_q12>=32768L)z_q12=32767L;          /* |z| >= 8 -> saturate       */
  /* index = z / 0.25 in Q12 = z >> 10; frac = low 10 bits              */
  uint8_t ix=(uint8_t)(z_q12>>10);
  int16_t a=TANH_LUT[ix],b=TANH_LUT[ix+1];
  int16_t fr=(int16_t)(z_q12&1023);
  int16_t r=(int16_t)(a+(int16_t)(((int32_t)(b-a)*fr)>>10));
  return neg?(int16_t)-r:r;
}

/* ---- 16-bit xorshift PRNG (wiring only; determinism per seed) ------ */
static uint16_t xs16(uint16_t *s){
  uint16_t x=*s;
  x^=x<<7;x^=x>>9;x^=x<<8;
  *s=x;return x;
}
/* gaussian-ish Q15: sum of 4 uniforms, centered — good enough for a
   random basis (matches the JS library's Irwin-Hall spirit) */
static int16_t gauss4_q15(uint16_t *s){
  int32_t a=0;
  a+=(int16_t)(xs16(s)>>2);a+=(int16_t)(xs16(s)>>2);
  a+=(int16_t)(xs16(s)>>2);a+=(int16_t)(xs16(s)>>2);
  return (int16_t)((a>>1)-16384);
}

void lb16_init(Lb16 *b, uint16_t seed, uint16_t dt_ms, int16_t authority_q12){
  uint8_t i;uint16_t k;
  if(seed==0)seed=1;
  b->rng=seed;
  for(i=0;i<LB16_N;i++){
    for(k=0;k<LB16_FAN;k++){
      b->idx[i*LB16_FAN+k]=(uint8_t)(xs16(&b->rng)%LB16_NCTX);
      /* x3.44 lifts sigma to ~0.5 Q15 (the reference basis width) so
         units leave tanh's linear region; saturate the tails at +-1 */
      {
        int32_t w=((int32_t)gauss4_q15(&b->rng)*55)>>4;
        if(w>32767L)w=32767L;else if(w<-32767L)w=-32767L;
        b->w_in[i*LB16_FAN+k]=(int16_t)w;
      }
    }
    b->bias[i]=(int16_t)(((int32_t)gauss4_q15(&b->rng)*7)>>4); /* Q12 */
    b->wf[i]=0;b->ws[i]=0;b->elig[i]=0;b->s_act[i]=0;b->prev_s[i]=0;
  }
  for(k=0;k<LB16_NCTX;k++){
    b->nrm_mu[k]=0;b->nrm_amp16[k]=16;b->nrm_sh[k]=0;
  }
  /* td = exp(-dt/300ms) ~ 1 - dt/300 for small dt (Q15) */
  {
    uint32_t d=(uint32_t)dt_ms*32768UL/300UL;
    b->td_q15=d>=32768UL?0:(int16_t)(32768UL-d);
  }
  b->leak_q15=32751;                                 /* ~0.9995         */
  b->clamp_q12=(int16_t)(authority_q12/2);
  b->wbox_q12=authority_q12;
  b->out_q12=0;b->corr_q12=0;
  /* tau_out = max(50ms, 2.5*dt): smoothing coeff = dt/tau_out in Q15 */
  {
    uint16_t tau=dt_ms*5U/2U;if(tau<50U)tau=50U;
    uint32_t c=(uint32_t)dt_ms*32768UL/tau;
    b->tau_mix_q15=c>=32768UL?32767:(int16_t)c;
  }
  b->g_ru=0;b->g_rr=0;b->g_uu=0;b->uu0=0;b->uu0_set=0;
  b->alpha_q15=0;
  /* gate ramp: full ramp in ~8 s -> per-step = 32768*dt/8000ms */
  {
    uint32_t up=(uint32_t)dt_ms*32768UL/8000UL;
    b->gate_up_q15=up==0?1U:(uint16_t)up;
    uint32_t dn=(uint32_t)dt_ms*32768UL/2000UL;
    b->gate_dn_q15=dn==0?1U:(uint16_t)dn;
  }
  b->step_lo=0;
  b->warm_steps=(uint16_t)(10000UL/(dt_ms?dt_ms:1));  /* uu0 at ~10 s  */
  b->o_pre=0;b->o_now=0;b->o_pre_n=0;b->o_n=0;b->o_started=0;
  b->o_cap_q15=32767;
}

void lb16_step(Lb16 *b, const int16_t *ctx, int16_t teacher_q12, uint8_t learn){
  uint8_t i;
  /* normalize: xn (Q13, 8192 ~ one typical swing) from raw ctx */
  int16_t xn[LB16_NCTX];
  for(i=0;i<LB16_NCTX;i++){
    int32_t d=(int32_t)ctx[i]-b->nrm_mu[i];
    if(learn){
      /* tau ~20 s at 20 ms: the mean tracker must sit well below the
         plant's signal band or it eats the signal itself. Symmetric
         rounding so the mean does not drift; x16 amp so the EMA does
         not stall below its own LSB. */
      b->nrm_mu[i]=(int16_t)(b->nrm_mu[i]+(int16_t)((d+512)>>10));
      int32_t ad=d<0?-d:d;
      b->nrm_amp16[i]+=((ad<<4)-b->nrm_amp16[i])>>10;
      if((b->step_lo&63u)==0){                     /* AGC every 64     */
        int32_t amp=b->nrm_amp16[i]>>4;
        int32_t sc=b->nrm_sh[i]>=0
          ?(amp<<b->nrm_sh[i]):(amp>>(-b->nrm_sh[i]));
        if(sc>9830L&&b->nrm_sh[i]>-8)b->nrm_sh[i]--;
        else if(sc<3277L&&b->nrm_sh[i]<12)b->nrm_sh[i]++;
      }
    }
    {
      int32_t v=b->nrm_sh[i]>=0?(d<<b->nrm_sh[i]):(d>>(-b->nrm_sh[i]));
      if(v>32767L)v=32767L;else if(v<-32767L)v=-32767L;
      xn[i]=(int16_t)v;
    }
  }
  /* granule layer: z (Q12) = bias + sum w_in(Q15)*xn(Q13) >> 16 */
  int16_t g[LB16_N];
  for(i=0;i<LB16_N;i++){
    const int16_t *w=&b->w_in[i*LB16_FAN];
    const uint8_t *ix=&b->idx[i*LB16_FAN];
    int32_t z=(int32_t)b->bias[i]<<4;               /* to Q16          */
    z+=((int32_t)w[0]*xn[ix[0]])>>12;               /* Q28 -> Q16      */
    z+=((int32_t)w[1]*xn[ix[1]])>>12;
    z+=((int32_t)w[2]*xn[ix[2]])>>12;
    z+=((int32_t)w[3]*xn[ix[3]])>>12;
    g[i]=q_tanh(z>>4);                              /* Q12 in, Q15 out */
  }
  /* Golgi fixed point inh = 0.3 * sum(relu(g-inh)), 10-bit bisection */
  {
    int16_t lo=0,hi=32767;
    uint8_t it;
    for(it=0;it<10;it++){
      int16_t mid=(int16_t)((lo+hi)>>1);
      int32_t ssum=0;
      for(i=0;i<LB16_N;i++){
        int16_t v=(int16_t)(g[i]-mid);
        if(v>0)ssum+=v;
      }
      /* 0.3 in Q15 = 9830 */
      if(((ssum*9830L)>>15)>mid)lo=mid;else hi=mid;
    }
    int16_t inh=(int16_t)((lo+hi)>>1);
    for(i=0;i<LB16_N;i++){
      int16_t s=(int16_t)(g[i]-inh);
      b->s_act[i]=s>0?s:0;
    }
  }
  /* readout: raw (Q12) = sum (wf(Q12) + ws(Q27->Q12)) * s(Q15) >> 15 */
  {
    int32_t acc=0;
    for(i=0;i<LB16_N;i++){
      int16_t s=b->s_act[i];
      if(s){
        int16_t w=(int16_t)(b->wf[i]+(int16_t)(b->ws[i]>>15));
        acc+=((int32_t)w*s)>>15;
      }
    }
    if(acc>b->clamp_q12)acc=b->clamp_q12;
    else if(acc<-b->clamp_q12)acc=-b->clamp_q12;
    /* one-pole smoothing */
    b->out_q12=(int16_t)(b->out_q12
      +(int16_t)(((int32_t)((int16_t)acc-b->out_q12)*b->tau_mix_q15)>>15));
  }
  /* competence gate: EMAs in Q(12+12-8)=Q16-ish int32; rho tested
     squared so no sqrt: ramp when g_ru>0 and g_ru^2 > 0.0225*g_rr*g_uu */
  if(learn){
    int32_t r=b->out_q12,u=teacher_q12;
    int32_t ru=(r*u)>>8,rr=(r*r)>>8,uu=(u*u)>>8;
    /* 5 s EMA at 20 ms (>>8), matching the reference gateTau — faster
       EMAs thrash the gate shut on transient anti-correlation and
       deadlock the closed loop (learned the hard way) */
    b->g_ru+=(ru-b->g_ru)>>8;
    b->g_rr+=(rr-b->g_rr)>>8;
    b->g_uu+=(uu-b->g_uu)>>8;
    if(!b->uu0_set){
      if(b->step_lo>=b->warm_steps){b->uu0=b->g_uu;b->uu0_set=1;}
    }
    {
      /* rho > 0.15  <=>  ru>0 and ru^2 > 0.0225*rr*uu
         0.0225 ~ 737/32768 ; guard against overflow by scaling down  */
      int32_t ru_s=b->g_ru>>4,rr_s=b->g_rr>>4,uu_s=b->g_uu>>4;
      int32_t lhs=ru_s>0?(ru_s>46340L?0x7FFFFFFFL:ru_s*ru_s):0;
      int32_t rhs;
      {
        int32_t p=rr_s>0x7FFFL?0x7FFFL:rr_s;
        int32_t q=uu_s>0x7FFFL?0x7FFFL:uu_s;
        rhs=((p*q)>>15)*737L;
      }
      uint8_t harm=(b->uu0_set&&b->g_uu*10>b->uu0*7          /* >0.7x  */
                    &&!(b->g_ru>0&&lhs>rhs*4));             /* rho<.3~ */
      if(harm){
        uint16_t a=b->alpha_q15;
        b->alpha_q15=a>b->gate_dn_q15?a-b->gate_dn_q15:0;
      }else if(b->g_ru>0&&lhs>rhs){
        uint32_t a=(uint32_t)b->alpha_q15+b->gate_up_q15;
        b->alpha_q15=a>32767U?32767U:(uint16_t)a;
      }
    }
  }
  /* learning: familiarity-scaled NLMS on the eligibility trace.
     elig is Q10 (real trace values reach ~10 with a 300 ms window at
     50 Hz, so Q10 tops out ~10k of 32767 — no saturation, which was
     the first integer build's killer). */
  if(learn&&teacher_q12!=0){
    uint32_t ep=10;                                  /* eps ~0.01 Q10   */
    int32_t dot=0;uint32_t na=1,nb=1;
    for(i=0;i<LB16_N;i++){
      int32_t e=(((int32_t)b->elig[i]*b->td_q15)>>15)+(b->s_act[i]>>5);
      if(e>32767L)e=32767L;
      b->elig[i]=(int16_t)e;                         /* Q10             */
      ep+=(uint32_t)(((int32_t)b->elig[i]*b->elig[i])>>10);
      dot+=((int32_t)b->s_act[i]*b->prev_s[i])>>15;
      na+=(uint32_t)(((int32_t)b->s_act[i]*b->s_act[i])>>15);
      nb+=(uint32_t)(((int32_t)b->prev_s[i]*b->prev_s[i])>>15);
    }
    for(i=0;i<LB16_N;i++)b->prev_s[i]=b->s_act[i];
    /* familiarity ~ dot/max(na,nb) (cheap cosine proxy, no sqrt);
       rate = 0.2 + 0.8*fam in Q15 */
    int32_t den=(int32_t)(na>nb?na:nb);
    int32_t fam=dot>0?(dot>=den?32767L:(dot<<15)/den):0;
    int32_t rate=6554L+((26214L*fam)>>15);
    /* NLMS scalar (one division per step):
       F(Q12) = rate*0.5*teacher ; f = (F<<10)/ep ; dW(Q12) = f*e>>10 */
    int32_t F=((int32_t)teacher_q12*rate)>>16;
    int32_t f=(F<<10)/(int32_t)ep;
    if(f>1048576L)f=1048576L;else if(f<-1048576L)f=-1048576L;
    for(i=0;i<LB16_N;i++){
      int16_t e=b->elig[i];
      if(!e)continue;
      int32_t d=((int32_t)f*e)>>10;
      int32_t w=(int32_t)b->wf[i]+d;
      if(w>b->wbox_q12)w=b->wbox_q12;
      else if(w<-b->wbox_q12)w=-b->wbox_q12;
      b->wf[i]=(int16_t)w;
      b->ws[i]+=d<<8;                                /* Q27, mu_s ratio */
      if(b->ws[i]>((int32_t)b->wbox_q12<<15))b->ws[i]=(int32_t)b->wbox_q12<<15;
      else if(b->ws[i]<-((int32_t)b->wbox_q12<<15))b->ws[i]=-((int32_t)b->wbox_q12<<15);
    }
    /* fast-layer leak */
    for(i=0;i<LB16_N;i++)
      b->wf[i]=(int16_t)(((int32_t)b->wf[i]*b->leak_q15)>>15);
  }else if(learn){
    /* teacher==0: still decay/accumulate eligibility */
    for(i=0;i<LB16_N;i++){
      int32_t e=(((int32_t)b->elig[i]*b->td_q15)>>15)+(b->s_act[i]>>5);
      if(e>32767L)e=32767L;
      b->elig[i]=(int16_t)e;
    }
  }
  b->step_lo++;
  /* gated, watchdog-capped output */
  {
    uint32_t a=b->alpha_q15;
    if(a>b->o_cap_q15)a=b->o_cap_q15;
    b->corr_q12=(int16_t)(((int32_t)b->out_q12*(int32_t)a)>>15);
  }
}

void lb16_outcome(Lb16 *b, uint16_t cost){
  if(b->o_pre_n<8||b->alpha_q15<16384U){
    b->o_pre_n=b->o_pre_n<8?b->o_pre_n+1:8;
    if(!b->o_started){b->o_pre=cost;b->o_started=1;}
    else b->o_pre+=(uint16_t)(((int32_t)cost-b->o_pre)/(int8_t)b->o_pre_n);
    if(b->alpha_q15<16384U)return;
  }
  if(b->o_n==0)b->o_now=cost;
  else b->o_now+=(uint16_t)(((int32_t)cost-b->o_now)/10);
  if(b->o_n<250)b->o_n++;
  {
    uint32_t anchor=b->o_pre?b->o_pre:1;
    if(b->o_n>=4&&(uint32_t)b->o_now*2>anchor*3){       /* >1.5x       */
      uint32_t c=(uint32_t)b->o_cap_q15*7/10;
      b->o_cap_q15=c<1638U?1638U:(uint16_t)c;           /* floor 0.05  */
    }else if((uint32_t)b->o_now*20<anchor*23){          /* <1.15x      */
      uint32_t c=(uint32_t)b->o_cap_q15*33/32+164U;
      b->o_cap_q15=c>32767U?32767U:(uint16_t)c;
    }
  }
}

/* ---- persistence: caller-owned bytes ------------------------------- */
static uint16_t crc16(const uint8_t *p, uint16_t n){
  uint16_t crc=0xFFFF;uint16_t i;uint8_t k;
  for(i=0;i<n;i++){
    crc^=p[i];
    for(k=0;k<8;k++)crc=(crc&1)?(crc>>1)^0xA001:crc>>1;
  }
  return crc;
}
uint16_t lb16_save(const Lb16 *b, uint8_t *buf, uint16_t buf_len){
  uint16_t need=(uint16_t)(6+LB16_N*4+2);
  uint16_t p=0;uint8_t i;
  if(buf_len<need)return 0;
  buf[p++]='L';buf[p++]='b';buf[p++]=16;buf[p++]=LB16_N;
  buf[p++]=(uint8_t)(b->rng==0);            /* reserved                */
  buf[p++]=LB16_NCTX;
  for(i=0;i<LB16_N;i++){
    uint32_t w=(uint32_t)b->ws[i];
    buf[p++]=(uint8_t)w;buf[p++]=(uint8_t)(w>>8);
    buf[p++]=(uint8_t)(w>>16);buf[p++]=(uint8_t)(w>>24);
  }
  {
    uint16_t c=crc16(buf,p);
    buf[p++]=(uint8_t)c;buf[p++]=(uint8_t)(c>>8);
  }
  return p;
}
int8_t lb16_load(Lb16 *b, const uint8_t *buf, uint16_t len){
  uint16_t need=(uint16_t)(6+LB16_N*4+2);
  uint16_t p=6;uint8_t i;
  if(len!=need)return -1;
  if(buf[0]!='L'||buf[1]!='b'||buf[2]!=16)return -2;
  if(buf[3]!=LB16_N||buf[5]!=LB16_NCTX)return -3;
  {
    uint16_t c=crc16(buf,(uint16_t)(len-2));
    if(buf[len-2]!=(uint8_t)c||buf[len-1]!=(uint8_t)(c>>8))return -4;
  }
  for(i=0;i<LB16_N;i++){
    uint32_t w=(uint32_t)buf[p]|((uint32_t)buf[p+1]<<8)
      |((uint32_t)buf[p+2]<<16)|((uint32_t)buf[p+3]<<24);
    b->ws[i]=(int32_t)w;p+=4;
  }
  return 0;
}
