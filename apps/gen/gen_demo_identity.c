/* Public test identity only: never use this fixed seed in production. */
#include <stdio.h>
#include <string.h>
#include "dtlcp_internal.h"
#include "smc/sm2.h"
#if defined(SMC_PRODUCTION_BUILD)
#error "Fixed demo identity is forbidden in production"
#endif
static void array(const char *field,const uint8_t *p,size_t n)
{
    size_t i;
    printf(".%s={",field);
    for(i=0;i<n;i++) printf("%s0x%02x",i?",":"",p[i]);
    puts("},");
}
static void cert(const char *name,const dtlcp_cert *c)
{
    printf("static const dtlcp_cert %s = {\n.version=%u,.serial_len=%u,.subject_len=%u,.issuer_len=%u,.sig_alg=%u,\n",
           name,c->version,c->serial_len,c->subject_len,c->issuer_len,c->sig_alg);
    array("serial",c->serial,c->serial_len);
    array("subject",c->subject,c->subject_len);
    array("issuer",c->issuer,c->issuer_len);
    array("pubkey",c->pubkey,sizeof c->pubkey);
    array("sig",c->sig,sizeof c->sig);
    puts("};");
}
int main(void)
{
    uint8_t d[32],pub[65];
    dtlcp_cert ca,node;
    size_t i;
    if(sm2_keygen(d,pub,NULL)!=0 ||
       dtlcp_cert_issue(&ca,"DTLCP-ROOT-CA",1,pub,"DTLCP-ROOT-CA",d)!=0 ||
       dtlcp_cert_issue(&node,"sensor-node-01",3,pub,"DTLCP-ROOT-CA",d)!=0 ||
       dtlcp_cert_check(&ca,&ca)!=0 || dtlcp_cert_check(&node,&ca)!=0) return 1;
    puts("/* Generated public test fixture. NOT a production identity. */\n#ifndef DEMO_NODE_IDENTITY_H\n#define DEMO_NODE_IDENTITY_H\n#if defined(SMC_PRODUCTION_BUILD)\n#error \"Fixed demo identity forbidden in production\"\n#endif");
    puts("static const uint8_t demo_node_private[32] = {");
    for(i=0;i<32;i++) printf("0x%02x%s",d[i],i==31?"\n":",");
    puts("};");
    cert("demo_node_ca",&ca);
    cert("demo_node_cert",&node);
    puts("#endif");
    return 0;
}
