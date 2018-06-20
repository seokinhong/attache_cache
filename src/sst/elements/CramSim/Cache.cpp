//
// Created by Seokin Hong on 3/29/18.
//

#include "Cache.hpp"
#include "math.h"
#include "stdlib.h"
#include "assert.h"
#include "stdio.h"

using namespace SST::CACHE;

SCache::SCache(uns sets, uns assocs, uns repl_policy, uns block_size)
{
    m_cache=mcache_new(sets,assocs,repl_policy);
    m_offset = log2(block_size);
}

SCache::~SCache() {
    delete m_cache;
}

bool SCache::isHit(Addr addr, Flag dirty)
{
    Addr tag = addr >>  m_offset;
    bool isHit=mcache_access(m_cache,tag,dirty);

    return isHit;
}

MCache_Entry SCache::install(Addr addr, Flag dirty)
{
    Addr tag = addr >>  m_offset;
    MCache_Entry victim=mcache_install(m_cache,tag,dirty);
    return victim;
}
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

MCache *SCache::mcache_new(uns sets, uns assocs, uns repl_policy )
{
    MCache *c = (MCache *) calloc (1, sizeof (MCache));
    c->sets    = sets;
    c->assocs  = assocs;
    c->repl_policy = (MCache_ReplPolicy)repl_policy;

    c->entries  = (MCache_Entry *) calloc (sets * assocs, sizeof(MCache_Entry));


    c->fifo_ptr  = (uns *) calloc (sets, sizeof(uns));

    //for drrip or dip
    mcache_select_leader_sets(c,sets);
    c->psel=(MCACHE_PSEL_MAX+1)/2;


    return c;
}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

void SCache::mcache_select_leader_sets(MCache *c, uns sets){
    uns done=0;

    c->is_leader_p0  = (Flag *) calloc (sets, sizeof(Flag));
    c->is_leader_p1  = (Flag *) calloc (sets, sizeof(Flag));

    while(done <= MCACHE_LEADER_SETS){
        uns randval=rand()%sets;
        if( (c->is_leader_p0[randval]==FALSE)&&(c->is_leader_p1[randval]==FALSE)){
            c->is_leader_p0[randval]=TRUE;
            done++;
        }
    }

    done=0;
    while(done <= MCACHE_LEADER_SETS){
        uns randval=rand()%sets;
        if( (c->is_leader_p0[randval]==FALSE)&&(c->is_leader_p1[randval]==FALSE)){
            c->is_leader_p1[randval]=TRUE;
            done++;
        }
    }
}



bool SCache::mcache_access(MCache *c, Addr addr, Flag dirty)
{
    Addr  tag  = addr; // full tags
    uns   set  = mcache_get_index(c,addr);
    uns   start = set * c->assocs;
    uns   end   = start + c->assocs;
    uns   ii;

    c->s_count++;

    for (ii=start; ii<end; ii++){
        MCache_Entry *entry = &c->entries[ii];

        if(entry->valid && (entry->tag == tag))
        {
            entry->last_access  = c->s_count;
            entry->ripctr       = MCACHE_SRRIP_MAX;
            c->touched_wayid = (ii-start);
            c->touched_setid = set;
            c->touched_lineid = ii;
            if(dirty==TRUE) //If the operation is a WB then mark it as dirty
            {
                mcache_mark_dirty(c,tag);
            }
            return true;
        }
    }

    //even on a miss, we need to know which set was accessed
    c->touched_wayid = 0;
    c->touched_setid = set;
    c->touched_lineid = start;

    c->s_miss++;
    return false;
}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

Flag    SCache::mcache_probe    (MCache *c, Addr addr)
{
    Addr  tag  = addr; // full tags
    uns   set  = mcache_get_index(c,addr);
    uns   start = set * c->assocs;
    uns   end   = start + c->assocs;
    uns   ii;

    for (ii=start; ii<end; ii++){
        MCache_Entry *entry = &c->entries[ii];
        if(entry->valid && (entry->tag == tag))
        {
            return TRUE;
        }
    }

    return FALSE;
}


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

Flag    SCache::mcache_invalidate    (MCache *c, Addr addr)
{
    Addr  tag  = addr; // full tags
    uns   set  = mcache_get_index(c,addr);
    uns   start = set * c->assocs;
    uns   end   = start + c->assocs;
    uns   ii;

    for (ii=start; ii<end; ii++){
        MCache_Entry *entry = &c->entries[ii];
        if(entry->valid && (entry->tag == tag))
        {
            entry->valid = FALSE;
            return TRUE;
        }
    }

    return FALSE;
}


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

void    SCache::mcache_swap_lines(MCache *c, uns set, uns way_ii, uns way_jj)
{
    uns   start = set * c->assocs;
    uns   loc_ii   = start + way_ii;
    uns   loc_jj   = start + way_jj;

    MCache_Entry tmp = c->entries[loc_ii];
    c->entries[loc_ii] = c->entries[loc_jj];
    c->entries[loc_jj] = tmp;

}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

Flag    SCache::mcache_mark_dirty    (MCache *c, Addr addr)
{
    Addr  tag  = addr; // full tags
    uns   set  = mcache_get_index(c,addr);
    uns   start = set * c->assocs;
    uns   end   = start + c->assocs;
    uns   ii;

    for (ii=start; ii<end; ii++){
        MCache_Entry *entry = &c->entries[ii];
        if(entry->valid && (entry->tag == tag))
        {
            entry->dirty = TRUE;
            return TRUE;
        }
    }

    return FALSE;
}

////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

MCache_Entry SCache::mcache_install(MCache *c, Addr addr, Flag dirty)
{
    Addr  tag  = addr; // full tags
    uns   set  = mcache_get_index(c,addr);
    uns   start = set * c->assocs;
    uns   end   = start + c->assocs;
    uns   ii, victim;

    Flag update_lrubits=TRUE;

    MCache_Entry *entry;
    MCache_Entry evicted_entry;

    for (ii=start; ii<end; ii++){
        entry = &c->entries[ii];
        if(entry->valid && (entry->tag == tag)){
            printf("Installed entry already with addr:%llx present in set:%u\n", addr, set);
            fflush(stdout);
            exit(-1);
        }
    }

    // find victim and install entry
    victim = mcache_find_victim(c, set);
    entry = &c->entries[victim];
    evicted_entry =c->entries[victim];
    if(entry->valid){
        c->s_evict++;
    }

    //udpate DRRIP info and select value of ripctr
    uns ripctr_val=MCACHE_SRRIP_INIT;

    if(c->repl_policy==REPL_DRRIP){
        ripctr_val=mcache_drrip_get_ripctrval(c,set);
    }

    if(c->repl_policy==REPL_DIP){
        update_lrubits=mcache_dip_check_lru_update(c,set);
    }


    //put new information in
    entry->tag   = tag;
    entry->valid = TRUE;
    if(dirty==TRUE)
        entry->dirty=TRUE;
    else
        entry->dirty = FALSE;
    entry->ripctr  = ripctr_val;

    if(update_lrubits){
        entry->last_access  = c->s_count;
    }



    c->fifo_ptr[set] = (c->fifo_ptr[set]+1)%c->assocs; // fifo update

    c->touched_lineid=victim;
    c->touched_setid=set;
    c->touched_wayid=victim-(set*c->assocs);
    return evicted_entry;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
Flag SCache::mcache_dip_check_lru_update(MCache *c, uns set){
    Flag update_lru=TRUE;

    if(c->is_leader_p0[set]){
        if(c->psel<MCACHE_PSEL_MAX){
            c->psel++;
        }
        update_lru=FALSE;
        if(rand()%100<5) update_lru=TRUE; // BIP
    }

    if(c->is_leader_p1[set]){
        if(c->psel){
            c->psel--;
        }
        update_lru=1;
    }

    if( (c->is_leader_p0[set]==FALSE)&& (c->is_leader_p1[set]==FALSE)){
        if(c->psel >= (MCACHE_PSEL_MAX+1)/2){
            update_lru=1; // policy 1 wins
        }else{
            update_lru=FALSE; // policy 0 wins
            if(rand()%100<5) update_lru=TRUE; // BIP
        }
    }

    return update_lru;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
uns SCache::mcache_drrip_get_ripctrval(MCache *c, uns set){
    uns ripctr_val=MCACHE_SRRIP_INIT;

    if(c->is_leader_p0[set]){
        if(c->psel<MCACHE_PSEL_MAX){
            c->psel++;
        }
        ripctr_val=0;
        if(rand()%100<5) ripctr_val=1; // BIP
    }

    if(c->is_leader_p1[set]){
        if(c->psel){
            c->psel--;
        }
        ripctr_val=1;
    }

    if( (c->is_leader_p0[set]==FALSE)&& (c->is_leader_p1[set]==FALSE)){
        if(c->psel >= (MCACHE_PSEL_MAX+1)/2){
            ripctr_val=1; // policy 1 wins
        }else{
            ripctr_val=0; // policy 0 wins
            if(rand()%100<5) ripctr_val=1; // BIP
        }
    }


    return ripctr_val;
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns SCache::mcache_find_victim (MCache *c, uns set)
{
    int ii;
    int start = set   * c->assocs;
    int end   = start + c->assocs;

    //search for invalid first
    for (ii = start; ii < end; ii++){
        if(!c->entries[ii].valid){
            return ii;
        }
    }


    switch(c->repl_policy){
        case REPL_LRU:
            return mcache_find_victim_lru(c, set);
        case REPL_RND:
            return mcache_find_victim_rnd(c, set);
        case REPL_SRRIP:
            return mcache_find_victim_srrip(c, set);
        case REPL_DRRIP:
            return mcache_find_victim_srrip(c, set);
        case REPL_FIFO:
            return mcache_find_victim_fifo(c, set);
        case REPL_DIP:
            return mcache_find_victim_lru(c, set);
        default:
            assert(0);
    }

    return -1;

}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns SCache::mcache_find_victim_lru (MCache *c,  uns set)
{
    uns start = set   * c->assocs;
    uns end   = start + c->assocs;
    uns lowest=start;
    uns ii;


    for (ii = start; ii < end; ii++){
        if (c->entries[ii].last_access < c->entries[lowest].last_access){
            lowest = ii;
        }
    }

    return lowest;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns SCache::mcache_find_victim_rnd (MCache *c,  uns set)
{
    uns start = set   * c->assocs;
    uns victim = start + rand()%c->assocs;

    return  victim;
}



////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns SCache::mcache_find_victim_srrip (MCache *c,  uns set)
{
    uns start = set   * c->assocs;
    uns end   = start + c->assocs;
    uns ii;
    uns victim = end; // init to impossible

    while(victim == end){
        for (ii = start; ii < end; ii++){
            if (c->entries[ii].ripctr == 0){
                victim = ii;
                break;
            }
        }

        if(victim == end){
            for (ii = start; ii < end; ii++){
                c->entries[ii].ripctr--;
            }
        }
    }

    return  victim;
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns SCache::mcache_find_victim_fifo (MCache *c,  uns set)
{
    uns start = set   * c->assocs;
    uns retval = start + c->fifo_ptr[set];
    return retval;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns SCache::mcache_get_index(MCache *c, Addr addr){
    uns retval;

    switch(c->index_policy){
        case 0:
            retval=addr%c->sets;
            break;

        default:
            exit(-1);
    }

    return retval;
}

