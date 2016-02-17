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
#define PUBKEY_ADDRESS_BTC 0
#define SCRIPT_ADDRESS_BTC 5
#define PRIVKEY_ADDRESS_BTC 128
#define PUBKEY_ADDRESS_BTCD 60
#define SCRIPT_ADDRESS_BTCD 85
#define PRIVKEY_ADDRESS_BTCD 188
#define PUBKEY_ADDRESS_TEST 111
#define SCRIPT_ADDRESS_TEST 196
#define PRIVKEY_ADDRESS_TEST 239

static struct iguana_chain Chains[] =
{
	//[CHAIN_TESTNET3] =
    {
		//CHAIN_TESTNET3,
        "testnet3", "tBTC",
		PUBKEY_ADDRESS_TEST, SCRIPT_ADDRESS_TEST, PRIVKEY_ADDRESS_TEST,
		"\x0b\x11\x09\x07",
        "000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943",
        "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4adae5494dffff001d1aa4ae180101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4d04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000",
        18333,18334,0,
    },
    //[CHAIN_BITCOIN] =
    {
		//CHAIN_BITCOIN,
        "bitcoin", "BTC",
		0, 5, 0x80,
		"\xf9\xbe\xb4\xd9",
        "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f",
        "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c0101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4d04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000",
        8333,8334,0,0x1d,
        { { 210000, (50 * SATOSHIDEN) }, { 420000, (50 * SATOSHIDEN) / 2 }, { 630000, (50 * SATOSHIDEN) / 4 },{ 840000, (50 * SATOSHIDEN) / 8 },
        }
	},
	//[CHAIN_BTCD] =
    {
		//CHAIN_BTCD,
        "btcd", "BTCD",
		PUBKEY_ADDRESS_BTCD, SCRIPT_ADDRESS_BTCD, PRIVKEY_ADDRESS_BTCD,
		"\xe4\xc2\xd8\xe6",
        "0000044966f40703b516c5af180582d53f783bfd319bb045e2dc3e05ea695d46",
        "0100000000000000000000000000000000000000000000000000000000000000000000002b5b9d8cdd624d25ce670a7aa34726858388da010d4ca9ec8fd86369cc5117fd0132a253ffff0f1ec58c7f0000",
        //       "0100000000000000000000000000000000000000000000000000000000000000000000002b5b9d8cdd624d25ce670a7aa34726858388da010d4ca9ec8fd86369cc5117fd0132a253ffff0f1ec58c7f0001010000000132a253010000000000000000000000000000000000000000000000000000000000000000ffffffff4100012a3d3138204a756e652032303134202d204269746f696e20796f75722077617920746f206120646f75626c6520657370726573736f202d20636e6e2e636f6dffffffff010000000000000000000000000000",
        14631,14632,1,0x1e,
        { { 12000, (80 * SATOSHIDEN) }, }
    },
	//[CHAIN_VPN] =
    {
        "vpncoin", "VPN",
		71, 5, 199, // PUBKEY_ADDRESS + SCRIPT_ADDRESS addrman.h, use wif2priv API on any valid wif
		"\xfb\xc0\xb6\xdb", // pchMessageStart main.cpp
        //"aaea16b9b820180153d9cd069dbfd54764f07cb49c71987163132a72d568cb14",
        "00000ac7d764e7119da60d3c832b1d4458da9bc9ef9d5dd0d91a15f690a46d99", // hashGenesisBlock main.h
        "01000000000000000000000000000000000000000000000000000000000000000000000028581b3ba53e73adaaf957bced1d42d46ed0d84a86b34f7a5a49cdcaa1938a6940540854ffff0f1e78b20100010100000040540854010000000000000000000000000000000000000000000000000000000000000000ffffffff2404ffff001d01041c5468752c20342053657020323031342031323a30303a303020474d54ffffffff01000000000000000000000000000000",
        1920,1921,1,0x1e // port and rpcport vpncoin.conf
    },
	//[CHAIN_LTC] =
    {
        "litecoin", "LTC",
		0, 5, 176, // PUBKEY_ADDRESS + SCRIPT_ADDRESS addrman.h, use wif2priv API on any valid wif
		"\xfb\xc0\xb6\xdb", // pchMessageStart main.cpp
        //"12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195a7e7e04bfe2",
        "12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195a7e7e04bfe2",
        "010000000000000000000000000000000000000000000000000000000000000000000000d9ced4ed1130f7b7faad9be25323ffafa33232a17c3edf6cfd97bee6bafbdd97b9aa8e4ef0ff0f1ecd513f7c0101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4804ffff001d0104404e592054696d65732030352f4f63742f32303131205374657665204a6f62732c204170706c65e280997320566973696f6e6172792c2044696573206174203536ffffffff0100f2052a010000004341040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9ac00000000",
        9333,9334,0,0x1e // port and rpcport vpncoin.conf
    },
};

/*
// PUBKEY_ADDRESS + SCRIPT_ADDRESS addrman.h
// PRIVKEY_ADDRESS use wif2priv API on any valid wif
// networkmagic pchMessageStart main.cpp
// genesis block from any blockexplorer, calendar strings can be converted by utime2utc
{
    "name":"BitcoinDark","symbol":"BTCD",
    "PUBKEY_ADDRESS":60,"SCRIPT_ADDRESS":85,"PRIVKEY_ADDRESS":188,
    "networkmagic":"e4c2d8e6","portp2p:14631,"portrpc":14632,"txhastimestamp":1,
    "genesis":{"version":1,"timestamp":1403138561,"nBits":"1e0fffff","nonce":8359109,"hash":"0000044966f40703b516c5af180582d53f783bfd319bb045e2dc3e05ea695d46","merkle":"fd1751cc6963d88feca94c0d01da8883852647a37a0a67ce254d62dd8c9d5b2b"}
}*/

bits256 iguana_chaingenesis(int32_t version,uint32_t timestamp,uint32_t bits,uint32_t nonce,bits256 merkle_root)
{
    struct iguana_msgblock msg; int32_t len; bits256 hash2; uint8_t serialized[1024]; char hexstr[2049];
    memset(&msg,0,sizeof(msg));
    msg.H.version = version;
    msg.H.merkle_root = merkle_root;
    msg.H.timestamp = timestamp;
    msg.H.bits = bits;
    msg.H.nonce = nonce;
    len = iguana_rwblock(1,&hash2,serialized,&msg);
    init_hexbytes_noT(hexstr,serialized,len);
    char str[65],str2[65]; printf("v.%d t.%u bits.%x nonce.%u merkle.(%s) genesis.(%s) hash.(%s) size.%ld\n",version,timestamp,bits,nonce,bits256_str(str2,merkle_root),hexstr,bits256_str(str,hash2),strlen(hexstr)/2);
    return(hash2);
}

void iguana_chaininit(struct iguana_chain *chain,int32_t hasheaders)
{
    chain->hasheaders = hasheaders;
    chain->minoutput = 10000;
    if ( strcmp(chain->symbol,"BTC") == 0 )
    {
        chain->unitval = 0x1d;
        chain->txfee = 10000;
    }
    else
    {
        if ( strcmp(chain->symbol,"LTC") == 0 )
            chain->txfee = 100000;
        else chain->txfee = 1000000;
        if ( chain->unitval == 0 )
            chain->unitval = 0x1e;
    }
    if ( hasheaders != 0 )
    {
        strcpy(chain->gethdrsmsg,"getheaders");
        chain->bundlesize = _IGUANA_HDRSCOUNT;
    }
    else
    {
        strcpy(chain->gethdrsmsg,"getblocks");
        chain->bundlesize = _IGUANA_BLOCKHASHES;
    }
    decode_hex((uint8_t *)chain->genesis_hashdata,32,(char *)chain->genesis_hash);
    if ( chain->ramchainport == 0 )
        chain->ramchainport = chain->portp2p - 1;
    if ( chain->portrpc == 0 )
        chain->portrpc = chain->portp2p + 1;
}

struct iguana_chain *iguana_chainfind(char *name)
{
    struct iguana_chain *chain; uint32_t i;
	for (i=0; i<sizeof(Chains)/sizeof(*Chains); i++)
    {
		chain = &Chains[i];
        printf("chain.(%s).%s vs %s.%d\n",chain->genesis_hash,chain->name,name,strcmp(name,chain->name));
		if ( chain->name[0] == 0 || chain->genesis_hash == 0 )
			continue;
		if ( strcmp(name,chain->symbol) == 0 )
        {
            printf("found.(%s)\n",name);
            iguana_chaininit(chain,strcmp(chain->symbol,"BTC") == 0);
            return(chain);
        }
	}
	return NULL;
}

struct iguana_chain *iguana_findmagic(uint8_t netmagic[4])
{
    struct iguana_chain *chain; uint8_t i;
	for (i=0; i<sizeof(Chains)/sizeof(*Chains); i++)
    {
		chain = &Chains[i];
		if ( chain->name[0] == 0 || chain->genesis_hash == 0 )
			continue;
		if ( memcmp(netmagic,chain->netmagic,4) == 0 )
			return(iguana_chainfind((char *)chain->symbol));
	}
	return NULL;
}

uint64_t iguana_miningreward(struct iguana_info *coin,uint32_t blocknum)
{
    int32_t i; uint64_t reward = 50LL * SATOSHIDEN;
    for (i=0; i<sizeof(coin->chain->rewards)/sizeof(*coin->chain->rewards); i++)
    {
        //printf("%d: %u %.8f\n",i,(int32_t)coin->chain->rewards[i][0],dstr(coin->chain->rewards[i][1]));
        if ( blocknum >= coin->chain->rewards[i][0] )
            reward = coin->chain->rewards[i][1];
        else break;
    }
    return(reward);
}

struct iguana_chain *iguana_createchain(cJSON *json)
{
    char *symbol,*name,*hexstr; cJSON *rewards,*rpair,*item; int32_t i,m,n; struct iguana_chain *chain = 0;
    if ( (symbol= jstr(json,"name")) != 0 && strlen(symbol) < 8 )
    {
        chain = mycalloc('C',1,sizeof(*chain));
        strcpy(chain->symbol,symbol);
        if ( (name= jstr(json,"description")) != 0 && strlen(name) < 32 )
            strcpy(chain->name,name);
        if ( (hexstr= jstr(json,"pubval")) != 0 && strlen(hexstr) == 2 )
            decode_hex((uint8_t *)&chain->pubtype,1,hexstr);
        if ( (hexstr= jstr(json,"scriptval")) != 0 && strlen(hexstr) == 2 )
            decode_hex((uint8_t *)&chain->p2shtype,1,hexstr);
        if ( (hexstr= jstr(json,"wiftype")) != 0 && strlen(hexstr) == 2 )
            decode_hex((uint8_t *)&chain->wiftype,1,hexstr);
        if ( (hexstr= jstr(json,"netmagic")) != 0 && strlen(hexstr) == 8 )
            decode_hex((uint8_t *)chain->netmagic,1,hexstr);
        if ( (hexstr= jstr(json,"unitval")) != 0 && strlen(hexstr) == 2 )
            decode_hex((uint8_t *)&chain->unitval,1,hexstr);
        if ( (hexstr= jstr(json,"genesishash")) != 0 )
        {
            chain->genesis_hash = mycalloc('G',1,strlen(hexstr)+1);
            strcpy(chain->genesis_hash,hexstr);
        }
        if ( (hexstr= jstr(json,"genesisblock")) != 0 )
        {
            chain->genesis_hex = mycalloc('G',1,strlen(hexstr)+1);
            strcpy(chain->genesis_hex,hexstr);
        }
        chain->portp2p = juint(json,"p2p");
        if ( (chain->ramchainport= juint(json,"ramchain")) == 0 )
            chain->ramchainport = chain->portp2p - 1;
        if ( (chain->portrpc= juint(json,"rpc")) == 0 )
            chain->portrpc = chain->portp2p + 1;
        chain->hastimestamp = juint(json,"hastimestamp");
        if ( (rewards= jarray(&n,json,"rewards")) != 0 )
        {
            for (i=0; i<n; i++)
            {
                item = jitem(rewards,i);
                if ( (rpair= jarray(&m,item,0)) != 0 && m == 0 )
                {
                    chain->rewards[i][0] = j64bits(jitem(rpair,0),0);
                    chain->rewards[i][1] = j64bits(jitem(rpair,1),0);
                }
            }
        }
        iguana_chaininit(chain,juint(json,"hasheaders"));
    }
    return(chain);
}
