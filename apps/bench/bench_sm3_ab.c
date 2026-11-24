/* Same-image A/B: compare previous MCU SM3 with the current MCU kernel.
 * Board: read sm3_ab_results over SWD. Host: exhaustive length/split checks.
 * The baseline functions are renamed at compilation, never runtime-switched. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "smc/sm3.h"
#include "smc/hmac_sm3.h"
#ifndef SM3_AB_BOARD
#include <stdio.h>
#endif

void before_sm3(const uint8_t *,size_t,uint8_t *);
void before_sm3_init(sm3_ctx *);
void before_sm3_update(sm3_ctx *,const uint8_t *,size_t);
void before_sm3_final(sm3_ctx *,uint8_t *);
void before_hmac_sm3(const uint8_t *,size_t,const uint8_t *,size_t,uint8_t *);

#define COUNT 10
#define ROUNDS 7
#define REPEATS 8
static const uint32_t lengths[COUNT]={0,28,55,56,64,65,256,1024,4096,256};
static uint8_t input[4097],before[32],after[32];
volatile struct {
    uint32_t magic,state,failures,checks,count,rounds;
    uint32_t before_cycles[COUNT],after_cycles[COUNT];
} sm3_ab_results;

static void check(int condition)
{
    sm3_ab_results.checks++;
    if(!condition) sm3_ab_results.failures++;
}

static void compare(size_t length)
{
    sm3_ctx ctx;
    size_t offset,chunk;
    before_sm3(input,length,before);
    sm3(input,length,after);
    check(memcmp(before,after,32)==0);
    sm3_init(&ctx);
    for(offset=0;offset<length;offset+=chunk) {
        chunk=(offset%67)+1;
        if(chunk>length-offset) chunk=length-offset;
        sm3_update(&ctx,input+offset,chunk);
    }
    sm3_update(&ctx,input,0);
    sm3_final(&ctx,after);
    check(memcmp(before,after,32)==0);
}

static void operation(int old,unsigned index)
{
    if(index==COUNT-1) {
        if(old) before_hmac_sm3(input,32,input,256,before);
        else hmac_sm3(input,32,input,256,after);
    } else {
        if(old) before_sm3(input,lengths[index],before);
        else sm3(input,lengths[index],after);
    }
}

#ifdef SM3_AB_BOARD
#define REG32(addr) (*(volatile uint32_t *)(addr))
static uint32_t measure(int old,unsigned index)
{
    uint32_t start,elapsed;
    unsigned i;
    start=REG32(0xe0001004u);
    for(i=0;i<REPEATS;i++) operation(old,index);
    elapsed=REG32(0xe0001004u)-start;
    return elapsed/REPEATS;
}
#endif

int main(void)
{
    unsigned i,j;
    uint32_t random=0x31592653u;
    sm3_ab_results.magic=0x534d3341u;
    sm3_ab_results.state=1;
    sm3_ab_results.count=COUNT;
    sm3_ab_results.rounds=ROUNDS;
#ifdef SM3_AB_BOARD
    REG32(0xe000edfcu)|=1u<<24;
    REG32(0xe0001004u)=0;
    REG32(0xe0001000u)|=1u;
#endif
    for(i=0;i<sizeof input;i++) {
        random=random*1664525u+1013904223u;
        input[i]=(uint8_t)(random>>24);
    }
    for(i=0;i<258;i++) compare(i);
    for(i=1023;i<=1025;i++) compare(i);
    for(i=4095;i<=4097;i++) compare(i);
#ifndef SM3_AB_BOARD
    for(i=258;i<4095;i++) compare(i);
    /* Every possible split around padding and block boundaries. */
    for(i=0;i<=129;i++) {
        size_t split;
        before_sm3(input,i,before);
        for(split=0;split<=i;split++) {
            sm3_ctx ctx;
            sm3_init(&ctx);
            sm3_update(&ctx,input,split);
            sm3_update(&ctx,input+split,i-split);
            sm3_final(&ctx,after);
            check(memcmp(before,after,32)==0);
        }
    }
    /* Unaligned inputs and HMAC keys crossing the 64-byte key block. */
    for(i=0;i<=129;i++) {
        unsigned offset;
        static const size_t key_lengths[]={0,1,32,63,64,65,128};
        for(offset=1;offset<=3;offset++) {
            before_sm3(input+offset,i,before);
            sm3(input+offset,i,after);
            check(memcmp(before,after,32)==0);
        }
        for(offset=0;offset<sizeof key_lengths/sizeof key_lengths[0];offset++) {
            before_hmac_sm3(input,key_lengths[offset],input,i,before);
            hmac_sm3(input,key_lengths[offset],input,i,after);
            check(memcmp(before,after,32)==0);
        }
    }
#endif
    for(i=0;i<COUNT;i++) {
        operation(1,i);operation(0,i);
        check(memcmp(before,after,32)==0);
#ifdef SM3_AB_BOARD
        sm3_ab_results.before_cycles[i]=UINT32_MAX;
        sm3_ab_results.after_cycles[i]=UINT32_MAX;
        for(j=0;j<ROUNDS;j++) {
            uint32_t old,newer;
            /* Alternate which kernel goes first, in the same firmware image. */
            if(j&1) {newer=measure(0,i);old=measure(1,i);}
            else {old=measure(1,i);newer=measure(0,i);}
            if(old<sm3_ab_results.before_cycles[i]) sm3_ab_results.before_cycles[i]=old;
            if(newer<sm3_ab_results.after_cycles[i]) sm3_ab_results.after_cycles[i]=newer;
        }
#endif
    }
    (void)j;
    sm3_ab_results.state=2;
#ifdef SM3_AB_BOARD
    for(;;) { }
#else
    printf("SM3 A/B correctness: %u checks, %u failures\n",sm3_ab_results.checks,sm3_ab_results.failures);
    return sm3_ab_results.failures?1:0;
#endif
}
