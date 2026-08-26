/* Golden-vector test: replays the exact drivers of tools/gen_vectors.js
 * through the C core and demands BIT-identical outputs and weights.
 * Build:  gcc -O2 -std=c99 -ffp-contract=off lb.c test_vectors.c -o test_vectors
 * Run:    ./test_vectors vectors.bin        (exit 0 = bit-identical)   */
#include "lb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* JS `(x % TWO_PI) - PI` for x>=0: fmod is an exact operation, so this
   is bit-identical to the generator's expression. */
static double lb_wrap_pi_mod(double x){
  return fmod(x,6.283185307179586)-3.141592653589793;
}

static uint8_t *slurp(const char *path, size_t *len){
  FILE *f=fopen(path,"rb");
  if(!f)return NULL;
  fseek(f,0,SEEK_END);long sz=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t *b=(uint8_t*)malloc((size_t)sz);
  if(fread(b,1,(size_t)sz,f)!=(size_t)sz){fclose(f);free(b);return NULL;}
  fclose(f);*len=(size_t)sz;return b;
}
static uint32_t rd_u32(const uint8_t **p){
  uint32_t v=(uint32_t)(*p)[0]|((uint32_t)(*p)[1]<<8)
    |((uint32_t)(*p)[2]<<16)|((uint32_t)(*p)[3]<<24);
  *p+=4;return v;
}
static double rd_f64(const uint8_t **p){
  double v;memcpy(&v,*p,8);*p+=8;return v;
}
static int bits_equal(double a,double b){
  uint64_t ua,ub;memcpy(&ua,&a,8);memcpy(&ub,&b,8);
  return ua==ub;
}
static long mismatches=0;
static void chk(double got,double want,const char *what,long i){
  if(!bits_equal(got,want)){
    if(mismatches<8)
      printf("  MISMATCH %s[%ld]: got %.17g want %.17g\n",what,i,got,want);
    mismatches++;
  }
}
static void verify_tail(const uint8_t **p,LbBrain *b,uint32_t J,uint32_t n){
  for(uint32_t j=0;j<J;j++)for(uint32_t i=0;i<n;i++)
    chk(b->wf[(size_t)j*b->cap+i],rd_f64(p),"wf",(long)(j*n+i));
  for(uint32_t j=0;j<J;j++)for(uint32_t i=0;i<n;i++)
    chk(b->ws[(size_t)j*b->cap+i],rd_f64(p),"ws",(long)(j*n+i));
}

int main(int argc,char **argv){
  const char *path=argc>1?argv[1]:"vectors.bin";
  size_t len;uint8_t *data=slurp(path,&len);
  if(!data){printf("cannot read %s\n",path);return 2;}
  const uint8_t *p=data;
  if(rd_u32(&p)!=0x4C425456u){printf("bad magic\n");return 2;}
  uint32_t nconf=rd_u32(&p);
  if(nconf!=3){printf("expected 3 configs, got %u\n",nconf);return 2;}
  static uint8_t arena[512*1024];

  /* ---- config A: closed FEL loop ---- */
  {
    uint32_t steps=rd_u32(&p),J=rd_u32(&p),n_final=rd_u32(&p);
    LbConfig c={.n_ctx=3,.n_out=1,.dt=0.02,.authority=4,.n=16,.cap=32,
                .seed=3};
    LbBrain b;
    if(lb_init(&b,&c,arena,sizeof arena)){printf("initA failed\n");return 2;}
    double ctx[3];double y=0;
    for(uint32_t i=0;i<steps;i++){
      double ph=lb_soft_sin(lb_wrap_pi_mod(i*0.02*1.3));
      double ph2=lb_soft_cos(lb_wrap_pi_mod(i*0.02*1.3));
      double ref=0.5*ph;
      double dist=1.6*ph2+0.8;
      double upid=3.0*(ref-y);
      ctx[0]=ref;ctx[1]=y;ctx[2]=ph2;
      const double *corr=lb_step(&b,ctx,&upid,1);
      chk(corr[0],rd_f64(&p),"A.corr",(long)i);
      double u=upid+corr[0];
      y+=0.02*4.0*(u+dist-y);
    }
    if((uint32_t)b.n!=n_final){printf("A: n=%d want %u\n",b.n,n_final);mismatches++;}
    verify_tail(&p,&b,J,n_final);
    printf("config A: %s (final n=%d)\n",mismatches?"FAIL":"bit-identical",b.n);
  }
  long after_a=mismatches;

  /* ---- config B: episodic + replay + outcome ---- */
  {
    uint32_t steps=rd_u32(&p),J=rd_u32(&p),n_final=rd_u32(&p);
    LbConfig c={.n_ctx=4,.n_out=2,.dt=0.5,.authority=2,.n=24,.seed=7};
    LbBrain b;
    if(lb_init(&b,&c,arena,sizeof arena)){printf("initB failed\n");return 2;}
    double ctx[4],tch[2];
    for(uint32_t i=0;i<steps;i++){
      for(int q=0;q<4;q++)
        ctx[q]=lb_soft_sin(lb_wrap_pi_mod(i*(0.37+0.11*q)));
      tch[0]=0.5*ctx[0]*ctx[1]-b.out[0];
      tch[1]=(i%3==0)?0:0.3*ctx[2]-b.out[1];
      const double *corr=lb_step(&b,ctx,tch,1);
      chk(corr[0],rd_f64(&p),"B.corr0",(long)i);
      chk(corr[1],rd_f64(&p),"B.corr1",(long)i);
      if(i%10==9){
        double c0=tch[0]<0?-tch[0]:tch[0];
        lb_outcome(&b,c0);
      }
    }
    verify_tail(&p,&b,J,n_final);
    printf("config B: %s\n",mismatches>after_a?"FAIL":"bit-identical");
  }
  long after_b=mismatches;

  /* ---- config C: kwta + nlms continuous ---- */
  {
    uint32_t steps=rd_u32(&p),J=rd_u32(&p),n_final=rd_u32(&p);
    LbConfig c={.n_ctx=5,.n_out=1,.dt=0.01,.authority=1,.n=32,.seed=11,
                .sparsifier=1,.osc=-1,.rule=1};
    LbBrain b;
    if(lb_init(&b,&c,arena,sizeof arena)){printf("initC failed\n");return 2;}
    double ctx[5];
    for(uint32_t i=0;i<steps;i++){
      for(int q=0;q<5;q++)
        ctx[q]=lb_soft_sin(lb_wrap_pi_mod(i*0.01*(1+0.7*q)));
      double t=0.4*ctx[0]+0.2*ctx[3]-b.out[0];
      const double *corr=lb_step(&b,ctx,&t,1);
      chk(corr[0],rd_f64(&p),"C.corr",(long)i);
    }
    verify_tail(&p,&b,J,n_final);
    printf("config C: %s\n",mismatches>after_b?"FAIL":"bit-identical");
  }

  /* save/load round-trip sanity in C */
  {
    LbConfig c={.n_ctx=3,.n_out=1,.dt=0.02,.authority=2,.n=16,.seed=5};
    LbBrain b1,b2;
    static uint8_t a1[64*1024],a2[64*1024];
    lb_init(&b1,&c,a1,sizeof a1);
    double ctx[3];
    for(int i=0;i<500;i++){
      ctx[0]=lb_soft_sin(lb_wrap_pi_mod(i*0.03));
      ctx[1]=lb_soft_cos(lb_wrap_pi_mod(i*0.05));ctx[2]=0.5;
      double t=0.7*ctx[0];
      lb_step(&b1,ctx,&t,1);
    }
    uint8_t blob[8192];
    size_t bl=lb_save(&b1,blob,sizeof blob,1);
    lb_init(&b2,&c,a2,sizeof a2);
    int rc=lb_load(&b2,blob,bl);
    int okrt=rc==0;
    if(okrt)for(int i=0;i<16;i++)
      if(!bits_equal(b1.ws[i],b2.ws[i])||!bits_equal(b1.wf[i],b2.wf[i]))okrt=0;
    printf("save/load C round-trip: %s (%u bytes, rc=%d)\n",
      okrt?"ok":"FAIL",(unsigned)bl,rc);
    if(!okrt)mismatches++;
  }

  free(data);
  printf(mismatches?"TOTAL MISMATCHES: %ld\n":"ALL BIT-IDENTICAL\n",mismatches);
  return mismatches?1:0;
}
