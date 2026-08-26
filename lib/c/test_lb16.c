/* Desktop functional test for the integer-only lb16 build: the same
 * closed FEL loop as the golden vectors — first-order plant, P host,
 * context-locked disturbance. Verifies: (1) the brain cuts tracking
 * error vs the plain loop, (2) the gate opens, (3) save/load restores
 * the slow layer, (4) the outcome watchdog caps a harmful brain.
 * Build: gcc -O2 -std=c99 lb16.c test_lb16.c -o test_lb16 -lm          */
#include "lb16.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static double run_loop(Lb16 *b, int use_brain, double *alpha_out){
  double y=0,err_acc=0;int cnt=0;
  int16_t ctx[LB16_NCTX];
  memset(ctx,0,sizeof ctx);
  for(int i=0;i<15000;i++){
    double ph=sin(i*0.02*1.3),ph2=cos(i*0.02*1.3);
    double ref=0.5*ph;
    double dist=1.6*ph2+0.8;
    double upid=3.0*(ref-y);
    double u=upid;
    if(b){
      /* caller scales sensors to Q15 once — the documented contract */
      ctx[0]=(int16_t)(ref*32760);
      ctx[1]=(int16_t)(y*16384);
      ctx[2]=(int16_t)(ph2*16384);
      int16_t t_q12=(int16_t)(upid*4096.0);       /* plant units, Q12  */
      lb16_step(b,ctx,t_q12,use_brain);
      u+=(double)b->corr_q12/4096.0;
    }
    y+=0.02*4.0*(u+dist-y);
    if(i>12000){double e=ref-y;err_acc+=e*e;cnt++;}
  }
  if(alpha_out&&b)*alpha_out=b->alpha_q15/32767.0;
  return sqrt(err_acc/cnt);
}

int main(void){
  int fails=0;
  printf("sizeof(Lb16) = %u bytes (N=%d, NCTX=%d)\n",
    (unsigned)sizeof(Lb16),LB16_N,LB16_NCTX);

  double rms_plain=run_loop(NULL,0,NULL);
  Lb16 b;lb16_init(&b,3,20,16384);      /* dt 20ms, authority 4.0 Q12 */
  double alpha=0;
  double rms_brain=run_loop(&b,1,&alpha);
  printf("plain rms %.4f | brain rms %.4f | alpha %.2f\n",
    rms_plain,rms_brain,alpha);
  if(!(rms_brain<0.6*rms_plain)){printf("FAIL: brain not helping\n");fails++;}
  if(!(alpha>0.9)){printf("FAIL: gate did not open\n");fails++;}

  /* save/load: slow layer survives */
  uint8_t blob[6+LB16_N*4+2];
  uint16_t bl=lb16_save(&b,blob,sizeof blob);
  Lb16 b2;lb16_init(&b2,3,20,4096);
  int rc=lb16_load(&b2,blob,bl);
  int okrt=rc==0;
  for(int i=0;i<LB16_N&&okrt;i++)if(b2.ws[i]!=b.ws[i])okrt=0;
  printf("save/load: %s (%u bytes, rc=%d)\n",okrt?"ok":"FAIL",bl,rc);
  if(!okrt)fails++;

  /* corrupt blob rejected */
  blob[8]^=0xFF;
  if(lb16_load(&b2,blob,bl)==0){printf("FAIL: corrupt blob accepted\n");fails++;}
  else printf("corrupt blob rejected: ok\n");

  /* outcome watchdog: rising cost caps authority */
  Lb16 b3;lb16_init(&b3,3,20,4096);
  b3.alpha_q15=32767;                    /* pretend gate is open */
  for(int i=0;i<8;i++)lb16_outcome(&b3,100);
  for(int i=0;i<40;i++)lb16_outcome(&b3,300);
  printf("watchdog cap after 3x cost: %.2f\n",b3.o_cap_q15/32767.0);
  if(!(b3.o_cap_q15<16384)){printf("FAIL: watchdog did not cap\n");fails++;}

  printf(fails?"LB16: %d FAILURES\n":"LB16: all ok\n",fails);
  return fails?1:0;
}
