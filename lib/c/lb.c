/* Little Brains C99 core — exact operation-order mirror of
 * lib/littlebrains.js. Compile: -O2 -std=c99 -ffp-contract=off.
 * Every arithmetic expression below reproduces the JS grouping so the
 * golden vectors match bit-for-bit on IEEE-754 double hardware. */
#include "lb.h"
#include <math.h>    /* sqrt only (IEEE-exact); no transcendentals */
#include <string.h>

/* ---- deterministic math -------------------------------------------- */
static double rng_next(uint32_t *state){
  uint32_t a=*state;
  a=a+0x6D2B79F5u;
  *state=a;
  uint32_t t=(uint32_t)((int32_t)(a^(a>>15))*(int32_t)(1u|a));
  t=(uint32_t)((int32_t)t+(int32_t)((uint32_t)((int32_t)(t^(t>>7))*(int32_t)(61u|t))))^t;
  return (double)((t^(t>>14)))*(1.0/4294967296.0);
}
static double gauss12(uint32_t *state){
  double s=0;
  for(int i=0;i<12;i++)s+=rng_next(state);
  return s-6;
}
double lb_soft_tanh(double x){
  if(x>4.97)return 1;
  if(x<-4.97)return -1;
  double x2=x*x;
  double p=x*(135135+x2*(17325+x2*(378+x2)));
  double q=135135+x2*(62370+x2*(3150+x2*28));
  return p/q;
}
#define LB_LN2 0.6931471805599453
#define LB_INV_LN2 1.4426950408889634
double lb_soft_exp(double x){
  if(x<-745)return 0;
  if(x>709)return (double)INFINITY;
  double y=x*LB_INV_LN2;
  int32_t k=y>=0?(int32_t)(y+0.5):-(int32_t)(-y+0.5);
  double f=(x-k*LB_LN2);
  double p=1+f*(1+f*(0.5+f*(0.16666666666666666
        +f*(0.041666666666666664+f*(0.008333333333333333
        +f*0.001388888888888889)))));
  double s=1;
  if(k>0){while(k>=30){s*=1073741824;k-=30;}s*=(double)(1<<k);}
  else if(k<0){k=-k;while(k>=30){s/=1073741824;k-=30;}s/=(double)(1<<k);}
  return p*s;
}
double lb_soft_log(double x){
  if(!(x>0))return -(double)INFINITY;
  int k=0;double v=x;
  while(v>1.5){v*=0.5;k++;}
  while(v<0.75){v*=2;k--;}
  double t=(v-1)/(v+1),t2=t*t;
  double l=2*t*(1+t2*(0.3333333333333333+t2*(0.2+t2*(0.14285714285714285
        +t2*0.1111111111111111))));
  return l+k*LB_LN2;
}
#define LB_PI 3.141592653589793
#define LB_TWO_PI 6.283185307179586
double lb_wrap_pi(double x){
  while(x>LB_PI)x-=LB_TWO_PI;
  while(x<-LB_PI)x+=LB_TWO_PI;
  return x;
}
double lb_soft_sin(double x){
  if(x>1.5707963267948966)x=LB_PI-x;
  else if(x<-1.5707963267948966)x=-LB_PI-x;
  double x2=x*x;
  return x*(1+x2*(-0.16666666666666666+x2*(0.008333333333333333
    +x2*(-0.0001984126984126984+x2*0.0000027557319223985893))));
}
double lb_soft_cos(double x){return lb_soft_sin(lb_wrap_pi(x+1.5707963267948966));}
uint32_t lb_crc32(const uint8_t *p, size_t n){
  uint32_t crc=0xFFFFFFFFu;
  for(size_t i=0;i<n;i++){
    uint32_t c=(crc^p[i])&0xFFu;
    for(int k=0;k<8;k++)c=(c&1u)?((c>>1)^0xEDB88320u):(c>>1);
    crc=(crc>>8)^c;
  }
  return crc^0xFFFFFFFFu;
}
static double dmin(double a,double b){return a<b?a:b;}
static double dmax(double a,double b){return a>b?a:b;}
static int32_t jsround(double x){return (int32_t)floor(x+0.5);}

/* ---- arena layout --------------------------------------------------- */
static void norm_cfg(LbConfig *c){
  if(c->n<=0)c->n=64;
  if(c->cap<c->n)c->cap=c->n;
  if(c->seed==0)c->seed=1;
  if(c->tau_elig<=0)c->tau_elig=0.3;
  if(c->leak==0)c->leak=0.9995;
  if(c->alpha0<=0)c->alpha0=0.05;
}
size_t lb_mem_required(const LbConfig *cfg_in){
  if(!cfg_in||cfg_in->n_ctx<=0||cfg_in->n_out<=0||cfg_in->dt<=0
     ||cfg_in->authority<=0)return 0;
  LbConfig c=*cfg_in;norm_cfg(&c);
  int episodic=c.dt>=0.1;
  /* 0 = auto, -1 = explicitly none, >0 = count */
  int n_osc=c.osc==0?(episodic?0:3):(c.osc<0?0:c.osc);
  int n_ctx=c.n_ctx+2*n_osc;
  int rb=c.rb_cap==0?(episodic?256:0):(c.rb_cap<0?0:c.rb_cap);
  int cap=c.cap,J=c.n_out;
  int grow=cap>c.n;
  int kwta_max=(int)(0.25*cap)+2;
  size_t d=0;
  d+=(size_t)cap*LB_FAN;                    /* w_in */
  d+=(size_t)cap;                           /* bias */
  d+=(size_t)n_ctx*4;                       /* mu, varr, xn, xn_tmp */
  d+=(size_t)J*cap*7;                       /* wf ws beta h v energy cnt */
  d+=(size_t)J*4;                           /* mix_a mix_denom lam diff */
  d+=(size_t)cap*6;                         /* prev_s elig s_act s_tmp s_tmp2 low_time */
  d+=(size_t)J*3;                           /* out raw corr */
  d+=(size_t)J*6;                           /* g_ru g_rr g_uu uu0 alpha dec_acc */
  d+=(size_t)n_osc*2;
  d+=(size_t)rb*c.n_ctx+(size_t)rb*J;       /* rb_ctx rb_tot */
  d+=(size_t)8*n_ctx;                       /* wc_ctx */
  if(grow)d+=(size_t)300*J+11;              /* err_hist norm_hist */
  d+=(size_t)(c.sparsifier==1?kwta_max:0);  /* top_buf */
  size_t bytes=d*sizeof(double);
  bytes+=(size_t)cap*LB_FAN*sizeof(int32_t);/* idx */
  bytes+=(size_t)rb*J;                      /* rb_mask u8 */
  bytes+=64;                                /* alignment slack */
  return bytes;
}

static void seed_unit(LbBrain *b,int i){
  int o=i*LB_FAN;
  for(int k=0;k<LB_FAN;k++){
    b->idx[o+k]=(int32_t)(rng_next(&b->rng)*b->n_ctx);
    b->w_in[o+k]=gauss12(&b->rng)*0.5;
  }
  b->bias[i]=gauss12(&b->rng)*0.5;
}

int lb_init(LbBrain *b, const LbConfig *cfg_in, void *mem, size_t mem_len){
  if(!b||!cfg_in||!mem)return -1;
  size_t need=lb_mem_required(cfg_in);
  if(need==0)return -1;
  if(mem_len<need)return -2;
  memset(b,0,sizeof *b);
  LbConfig c=*cfg_in;norm_cfg(&c);
  b->n_ctx_in=c.n_ctx;b->n_out=c.n_out;b->dt=c.dt;
  b->clamp=0.5*c.authority;b->wbox=c.authority;
  b->n0=c.n;b->cap=c.cap;b->n=c.n;
  b->tau_golgi=0.1;
  b->episodic=b->dt>=b->tau_golgi;
  b->sparsifier=c.sparsifier;
  b->bound_es=0;
  b->n_osc=c.osc==0?(b->episodic?0:3):(c.osc<0?0:c.osc);
  b->n_ctx=b->n_ctx_in+2*b->n_osc;
  b->td=lb_soft_exp(-b->dt/c.tau_elig);
  b->leak_f=c.leak;
  b->mu_s=0.005;
  b->a_norm=1-lb_soft_exp(-0.2*b->dt);
  b->tau_out=c.tau_out>0?c.tau_out:dmax(0.05,2.5*b->dt);
  b->gate_tau=5.0;
  b->gate_on=c.gate<0?0:1;                  /* 0 = default on, -1 = off */
  b->meta_theta=0.01;b->meta_tau=1e4;b->alpha0=c.alpha0;
  b->log_alpha0=lb_soft_log(b->alpha0);
  b->rule_nlms=c.rule?(c.rule==1):b->episodic;
  b->mu_f=0.5;
  b->mix_on=c.mix?1:0;b->mix_pc=c.mix_pc?1:0;
  b->seed=c.seed;b->rng=(uint32_t)c.seed;
  b->k_frac=c.k_frac>0?c.k_frac:(b->episodic?0.25:0.08);
  b->kwta=(int)dmax(4,(double)jsround(b->k_frac*b->n0));
  b->rb_cap=c.rb_cap==0?(b->episodic?256:0):(c.rb_cap<0?0:c.rb_cap);
  b->grow_enabled=b->cap>b->n0;
  b->harm_ratio=1.5;b->o_cap=1;b->t_mag=0.1;
  b->dec_every=(int)dmax(1,(double)jsround(0.1/b->dt));
  b->check_every=(int)dmax(1,(double)jsround(1.0/b->dt));
  b->last_growth=-1e9;b->fam_step=-1;
  /* carve the arena */
  uint8_t *p=(uint8_t*)mem;
  uintptr_t up=(uintptr_t)p;up=(up+7u)&~(uintptr_t)7u;p=(uint8_t*)up;
  double **dsl[]= {
    &b->w_in,&b->bias,&b->mu,&b->varr,&b->xn,&b->xn_tmp,
    &b->wf,&b->ws,&b->beta,&b->h_tr,&b->v_n,&b->energy,&b->cnt,
    &b->mix_a,&b->mix_denom,&b->lam_j,&b->diff_j,
    &b->prev_s,&b->elig,&b->s_act,&b->s_tmp,&b->s_tmp2,&b->low_time,
    &b->out,&b->raw,&b->corr,
    &b->g_ru,&b->g_rr,&b->g_uu,&b->uu0,&b->alpha,&b->dec_acc,
    &b->osc_ph,&b->osc_w,&b->rb_ctx,&b->rb_tot,&b->wc_ctx,
    &b->err_hist,&b->norm_hist,&b->top_buf};
  int cap=b->cap,J=b->n_out,nc=b->n_ctx,rb=b->rb_cap;
  int kwta_max=(int)(0.25*cap)+2;
  size_t sizes[]={
    (size_t)cap*LB_FAN,(size_t)cap,(size_t)nc,(size_t)nc,(size_t)nc,(size_t)nc,
    (size_t)J*cap,(size_t)J*cap,(size_t)J*cap,(size_t)J*cap,(size_t)J*cap,
    (size_t)J*cap,(size_t)J*cap,
    (size_t)J,(size_t)J,(size_t)J,(size_t)J,
    (size_t)cap,(size_t)cap,(size_t)cap,(size_t)cap,(size_t)cap,(size_t)cap,
    (size_t)J,(size_t)J,(size_t)J,
    (size_t)J,(size_t)J,(size_t)J,(size_t)J,(size_t)J,(size_t)J,
    (size_t)b->n_osc,(size_t)b->n_osc,
    (size_t)rb*b->n_ctx_in,(size_t)rb*J,(size_t)8*nc,
    b->grow_enabled?(size_t)300*J:0,b->grow_enabled?(size_t)11:0,
    b->sparsifier==1?(size_t)kwta_max:0};
  for(size_t s=0;s<sizeof(sizes)/sizeof(sizes[0]);s++){
    *dsl[s]=(double*)p;
    memset(p,0,sizes[s]*sizeof(double));
    p+=sizes[s]*sizeof(double);
  }
  b->idx=(int32_t*)p;p+=(size_t)cap*LB_FAN*sizeof(int32_t);
  b->rb_mask=(uint8_t*)p;p+=(size_t)rb*J;
  memset(b->rb_mask,0,(size_t)rb*J);
  /* init values */
  for(int i=0;i<cap;i++)seed_unit(b,i);
  for(int i=0;i<nc;i++)b->varr[i]=1;
  for(int j=0;j<J;j++){
    for(int i=0;i<cap;i++)b->beta[(size_t)j*cap+i]=b->log_alpha0;
    b->mix_denom[j]=1e-2;
    if(!b->gate_on)b->alpha[j]=1;
  }
  for(int o=0;o<b->n_osc;o++){
    double w0=LB_TWO_PI*0.3;
    for(int q=0;q<o;q++)w0*=3;
    b->osc_w[o]=w0;
  }
  b->osc_k=1.5;
  b->o_pre_set=0;b->o_now_set=0;
  return 0;
}

void lb_reset(LbBrain *b){
  int cap=b->cap,J=b->n_out;
  for(int j=0;j<J;j++){
    memset(b->wf+(size_t)j*cap,0,(size_t)cap*sizeof(double));
    memset(b->ws+(size_t)j*cap,0,(size_t)cap*sizeof(double));
    for(int i=0;i<cap;i++)b->beta[(size_t)j*cap+i]=b->log_alpha0;
    memset(b->h_tr+(size_t)j*cap,0,(size_t)cap*sizeof(double));
    memset(b->v_n+(size_t)j*cap,0,(size_t)cap*sizeof(double));
    memset(b->energy+(size_t)j*cap,0,(size_t)cap*sizeof(double));
    memset(b->cnt+(size_t)j*cap,0,(size_t)cap*sizeof(double));
    b->mix_a[j]=0;b->mix_denom[j]=1e-2;
    b->alpha[j]=b->gate_on?0:1;
    b->g_ru[j]=0;b->g_rr[j]=0;b->g_uu[j]=0;b->dec_acc[j]=0;
    b->out[j]=0;
  }
  memset(b->prev_s,0,(size_t)cap*sizeof(double));
  memset(b->elig,0,(size_t)cap*sizeof(double));
  memset(b->low_time,0,(size_t)cap*sizeof(double));
  b->n=b->n0;b->uu0_set=0;b->t=0;b->step_i=0;b->mass=0;
  b->o_pre=0;b->o_now=0;b->o_pre_n=0;b->o_n=0;b->o_cap=1;
  b->o_pre_set=0;b->o_now_set=0;
  b->err_n=0;b->err_i=0;b->norm_n=0;
  b->last_growth=-1e9;b->dec_n=0;
  b->rb_n=0;b->rb_i=0;b->wc_n=0;b->wc_i=0;
  b->fam_step=-1;
}

/* ---- basis ---------------------------------------------------------- */
static void activate(LbBrain *b, const double *xn, double *s_out){
  int n=b->n;
  for(int i=0;i<n;i++){
    int o=i*LB_FAN;
    double z=b->bias[i]
      +b->w_in[o]*xn[b->idx[o]]+b->w_in[o+1]*xn[b->idx[o+1]]
      +b->w_in[o+2]*xn[b->idx[o+2]]+b->w_in[o+3]*xn[b->idx[o+3]];
    b->s_tmp[i]=lb_soft_tanh(z);
  }
  int active=0;
  if(b->sparsifier==0){
    double lo=0,hi=1;
    for(int it=0;it<24;it++){
      double mid=(lo+hi)/2,ssum=0;
      for(int i=0;i<n;i++){double v=b->s_tmp[i]-mid;if(v>0)ssum+=v;}
      if(ssum*0.3>mid)lo=mid;else hi=mid;
    }
    double inh=(lo+hi)/2;
    for(int i=0;i<n;i++){
      double s=b->s_tmp[i]-inh;
      if(s<0)s=0;else if(s>0)active++;
      s_out[i]=s;
    }
  }else{
    int k=b->kwta;
    double *top=b->top_buf;
    int filled=0;
    for(int i=0;i<n;i++){
      double g=b->s_tmp[i];
      if(filled<k+1){
        int q=filled++;top[q]=g;
        while(q>0&&top[q]<top[q-1]){double tp=top[q];top[q]=top[q-1];
          top[q-1]=tp;q--;}
      }else if(g>top[0]){
        top[0]=g;int q=0;
        while(q<k&&top[q]>top[q+1]){double tq=top[q];top[q]=top[q+1];
          top[q+1]=tq;q++;}
      }
    }
    double theta=dmax(0,filled>k?top[0]:0);
    for(int i=0;i<n;i++){
      double s=b->s_tmp[i]-theta;
      if(s<0)s=0;else if(s>0)active++;
      s_out[i]=s;
    }
  }
  b->active=(double)active/(double)(n>1?n:1);
}

static void forward_ro(LbBrain *b, const double *ctx, double *s_out){
  for(int c=0;c<b->n_ctx_in;c++)
    b->xn_tmp[c]=(ctx[c]-b->mu[c])/sqrt(b->varr[c]+1e-8);
  for(int o=0;o<b->n_osc;o++){
    b->xn_tmp[b->n_ctx_in+2*o]=lb_soft_sin(b->osc_ph[o]);
    b->xn_tmp[b->n_ctx_in+2*o+1]=lb_soft_cos(b->osc_ph[o]);
  }
  double save_active=b->active;
  activate(b,b->xn_tmp,s_out);
  b->active=save_active;
}

static void replay(LbBrain *b){
  if(b->rb_n<8)return;
  int n=b->n,J=b->n_out;
  double *s=b->s_tmp2;
  for(int k=0;k<4;k++){
    int pick=(int)(rng_next(&b->rng)*b->rb_n);
    int co=pick*b->n_ctx_in,to=pick*J;
    forward_ro(b,b->rb_ctx+co,s);
    double ep2=1e-8;
    for(int i=0;i<n;i++)ep2+=s[i]*s[i];
    for(int j=0;j<J;j++){
      if(!b->rb_mask[to+j])continue;
      double pred=0;
      const double *wfj=b->wf+(size_t)j*b->cap,*wsj=b->ws+(size_t)j*b->cap;
      for(int i=0;i<n;i++)pred+=(wfj[i]+wsj[i])*s[i];
      double f=0.2*(b->rb_tot[to+j]-pred)/ep2;
      double *wfw=b->wf+(size_t)j*b->cap;
      for(int i=0;i<n;i++)wfw[i]+=f*s[i];
    }
  }
}

static double fast_norm(const LbBrain *b){
  double s=0;
  for(int j=0;j<b->n_out;j++){
    const double *w=b->wf+(size_t)j*b->cap;
    for(int i=0;i<b->n;i++)s+=w[i]*w[i];
  }
  return sqrt(s);
}

static void resid_stats(const LbBrain *b,double *rms_o,double *rho_o){
  if(b->err_n<100){*rms_o=0;*rho_o=0;return;}
  int N=b->err_n,J=b->n_out;
  double ms=0;
  for(int q=0;q<N;q++)
    for(int j=0;j<J;j++){double v=b->err_hist[(size_t)q*J+j];ms+=v*v;}
  double rms=sqrt(ms/N);
  double rho_max=0;
  for(int j=0;j<J;j++){
    double m=0;
    for(int q=0;q<N;q++)m+=b->err_hist[(size_t)q*J+j];
    m/=N;
    double den=1e-12;
    for(int q=0;q<N;q++){double dd=b->err_hist[(size_t)q*J+j]-m;den+=dd*dd;}
    for(int lag=1;lag<=5;lag++){
      double num=0;
      for(int q=lag;q<N;q++)
        num+=(b->err_hist[(size_t)q*J+j]-m)*(b->err_hist[(size_t)(q-lag)*J+j]-m);
      double rr=num/den;if(rr<0)rr=-rr;
      if(rr>rho_max)rho_max=rr;
    }
  }
  *rms_o=rms;*rho_o=rho_max;
}

static void recycle(LbBrain *b,int i){
  b->low_time[i]=0;b->elig[i]=0;
  for(int j=0;j<b->n_out;j++){
    size_t o=(size_t)j*b->cap+i;
    b->wf[o]=0;b->ws[o]=0;b->beta[o]=b->log_alpha0;
    b->h_tr[o]=0;b->v_n[o]=0;b->energy[o]=0;
  }
  if(b->wc_n>0&&rng_next(&b->rng)<0.5){
    int pick=(int)(rng_next(&b->rng)*b->wc_n)*b->n_ctx;
    int o=i*LB_FAN;int used[LB_FAN];
    for(int k=0;k<LB_FAN;k++){
      int bi=0;double bv=-1;
      for(int c=0;c<b->n_ctx;c++){
        int skip=0;
        for(int u=0;u<k;u++)if(used[u]==c){skip=1;break;}
        if(skip)continue;
        double v=b->wc_ctx[pick+c];if(v<0)v=-v;
        if(v>bv){bv=v;bi=c;}
      }
      used[k]=bi;
      b->idx[o+k]=bi;
      b->w_in[o+k]=(b->wc_ctx[pick+bi]>=0?1:-1)*(0.5+0.5*rng_next(&b->rng))*0.5;
    }
    double z=0;
    for(int k=0;k<LB_FAN;k++)z+=b->w_in[o+k]*b->wc_ctx[pick+b->idx[o+k]];
    b->bias[i]=-z+0.3;
  }else seed_unit(b,i);
}

static void housekeeping(LbBrain *b,const double *tch){
  int J=b->n_out;
  for(int j=0;j<J;j++)b->dec_acc[j]+=tch[j];
  b->dec_n++;
  if(b->dec_n>=b->dec_every){
    if(b->grow_enabled){
      if(b->err_n<300){
        for(int j=0;j<J;j++)
          b->err_hist[(size_t)b->err_n*J+j]=b->dec_acc[j]/b->dec_n;
        b->err_n++;
      }else{
        memmove(b->err_hist,b->err_hist+J,(size_t)299*J*sizeof(double));
        for(int j=0;j<J;j++)
          b->err_hist[(size_t)299*J+j]=b->dec_acc[j]/b->dec_n;
      }
    }
    for(int j=0;j<J;j++)b->dec_acc[j]=0;
    b->dec_n=0;
  }
  if(b->step_i%b->check_every!=0)return;
  if(b->grow_enabled){
    if(b->norm_n<11)b->norm_hist[b->norm_n++]=fast_norm(b);
    else{
      memmove(b->norm_hist,b->norm_hist+1,10*sizeof(double));
      b->norm_hist[10]=fast_norm(b);
    }
  }
  /* prune */
  {
    double mean=0;int n=b->n;
    for(int i=0;i<n;i++){
      double w=0;
      for(int j=0;j<J;j++){
        double a=b->wf[(size_t)j*b->cap+i];if(a<0)a=-a;
        double c2=b->ws[(size_t)j*b->cap+i];if(c2<0)c2=-c2;
        w+=a+c2;
      }
      mean+=w;
    }
    mean/=n;
    if(mean>=1e-9){
      for(int i=0;i<n;i++){
        double w=0;
        for(int j=0;j<J;j++){
          double a=b->wf[(size_t)j*b->cap+i];if(a<0)a=-a;
          double c2=b->ws[(size_t)j*b->cap+i];if(c2<0)c2=-c2;
          w+=a+c2;
        }
        if(w<0.02*mean){
          b->low_time[i]+=1;
          if(b->low_time[i]>60)recycle(b,i);
        }else b->low_time[i]=0;
      }
    }
  }
  /* grow */
  if(!b->episodic&&b->n<b->cap&&b->grow_enabled){
    if(b->uu0_set||!b->gate_on){
      if(b->t-b->last_growth>=20&&b->norm_n>=11){
        double now=b->norm_hist[10],ago=b->norm_hist[0];
        double dl=now-ago;if(dl<0)dl=-dl;
        if(dl<0.01*dmax(now,1e-9)){
          double rms,rho;resid_stats(b,&rms,&rho);
          double rms0=0;
          if(b->uu0_set)for(int j=0;j<J;j++)rms0+=b->uu0[j];
          rms0=sqrt(rms0);
          if(!(rms<0.35*rms0||rho<0.15)){
            int n_new=(int)dmin(dmax((double)((int)(b->n*0.2)),4),
                                (double)(b->cap-b->n));
            b->n+=n_new;b->last_growth=b->t;
            b->kwta=(int)dmax(4,(double)jsround(b->k_frac*b->n));
          }
        }
      }
    }
  }
}

/* ---- main step ------------------------------------------------------ */
const double *lb_step(LbBrain *b,const double *ctx,const double *teacher,
                      int learn){
  int J=b->n_out,n=b->n;
  double a=b->a_norm;
  for(int o=0;o<b->n_osc;o++)
    b->osc_ph[o]=lb_wrap_pi(b->osc_ph[o]+b->osc_w[o]*b->dt);
  if(learn&&b->n_osc){
    double f=0;
    for(int j=0;j<J;j++)f+=teacher[j];
    f/=J;
    double af=f<0?-f:f;
    b->t_mag+=0.005*(af-b->t_mag);
    double F=f/(b->t_mag+1e-6);
    for(int o=0;o<b->n_osc;o++){
      double d=b->osc_k*F*lb_soft_sin(b->osc_ph[o])*b->dt;
      b->osc_ph[o]=lb_wrap_pi(b->osc_ph[o]-d);b->osc_w[o]-=d;
      double lo2=LB_TWO_PI*0.05,hi2=0.8*LB_PI/b->dt;
      if(b->osc_w[o]<lo2)b->osc_w[o]=lo2;
      if(b->osc_w[o]>hi2)b->osc_w[o]=hi2;
    }
  }
  for(int c=0;c<b->n_ctx_in;c++){
    if(learn){
      b->mu[c]+=a*(ctx[c]-b->mu[c]);
      double dv=ctx[c]-b->mu[c];
      b->varr[c]+=a*(dv*dv-b->varr[c]);
    }
    b->xn[c]=(ctx[c]-b->mu[c])/sqrt(b->varr[c]+1e-8);
  }
  for(int o=0;o<b->n_osc;o++){
    b->xn[b->n_ctx_in+2*o]=lb_soft_sin(b->osc_ph[o]);
    b->xn[b->n_ctx_in+2*o+1]=lb_soft_cos(b->osc_ph[o]);
  }
  activate(b,b->xn,b->s_act);
  for(int j=0;j<J;j++){
    double yf=0,ys=0;
    const double *wfj=b->wf+(size_t)j*b->cap,*wsj=b->ws+(size_t)j*b->cap;
    for(int i=0;i<n;i++){
      double sv=b->s_act[i];
      if(sv!=0){yf+=wfj[i]*sv;ys+=wsj[i]*sv;}
    }
    double lam=b->mix_on?1/(1+lb_soft_exp(-b->mix_a[j])):1;
    double r=b->mix_on?lam*yf+(1-lam)*ys:yf+ys;
    if(r>b->clamp)r=b->clamp;else if(r<-b->clamp)r=-b->clamp;
    b->raw[j]=r;
    b->lam_j[j]=lam;b->diff_j[j]=yf-ys;
    if(b->tau_out>b->dt)b->out[j]+=(r-b->out[j])*(b->dt/b->tau_out);
    else b->out[j]=r;
    if(learn&&b->mix_on){
      double diff=yf-ys;
      b->mix_denom[j]+=0.01*(diff*diff-b->mix_denom[j]);
      b->mix_a[j]+=0.5*teacher[j]*diff*lam*(1-lam)/(b->mix_denom[j]+1e-6);
      if(b->mix_a[j]>4)b->mix_a[j]=4;
      if(b->mix_a[j]<-4)b->mix_a[j]=-4;
    }
  }
  if(!b->gate_on)for(int j=0;j<J;j++)b->alpha[j]=1;
  if(learn&&b->gate_on){
    double ga=dmin(0.15,b->dt/b->gate_tau);
    for(int j=0;j<J;j++){
      b->g_ru[j]+=ga*(b->raw[j]*teacher[j]-b->g_ru[j]);
      b->g_rr[j]+=ga*(b->raw[j]*b->raw[j]-b->g_rr[j]);
      b->g_uu[j]+=ga*(teacher[j]*teacher[j]-b->g_uu[j]);
    }
    if(!b->uu0_set&&b->t>2*b->gate_tau){
      for(int j=0;j<J;j++)b->uu0[j]=b->g_uu[j];
      b->uu0_set=1;
    }
    for(int j=0;j<J;j++){
      double rho=b->g_ru[j]/sqrt(b->g_rr[j]*b->g_uu[j]+1e-12);
      int harm=b->uu0_set&&b->g_uu[j]>0.7*b->uu0[j]&&rho<0.3;
      if(harm)b->alpha[j]*=dmax(0,1-b->dt/2);
      else{
        double up=(rho-0.15)/0.45;
        if(up<0)up=0;else if(up>1)up=1;
        b->alpha[j]=dmin(1,b->alpha[j]+(b->dt/8)*up);
      }
    }
  }
  if(learn){
    double td=b->td;
    for(int i=0;i<n;i++)b->elig[i]=b->elig[i]*td+b->s_act[i];
    int bad=0;
    for(int j=0;j<J;j++){
      double dj=teacher[j];
      if(dj==0)continue;
      double djF=dj,djS=dj;
      if(b->mix_on&&b->mix_pc){
        djF=dj-(1-b->lam_j[j])*b->diff_j[j];
        djS=dj+b->lam_j[j]*b->diff_j[j];
      }
      double *wf2=b->wf+(size_t)j*b->cap,*ws2=b->ws+(size_t)j*b->cap;
      double *bj=b->beta+(size_t)j*b->cap,*hj=b->h_tr+(size_t)j*b->cap;
      double *vj=b->v_n+(size_t)j*b->cap,*Ej=b->energy+(size_t)j*b->cap;
      double *cj=b->cnt+(size_t)j*b->cap;
      double ep=1e-8;
      for(int i=0;i<n;i++){double x0=b->elig[i];if(x0!=0)ep+=x0*x0;}
      double scale=1,nlms_step=0;
      if(b->rule_nlms){
        if(b->fam_step!=b->step_i){
          double dot=0,na=1e-12,nb=1e-12;
          for(int i=0;i<n;i++){
            double av=b->s_act[i],bv=b->prev_s[i];
            dot+=av*bv;na+=av*av;nb+=bv*bv;
          }
          double fm=dot/sqrt(na*nb);
          if(fm<0)fm=0;else if(fm>1)fm=1;
          b->fam=fm;
          memcpy(b->prev_s,b->s_act,(size_t)n*sizeof(double));
          b->fam_step=b->step_i;
        }
        nlms_step=(0.2+0.8*b->fam)*b->mu_f/ep;
      }else{
        double M=0;
        for(int i=0;i<n;i++){
          double x=b->elig[i];if(x==0)continue;
          double dxh=djF*x*hj[i],adx=dxh<0?-dxh:dxh;
          double al0=lb_soft_exp(bj[i]);
          double vv=vj[i];
          double vn=vv+(1/b->meta_tau)*al0*x*x*(adx-vv);
          vj[i]=adx>vn?adx:vn;
          if(vj[i]>1e-12){
            bj[i]+=b->meta_theta*dxh/vj[i];
            if(bj[i]>1)bj[i]=1;else if(bj[i]<-12)bj[i]=-12;
          }
          M+=lb_soft_exp(bj[i])*x*x;
        }
        scale=M>1?1/M:1;
      }
      for(int i=0;i<n;i++){
        double x2=b->elig[i];if(x2==0)continue;
        double al;
        if(b->rule_nlms)al=nlms_step;
        else{
          al=lb_soft_exp(bj[i])*scale;
          double hd=1-al*x2*x2;if(hd<0)hd=0;
          hj[i]=hj[i]*hd+al*djF*x2;
        }
        wf2[i]+=al*djF*x2;
        ws2[i]+=(b->rule_nlms?(0.2+0.8*b->fam):1)*(b->mu_s/ep)*djS*x2;
        double s2=b->s_act[i]*b->s_act[i];
        if(s2>0){Ej[i]=dmin(Ej[i]+s2,1e4);cj[i]+=s2;}
        if(wf2[i]>b->wbox)wf2[i]=b->wbox;
        else if(wf2[i]<-b->wbox)wf2[i]=-b->wbox;
        if(ws2[i]>b->wbox)ws2[i]=b->wbox;
        else if(ws2[i]<-b->wbox)ws2[i]=-b->wbox;
        if(!isfinite(wf2[i])||!isfinite(ws2[i]))bad=1;
      }
      b->mass+=1;
    }
    double lk=b->leak_f;
    if(lk<1)for(int j=0;j<J;j++){
      double *wl=b->wf+(size_t)j*b->cap;
      for(int i=0;i<n;i++)wl[i]*=lk;
    }
    if(bad){lb_reset(b);b->nan_events++;}
    double dmx=0;
    for(int j=0;j<J;j++){
      double ab=teacher[j]<0?-teacher[j]:teacher[j];
      if(ab>dmx)dmx=ab;
    }
    if(dmx>3*b->t_mag&&dmx>1e-3){
      int wo=b->wc_i*b->n_ctx;
      for(int c=0;c<b->n_ctx;c++)b->wc_ctx[wo+c]=b->xn[c];
      b->wc_i=(b->wc_i+1)%8;
      if(b->wc_n<8)b->wc_n++;
    }
    if(b->rb_cap){
      int co=b->rb_i*b->n_ctx_in,to=b->rb_i*J;
      for(int c=0;c<b->n_ctx_in;c++)b->rb_ctx[co+c]=ctx[c];
      for(int j=0;j<J;j++){
        b->rb_tot[to+j]=b->alpha[j]*b->out[j]+teacher[j];
        b->rb_mask[to+j]=teacher[j]!=0?1:0;
      }
      b->rb_i=(b->rb_i+1)%b->rb_cap;
      if(b->rb_n<b->rb_cap)b->rb_n++;
      replay(b);
    }
    housekeeping(b,teacher);
  }
  b->t+=b->dt;b->step_i++;
  if(b->o_cap<1)for(int j=0;j<J;j++)
    if(b->alpha[j]>b->o_cap)b->alpha[j]=b->o_cap;
  for(int j=0;j<J;j++)b->corr[j]=b->alpha[j]*b->out[j];
  return b->corr;
}

const double *lb_read(LbBrain *b,const double *ctx){
  double z[16]={0};
  return lb_step(b,ctx,z,0);
}

double lb_mean_alpha(const LbBrain *b){
  double s=0;
  for(int j=0;j<b->n_out;j++)s+=b->alpha[j];
  return s/b->n_out;
}

void lb_outcome(LbBrain *b,double c){
  if(!(c>=0)||!isfinite(c))return;
  if(b->o_pre_n<8||lb_mean_alpha(b)<0.5){
    b->o_pre_n++;
    b->o_pre=!b->o_pre_set?c
      :b->o_pre+(c-b->o_pre)/dmin(b->o_pre_n,8);
    b->o_pre_set=1;
    if(lb_mean_alpha(b)<0.5)return;
  }
  b->o_now=!b->o_now_set?c:b->o_now+0.1*(c-b->o_now);
  b->o_now_set=1;
  b->o_n++;
  double anchor=b->o_pre>1e-9?b->o_pre:1e-9;
  if(b->o_n>=4&&b->o_now>b->harm_ratio*anchor){
    b->o_cap=dmax(0.05,b->o_cap*0.7);
  }else if(b->o_now<1.15*anchor){
    b->o_cap=dmin(1,b->o_cap*1.03+0.005);
  }
}

LbHealth lb_health(const LbBrain *b){
  LbHealth h;int clamp=0,tot=0;double sn=0;
  for(int j=0;j<b->n_out;j++){
    const double *wf=b->wf+(size_t)j*b->cap,*ws=b->ws+(size_t)j*b->cap;
    for(int i=0;i<b->n;i++){
      tot++;sn+=ws[i]*ws[i];
      double a=wf[i]<0?-wf[i]:wf[i],c=ws[i]<0?-ws[i]:ws[i];
      if(a>=0.98*b->wbox||c>=0.98*b->wbox)clamp++;
    }
  }
  h.wf=fast_norm(b);h.ws=sqrt(sn);
  h.clamp_frac=(double)clamp/(double)(tot>1?tot:1);
  h.o_cap=b->o_cap;
  return h;
}

void lb_merge_from(LbBrain *b,const LbBrain *other){
  int n=b->n<other->n?b->n:other->n;
  for(int j=0;j<b->n_out;j++){
    double *wa=b->ws+(size_t)j*b->cap,*Ea=b->energy+(size_t)j*b->cap;
    const double *wb=other->ws+(size_t)j*other->cap,
                 *Eb=other->energy+(size_t)j*other->cap;
    for(int i=0;i<n;i++){
      double tot=Ea[i]+Eb[i];
      if(tot>1e-9)wa[i]=(Ea[i]*wa[i]+Eb[i]*wb[i])/tot;
      Ea[i]=dmax(Ea[i],Eb[i]);
    }
  }
}

/* ---- persistence (LB02, little-endian, JS-compatible) --------------- */
static void put_u16(uint8_t *p,uint16_t v){p[0]=v&0xFF;p[1]=v>>8;}
static void put_u32(uint8_t *p,uint32_t v){
  p[0]=v&0xFF;p[1]=(v>>8)&0xFF;p[2]=(v>>16)&0xFF;p[3]=(v>>24)&0xFF;}
static void put_f64(uint8_t *p,double v){memcpy(p,&v,8);}
static void put_f32(uint8_t *p,float v){memcpy(p,&v,4);}
static uint16_t get_u16(const uint8_t *p){return (uint16_t)(p[0]|(p[1]<<8));}
static uint32_t get_u32(const uint8_t *p){
  return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)
        |((uint32_t)p[3]<<24);}
static double get_f64(const uint8_t *p){double v;memcpy(&v,p,8);return v;}
static float get_f32(const uint8_t *p){float v;memcpy(&v,p,4);return v;}

size_t lb_save_size(const LbBrain *b,int include_fast){
  int J=b->n_out,n=b->n,nc=b->n_ctx;
  return 4+1+1+2+2+1+1+4+8+8+(size_t)nc*8*2+(size_t)J*n*8+(size_t)J*n*4
       +(include_fast?(size_t)J*n*8:0)+4;
}
size_t lb_save(const LbBrain *b,uint8_t *buf,size_t buf_len,int include_fast){
  size_t need=lb_save_size(b,include_fast);
  if(buf_len<need)return 0;
  int J=b->n_out,n=b->n,nc=b->n_ctx;
  size_t p=0;
  buf[p++]=0x4C;buf[p++]=0x42;buf[p++]=0x30;buf[p++]=0x32;
  buf[p++]=2;buf[p++]=include_fast?1:0;
  put_u16(buf+p,(uint16_t)n);p+=2;
  put_u16(buf+p,(uint16_t)b->n_ctx_in);p+=2;
  buf[p++]=(uint8_t)J;buf[p++]=LB_FAN;
  put_u32(buf+p,(uint32_t)b->seed);p+=4;
  put_f64(buf+p,b->dt);p+=8;
  put_f64(buf+p,b->mass);p+=8;
  for(int i=0;i<nc;i++){put_f64(buf+p,b->mu[i]);p+=8;}
  for(int i=0;i<nc;i++){put_f64(buf+p,b->varr[i]);p+=8;}
  for(int j=0;j<J;j++)for(int i=0;i<n;i++){
    put_f64(buf+p,b->ws[(size_t)j*b->cap+i]);p+=8;}
  for(int j=0;j<J;j++)for(int i=0;i<n;i++){
    put_f32(buf+p,(float)b->energy[(size_t)j*b->cap+i]);p+=4;}
  if(include_fast)
    for(int j=0;j<J;j++)for(int i=0;i<n;i++){
      put_f64(buf+p,b->wf[(size_t)j*b->cap+i]);p+=8;}
  put_u32(buf+p,lb_crc32(buf,p));p+=4;
  return p;
}
int lb_load(LbBrain *b,const uint8_t *buf,size_t len){
  if(len<40||buf[0]!=0x4C||buf[1]!=0x42||buf[2]!=0x30||buf[3]!=0x32)
    return -1;
  size_t p=4;
  int profile=buf[p++],flags=buf[p++];
  if(profile!=2)return -2;
  int n=get_u16(buf+p);p+=2;
  int nci=get_u16(buf+p);p+=2;
  int J=buf[p++],fan=buf[p++];
  int32_t seed=(int32_t)get_u32(buf+p);p+=4;
  p+=8; /* dt (informational) */
  if(n>b->cap||nci!=b->n_ctx_in||J!=b->n_out||fan!=LB_FAN)return -3;
  if(seed!=b->seed)return -4;
  size_t expect=4+1+1+2+2+1+1+4+8+8+(size_t)b->n_ctx*8*2
        +(size_t)J*n*8+(size_t)J*n*4+((flags&1)?(size_t)J*n*8:0)+4;
  if(len!=expect)return -5;
  if(get_u32(buf+len-4)!=lb_crc32(buf,len-4))return -6;
  b->mass=get_f64(buf+p);p+=8;
  int nc=b->n_ctx;
  for(int i=0;i<nc;i++){b->mu[i]=get_f64(buf+p);p+=8;}
  for(int i=0;i<nc;i++){b->varr[i]=get_f64(buf+p);p+=8;}
  b->n=n;
  for(int j=0;j<J;j++)for(int i=0;i<n;i++){
    b->ws[(size_t)j*b->cap+i]=get_f64(buf+p);p+=8;}
  for(int j=0;j<J;j++)for(int i=0;i<n;i++){
    b->energy[(size_t)j*b->cap+i]=get_f32(buf+p);p+=4;}
  if(flags&1)
    for(int j=0;j<J;j++)for(int i=0;i<n;i++){
      b->wf[(size_t)j*b->cap+i]=get_f64(buf+p);p+=8;}
  return 0;
}
