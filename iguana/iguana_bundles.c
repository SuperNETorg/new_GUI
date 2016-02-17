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

#include "iguana777.h"

static uint16_t iguana_primes[] = { 65353, 65357, 65371, 65381, 65393, 65407, 65413, 65419, 65423, 65437, 65447, 65449, 65479, 65497, 65519, 65521 };

struct iguana_bloominds { uint16_t inds[8]; };

struct iguana_bloominds iguana_calcbloom(bits256 hash2)
{
    int32_t i,j,k; struct iguana_bloominds bit;
    k = (int32_t)(sizeof(bit)/sizeof(uint16_t)) - 1;
    j = 15;
    for (i=0; i<sizeof(bit)/sizeof(uint16_t); i++,j--,k--)
        bit.inds[i] = (hash2.ushorts[j] % iguana_primes[k]);//, printf("%d ",bit.inds[i]);
    //printf("bit.inds\n");
    return(bit);
}

struct iguana_bloominds iguana_bloomset(struct iguana_info *coin,struct iguana_bloom16 *bloom,int32_t incr,struct iguana_bloominds bit)
{
    int32_t i,alreadyset;
    for (alreadyset=i=0; i<sizeof(bit)/sizeof(uint16_t); i++,bloom+=incr)
    {
        if ( GETBIT(bloom->hash2bits,bit.inds[i]) == 0 )
            SETBIT(bloom->hash2bits,bit.inds[i]);
        else alreadyset++;
    }
    if ( alreadyset == i )
        printf("iguana_bloomset: collision\n");
    return(bit);
}

int32_t iguana_bloomfind(struct iguana_info *coin,struct iguana_bloom16 *bloom,int32_t incr,struct iguana_bloominds bit)
{
    int32_t i;
    coin->bloomsearches++;
    for (i=0; i<sizeof(bit)/sizeof(uint16_t); i++,bloom+=incr)
        if ( GETBIT(bloom->hash2bits,bit.inds[i]) == 0 )
            return(-1);
    coin->bloomhits++;
    return(0);
}

int32_t iguana_bundlescan(struct iguana_info *coin,struct iguana_bundle *bp,bits256 hash2)
{
    int32_t bundlei;
    for (bundlei=0; bundlei<bp->n; bundlei++)
    {
        if ( memcmp(hash2.bytes,bp->hashes[bundlei].bytes,sizeof(hash2)) == 0 )
        {
            //char str[65]; printf("hdrsi.%d scan.%s found %d of %d\n",bp->hdrsi,bits256_str(str,hash2),bundlei,bp->n);
            return(bundlei);
        }
    }
    return(-2);
}

struct iguana_bundle *iguana_bundlefind(struct iguana_info *coin,struct iguana_bundle **bpp,int32_t *bundleip,bits256 hash2)
{
    int32_t i; struct iguana_bloominds bit; struct iguana_bundle *bp = *bpp;
    bit = iguana_calcbloom(hash2);
    if ( bp == 0 )
    {
        for (i=coin->bundlescount-1; i>=0; i--)
        {
            if ( (bp= coin->bundles[i]) != 0 )
            {
                if ( iguana_bloomfind(coin,&bp->bloom,0,bit) == 0 )
                {
                    *bpp = bp;
                    if ( (*bundleip= iguana_bundlescan(coin,bp,hash2)) < 0 )
                    {
                        //printf("bloom miss\n");
                        coin->bloomfalse++;
                    }
                    else return(bp);
                } //else printf("no bloom\n");
            }
        }
        *bundleip = -2;
        *bpp = 0;
        return(0);
    }
    else if ( iguana_bloomfind(coin,&bp->bloom,0,bit) == 0 )
    {
        if ( (*bundleip= iguana_bundlescan(coin,bp,hash2)) >= 0 )
        {
            *bpp = bp;
            return(bp);
        } else printf("scan miss\n");
    }
    *bpp = 0;
    *bundleip = -2;
    return(0);
}

bits256 *iguana_bundleihash2p(struct iguana_info *coin,int32_t *isinsidep,struct iguana_bundle *bp,int32_t bundlei)
{
    *isinsidep = 0;
    if ( bundlei >= 0 && bundlei < coin->chain->bundlesize )
    {
        *isinsidep = 1;
        return(&bp->hashes[bundlei]);
    }
    else if ( bundlei == -1 )
        return(&bp->prevbundlehash2);
    else if ( bundlei == coin->chain->bundlesize)
        return(&bp->nextbundlehash2);
    else return(0);
}

int32_t iguana_hash2set(struct iguana_info *coin,char *debugstr,struct iguana_bundle *bp,int32_t bundlei,bits256 newhash2)
{
    int32_t isinside,checki,retval = -1; bits256 *orighash2p = 0; struct iguana_bundle *checkbp; char str[65]; struct iguana_bloominds bit;
    if ( bp == 0 )
        return(-1);
    if ( bp->n <= bundlei )
    {
        printf("hash2set.%s [%d] of %d <- %s\n",debugstr,bundlei,bp->n,bits256_str(str,newhash2));
        bp->n = coin->chain->bundlesize;
    }
    if ( bits256_nonz(newhash2) == 0 || (orighash2p= iguana_bundleihash2p(coin,&isinside,bp,bundlei)) == 0 )
    {
        printf("iguana_hash2set warning: bundlei.%d newhash2.%s orighash2p.%p\n",bundlei,bits256_str(str,newhash2),orighash2p);
        *orighash2p = newhash2;
      //getchar();
        return(-1);
    }
    if ( bits256_nonz(*orighash2p) > 0 && memcmp(newhash2.bytes,orighash2p,sizeof(bits256)) != 0 )
    {
        char str2[65],str3[65];
        bits256_str(str2,*orighash2p), bits256_str(str3,newhash2);
        printf("ERRRO iguana_hash2set overwrite [%s] %s with %s [%d:%d]\n",debugstr,str2,str3,bp->hdrsi,bundlei);
        *orighash2p = newhash2;
        getchar();
        return(-1);
    }
    if ( isinside != 0 )
    {
        bit = iguana_calcbloom(newhash2);
        if ( iguana_bloomfind(coin,&bp->bloom,0,bit) < 0 )
        {
            //printf("bloomset (%s)\n",bits256_str(str,newhash2));
            iguana_bloomset(coin,&bp->bloom,0,bit);
            if ( 0 )
            {
                int32_t i;
                if ( iguana_bloomfind(coin,&bp->bloom,0,bit) < 0 )
                {
                    for (i=0; i<8; i++)
                        printf("%d ",bit.inds[i]);
                    printf("cant bloomfind just bloomset\n");
                }
                else
                {
                    *orighash2p = newhash2;
                    checkbp = bp, checki = -2;
                    if ( iguana_bundlefind(coin,&checkbp,&checki,newhash2) == 0 || checki != bundlei )
                    {
                        printf("cant iguana_bundlefind just added.(%s) bundlei.%d %p vs checki.%d %p\n",bits256_str(str,newhash2),bundlei,bp,checki,checkbp);
                    }
                    else if ( (coin->bloomsearches % 100000) == 0 )
                        printf("BLOOM SUCCESS %.2f%% FP.%d/%d collisions.%d\n",100.*(double)coin->bloomhits/coin->bloomsearches,(int32_t)coin->bloomfalse,(int32_t)coin->bloomsearches,(int32_t)coin->collisions);
                }
            }
        } //else printf("bloom found\n");
        retval = 0;
    } else retval = (bundlei >= 0 && bundlei < coin->chain->bundlesize) ? 0 : 1;
    //printf("set [%d] <- %s\n",bundlei,bits256_str(str,newhash2));
    *orighash2p = newhash2;
    return(retval);
}

int32_t iguana_bundlehash2add(struct iguana_info *coin,struct iguana_block **blockp,struct iguana_bundle *bp,int32_t bundlei,bits256 hash2)
{
    struct iguana_block *block =0; struct iguana_bundle *otherbp; //
    int32_t otherbundlei,setval,bundlesize,err = 0;
    if ( blockp != 0 )
        *blockp = 0;
    if ( bp == 0 )
        return(-1111);
    if ( bits256_nonz(hash2) > 0 && (block= iguana_blockhashset(coin,-1,hash2,1)) != 0 )
    {
        /*if ( (block->hdrsi != bp->hdrsi || block->bundlei != bundlei) && (block->hdrsi != 0 || block->bundlei != 0) )
        {
            return(-2);
        }*/
        bundlesize = coin->chain->bundlesize;
        if ( bp->n > bundlesize )
        {
            printf("bp->n.%d is too big\n",bp->n);
            return(0);
        }
        if ( bundlei >= bp->n && bundlei < coin->chain->bundlesize )
            bp->n = bundlesize;//(bundlei < bundlesize-1) ? bundlesize : (bundlei + 1);
        if ( (setval= iguana_hash2set(coin,"blockadd",bp,bundlei,hash2)) == 0 )
        {
            if ( (block->hdrsi != bp->hdrsi || block->bundlei != bundlei) && (block->hdrsi != 0 || block->bundlei != 0) )
            {
                printf("blockadd warning: %d[%d] <- %d[%d]\n",block->hdrsi,block->bundlei,bp->hdrsi,bundlei);
                err |= 2;
                exit(-1);
            }
            else
            {
//char str[65]; printf(">>>>>>>>>>>>>> bundlehash2.(%s) ht.(%d %d)\n",bits256_str(str,hash2),bp->bundleheight,bundlei);
                block->hdrsi = bp->hdrsi;
                block->bundlei = bundlei;
                bp->blocks[bundlei] = block;
                otherbp = 0;
                if ( (otherbp= iguana_bundlefind(coin,&otherbp,&otherbundlei,hash2)) != 0 || (bundlei % (bundlesize-1)) == 0)
                {
                    if ( bundlei == 0 && (otherbundlei == -2 || otherbundlei == bundlesize-1) )
                    {
                        if ( otherbp != 0 && iguana_hash2set(coin,"blockadd0_prev",bp,-1,otherbp->hashes[0]) != 0 )
                            err |= 4;
                        if ( otherbp != 0 && iguana_hash2set(coin,"blockadd0_next",otherbp,bundlesize,bp->hashes[0]) != 0 )
                            err |= 8;
                    }
                    else if ( bundlei == bundlesize-1 && (otherbundlei == -2 || otherbundlei == 0) )
                    {
                        if ( otherbp != 0 && iguana_hash2set(coin,"blockaddL_prev",otherbp,-1,bp->hashes[0]) != 0 )
                            err |= 16;
                        if ( otherbp != 0 && iguana_hash2set(coin,"blockaddL_next",bp,bundlesize,otherbp->hashes[0]) != 0 )
                            err |= 32;
                    }
                    //else printf("blockadd warning: %d[%d] bloomfound %d[%d]\n",bp->hdrsi,bundlei,otherbp!=0?otherbp->hdrsi:-1,otherbundlei);
                }
            }
        }
        else if ( setval == 1 )
        {
            if ( bundlei == -1 && iguana_hash2set(coin,"blockadd_m1",bp,-1,hash2) != 0 )
                err |= 4;
            if ( bundlei == bundlesize && iguana_hash2set(coin,"blockaddL_m1",bp,bundlesize,hash2) != 0 )
                err |= 4;
        }
        else if ( setval < 0 )
        {
            printf("neg setval error\n");
            err |= 64;
        }
        if ( err == 0 && blockp != 0 )
            *blockp = block;
    } else err |= 128;
    if ( err != 0 )
    {
        printf("bundlehash2add err.%d\n",err);
        while ( 1 )
            sleep(1);
        exit(-1);
    }
    return(-err);
}

struct iguana_bundle *iguana_bundlecreate(struct iguana_info *coin,int32_t *bundleip,int32_t bundleheight,bits256 bundlehash2,bits256 allhash,int32_t issueflag)
{
    char str[65]; struct iguana_bundle *bp = 0;
    if ( bits256_nonz(bundlehash2) > 0 )
    {
        bits256_str(str,bundlehash2);
        if ( iguana_bundlefind(coin,&bp,bundleip,bundlehash2) != 0 )
        {
            if ( bp->bundleheight >= 0 && bp->bundleheight != (bundleheight - *bundleip) )
                printf("bundlecreate warning: bp->bundleheight %d != %d (bundleheight %d - %d bundlei)\n",bp->bundleheight,(bundleheight - *bundleip),bundleheight,*bundleip);
            else if ( bits256_nonz(bp->allhash) == 0 )
                bp->allhash = allhash;
            return(bp);
        }
        bp = mycalloc('b',1,sizeof(*bp));
        bp->n = coin->chain->bundlesize;
        bp->hdrsi = coin->bundlescount;
        bp->bundleheight = bundleheight;
        bp->allhash = allhash;
        iguana_hash2set(coin,"create",bp,0,bundlehash2);
        if ( iguana_bundlehash2add(coin,0,bp,0,bundlehash2) == 0 )
        {
            bp->coin = coin;
            bp->avetime = coin->avetime * 2.;
            coin->bundles[coin->bundlescount] = bp;
            if ( coin->bundlescount > 0 )
                coin->bundles[coin->bundlescount-1]->nextbp = bp;
            *bundleip = 0;
            bits256_str(str,bundlehash2);
            fprintf(stderr,"{%d} ",bp->bundleheight);
            //printf("ht.%d alloc.[%d] new hdrs.%s %s\n",bp->bundleheight,coin->bundlescount,str,bits256_str(str2,allhash));
            iguana_bundlehash2add(coin,0,bp,0,bundlehash2);
            if ( issueflag != 0 )
            {
                iguana_blockQ(coin,bp,0,bundlehash2,1);
                queue_enqueue("hdrsQ",&coin->hdrsQ,queueitem(str),1);
            }
            coin->bundlescount++;
        }
        else
        {
            printf("error adding bundlehash2 bundleheight.%d\n",bundleheight);
            myfree(bp,sizeof(*bp));
            bp = 0;
        }
        return(bp);
    } else printf("cant create bundle with zerohash\n");
    //else printf("iguana_hdrscreate cant find hdr with %s or %s\n",bits256_str(bundlehash2),bits256_str2(firstblockhash2));
    return(0);
}

struct iguana_txid *iguana_bundletx(struct iguana_info *coin,struct iguana_bundle *bp,int32_t bundlei,struct iguana_txid *tx,int32_t txidind)
{
    static bits256 zero;
    int32_t hdrsi; int64_t Toffset; char fname[1024]; FILE *fp; struct iguana_ramchaindata rdata;
    iguana_peerfname(coin,&hdrsi,"DB",fname,0,bp->hashes[0],zero,bp->n);
    if ( (fp= fopen(fname,"rb")) != 0 )
    {
        fseek(fp,(long)&rdata.Toffset - (long)&rdata,SEEK_SET);
        if ( fread(&Toffset,1,sizeof(Toffset),fp) == sizeof(Toffset) )
        {
            fseek(fp,Toffset + sizeof(struct iguana_txid) * txidind,SEEK_SET);
            if ( fread(tx,1,sizeof(*tx),fp) == sizeof(*tx) )
            {
                fclose(fp);
                return(tx);
            } else printf("bundletx read error\n");
        } else printf("bundletx Toffset read error\n");
        fclose(fp);
    } else printf("bundletx couldnt open.(%s)\n",fname);
    return(0);
}

void iguana_bundlepurge(struct iguana_info *coin,struct iguana_bundle *bp)
{
    static bits256 zero;
    char fname[1024]; int32_t hdrsi,m,j; uint32_t ipbits;
    if ( bp->emitfinish > coin->startutc && bp->purgetime == 0 && time(NULL) > bp->emitfinish+30 )
    {
        for (j=m=0; j<sizeof(coin->peers.active)/sizeof(*coin->peers.active); j++)
        {
            if ( (ipbits= (uint32_t)coin->peers.active[j].ipbits) != 0 )
            {
                if ( iguana_peerfname(coin,&hdrsi,"tmp",fname,ipbits,bp->hashes[0],zero,1) >= 0 )
                {
                    if ( OS_removefile(fname,0) > 0 )
                        coin->peers.numfiles--, m++;
                }
                else printf("error removing.(%s)\n",fname);
            }
        }
        //printf("purged hdrsi.%d m.%d\n",bp->hdrsi,m);
        bp->purgetime = (uint32_t)time(NULL);
    }
}

int64_t iguana_bundlecalcs(struct iguana_info *coin,struct iguana_bundle *bp)
{
    int32_t bundlei; struct iguana_block *block;
    if ( bp->emitfinish > coin->startutc )
    {
        bp->numhashes = bp->numsaved = bp->numcached = bp->numrecv = bp->n;
        return(bp->datasize);
    }
    bp->datasize = bp->numhashes = bp->numsaved = bp->numcached = bp->numrecv = bp->minrequests = 0;
    for (bundlei=0; bundlei<bp->n; bundlei++)
    {
        if ( bits256_nonz(bp->hashes[bundlei]) > 0 )
        {
            if ( (block= bp->blocks[bundlei]) != 0 || (block= iguana_blockfind(coin,bp->hashes[bundlei])) != 0 )
            {
                bp->blocks[bundlei] = block;
                if ( bp->minrequests == 0 || (block->numrequests > 0 && block->numrequests < bp->minrequests) )
                    bp->minrequests = block->numrequests;
                if ( block->fpipbits != 0 )
                    bp->numsaved++;
                if ( block->RO.recvlen != 0 )
                {
                    bp->numrecv++;
                    bp->datasize += block->RO.recvlen;
                    if ( block->queued != 0 )
                        bp->numcached++;
                }
            }
            bp->numhashes++;
        }
    }
    bp->metric = bp->numhashes;
    return(bp->estsize);
}

void iguana_bundlestats(struct iguana_info *coin,char *str)
{
    static uint32_t lastdisp;
    int32_t i,n,dispflag,numrecv,done,numhashes,numcached,numsaved,numemit; int64_t estsize = 0;
    struct iguana_bundle *bp,*firstgap = 0;
    dispflag = (rand() % 1000) == 0;
    numrecv = numhashes = numcached = numsaved = numemit = done = 0;
    for (i=n=0; i<coin->bundlescount; i++)
    {
        if ( (bp= coin->bundles[i]) != 0 )
        {
            estsize += iguana_bundlecalcs(coin,bp);
            numhashes += bp->numhashes;
            numcached += bp->numcached;
            numrecv += bp->numrecv;
            numsaved += bp->numsaved;
            if ( bp->emitfinish != 0 )
            {
                done++;
                if ( bp->emitfinish > 1 )
                    numemit++;
                iguana_bundlepurge(coin,bp);
            } else if ( firstgap == 0 )
                firstgap = bp;
        }
    }
    coin->numremain = n;
    coin->blocksrecv = numrecv;
    char str2[65]; uint64_t tmp; int32_t diff,p = 0; struct tai difft,t = tai_now();
    for (i=0; i<IGUANA_MAXPEERS; i++)
        if ( coin->peers.active[i].usock >= 0 )
            p++;
    diff = (int32_t)time(NULL) - coin->startutc;
    difft.x = (t.x - coin->starttime.x), difft.millis = (t.millis - coin->starttime.millis);
    tmp = (difft.millis * 1000000);
    tmp %= 1000000000;
    difft.millis = ((double)tmp / 1000000.);
    sprintf(str,"N[%d] Q.%d h.%d r.%d c.%d:%d:%d s.%d d.%d E.%d:%d M.%d L.%d est.%d %s %d:%02d:%02d %03.3f peers.%d/%d Q.(%d %d)",coin->bundlescount,coin->numbundlesQ,numhashes,coin->blocksrecv,coin->numcached,numcached,coin->cachefreed,numsaved,done,numemit,coin->numreqsent,coin->blocks.hwmchain.height,coin->longestchain,coin->MAXBUNDLES,mbstr(str2,estsize),(int32_t)difft.x/3600,(int32_t)(difft.x/60)%60,(int32_t)difft.x%60,difft.millis,p,coin->MAXPEERS,queue_size(&coin->priorityQ),queue_size(&coin->blocksQ));
    //sprintf(str+strlen(str),"%s.%-2d %s time %.2f files.%d Q.%d %d\n",coin->symbol,flag,str,(double)(time(NULL)-coin->starttime)/60.,coin->peers.numfiles,queue_size(&coin->priorityQ),queue_size(&coin->blocksQ));
    if ( time(NULL) > lastdisp+1000 )
    {
        printf("%s\n",str);
        //myallocated(0,0);
        lastdisp = (uint32_t)time(NULL);
    }
    strcpy(coin->statusstr,str);
    coin->estsize = estsize;
    if ( firstgap != 0 && firstgap->queued == 0 )
        iguana_bundleQ(coin,firstgap,1000);
}

