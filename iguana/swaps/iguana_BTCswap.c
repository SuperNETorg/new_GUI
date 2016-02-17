/******************************************************************************
 * Copyright © 2014-2016 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "../exchanges/bitcoin.h"
#define INSTANTDEX_DECKSIZE 1000
/* https://bitcointalk.org/index.php?topic=1340621.msg13828271#msg13828271
   https://bitcointalk.org/index.php?topic=1364951
Tier Nolan's approach is followed with the following changes:
  a) instead of cutting 1000 keypairs, only INSTANTDEX_DECKSIZE are a
  b) instead of sending the entire 256 bits, it is truncated to 64 bits. With odds of collision being so low, it is dwarfed by the ~0.1% insurance factor.
  c) D is set to 100x the insurance rate of 1/777 12.87% + BTC amount
  d) insurance is added to Bob's payment, which is after the deposit and bailin
  e) BEFORE Bob broadcasts deposit, Alice broadcasts BTC denominated fee in cltv so if trade isnt done fee is reclaimed
*/

int64_t instantdex_BTCsatoshis(int64_t price,int64_t volume)
{
    if ( volume > price )
        return(price * dstr(volume));
    else return(dstr(price) * volume);
}

int64_t instantdex_insurance(struct iguana_info *coin,int64_t amount)
{
    return(amount * INSTANTDEX_INSURANCERATE + coin->chain->txfee); // insurance prevents attack
}

/*
both fees are standard payments: OP_DUP OP_HASH160 FEE_RMD160 OP_EQUALVERIFY OP_CHECKSIG

Alice altpayment: OP_2 <alice_pubM> <bob_pubN> OP_2 OP_CHECKMULTISIG

 Bob deposit: if ( (swap->deposit= instantdex_bobtx(myinfo,coinbtc,&swap->deposittxid,swap->otherpubs[0],swap->mypubs[0],swap->privkeys[swap->choosei],reftime,swap->satoshis[1],1)) != 0 )
OP_IF
    <now + INSTANTDEX_LOCKTIME*2> OP_CLTV OP_DROP <alice_pubA0> OP_CHECKSIG
OP_ELSE
    OP_HASH160 <hash(bob_privN)> OP_EQUALVERIFY <bob_pubB0> OP_CHECKSIG
OP_ENDIF
 
 Bob paytx: if ( (swap->payment= instantdex_bobtx(myinfo,coinbtc,&swap->deposittxid,swap->mypubs[1],swap->otherpubs[0],swap->privkeys[swap->otherschoosei],reftime,swap->satoshis[1],0)) != 0 )
OP_IF
    <now + INSTANTDEX_LOCKTIME> OP_CLTV OP_DROP <bob_pubB1> OP_CHECKSIG
OP_ELSE
    OP_HASH160 <hash(alice_privM)> OP_EQUALVERIFY <alice_pubA0> OP_CHECKSIG
OP_ENDIF
*/

int32_t instantdex_bobscript(uint8_t *script,int32_t n,int32_t *secretstartp,uint32_t locktime,bits256 cltvpub,uint8_t secret160[20],bits256 destpub)
{
    uint8_t pubkeyA[33],pubkeyB[33];
    memcpy(pubkeyA+1,cltvpub.bytes,sizeof(cltvpub)), pubkeyA[0] = 0x02;
    memcpy(pubkeyB+1,destpub.bytes,sizeof(destpub)), pubkeyB[0] = 0x03;
    script[n++] = SCRIPT_OP_IF;
        n = bitcoin_checklocktimeverify(script,n,locktime);
        n = bitcoin_pubkeyspend(script,n,pubkeyA);
    script[n++] = SCRIPT_OP_ELSE;
    if ( secretstartp != 0 )
        *secretstartp = n + 2;
        n = bitcoin_revealsecret160(script,n,secret160);
        n = bitcoin_pubkeyspend(script,n,pubkeyB);
    script[n++] = SCRIPT_OP_ENDIF;
    return(n);
}

int32_t instantdex_alicescript(uint8_t *script,int32_t n,char *msigaddr,uint8_t altps2h,bits256 pubAm,bits256 pubBn)
{
    uint8_t p2sh160[20]; struct vin_info V;
    memset(&V,0,sizeof(V));
    memcpy(&V.signers[0].pubkey[1],pubAm.bytes,sizeof(pubAm)), V.signers[0].pubkey[0] = 0x02;
    memcpy(&V.signers[1].pubkey[1],pubBn.bytes,sizeof(pubBn)), V.signers[1].pubkey[0] = 0x03;
    V.M = V.N = 2;
    n = bitcoin_MofNspendscript(p2sh160,script,n,&V);
    bitcoin_address(msigaddr,altps2h,p2sh160,sizeof(p2sh160));
    return(n);
}

int32_t instantdex_outputinsurance(struct iguana_info *coin,cJSON *txobj,int64_t insurance,uint64_t orderid)
{
    uint8_t rmd160[20],script[128]; int32_t n = 0;
    decode_hex(rmd160,sizeof(rmd160),(orderid % 10) == 0 ? TIERNOLAN_RMD160 : INSTANTDEX_RMD160);
    script[n++] = sizeof(uint64_t);
    n += iguana_rwnum(1,&script[n],sizeof(orderid),&orderid);
    script[n++] = OP_DROP;
    n = bitcoin_standardspend(script,n,rmd160);
    bitcoin_addoutput(coin,txobj,script,n,insurance);
    return(n);
}

char *instantdex_feetx(struct supernet_info *myinfo,bits256 *txidp,struct instantdex_accept *A)
{
    int32_t n; char *feetx = 0; struct iguana_info *coinbtc; cJSON *txobj; struct bitcoin_spend *spend; int64_t insurance;
    if ( (coinbtc= iguana_coinfind("BTC")) != 0 )
    {
        insurance = instantdex_insurance(coinbtc,instantdex_BTCsatoshis(A->offer.price64,A->offer.basevolume64));
        if ( (spend= iguana_spendset(myinfo,coinbtc,insurance,coinbtc->chain->txfee)) != 0 )
        {
            txobj = bitcoin_createtx(coinbtc,0);
            n = instantdex_outputinsurance(coinbtc,txobj,insurance,A->orderid);
            txobj = iguana_signtx(coinbtc,txidp,&feetx,spend,txobj);
            if ( feetx != 0  )
                printf("%s feetx.%s\n",A->offer.myside != 0 ? "BOB" : "ALICE",feetx);
            else printf("error signing %s feetx numinputs.%d\n",A->offer.myside != 0 ? "BOB" : "ALICE",spend->numinputs);
            free(spend);
        }
        else
        {
            printf("no unspents to spend\n");
            feetx = clonestr("deadbeefdeadbeef");
        }
    }
    return(feetx);
}

int32_t instantdex_feetxverify(struct supernet_info *myinfo,struct iguana_info *coin,struct bitcoin_swapinfo *swap,struct instantdex_accept *A,cJSON *argjson)
{
    cJSON *txobj; bits256 txid; uint32_t n; int32_t i,retval = -1; int64_t insurance;
    struct iguana_msgtx msgtx; uint8_t script[512];
    return(0);
    if ( swap->otherfeetx != 0 )
    {
        if ( (txobj= bitcoin_hex2json(coin,&txid,&msgtx,swap->otherfeetx)) != 0 )
        {
            insurance = instantdex_insurance(coin,instantdex_BTCsatoshis(A->offer.price64,A->offer.basevolume64));
            n = instantdex_outputinsurance(coin,txobj,insurance,A->orderid);
            if ( n == msgtx.vouts[0].pk_scriptlen )
            {
                if ( memcmp(script,msgtx.vouts[0].pk_script,n) == 0 )
                {
                    printf("feetx script verified\n");
                }
                else
                {
                    for (i=0; i<n; i++)
                        printf("%02x ",script[i]);
                    printf("fee script\n");
                    for (i=0; i<n; i++)
                        printf("%02x ",msgtx.vouts[0].pk_script[i]);
                    printf("feetx\n");
                }
            }
            free_json(txobj);
        }
    }
    return(retval);
}

char *instantdex_bobtx(struct supernet_info *myinfo,struct iguana_info *coin,bits256 *txidp,bits256 pub1,bits256 pub2,bits256 priv,uint32_t reftime,int64_t amount,int32_t depositflag)
{
    cJSON *txobj; int32_t n,secretstart; char *signedtx = 0;
    uint8_t script[1024],secret[20]; struct bitcoin_spend *spend; uint32_t locktime; int64_t insurance;
    if ( coin == 0 )
        return(0);
    locktime = (uint32_t)(reftime + INSTANTDEX_LOCKTIME * (1 + depositflag));
    txobj = bitcoin_createtx(coin,locktime);
    insurance = instantdex_insurance(coin,amount);
    if ( (spend= iguana_spendset(myinfo,coin,amount + insurance,coin->chain->txfee)) != 0 )
    {
        calc_rmd160_sha256(secret,priv.bytes,sizeof(priv));
        n = instantdex_bobscript(script,0,&secretstart,locktime,pub1,secret,pub2);
        bitcoin_addoutput(coin,txobj,script,n,amount + depositflag*insurance*100);
        txobj = iguana_signtx(coin,txidp,&signedtx,spend,txobj);
        if ( signedtx != 0  )
            printf("bob deposit.%s\n",signedtx);
        else printf("error signing bobdeposit numinputs.%d\n",spend->numinputs);
        free(spend);
    }
    free_json(txobj);
    return(signedtx);
}

int32_t instantdex_paymentverify(struct supernet_info *myinfo,struct iguana_info *coin,struct bitcoin_swapinfo *swap,struct instantdex_accept *A,cJSON *argjson,int32_t depositflag)
{
    cJSON *txobj; bits256 txid; uint32_t n,locktime; int32_t i,secretstart,retval = -1; uint64_t x;
    struct iguana_msgtx msgtx; uint8_t script[512],rmd160[20]; int64_t relsatoshis,amount,insurance = 0;
    if ( coin != 0 && jstr(argjson,depositflag != 0 ? "deposit" : "payment") != 0 )
    {
        relsatoshis = instantdex_BTCsatoshis(A->offer.price64,A->offer.basevolume64);
        if ( depositflag != 0 )
            insurance = 100 * (relsatoshis * INSTANTDEX_INSURANCERATE + coin->chain->txfee);
        amount = relsatoshis + insurance;
        if ( (txobj= bitcoin_hex2json(coin,&txid,&msgtx,swap->deposit)) != 0 )
        {
            locktime = A->offer.expiration;
            if ( depositflag == 0 )
                memset(rmd160,0,sizeof(rmd160));
            else calc_rmd160_sha256(rmd160,swap->privkeys[0].bytes,sizeof(rmd160));
            n = instantdex_bobscript(script,0,&secretstart,locktime,swap->mypubs[0],rmd160,swap->otherpubs[0]);
            if ( msgtx.lock_time == locktime && msgtx.vouts[0].value == amount && n == msgtx.vouts[0].pk_scriptlen )
            {
                memcpy(&script[secretstart],&msgtx.vouts[0].pk_script[secretstart],20);
                if ( memcmp(script,msgtx.vouts[0].pk_script,n) == 0 )
                {
                    iguana_rwnum(0,&script[secretstart],sizeof(x),&x);
                    printf("deposit script verified x.%llx vs otherscut %llx\n",(long long)x,(long long)swap->otherscut[swap->choosei][0]);
                    if ( x == swap->otherscut[swap->choosei][0] )
                        retval = 0;
                    else printf("deposit script verified but secret mismatch x.%llx vs otherscut %llx\n",(long long)x,(long long)swap->otherscut[swap->choosei][0]);
                }
                else
                {
                    for (i=0; i<n; i++)
                        printf("%02x ",script[i]);
                    printf("script\n");
                    for (i=0; i<n; i++)
                        printf("%02x ",msgtx.vouts[0].pk_script[i]);
                    printf("deposit\n");
                }
            }
            free_json(txobj);
        }
    }
    return(retval);
}

int32_t instantdex_altpaymentverify(struct supernet_info *myinfo,struct iguana_info *coin,struct bitcoin_swapinfo *swap,struct instantdex_accept *A,cJSON *argjson)
{
    cJSON *txobj; bits256 txid; uint32_t n; int32_t i,retval = -1;
    struct iguana_msgtx msgtx; uint8_t script[512]; char *altmsigaddr,msigaddr[64];
    if ( jstr(argjson,"altpayment") != 0 && (altmsigaddr= jstr(argjson,"altmsigaddr")) != 0 )
    {
        if ( (txobj= bitcoin_hex2json(coin,&txid,&msgtx,swap->altpayment)) != 0 )
        {
            n = instantdex_alicescript(script,0,msigaddr,coin->chain->p2shtype,swap->pubAm,swap->pubBn);
            if ( strcmp(msigaddr,altmsigaddr) == 0 && n == msgtx.vouts[0].pk_scriptlen )
            {
                if ( memcmp(script,msgtx.vouts[0].pk_script,n) == 0 )
                {
                    printf("deposit script verified\n");
                }
                else
                {
                    for (i=0; i<n; i++)
                        printf("%02x ",script[i]);
                    printf("altscript\n");
                    for (i=0; i<n; i++)
                        printf("%02x ",msgtx.vouts[0].pk_script[i]);
                    printf("altpayment\n");
                }
            }
            free_json(txobj);
        }
    }
    return(retval);
}

char *instantdex_alicetx(struct supernet_info *myinfo,struct iguana_info *altcoin,char *msigaddr,bits256 *txidp,bits256 pubAm,bits256 pubBn,int64_t amount)
{
    cJSON *txobj; int32_t n; char *signedtx = 0; uint8_t script[1024]; struct bitcoin_spend *spend;
    if ( altcoin != 0 && (spend= iguana_spendset(myinfo,altcoin,amount,altcoin->chain->txfee)) != 0 )
    {
        txobj = bitcoin_createtx(altcoin,0);
        n = instantdex_alicescript(script,0,msigaddr,altcoin->chain->p2shtype,pubAm,pubBn);
        bitcoin_addoutput(altcoin,txobj,script,n,amount);
        txobj = iguana_signtx(altcoin,txidp,&signedtx,spend,txobj);
        if ( signedtx != 0 )
            printf("alice payment.%s\n",signedtx);
        else printf("error signing alicetx numinputs.%d\n",spend->numinputs);
        free(spend);
        free_json(txobj);
    }
    return(signedtx);
}

cJSON *BOB_reclaimfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    *serdatap = 0, *serdatalenp = 0; struct bitcoin_swapinfo *swap = ap->info;
    printf("reclaim deposit.(%s) to %s\n",swap->deposit,myinfo->myaddr.BTC);
    // reclaim deposit
    return(newjson);
}

cJSON *BOB_claimaltfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    *serdatap = 0, *serdatalenp = 0; struct bitcoin_swapinfo *swap = ap->info; char altcoinaddr[64];
    printf("spend altpayment.(%s) -> %s\n",swap->altpayment,altcoinaddr);
    // spend altpayment
    return(newjson);
}

cJSON *ALICE_reclaimfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    *serdatap = 0, *serdatalenp = 0; struct bitcoin_swapinfo *swap = ap->info; char altcoinaddr[64];
    // reclaim altpayment
    printf("reclaim altpayment.(%s) -> %s\n",swap->altpayment,altcoinaddr);
    return(newjson);
}

cJSON *ALICE_claimbtcfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    *serdatap = 0, *serdatalenp = 0; struct bitcoin_swapinfo *swap = ap->info;
    printf("spend BTC payment.(%s) -> %s\n",swap->payment,myinfo->myaddr.BTC);
    // spend BTC
    return(newjson);
}

bits256 instantdex_derivekeypair(bits256 *newprivp,uint8_t pubkey[33],bits256 privkey,bits256 orderhash)
{
    bits256 sharedsecret;
    sharedsecret = curve25519_shared(privkey,orderhash);
    vcalc_sha256cat(newprivp->bytes,orderhash.bytes,sizeof(orderhash),sharedsecret.bytes,sizeof(sharedsecret));
    return(bitcoin_pubkey33(pubkey,*newprivp));
}

int32_t instantdex_pubkeyargs(struct bitcoin_swapinfo *swap,cJSON *newjson,int32_t numpubs,bits256 privkey,bits256 hash,int32_t firstbyte)
{
    char buf[3]; int32_t i,n,m,len=0; bits256 pubi; uint64_t txid; uint8_t secret160[20],pubkey[33];
    sprintf(buf,"%c0",'A' - 0x02 + firstbyte);
    for (i=n=m=0; i<numpubs*100 && n<numpubs; i++)
    {
        pubi = instantdex_derivekeypair(&swap->privkeys[n],pubkey,privkey,hash);
        privkey = swap->privkeys[n];
        //printf("i.%d n.%d numpubs.%d %02x vs %02x\n",i,n,numpubs,pubkey[0],firstbyte);
        if ( pubkey[0] != firstbyte )
            continue;
        if ( n < 2 && numpubs > 2 )
        {
            sprintf(buf+1,"%d",n);
            if ( jobj(newjson,buf) == 0 )
                jaddbits256(newjson,buf,pubi);
        }
        else
        {
            calc_rmd160_sha256(secret160,swap->privkeys[n].bytes,sizeof(swap->privkeys[n]));
            memcpy(&txid,secret160,sizeof(txid));
            txid = (m+1) | ((m+1)<<16);
            txid <<= 32;
            txid = (m+1) | ((m+1)<<16);
            pubi.txid = (m+1) | ((m+1)<<16);
            pubi.txid <<= 32;
            pubi.txid = (m+1) | ((m+1)<<16);
            len += iguana_rwnum(1,(uint8_t *)&swap->deck[m][0],sizeof(txid),&txid);
            len += iguana_rwnum(1,(uint8_t *)&swap->deck[m][1],sizeof(pubi.txid),&pubi.txid);
            m++;
        }
        n++;
    }
    return(n);
}

char *instantdex_choosei(struct bitcoin_swapinfo *swap,cJSON *newjson,cJSON *argjson,uint8_t *serdata,int32_t datalen)
{
    int32_t i,j,max,len = 0; uint64_t x;
    if ( swap->choosei < 0 && serdata != 0 && datalen == sizeof(swap->deck) )
    {
        max = (int32_t)(sizeof(swap->otherscut) / sizeof(*swap->otherscut));
        for (i=0; i<max; i++)
            for (j=0; j<2; j++)
                len += iguana_rwnum(1,(uint8_t *)&swap->otherscut[i][j],sizeof(x),&serdata[len]);
        OS_randombytes((uint8_t *)&swap->choosei,sizeof(swap->choosei));
        if ( swap->choosei < 0 )
            swap->choosei = -swap->choosei;
        swap->choosei %= max;
        jaddnum(newjson,"mychoosei",swap->choosei);
        printf("%llu/%llu %s send mychoosei.%d of max.%d\n",(long long)swap->bidid,(long long)swap->askid,swap->isbob!=0?"BOB":"alice",swap->choosei,max);
        return(0);
    }
    else
    {
        printf("invalid datalen.%d vs %ld\n",datalen,sizeof(swap->deck));
        return(clonestr("{\"error\":\"instantdex_BTCswap offer no cut\"}"));
    }
}

void instantdex_getpubs(struct bitcoin_swapinfo *swap,cJSON *argjson,cJSON *newjson)
{
    char fields[2][2][3]; int32_t i,j,myind,otherind;
    memset(fields,0,sizeof(fields));
    fields[0][0][0] = fields[0][1][0] = 'A';
    fields[1][0][0] = fields[1][1][0] = 'B';
    for (i=0; i<2; i++)
        for (j=0; j<2; j++)
            fields[i][j][1] = '0' + j;
    myind = swap->isbob;
    otherind = (myind ^ 1);
    for (j=0; j<2; j++)
    {
        if ( bits256_nonz(swap->mypubs[j]) == 0 && jobj(argjson,fields[myind][j]) != 0 )
            swap->mypubs[j] = jbits256(newjson,fields[myind][j]);
        if ( bits256_nonz(swap->otherpubs[j]) == 0 && jobj(argjson,fields[otherind][j]) != 0 )
            swap->otherpubs[j] = jbits256(argjson,fields[otherind][j]);
    }
}

void instantdex_privkeyextract(struct supernet_info *myinfo,struct bitcoin_swapinfo *swap,uint8_t *serdata,int32_t serdatalen)
{
    int32_t i,wrongfirstbyte,errs,len = 0; bits256 hashpriv,otherpriv,pubi; uint8_t otherpubkey[33];
    if ( swap->cutverified == 0 && swap->choosei >= 0 && serdatalen == sizeof(swap->privkeys) )
    {
        printf("got instantdex_privkeyextract serdatalen.%d choosei.%d cutverified.%d\n",serdatalen,swap->choosei,swap->cutverified);
        for (i=wrongfirstbyte=errs=0; i<sizeof(swap->privkeys)/sizeof(*swap->privkeys); i++)
        {
            len += iguana_rwbignum(0,&serdata[len],sizeof(bits256),otherpriv.bytes);
            if ( i == swap->choosei )
            {
                if ( bits256_nonz(otherpriv) != 0 )
                {
                    printf("got privkey in slot.%d my choosi??\n",i);
                    errs++;
                }
                continue;
            }
            pubi = bitcoin_pubkey33(otherpubkey,otherpriv);
            vcalc_sha256(0,hashpriv.bytes,otherpriv.bytes,sizeof(otherpriv));
            if ( otherpubkey[0] != (swap->isbob ^ 1) + 0x02 )
            {
                wrongfirstbyte++;
                printf("wrongfirstbyte[%d] %02x\n",i,otherpubkey[0]);
            }
            else if ( swap->otherscut[i][0] != hashpriv.txid )
            {
                printf("otherscut[%d] priv mismatch %llx != %llx\n",i,(long long)swap->otherscut[i][0],(long long)hashpriv.txid);
                errs++;
            }
            else if ( swap->otherscut[i][1] != pubi.txid )
            {
                printf("otherscut[%d] priv mismatch %llx != %llx\n",i,(long long)swap->otherscut[i][1],(long long)pubi.txid);
                errs++;
            }
        }
        if ( errs == 0 && wrongfirstbyte == 0 )
            swap->cutverified = 1;
        else printf("failed verification: wrong firstbyte.%d errs.%d\n",wrongfirstbyte,errs);
    }
}

void instantdex_swaptxupdate(char **ptrp,bits256 *txidp,cJSON *argjson,char *txname,char *txidfield)
{
    char *str;
    if ( (str= jstr(argjson,txname)) != 0 )
    {
        if ( *ptrp != 0 )
        {
            printf("got replacement %s? (%s)\n",txname,str);
            free(*ptrp);
        }
        *txidp = jbits256(argjson,txidfield);
        *ptrp = clonestr(str);
    }
}

void instantdex_swapbits256update(bits256 *txidp,cJSON *argjson,char *fieldname)
{
    bits256 txid; char str[65];
    txid = jbits256(argjson,fieldname);
    if ( bits256_nonz(txid) > 0 )
    {
        if ( bits256_nonz(*txidp) > 0 )
            printf("swapbits256: %s sent again\n",bits256_str(str,*txidp));
        *txidp = txid;
    }
}

cJSON *instantdex_parseargjson(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,int32_t deckflag)
{
    cJSON *newjson; struct bitcoin_swapinfo *swap;
    newjson = cJSON_CreateObject();
    if ( (swap= ap->info) == 0 )
        jaddstr(newjson,"error","missing swap info");
    else
    {
        if ( swap->isbob != 0 )
        {
            instantdex_swapbits256update(&swap->otherpubs[0],argjson,"pubA0");
            instantdex_swapbits256update(&swap->otherpubs[1],argjson,"pubA1");
            instantdex_swapbits256update(&swap->pubAm,argjson,"pubAm");
            instantdex_swapbits256update(&swap->privAm,argjson,"privAm");
            instantdex_swaptxupdate(&swap->altpayment,&swap->altpaymenttxid,argjson,"altpayment","altpaymenttxid");
        }
        else
        {
            instantdex_swapbits256update(&swap->otherpubs[0],argjson,"pubB0");
            instantdex_swapbits256update(&swap->otherpubs[1],argjson,"pubB1");
            instantdex_swapbits256update(&swap->pubBn,argjson,"pubBn");
            instantdex_swapbits256update(&swap->privBn,argjson,"privBn");
            instantdex_swaptxupdate(&swap->deposit,&swap->deposittxid,argjson,"deposit","deposittxid");
            instantdex_swaptxupdate(&swap->payment,&swap->paymenttxid,argjson,"payment","paymenttxid");
        }
        instantdex_swaptxupdate(&swap->otherfeetx,&swap->otherfeetxid,argjson,"feetx","feetxid");
        if ( swap->otherschoosei < 0 && jobj(argjson,"mychoosei") != 0 )
        {
            //printf("otherschoosei.%d\n",swap->otherschoosei);
            if ( (swap->otherschoosei= juint(argjson,"mychoosei")) >= sizeof(swap->otherscut)/sizeof(*swap->otherscut) )
                swap->otherschoosei = -1;
        }
        if ( juint(argjson,"verified") != 0 )
            swap->otherverifiedcut = 1;
        jaddnum(newjson,"verified",swap->otherverifiedcut);
        if ( instantdex_pubkeyargs(swap,newjson,2 + deckflag*INSTANTDEX_DECKSIZE,myinfo->persistent_priv,swap->orderhash,0x02+swap->isbob) == 2 + deckflag*INSTANTDEX_DECKSIZE )
            instantdex_getpubs(swap,argjson,newjson);
        else printf("ERROR: couldnt generate pubkeys\n");
    }
    return(newjson);
}

double iguana_numconfs(struct iguana_info *coin,bits256 txid,int32_t height)
{
    if ( coin->longestchain >= height )
        return((double)coin->longestchain - height);
    else return(0.); // 0.5 if zeroconfs
}

char *BTC_txconfirmed(struct supernet_info *myinfo,struct iguana_info *coin,struct instantdex_accept *ap,cJSON *newjson,bits256 txid,double *numconfirmsp,char *virtualevent,double requiredconfs)
{
    struct iguana_txid *tx,T; struct bitcoin_swapinfo *swap; int32_t height; char *retstr; double confs;
    swap = ap->info;
    *numconfirmsp = -1.;
    if ( coin != 0 && *numconfirmsp < 0 )
    {
        if ( (tx= iguana_txidfind(coin,&height,&T,txid)) != 0 && (confs= iguana_numconfs(coin,txid,height)) >= requiredconfs )
        {
            *numconfirmsp = confs;
            if ( (retstr= instantdex_sendcmd(myinfo,&ap->offer,newjson,virtualevent,myinfo->myaddr.persistent,0,0,0)) != 0 )
                return(retstr);
        }
    }
    return(0);
}

cJSON *BTC_waitdeckCfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    *serdatap = 0, *serdatalenp = 0; struct bitcoin_swapinfo *swap = ap->info;
    strcmp(swap->expectedcmdstr,"BTCdeckC");
    if ( instantdex_feetxverify(myinfo,iguana_coinfind("BTC"),swap,ap,argjson) != 0 )
        return(cJSON_Parse("{\"error\":\"feetx didnt verify\"}"));
    return(newjson);
}

cJSON *BTC_waitprivCfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    struct bitcoin_swapinfo *swap = ap->info;
    strcmp(swap->expectedcmdstr,"BTCprivC");
    instantdex_privkeyextract(myinfo,swap,*serdatap,*serdatalenp);
    if ( instantdex_feetxverify(myinfo,iguana_coinfind("BTC"),swap,ap,argjson) != 0 )
        return(cJSON_Parse("{\"error\":\"feetx didnt verify\"}"));
    *serdatap = 0, *serdatalenp = 0;
    return(newjson);
}

cJSON *BOB_waitBTCalttxfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    *serdatap = 0, *serdatalenp = 0; struct bitcoin_swapinfo *swap = ap->info;
    strcmp(swap->expectedcmdstr,"BTCalttx");
    if ( instantdex_altpaymentverify(myinfo,iguana_coinfind(ap->offer.base),swap,ap,argjson) != 0 )
        return(cJSON_Parse("{\"error\":\"altpayment didnt verify\"}"));
    return(newjson);
}

cJSON *ALICE_waitBTCpaytxfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    *serdatap = 0, *serdatalenp = 0; struct bitcoin_swapinfo *swap = ap->info;
    strcmp(swap->expectedcmdstr,"BTCpaytx");
    return(newjson);
}

cJSON *BOB_waitfeefunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    char *retstr; struct bitcoin_swapinfo *swap = ap->info; struct iguana_info *coinbtc; uint32_t reftime;
    coinbtc = iguana_coinfind("BTC");
    *serdatap = 0, *serdatalenp = 0;
    reftime = (uint32_t)(ap->offer.expiration - INSTANTDEX_LOCKTIME*2);
    if ( coinbtc != 0 && swap->deposit == 0 && (retstr= BTC_txconfirmed(myinfo,coinbtc,ap,newjson,swap->otherfeetxid,&swap->otherfeeconfirms,"feefound",0)) != 0 )
    {
        jaddstr(newjson,"feefound",retstr);
        if ( (swap->deposit= instantdex_bobtx(myinfo,coinbtc,&swap->deposittxid,swap->otherpubs[0],swap->mypubs[0],swap->privkeys[swap->choosei],reftime,swap->satoshis[1],1)) != 0 )
        {
            // broadcast deposit
            jaddstr(newjson,"deposit",swap->deposit);
            jaddbits256(newjson,"deposittxid",swap->deposittxid);
        }
        else jaddstr(newjson,"error","couldnt create paymenttx");
        return(newjson);
    }
    return(0);
}

cJSON *BOB_waitprivMfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    char *retstr; struct bitcoin_swapinfo *swap = ap->info;
    strcmp(swap->expectedcmdstr,"BTCprivM");
    if ( swap->payment != 0 && (retstr= BTC_txconfirmed(myinfo,iguana_coinfind("BTC"),ap,newjson,swap->paymenttxid,&swap->paymentconfirms,"btcfound",0)) != 0 )
        jaddstr(newjson,"btcfound",retstr);
    printf("search for payment spend in blockchain\n");
    *serdatap = 0, *serdatalenp = 0;
    return(newjson);
}

cJSON *BOB_waitaltconfirmfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    char *retstr; struct bitcoin_swapinfo *swap = ap->info; struct iguana_info *altcoin; uint32_t reftime;
    altcoin = iguana_coinfind(ap->offer.base);
    *serdatap = 0, *serdatalenp = 0;
    reftime = (uint32_t)(ap->offer.expiration - INSTANTDEX_LOCKTIME*2);
    if ( altcoin != 0 && swap->altpayment != 0 && (retstr= BTC_txconfirmed(myinfo,altcoin,ap,newjson,swap->altpaymenttxid,&swap->altpaymentconfirms,"altfound",altcoin->chain->minconfirms)) != 0 )
    {
        jaddstr(newjson,"altfound",retstr);
        if ( (swap->payment= instantdex_bobtx(myinfo,altcoin,&swap->deposittxid,swap->mypubs[1],swap->otherpubs[0],swap->privkeys[swap->otherschoosei],reftime,swap->satoshis[1],0)) != 0 )
        {
            // broadcast payment
            jaddstr(newjson,"payment",swap->payment);
            jaddbits256(newjson,"paymenttxid",swap->paymenttxid);
        }
        else jaddstr(newjson,"error","couldnt create paymenttx");
        return(newjson);
    }
    return(0);
}

cJSON *BTC_waitprivsfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    *serdatap = 0, *serdatalenp = 0; struct bitcoin_swapinfo *swap = ap->info;
    strcmp(swap->expectedcmdstr,"BTCprivs");
    instantdex_privkeyextract(myinfo,swap,*serdatap,*serdatalenp);
    return(newjson);
}

cJSON *ALICE_waitfeefunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    char *retstr; struct iguana_info *coinbtc; struct bitcoin_swapinfo *swap = ap->info;
    coinbtc = iguana_coinfind("BTC");
    *serdatap = 0, *serdatalenp = 0;
    if ( swap->otherfeetx != 0 && (retstr= BTC_txconfirmed(myinfo,coinbtc,ap,newjson,swap->otherfeetxid,&swap->otherfeeconfirms,"feefound",0)) != 0 )
    {
        jaddstr(newjson,"feefound",retstr);
        return(newjson);
    } else return(0);
}

cJSON *ALICE_waitdepositfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    char *retstr; struct iguana_info *coinbtc,*altcoin; struct bitcoin_swapinfo *swap = ap->info;
    coinbtc = iguana_coinfind("BTC");
    altcoin = iguana_coinfind(ap->offer.rel);
    *serdatap = 0, *serdatalenp = 0;
    if ( swap->deposit != 0 && (retstr= BTC_txconfirmed(myinfo,coinbtc,ap,newjson,swap->deposittxid,&swap->depositconfirms,"depfound",0.5)) != 0 )
    {
        jaddstr(newjson,"depfound",retstr);
        if ( instantdex_paymentverify(myinfo,iguana_coinfind("BTC"),swap,ap,argjson,1) != 0 )
            return(cJSON_Parse("{\"error\":\"deposit didnt verify\"}"));
        if ( (swap->altpayment= instantdex_alicetx(myinfo,altcoin,swap->altmsigaddr,&swap->altpaymenttxid,swap->pubAm,swap->pubBn,swap->satoshis[0])) != 0 )
        {
            // broadcast altpayment
            jaddstr(newjson,"altpayment",swap->altpayment);
            jaddbits256(newjson,"altpaymenttxid",swap->altpaymenttxid);
        } else return(cJSON_Parse("{\"error\":\"couldnt create altpayment\"}"));
    }
    return(newjson);
}

cJSON *ALICE_waitpayconf_or_bobreclaimfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    char *retstr; double btcconfirms; struct iguana_info *coinbtc; struct bitcoin_swapinfo *swap = ap->info;
    coinbtc = iguana_coinfind("BTC");
    *serdatap = 0, *serdatalenp = 0;
    if ( swap->satoshis[1] < SATOSHIDEN/10 )
        btcconfirms = 0;
    else btcconfirms = 1. + sqrt((double)swap->satoshis[1] / SATOSHIDEN);
    if ( swap->payment != 0 && (retstr= BTC_txconfirmed(myinfo,coinbtc,ap,newjson,swap->paymenttxid,&swap->paymentconfirms,"payfound",btcconfirms)) != 0 )
    {
        jaddstr(newjson,"payfound",retstr);
        // if bobreclaimed is there, then reclaim altpayment
        printf("search for Bob's reclaim in blockchain\n");
        return(newjson);
    } else return(0);
}

cJSON *BTC_cleanupfunc(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,cJSON *argjson,cJSON *newjson,uint8_t **serdatap,int32_t *serdatalenp)
{
    *serdatap = 0, *serdatalenp = 0;
    jaddstr(newjson,"error","need to cleanup");
    ap->dead = (uint32_t)time(NULL);
    return(newjson);
}

struct instantdex_stateinfo *BTC_initFSM(int32_t *n)
{
    struct instantdex_stateinfo *s = 0;
    *n = 2;
    // Four initial states are BOB_sentoffer, ALICE_gotoffer, ALICE_sentoffer, BOB_gotoffer
    // the initiator includes signed feetx and deck of INSTANTDEX_DECKSIZE keypairs
    //
    // "BTCabcde are message events from other party (message events capped at length 8)
    // "lowercas" are special events, <TX> types: <fee>, <dep>osit, <alt>payment, <acl> is altcoin claim
    // "<TX>found" means the other party's is confirmed at user specified confidence level
    // BTC_cleanup state just unwinds pending swap as nothing has been committed yet
    
    // states instantdex_statecreate(s,n,<Name of State>,handlerfunc,errorhandler,<Timeout State>,<Error State>
    // a given state has a couple of handlers and custom events, with timeouts and errors invoking a bypass
    s = instantdex_statecreate(s,n,"BTC_cleanup",BTC_cleanupfunc,0,0,0,-1); // from states without any commits
    
    s = instantdex_statecreate(s,n,"BOB_reclaim",BOB_reclaimfunc,0,0,0,0); // Bob's gets his deposit back
    instantdex_addevent(s,*n,"BOB_reclaim","brcfound","poll","BTC_cleanup");
    instantdex_addevent(s,*n,"BOB_reclaim","poll","poll","BOB_reclaim");

    s = instantdex_statecreate(s,n,"ALICE_reclaim",ALICE_reclaimfunc,0,0,0,0); // Alice retrieves alt payment
    instantdex_addevent(s,*n,"ALICE_reclaim","arcfound","poll","BTC_cleanup");
    instantdex_addevent(s,*n,"ALICE_reclaim","poll","poll","ALICE_reclaim");

    s = instantdex_statecreate(s,n,"ALICE_claimedbtc",ALICE_claimbtcfunc,0,0,0,0); // mainstream cases
    instantdex_addevent(s,*n,"ALICE_claimedbtc","aclfound","poll","BTC_cleanup");
    instantdex_addevent(s,*n,"ALICE_claimedbtc","poll","poll","ALICE_claimedbtc");
    
    s = instantdex_statecreate(s,n,"BOB_claimedalt",BOB_claimaltfunc,0,0,0,0);
    instantdex_addevent(s,*n,"BOB_claimedalt","bclfound","poll","BTC_cleanup");
    instantdex_addevent(s,*n,"BOB_claimedalt","poll","poll","BOB_claimedalt");

    // need to create states before they can be referred to, that way a one pass FSM compile is possible
    s = instantdex_statecreate(s,n,"BOB_sentprivs",BTC_waitprivsfunc,0,"BTC_cleanup",0,0);
    s = instantdex_statecreate(s,n,"BOB_waitfee",BOB_waitfeefunc,0,"BTC_cleanup",0,0);
    s = instantdex_statecreate(s,n,"BOB_sentdeposit",BOB_waitBTCalttxfunc,0,"BOB_reclaim",0,0);
    s = instantdex_statecreate(s,n,"BOB_altconfirm",BOB_waitaltconfirmfunc,0,"BOB_reclaim",0,0);
    s = instantdex_statecreate(s,n,"BOB_sentpayment",BOB_waitprivMfunc,0,"BOB_reclaim",0,0);
    s = instantdex_statecreate(s,n,"ALICE_sentprivs",BTC_waitprivsfunc,0,"BTC_cleanup",0,0);
    s = instantdex_statecreate(s,n,"Alice_waitfee",ALICE_waitfeefunc,0,"BTC_cleanup",0,0);
    s = instantdex_statecreate(s,n,"ALICE_waitdeposit",ALICE_waitdepositfunc,0,"BTC_cleanup",0,0);
    s = instantdex_statecreate(s,n,"ALICE_sentalt",ALICE_waitBTCpaytxfunc,0,"ALICE_reclaim",0,0);
    s = instantdex_statecreate(s,n,"ALICE_waitconfirms",ALICE_waitpayconf_or_bobreclaimfunc,0,"ALICE_reclaim",0,0);

    // events instantdex_addevent(s,*n,<Current State>,<event>,<message to send>,<Next State>)
    if ( 0 ) // following are implicit states and events handled externally to setup datastructures
    {
        //s = instantdex_statecreate(s,n,"BOB_idle",BTC_idlefunc,0,0,0);
        //s = instantdex_statecreate(s,n,"ALICE_idle",BTC_idlefunc,0,0,0);
        instantdex_addevent(s,*n,"BOB_idle","usrorder","BTCoffer","BOB_sentoffer"); // send deck
        instantdex_addevent(s,*n,"ALICE_idle","usrorder","BTCoffer","ALICE_sentoffer");
        instantdex_addevent(s,*n,"BOB_idle","BTCoffer","BTCdeckC","BOB_gotoffer"); // send deck + Chose
        instantdex_addevent(s,*n,"ALICE_idle","BTCoffer","BTCdeckC","ALICE_gotoffer");
    }
    // after offer is sent, wait for other side to choose and sent their deck, then send privs
    s = instantdex_statecreate(s,n,"BOB_sentoffer",BTC_waitdeckCfunc,0,"BTC_cleanup",0,1);
    s = instantdex_statecreate(s,n,"ALICE_sentoffer",BTC_waitdeckCfunc,0,"BTC_cleanup",0,1);
    instantdex_addevent(s,*n,"BOB_sentoffer","BTCdeckC","BTCprivC","BOB_sentprivs"); // send privs + Chose
    instantdex_addevent(s,*n,"ALICE_sentoffer","BTCdeckC","BTCprivC","ALICE_sentprivs");
    
    // gotoffer states have received deck and sent BTCdeckC already (along with deck)
    s = instantdex_statecreate(s,n,"BOB_gotoffer",BTC_waitprivCfunc,0,"BTC_cleanup",0,1);
    s = instantdex_statecreate(s,n,"ALICE_gotoffer",BTC_waitprivCfunc,0,"BTC_cleanup",0,1);
    instantdex_addevent(s,*n,"BOB_gotoffer","BTCprivC","BTCprivs","BOB_sentprivs"); // send privs
    instantdex_addevent(s,*n,"ALICE_gotoffer","BTCprivC","BTCprivs","ALICE_sentprivs");
    
    // to reach sentprivs, all paths must have sent/recv deck and Chose and verified cut and choose
    s = instantdex_statecreate(s,n,"BOB_sentprivs",BTC_waitprivsfunc,0,"BTC_cleanup",0,0);
    instantdex_addevent(s,*n,"BOB_sentprivs","BTCprivs","poll","BOB_waitfee");
    
    s = instantdex_statecreate(s,n,"ALICE_sentprivs",BTC_waitprivsfunc,0,"BTC_cleanup",0,0);
    instantdex_addevent(s,*n,"ALICE_sentprivs","BTCprivs","poll","Alice_waitfee");

    // Bob waits for fee and sends deposit when it appears
    s = instantdex_statecreate(s,n,"BOB_waitfee",BOB_waitfeefunc,0,"BTC_cleanup",0,0);
    instantdex_addevent(s,*n,"BOB_waitfee","feefound","BTCdeptx","BOB_sentdeposit");
    instantdex_addevent(s,*n,"BOB_waitfee","poll","poll","BOB_waitfee");

    // Alice waits for fee and then waits for deposit to confirm and sends altpayment
    s = instantdex_statecreate(s,n,"Alice_waitfee",ALICE_waitfeefunc,0,"BTC_cleanup",0,0);
    instantdex_addevent(s,*n,"Alice_waitfee","feefound","poll","ALICE_waitdeposit");
    instantdex_addevent(s,*n,"Alice_waitfee","poll","poll","Alice_waitfee");
    
    s = instantdex_statecreate(s,n,"ALICE_waitdeposit",ALICE_waitdepositfunc,0,"BTC_cleanup",0,0);
    instantdex_addevent(s,*n,"ALICE_waitdeposit","depfound","BTCalttx","ALICE_sentalt");
    instantdex_addevent(s,*n,"ALICE_waitdeposit","poll","poll","ALICE_waitdeposit");

    // now Bob's turn to make sure altpayment is confirmed and send real payment
    s = instantdex_statecreate(s,n,"BOB_sentdeposit",BOB_waitBTCalttxfunc,0,"BOB_reclaim",0,0);
    instantdex_addevent(s,*n,"BOB_sentdeposit","BTCalttx","poll","BOB_altconfirm");
 
    s = instantdex_statecreate(s,n,"BOB_altconfirm",BOB_waitaltconfirmfunc,0,"BOB_reclaim",0,0);
    instantdex_addevent(s,*n,"BOB_altconfirm","altfound","BTCpaytx","BOB_sentpayment");
    instantdex_addevent(s,*n,"BOB_altconfirm","poll","poll","BOB_altconfirm");
    
    // now Alice's turn to make sure payment is confrmed and send in claim or see bob's reclaim and reclaim
    s = instantdex_statecreate(s,n,"ALICE_sentalt",ALICE_waitBTCpaytxfunc,0,"ALICE_reclaim",0,0);
    instantdex_addevent(s,*n,"ALICE_sentalt","BTCpaytx","poll","ALICE_waitconfirms");
    
    s = instantdex_statecreate(s,n,"ALICE_waitconfirms",ALICE_waitpayconf_or_bobreclaimfunc,0,"ALICE_reclaim",0,0);
    instantdex_addevent(s,*n,"ALICE_waitconfirms","bobfound","poll","ALICE_reclaim");
    instantdex_addevent(s,*n,"ALICE_waitconfirms","payfound","BTCprivM","ALICE_claimedbtc");
    instantdex_addevent(s,*n,"ALICE_waitconfirms","poll","poll","ALICE_waitconfirms");

    // Bob waits for privM either from Alice or alt blockchain
    s = instantdex_statecreate(s,n,"BOB_sentpayment",BOB_waitprivMfunc,0,"BOB_reclaim",0,0);
    instantdex_addevent(s,*n,"BOB_sentpayment","btcfound","BTCdone","BOB_claimedalt");
    instantdex_addevent(s,*n,"BOB_sentpayment","BTCprivM","BTCdone","BOB_claimedalt");
    instantdex_addevent(s,*n,"BOB_sentpayment","poll","poll","BOB_sentpayment");
    {
        double startmillis = OS_milliseconds();
        instantdex_FSMtest(s,*n,10000000);
        printf("elapsed %.3f ave %.6f\n",OS_milliseconds() - startmillis,(OS_milliseconds() - startmillis)/10000000);
    }
    return(s);
}

char *instantdex_statemachine(struct instantdex_stateinfo *states,int32_t numstates,struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *A,char *cmdstr,cJSON *argjson,cJSON *newjson,uint8_t *serdata,int32_t serdatalen)
{
    uint32_t i; struct iguana_info *altcoin,*coinbtc; struct bitcoin_swapinfo *swap; struct instantdex_stateinfo *state; cJSON *origjson = newjson;
    if ( (swap= A->info) == 0 || (state= swap->state) == 0 || (coinbtc= iguana_coinfind("BTC")) == 0 || (altcoin= iguana_coinfind(A->offer.base)) == 0 )
        return(clonestr("{\"error\":\"instantdex_BTCswap missing coin info\"}"));
    printf("%llu/%llu cmd.(%s) state.(%s)\n",(long long)swap->bidid,(long long)swap->askid,cmdstr,swap->state->name);
    if ( swap->expiration != 0 && time(NULL) > swap->expiration )
    {
        swap->state = &states[state->timeoutind];
        if ( (newjson= (*state->timeout)(myinfo,exchange,A,argjson,newjson,&serdata,&serdatalen)) == 0 )
            return(clonestr("{\"error\":\"instantdex_BTCswap null return from timeoutfunc\"}"));
        return(jprint(newjson,1));
    }
    for (i=0; i<state->numevents; i++)
    {
        if ( strcmp(cmdstr,state->events[i].cmdstr) == 0 )
        {
            if ( (newjson= (*state->process)(myinfo,exchange,A,argjson,newjson,&serdata,&serdatalen)) == 0 )
            {
                free_json(origjson);
                if ( strcmp("poll",state->events[i].sendcmd) == 0 )
                {
                    printf("poll event\n");
                    return(instantdex_sendcmd(myinfo,&A->offer,newjson,state->events[i].sendcmd,myinfo->myaddr.persistent,0,serdata,serdatalen));
                }
                else
                {
                    printf("null return from non-poll event\n");
                    swap->state = &states[state->errorind];
                    return(clonestr("{\"error\":\"instantdex_statemachine: null return\"}"));
                }
            }
            else
            {
                if ( state->events[i].sendcmd[0] != 0 )
                {
                    if ( state->events[i].nextstateind > 1 )
                    {
                        swap->state = &states[state->events[i].nextstateind];
                        return(instantdex_sendcmd(myinfo,&A->offer,newjson,state->events[i].sendcmd,swap->othertrader,INSTANTDEX_HOPS,serdata,serdatalen));
                    } else return(clonestr("{\"error\":\"instantdex_statemachine: illegal state\"}"));
                } else return(clonestr("{\"result\":\"instantdex_statemachine: processed\"}"));
            }
        }
    }
    return(clonestr("{\"error\":\"instantdex_statemachine: unexpected state\"}"));
}

#ifdef oldway
// https://github.com/TierNolan/bips/blob/bip4x/bip-atom.mediawiki

int32_t bitcoin_2of2spendscript(int32_t *paymentlenp,uint8_t *paymentscript,uint8_t *msigscript,bits256 pub0,bits256 pub1)
{
    struct vin_info V; uint8_t p2sh_rmd160[20]; int32_t p2shlen;
    memset(&V,0,sizeof(V));
    V.M = V.N = 2;
    memcpy(V.signers[0].pubkey+1,pub0.bytes,sizeof(pub0)), V.signers[0].pubkey[0] = 0x02;
    memcpy(V.signers[1].pubkey+1,pub1.bytes,sizeof(pub1)), V.signers[1].pubkey[0] = 0x03;
    p2shlen = bitcoin_MofNspendscript(p2sh_rmd160,msigscript,0,&V);
    *paymentlenp = bitcoin_p2shspend(paymentscript,0,p2sh_rmd160);
    return(p2shlen);
}

/*
 Name: Bob.Bail.In
 Input value:     B + 2*fb + change
 Input source:    (From Bob's coins, multiple inputs are allowed)
 vout0 value:  B,  ScriptPubKey 0:  OP_HASH160 Hash160(P2SH Redeem) OP_EQUAL
 vout1 value:  fb, ScriptPubKey 1:  OP_HASH160 Hash160(x) OP_EQUALVERIFY pub-A1 OP_CHECKSIG
 vout2 value:  change, ScriptPubKey 2:  <= 100 bytes
 P2SH Redeem:  OP_2 pub-A1 pub-B1 OP_2 OP_CHECKMULTISIG
 Name: Alice.Bail.In
 vins:  A + 2*fa + change, Input source: (From Alice's altcoins, multiple inputs are allowed)
 vout0 value: A,  ScriptPubKey 0: OP_HASH160 Hash160(P2SH Redeem) OP_EQUAL
 vout1 value: fa, ScriptPubKey 1: OP_HASH160 Hash160(x) OP_EQUAL
 vout2 value: change, ScriptPubKey 2: <= 100 bytes
 */
char *instantdex_bailintx(struct iguana_info *coin,bits256 *txidp,struct bitcoin_spend *spend,bits256 A0,bits256 B0,uint8_t x[20],int32_t isbob)
{
    uint64_t change; char *rawtxstr,*signedtx; struct vin_info *V; bits256 txid,signedtxid;
    int32_t p2shlen,i; cJSON *txobj;  int32_t scriptv0len,scriptv1len,scriptv2len;
    uint8_t p2shscript[256],scriptv0[128],scriptv1[128],changescript[128],pubkey[35];
    p2shlen = bitcoin_2of2spendscript(&scriptv0len,scriptv0,p2shscript,A0,B0);
    txobj = bitcoin_createtx(coin,0);
    bitcoin_addoutput(coin,txobj,scriptv0,scriptv0len,spend->satoshis);
    if ( isbob != 0 )
    {
        scriptv1len = bitcoin_revealsecret160(scriptv1,0,x);
        scriptv1len = bitcoin_pubkeyspend(scriptv1,scriptv1len,pubkey);
    } else scriptv1len = bitcoin_p2shspend(scriptv1,0,x);
    bitcoin_addoutput(coin,txobj,scriptv1,scriptv1len,spend->txfee);
    if ( (scriptv2len= bitcoin_changescript(coin,changescript,0,&change,spend->changeaddr,spend->input_satoshis,spend->satoshis,spend->txfee)) > 0 )
        bitcoin_addoutput(coin,txobj,changescript,scriptv2len,change);
    for (i=0; i<spend->numinputs; i++)
        bitcoin_addinput(coin,txobj,spend->inputs[i].txid,spend->inputs[i].vout,0xffffffff);
    rawtxstr = bitcoin_json2hex(coin,&txid,txobj);
    char str[65]; printf("%s_bailin.%s (%s)\n",isbob!=0?"bob":"alice",bits256_str(str,txid),rawtxstr);
    V = calloc(spend->numinputs,sizeof(*V));
    for (i=0; i<spend->numinputs; i++)
        V[i].signers[0].privkey = spend->inputs[i].privkey;
    bitcoin_verifytx(coin,&signedtxid,&signedtx,rawtxstr,V);
    free(rawtxstr), free(V);
    if ( signedtx != 0 )
        printf("signed %s_bailin.%s (%s)\n",isbob!=0?"bob":"alice",bits256_str(str,signedtxid),signedtx);
    else printf("error generating signedtx\n");
    free_json(txobj);
    *txidp = txid;
    return(signedtx);
}

cJSON *instantdex_bailinspend(struct iguana_info *coin,bits256 privkey,uint64_t amount)
{
    int32_t n; cJSON *txobj;
    int32_t scriptv0len; uint8_t p2shscript[256],rmd160[20],scriptv0[128],pubkey[35];
    bitcoin_pubkey33(pubkey,privkey);
    n = bitcoin_pubkeyspend(p2shscript,0,pubkey);
    calc_rmd160_sha256(rmd160,p2shscript,n);
    scriptv0len = bitcoin_p2shspend(scriptv0,0,rmd160);
    txobj = bitcoin_createtx(coin,0);
    bitcoin_addoutput(coin,txobj,scriptv0,scriptv0len,amount);
    return(txobj);
}

/*
 Name: Bob.Payout
 vin0:  A, Input source: Alice.Bail.In:0
 vin1:  fa, Input source: Alice.Bail.In:1
 vout0: A, ScriptPubKey: OP_HASH160 Hash160(P2SH Redeem) OP_EQUAL; P2SH Redeem:  pub-B2 OP_CHECKSIG
 
 Name: Alice.Payout
 vin0:  B, Input source: Bob.Bail.In:0
 vin1:  fb, Input source: Bob.Bail.In:1
 vout0: B, ScriptPubKey: OP_HASH160 Hash160(P2SH Redeem) OP_EQUAL; P2SH Redeem:  pub-A2 OP_CHECKSIG
 */

char *instantdex_bailinsign(struct iguana_info *coin,bits256 bailinpriv,char *sigstr,int32_t *siglenp,bits256 *txidp,struct vin_info *V,cJSON *txobj,int32_t isbob)
{
    char *rawtxstr,*signedtx;
    rawtxstr = bitcoin_json2hex(coin,txidp,txobj);
    char str[65]; printf("%s_payout.%s (%s)\n",isbob!=0?"bob":"alice",bits256_str(str,*txidp),rawtxstr);
    V->signers[isbob].privkey = bailinpriv;
    bitcoin_verifytx(coin,txidp,&signedtx,rawtxstr,V);
    *siglenp = V->signers[isbob].siglen;
    init_hexbytes_noT(sigstr,V->signers[isbob].sig,*siglenp);
    free(rawtxstr);
    if ( signedtx != 0 )
        printf("signed %s_payout.%s (%s) sig.%s\n",isbob!=0?"bob":"alice",bits256_str(str,*txidp),signedtx,sigstr);
    else printf("error generating signedtx\n");
    free_json(txobj);
    return(signedtx);
}

char *instantdex_payouttx(struct iguana_info *coin,char *sigstr,int32_t *siglenp,bits256 *txidp,bits256 *sharedprivs,bits256 bailintxid,int64_t amount,int64_t txfee,int32_t isbob,char *othersigstr)
{
    struct vin_info V; cJSON *txobj;
    txobj = instantdex_bailinspend(coin,sharedprivs[1],amount);
    bitcoin_addinput(coin,txobj,bailintxid,0,0xffffffff);
    bitcoin_addinput(coin,txobj,bailintxid,1,0xffffffff);
    memset(&V,0,sizeof(V));
    if ( othersigstr != 0 )
    {
        printf("OTHERSIG.(%s)\n",othersigstr);
        V.signers[isbob ^ 1].siglen = (int32_t)strlen(othersigstr) >> 1;
        decode_hex(V.signers[isbob ^ 1].sig,V.signers[isbob ^ 1].siglen,othersigstr);
    }
    return(instantdex_bailinsign(coin,sharedprivs[0],sigstr,siglenp,txidp,&V,txobj,isbob));
}

/*
 Name: Alice.Refund
 vin0: A, Input source: Alice.Bail.In:0
 vout0: A - fa, ScriptPubKey: OP_HASH160 Hash160(P2SH) OP_EQUAL; P2SH Redeem:  pub-A3 OP_CHECKSIG
 Locktime: current block height + ((T/2)/(altcoin block rate))
 
 Name: Bob.Refund
 vin0:  B, Input source: Bob.Bail.In:0
 vout0: B - fb, ScriptPubKey: OP_HASH160 Hash160(P2SH Redeem) OP_EQUAL; P2SH Redeem:  pub-B3 OP_CHECKSIG
 Locktime:     (current block height) + (T / 10 minutes)
 */
char *instantdex_refundtx(struct iguana_info *coin,bits256 *txidp,bits256 bailinpriv,bits256 priv2,bits256 bailintxid,int64_t amount,int64_t txfee,int32_t isbob)
{
    char sigstr[256]; int32_t siglen; struct vin_info V; cJSON *txobj;
    txobj = instantdex_bailinspend(coin,priv2,amount - txfee);
    bitcoin_addinput(coin,txobj,bailintxid,0,0xffffffff);
    return(instantdex_bailinsign(coin,bailinpriv,sigstr,&siglen,txidp,&V,txobj,isbob));
}

int32_t instantdex_calcx20(char hexstr[41],uint8_t *p2shscript,uint8_t firstbyte,bits256 pub)
{
    uint8_t pubkey[33],rmd160[20]; int32_t n;
    memcpy(pubkey+1,pub.bytes,sizeof(pub)), pubkey[0] = firstbyte;
    n = bitcoin_pubkeyspend(p2shscript,0,pubkey);
    calc_rmd160_sha256(rmd160,p2shscript,n);
    init_hexbytes_noT(hexstr,rmd160,sizeof(rmd160));
    return(n);
}

char *instantdex_bailinrefund(struct supernet_info *myinfo,struct iguana_info *coin,struct exchange_info *exchange,struct instantdex_accept *A,char *nextcmd,uint8_t secret160[20],cJSON *newjson,int32_t isbob,bits256 A0,bits256 B0,bits256 *sharedprivs)
{
    struct bitcoin_spend *spend; char *bailintx,*refundtx,field[64]; bits256 bailintxid,refundtxid;
    if ( bits256_nonz(A0) > 0 && bits256_nonz(B0) > 0 )
    {
        if ( (spend= instantdex_spendset(myinfo,coin,A->offer.basevolume64,INSTANTDEX_DONATION)) != 0 )
        {
            bailintx = instantdex_bailintx(coin,&bailintxid,spend,A0,B0,secret160,0);
            refundtx = instantdex_refundtx(coin,&refundtxid,sharedprivs[0],sharedprivs[2],bailintxid,A->offer.basevolume64,coin->chain->txfee,isbob);
            if ( A->statusjson == 0 )
                A->statusjson = cJSON_CreateObject();
            sprintf(field,"bailin%c",'A'+isbob), jaddstr(A->statusjson,field,bailintx), free(bailintx);
            sprintf(field,"refund%c",'A'+isbob), jaddstr(A->statusjson,field,refundtx), free(refundtx);
            sprintf(field,"bailintx%c",'A'+isbob), jaddbits256(A->statusjson,field,bailintxid);
            sprintf(field,"bailintxid%c",'A'+isbob), jaddbits256(newjson,field,bailintxid);
            free(spend);
            return(instantdex_sendcmd(myinfo,&A->A,newjson,nextcmd,swap->othertrader,INSTANTDEX_HOPS));
        } else return(clonestr("{\"error\":\"couldnt create bailintx\"}"));
    } else return(clonestr("{\"error\":\"dont have pubkey0 pair\"}"));
}

cJSON *instantdex_payout(struct supernet_info *myinfo,struct iguana_info *coin,struct exchange_info *exchange,struct instantdex_accept *A,uint8_t secret160[20],int32_t isbob,bits256 *A0p,bits256 *B0p,bits256 *sharedprivs,bits256 hash,uint64_t satoshis[2],cJSON *argjson)
{
    cJSON *newjson; char field[32],payoutsigstr[256],*signedpayout; int32_t payoutsiglen; bits256 payouttxid,bailintxid;
    if ( (newjson= instantdex_newjson(myinfo,A0p,B0p,sharedprivs,secret160,isbob,argjson,hash,A)) == 0 )
        return(0);
    sprintf(field,"bailintxid%c",'A' + (isbob^1)), bailintxid = jbits256(argjson,field);
    sprintf(field,"payoutsig%c",'A' + (isbob^1));
    if ( (signedpayout= instantdex_payouttx(coin,payoutsigstr,&payoutsiglen,&payouttxid,sharedprivs,bailintxid,satoshis[isbob],coin->chain->txfee,isbob,jstr(argjson,field))) != 0 )
    {
        sprintf(field,"payoutsig%c",'A'+isbob), jaddstr(newjson,field,payoutsigstr);
        if ( A->statusjson == 0 )
            A->statusjson = cJSON_CreateObject();
        sprintf(field,"payout%c",'A'+isbob), jaddstr(A->statusjson,field,signedpayout);
        free(signedpayout);
    }
    return(newjson);
}

char *instantdex_advance(struct supernet_info *myinfo,bits256 *sharedprivs,int32_t isbob,cJSON *argjson,bits256 hash,char *addfield,char *nextstate,struct instantdex_accept *A)
{
    cJSON *newjson; bits256 A0,B0; uint8_t secret160[20];
    if ( (newjson= instantdex_newjson(myinfo,&A0,&B0,sharedprivs,secret160,isbob,argjson,hash,A)) == 0 )
        return(clonestr("{\"error\":\"instantdex_BTCswap offer null newjson\"}"));
    if ( A->statusjson != 0 && jstr(A->statusjson,addfield) != 0 )
    {
        jaddstr(newjson,addfield,jstr(A->statusjson,addfield));
        if ( nextstate != 0 )
            return(instantdex_sendcmd(myinfo,&A->A,newjson,nextstate,swap->othertrader,INSTANTDEX_HOPS));
        else return(clonestr("{\"result\":\"instantdex_BTCswap advance complete, wait or refund\"}"));
    } else return(clonestr("{\"error\":\"instantdex_BTCswap advance cant find statusjson\"}"));
}

void instantdex_pendingnotice(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *ap,uint64_t basevolume64)
{
    //    printf("need to start monitoring thread\n");
    ap->pendingvolume64 -= basevolume64;
}

char *instantdex_BTCswap(struct supernet_info *myinfo,struct exchange_info *exchange,struct instantdex_accept *A,char *cmdstr,struct instantdex_msghdr *msg,cJSON *argjson,char *remoteaddr,uint64_t signerbits,uint8_t *data,int32_t datalen) // receiving side
{
    uint8_t secret160[20]; bits256 hash,traderpub,A0,B0,sharedprivs[4]; uint64_t satoshis[2];
    cJSON *newjson; struct instantdex_accept *ap; char *retstr=0,*str;
    int32_t locktime,isbob=0,offerdir = 0; struct iguana_info *coinbtc,*other;
    if ( exchange == 0 )
        return(clonestr("{\"error\":\"instantdex_BTCswap null exchange ptr\"}"));
    offerdir = instantdex_bidaskdir(A);
    if ( (other= iguana_coinfind(A->offer.base)) == 0 || (coinbtc= iguana_coinfind("BTC")) == 0 )
    {
        printf("other.%p coinbtc.%p (%s/%s)\n",other,coinbtc,A->offer.base,A->offer.rel);
        return(clonestr("{\"error\":\"instantdex_BTCswap cant find btc or other coin info\"}"));
    }
    locktime = (uint32_t)(A->offer.expiration + INSTANTDEX_LOCKTIME);
    if ( strcmp(A->offer.rel,"BTC") != 0 )
        return(clonestr("{\"error\":\"instantdex_BTCswap offer non BTC rel\"}"));
    vcalc_sha256(0,hash.bytes,(void *)&A->A,sizeof(ap->offer));
    if ( hash.txid != A->orderid )
        return(clonestr("{\"error\":\"txid mismatches orderid\"}"));
    satoshis[0] = A->offer.basevolume64;
    satoshis[1] = instantdex_BTCsatoshis(A->offer.price64,A->offer.basevolume64);
    //printf("got offer.(%s) offerside.%d offerdir.%d\n",jprint(argjson,0),A->offer.myside,A->offer.acceptdir);
    if ( strcmp(cmdstr,"offer") == 0 ) // sender is Bob, receiver is network (Alice)
    {
        if ( A->offer.expiration < (time(NULL) + INSTANTDEX_DURATION) )
            return(clonestr("{\"error\":\"instantdex_BTCswap offer too close to expiration\"}"));
        if ( (ap= instantdex_acceptable(exchange,A,myinfo->myaddr.nxt64bits)) != 0 )
        {
            isbob = 0;
            if ( (newjson= instantdex_newjson(myinfo,&A0,&B0,sharedprivs,secret160,isbob,argjson,hash,A)) == 0 )
                return(clonestr("{\"error\":\"instantdex_BTCswap offer null newjson\"}"));
            else
            {
                //instantdex_pendingnotice(myinfo,exchange,ap,A);
                return(instantdex_bailinrefund(myinfo,other,exchange,A,"proposal",secret160,newjson,isbob,A0,B0,sharedprivs));
            }
        }
        else
        {
            printf("no matching trade.(%s)\n",jprint(argjson,0));
            if ( (str= InstantDEX_minaccept(myinfo,0,argjson,0,A->offer.base,"BTC",dstr(A->offer.price64),dstr(A->offer.basevolume64))) != 0 )
                free(str);
        }
    }
    else if ( strcmp(cmdstr,"proposal") == 0 ) // sender is Alice, receiver is Bob
    {
        isbob = 1;
        newjson = instantdex_payout(myinfo,coinbtc,exchange,A,secret160,isbob,&A0,&B0,sharedprivs,hash,satoshis,argjson);
        return(instantdex_bailinrefund(myinfo,coinbtc,exchange,A,"BTCaccept",secret160,newjson,isbob,A0,B0,sharedprivs));
    }
    else if ( strcmp(cmdstr,"accept") == 0 ) // sender is Bob, receiver is Alice
    {
        isbob = 0;
        newjson = instantdex_payout(myinfo,other,exchange,A,secret160,isbob,&A0,&B0,sharedprivs,hash,satoshis,argjson);
        return(instantdex_sendcmd(myinfo,&A->A,newjson,"BTCconfirm",swap->othertrader,INSTANTDEX_HOPS));
    }
    else if ( strcmp(cmdstr,"confirm") == 0 ) // sender is Alice, receiver is Bob
    {
        isbob = 1;
        newjson = instantdex_payout(myinfo,coinbtc,exchange,A,secret160,isbob,&A0,&B0,sharedprivs,hash,satoshis,argjson);
        return(instantdex_sendcmd(myinfo,&A->A,newjson,"BTCbroadcast",swap->othertrader,INSTANTDEX_HOPS));
    }
    else if ( strcmp(cmdstr,"broadcast") == 0 ) // sender is Bob, receiver is Alice
    {
        isbob = 0;
        return(instantdex_advance(myinfo,sharedprivs,isbob,argjson,hash,"bailintxA","BTCcommit",A));
    }
    else if ( strcmp(cmdstr,"commit") == 0 ) // sender is Alice, receiver is Bob
    {
        isbob = 1;
        // go into refund state, ie watch for payouts to complete or get refund
        return(instantdex_advance(myinfo,sharedprivs,isbob,argjson,hash,"payoutB","BTCcomplete",A));
    }
    else if ( strcmp(cmdstr,"complete") == 0 ) // sender is Bob, receiver is Alice
    {
        isbob = 0;
        // go into refund state, ie watch for payouts to complete or get refund
        return(instantdex_advance(myinfo,sharedprivs,isbob,argjson,hash,"payoutA",0,A));
    }
    else retstr = clonestr("{\"error\":\"BTC swap got unrecognized command\"}");
    if ( retstr == 0 )
        retstr = clonestr("{\"error\":\"BTC swap null retstr\"}");
    return(retstr);
}

#endif

