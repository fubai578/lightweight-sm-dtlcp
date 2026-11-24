/* Same-image SM2 square A/B. Baseline entry points are renamed at build time. */
#include <stdint.h>
#include <string.h>
#include "sm2_internal.h"
#include "smc/sm2.h"
#ifndef SQR_BOARD
#include <stdio.h>
#endif
void before_sm2_fp_sqr(sm2_fe *,const sm2_fe *);
void before_sm2_fp_inv(sm2_fe *,const sm2_fe *);
int before_sm2_pub_from_priv(uint8_t *,const uint8_t *);
int before_sm2_sign(uint8_t *,const uint8_t *,const uint8_t *,const uint8_t *);
int before_sm2_verify(const uint8_t *,const uint8_t *,const uint8_t *);
int before_sm2_ecdh(uint8_t *,const uint8_t *,const uint8_t *);
volatile uint32_t square_results[18]; /* magic,state,fail,checks,old[7],new[7] */
static uint8_t priv[32]={0},pub[65],dgst[32]={1},sig[64],out[65];
static sm2_fe input={{0xFFFFFFFF,0x12345678,0x9abcdef0,0xFFFFFFFF,2,3,4,5}},output;
static void check(int yes) {square_results[3]++;if(!yes)square_results[2]++;}
static void operation(int old,unsigned i)
{
    int rc=0;
    switch(i) {
    case 0:if(old)before_sm2_fp_sqr(&output,&input);else sm2_fp_sqr(&output,&input);break;
    case 1:if(old)before_sm2_fp_inv(&output,&input);else sm2_fp_inv(&output,&input);break;
    case 2:rc=old?before_sm2_pub_from_priv(out,priv):sm2_pub_from_priv(out,priv);break;
    case 3:rc=old?before_sm2_sign(out,dgst,priv,0):sm2_sign(out,dgst,priv,0);break;
    case 4:rc=old?before_sm2_verify(sig,dgst,pub):sm2_verify(sig,dgst,pub);break;
    case 5:rc=old?before_sm2_ecdh(out,priv,pub):sm2_ecdh(out,priv,pub);break;
    case 6:sm2_fp_mul(&output,&input,&input);break;
    }
    if(rc) square_results[2]++;
}
#ifdef SQR_BOARD
#define REG(a) (*(volatile uint32_t *)(a))
static uint32_t measure(int old,unsigned i)
{
    unsigned j,n=i==0||i==6?64:1;
    uint32_t start=REG(0xe0001004u),elapsed;
    for(j=0;j<n;j++)operation(old,i);
    elapsed=REG(0xe0001004u)-start;
    return elapsed/n;
}
#endif
int main(void)
{
    unsigned i,j;
    uint32_t rng=0x1345779;
    square_results[0]=0x53515231;square_results[1]=1;
#ifdef SQR_BOARD
    REG(0xe000edfcu)|=1u<<24;REG(0xe0001004u)=0;REG(0xe0001000u)|=1;
#endif
    for(i=0;i<1024;i++) {
        sm2_fe x,y,z;
        for(j=0;j<8;j++){rng=rng*1664525u+1013904223u;x.w[j]=rng;}
        if(i<256)for(j=0;j<8;j++)x.w[j]=(i&(1u<<j))?UINT32_MAX:0;
        before_sm2_fp_sqr(&y,&x);sm2_fp_sqr(&z,&x);check(memcmp(&y,&z,sizeof y)==0);
        z=x;sm2_fp_sqr(&z,&z);check(memcmp(&y,&z,sizeof y)==0);
    }
    for(i=0;i<32;i++)priv[i]=(uint8_t)(i+7);
    check(sm2_pub_from_priv(pub,priv)==0);check(sm2_sign(sig,dgst,priv,0)==0);
    for(i=0;i<7;i++) {
        uint8_t copy[65];sm2_fe y;
        operation(1,i);memcpy(copy,out,sizeof out);y=output;
        operation(0,i);check(i==0||i==1||i==6?memcmp(&y,&output,sizeof y)==0:memcmp(copy,out,sizeof out)==0);
#ifdef SQR_BOARD
        square_results[4+i]=UINT32_MAX;square_results[11+i]=UINT32_MAX;
        for(j=0;j<5;j++) {
            uint32_t a,b;
            if(j&1){b=measure(0,i);a=measure(1,i);}else{a=measure(1,i);b=measure(0,i);}
            if(a<square_results[4+i])square_results[4+i]=a;
            if(b<square_results[11+i])square_results[11+i]=b;
        }
#endif
    }
    square_results[1]=2;
#ifdef SQR_BOARD
    for(;;){}
#else
    printf("square A/B: %u checks, %u failures\n",square_results[3],square_results[2]);
    return square_results[2]?1:0;
#endif
}
