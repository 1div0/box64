#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <signal.h>

#include "box64context.h"
#include "elfloader.h"
#include "debug.h"
#include "x64trace.h"
#include "x64emu.h"
#include "librarian.h"
#include "bridge.h"
#include "library.h"
#include "callback.h"
#include "wrapper.h"
#include "threads.h"
#include "x64trace.h"
#include "signals.h"
#include <sys/mman.h>
#include "custommem.h"
#include "khash.h"
#include "threads.h"
#ifdef DYNAREC
#include "dynablock.h"
#include "dynarec/arm64_lock.h"

//#define USE_MMAP

// init inside dynablocks.c
KHASH_MAP_INIT_INT64(dynablocks, dynablock_t*)
static dynablocklist_t***  dynmap123[1<<DYNAMAP_SHIFT]; // 64bits.. in 4x16bits array
static pthread_mutex_t     mutex_mmap;
static mmaplist_t          *mmaplist = NULL;
static size_t              mmapsize = 0;
static size_t              mmapcap = 0;
static kh_dynablocks_t     *dblist_oversized;      // store the list of oversized dynablocks (normal sized are inside mmaplist)
static uintptr_t***        box64_jmptbl3[1<<JMPTABL_SHIFT];
static uintptr_t**         box64_jmptbldefault2[1<<JMPTABL_SHIFT];
static uintptr_t*          box64_jmptbldefault1[1<<JMPTABL_SHIFT];
static uintptr_t           box64_jmptbldefault0[1<<JMPTABL_SHIFT];
#endif
#define MEMPROT_SHIFT 12
#define MEMPROT_SHIFT2 (32-MEMPROT_SHIFT)
#define MEMPROT_SIZE (1<<(32-MEMPROT_SHIFT))
KHASH_MAP_INIT_INT(memprot, uint8_t*)
static kh_memprot_t        *memprot;
static int inited = 0;

typedef struct blocklist_s {
    void*               block;
    size_t              maxfree;
    size_t              size;
} blocklist_t;

#define MMAPSIZE (256*1024)      // allocate 256kb sized blocks

static pthread_mutex_t     mutex_blocks;
static int                 n_blocks = 0;       // number of blocks for custom malloc
static blocklist_t*        p_blocks = NULL;    // actual blocks for custom malloc

typedef union mark_s {
    struct {
        unsigned int    size:31;
        unsigned int    fill:1;
    };
    uint32_t            x32;
} mark_t;
typedef struct blockmark_s {
    mark_t  prev;
    mark_t  next;
} blockmark_t;

#define NEXT_BLOCK(b) (blockmark_t*)((uintptr_t)(b) + (b)->next.size + sizeof(blockmark_t))
#define PREV_BLOCK(b) (blockmark_t*)(((uintptr_t)(b) - (b)->prev.size) - sizeof(blockmark_t))
#define LAST_BLOCK(b, s) (blockmark_t*)(((uintptr_t)(b)+(s))-sizeof(blockmark_t))

static void printBlock(blockmark_t* b, void* start)
{
    printf_log(LOG_INFO, "========== Block is:\n");
    do {
        printf_log(LOG_INFO, "%c%p, fill=%d, size=0x%x (prev=%d/0x%x)\n", b==start?'*':' ', b, b->next.fill, b->next.size, b->prev.fill, b->prev.size);
        b = NEXT_BLOCK(b);
    } while(b->next.x32);
    printf_log(LOG_INFO, "===================\n");
}

// get first subblock free in block. Return NULL if no block, else first subblock free (mark included), filling size
static void* getFirstBlock(void* block, size_t maxsize, size_t* size, void* start)
{
    // get start of block
    blockmark_t *m = (blockmark_t*)((start)?start:block);
    while(m->next.x32) {    // while there is a subblock
        if(!m->next.fill && m->next.size>=maxsize) {
            *size = m->next.size;
            return m;
        }
        m = NEXT_BLOCK(m);
    }

    return NULL;
}

static size_t getMaxFreeBlock(void* block, size_t block_size, void* start)
{
    // get start of block
    if(start) {
        blockmark_t *m = (blockmark_t*)start;
        int maxsize = 0;
        while(m->next.x32) {    // while there is a subblock
            if(!m->next.fill && m->next.size>maxsize) {
                maxsize = m->next.size;
            }
            m = NEXT_BLOCK(m);
        }
        return (maxsize>=sizeof(blockmark_t))?maxsize:0;
    } else {
        blockmark_t *m = LAST_BLOCK(block, block_size); // start with the end
        int maxsize = 0;
        while(m->prev.x32) {    // while there is a subblock
            if(!m->prev.fill && m->prev.size>maxsize) {
                maxsize = m->prev.size;
                if((uintptr_t)block+maxsize>(uintptr_t)m)
                    return (maxsize>=sizeof(blockmark_t))?maxsize:0; // no block large enough left...
            }
            m = PREV_BLOCK(m);
        }
        return (maxsize>=sizeof(blockmark_t))?maxsize:0;
    }
}

static void* allocBlock(void* block, void *sub, size_t size, void** pstart)
{
    (void)block;

    blockmark_t *s = (blockmark_t*)sub;
    blockmark_t *n = NEXT_BLOCK(s);

    s->next.fill = 1;
    // check if a new mark is worth it
    if(s->next.size>size+2*sizeof(blockmark_t))
        s->next.size = size;
    blockmark_t *m = NEXT_BLOCK(s);   // this is new n
    m->prev.fill = 1;
    if(n!=m) {
        // new mark
        m->prev.size = s->next.size;
        m->next.fill = 0;
        m->next.size = ((uintptr_t)n - (uintptr_t)m) - sizeof(blockmark_t);
        n->prev.fill = 0;
        n->prev.size = m->next.size;
    }

    if(pstart && sub==*pstart) {
        // get the next free block
        m = (blockmark_t*)*pstart;
        while(m->next.fill)
            m = NEXT_BLOCK(m);
        *pstart = (void*)m;
    }
    return (void*)((uintptr_t)sub + sizeof(blockmark_t));
}
static size_t freeBlock(void *block, void* sub, void** pstart)
{
    blockmark_t *m = (blockmark_t*)block;
    blockmark_t *s = (blockmark_t*)sub;
    blockmark_t *n = NEXT_BLOCK(s);
    if(block!=sub)
        m = PREV_BLOCK(s);
    s->next.fill = 0;
    n->prev.fill = 0;
    // check if merge with previous
    if (s->prev.x32 && !s->prev.fill) {
        // remove s...
        m->next.size += s->next.size + sizeof(blockmark_t);
        n->prev.size = m->next.size;
        s = m;
    }
    // check if merge with next
    if(n->next.x32 && !n->next.fill) {
        blockmark_t *n2 = NEXT_BLOCK(n);
        //remove n
        s->next.size += n->next.size + sizeof(blockmark_t);
        n2->prev.size = s->next.size;
    }
    if(pstart && (uintptr_t)*pstart>(uintptr_t)sub) {
        *pstart = (void*)s;
    }
    // return free size at current block (might be bigger)
    return s->next.size;
}
// return 1 if block has been expanded to new size, 0 if not
static int expandBlock(void* block, void* sub, size_t newsize)
{
    (void)block;

    newsize = (newsize+3)&~3;
    blockmark_t *s = (blockmark_t*)sub;
    blockmark_t *n = NEXT_BLOCK(s);
    if(s->next.size>=newsize)
        // big enough, no shrinking...
        return 1;
    if(s->next.fill)
        return 0;   // next block is filled
    // unsigned bitfield of this length gets "promoted" to *signed* int...
    if((size_t)(s->next.size + n->next.size + sizeof(blockmark_t)) < newsize)
        return 0;   // free space too short
    // ok, doing the alloc!
    if((s->next.size+n->next.size+sizeof(blockmark_t))-newsize<2*sizeof(blockmark_t))
        s->next.size += n->next.size+sizeof(blockmark_t);
    else
        s->next.size = newsize+sizeof(blockmark_t);
    blockmark_t *m = NEXT_BLOCK(s);   // this is new n
    m->prev.fill = 1;
    m->prev.size = s->next.size;
    if(n!=m) {
        // new mark
        m->prev.fill = 1;
        m->prev.size = s->next.size;
        m->next.fill = 0;
        m->next.size = (uintptr_t)n - (uintptr_t)m;
        n->prev.fill = 0;
        n->prev.size = m->next.size;
    }
    return 1;
}
// return size of block
static size_t sizeBlock(void* sub)
{
    blockmark_t *s = (blockmark_t*)sub;
    return s->next.size;
}

void* customMalloc(size_t size)
{
    // look for free space
    void* sub = NULL;
    pthread_mutex_lock(&mutex_blocks);
    for(int i=0; i<n_blocks; ++i) {
        if(p_blocks[i].maxfree>=size) {
            size_t rsize = 0;
            sub = getFirstBlock(p_blocks[i].block, size, &rsize, NULL);
            if(sub) {
                void* ret = allocBlock(p_blocks[i].block, sub, size, NULL);
                if(rsize==p_blocks[i].maxfree)
                    p_blocks[i].maxfree = getMaxFreeBlock(p_blocks[i].block, p_blocks[i].size, NULL);
                pthread_mutex_unlock(&mutex_blocks);
                return ret;
            }
        }
    }
    // add a new block
    int i = n_blocks++;
    p_blocks = (blocklist_t*)realloc(p_blocks, n_blocks*sizeof(blocklist_t));
    size_t allocsize = MMAPSIZE;
    if(size+2*sizeof(blockmark_t)>allocsize)
        allocsize = size+2*sizeof(blockmark_t);
    #ifdef USE_MMAP
    void* p = mmap(NULL, allocsize, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    memset(p, 0, allocsize);
    #else
    void* p = calloc(1, allocsize);
    #endif
    p_blocks[i].block = p;
    p_blocks[i].size = allocsize;
    // setup marks
    blockmark_t* m = (blockmark_t*)p;
    m->prev.x32 = 0;
    m->next.fill = 0;
    m->next.size = allocsize-2*sizeof(blockmark_t);
    blockmark_t* n = NEXT_BLOCK(m);
    n->next.x32 = 0;
    n->prev.fill = 0;
    n->prev.size = m->next.size;
    // alloc 1st block
    void* ret  = allocBlock(p_blocks[i].block, p, size, NULL);
    p_blocks[i].maxfree = getMaxFreeBlock(p_blocks[i].block, p_blocks[i].size, NULL);
    pthread_mutex_unlock(&mutex_blocks);
    return ret;
}
void* customCalloc(size_t n, size_t size)
{
    size_t newsize = n*size;
    void* ret = customMalloc(newsize);
    memset(ret, 0, newsize);
    return ret;
}
void* customRealloc(void* p, size_t size)
{
    if(!p)
        return customMalloc(size);
    uintptr_t addr = (uintptr_t)p;
    pthread_mutex_lock(&mutex_blocks);
    for(int i=0; i<n_blocks; ++i) {
        if ((addr>(uintptr_t)p_blocks[i].block) 
         && (addr<((uintptr_t)p_blocks[i].block+p_blocks[i].size))) {
            void* sub = (void*)(addr-sizeof(blockmark_t));
            if(expandBlock(p_blocks[i].block, sub, size)) {
                p_blocks[i].maxfree = getMaxFreeBlock(p_blocks[i].block, p_blocks[i].size, NULL);
                pthread_mutex_unlock(&mutex_blocks);
                return p;
            }
            pthread_mutex_unlock(&mutex_blocks);
            void* newp = customMalloc(size);
            memcpy(newp, p, sizeBlock(sub));
            customFree(p);
            return newp;
        }
    }
    pthread_mutex_unlock(&mutex_blocks);
    if(n_blocks)
        dynarec_log(LOG_NONE, "Warning, block %p not found in p_blocks for realloc, malloc'ing again without free\n", (void*)addr);
    return customMalloc(size);
}
void customFree(void* p)
{
    if(!p)
        return;
    uintptr_t addr = (uintptr_t)p;
    pthread_mutex_lock(&mutex_blocks);
    for(int i=0; i<n_blocks; ++i) {
        if ((addr>(uintptr_t)p_blocks[i].block) 
         && (addr<((uintptr_t)p_blocks[i].block+p_blocks[i].size))) {
            void* sub = (void*)(addr-sizeof(blockmark_t));
            size_t newfree = freeBlock(p_blocks[i].block, sub, NULL);
            if(p_blocks[i].maxfree < newfree) p_blocks[i].maxfree = newfree;
            pthread_mutex_unlock(&mutex_blocks);
            return;
        }
    }
    pthread_mutex_unlock(&mutex_blocks);
    if(n_blocks)
        dynarec_log(LOG_NONE, "Warning, block %p not found in p_blocks for Free\n", (void*)addr);
}

#ifdef DYNAREC
typedef struct mmaplist_s {
    void*               block;
    size_t              maxfree;
    size_t              size;
    kh_dynablocks_t*    dblist;
    uint8_t*            helper;
    void*               first;  // first free block, to speed up things
    int                 locked; // don't try to add stuff on locked block
} mmaplist_t;

uintptr_t FindFreeDynarecMap(dynablock_t* db, size_t size)
{
    // look for free space
    void* sub = NULL;
    for(size_t i=0; i<mmapsize; ++i) {
        if(mmaplist[i].maxfree>=size+sizeof(blockmark_t) && !mmaplist[i].locked) {
            mmaplist[i].locked = 1;
            size_t rsize = 0;
            sub = getFirstBlock(mmaplist[i].block, size, &rsize, mmaplist[i].first);
            if(sub) {
                uintptr_t ret = (uintptr_t)allocBlock(mmaplist[i].block, sub, size, &mmaplist[i].first);
                if(rsize==mmaplist[i].maxfree) {
                    mmaplist[i].maxfree = getMaxFreeBlock(mmaplist[i].block, mmaplist[i].size, mmaplist[i].first);
                }
                kh_dynablocks_t *blocks = mmaplist[i].dblist;
                if(!blocks) {
                    blocks = mmaplist[i].dblist = kh_init(dynablocks);
                    kh_resize(dynablocks, blocks, 64);
                }
                khint_t k;
                int r;
                k = kh_put(dynablocks, blocks, (uintptr_t)ret, &r);
                kh_value(blocks, k) = db;
                int size255=(size<256)?size:255;
                for(size_t j=0; j<size255; ++j)
                    mmaplist[i].helper[(uintptr_t)ret-(uintptr_t)mmaplist[i].block+j] = j;
                if(size!=size255)
                    memset(&mmaplist[i].helper[(uintptr_t)ret-(uintptr_t)mmaplist[i].block+256], -1, size-255);
                mmaplist[i].locked = 0;
                return ret;
            } else {
                printf_log(LOG_INFO, "BOX64: Warning, sub not found, corrupted mmaplist[%zu] info?\n", i);
                if(box64_log >= LOG_DEBUG)
                    printBlock(mmaplist[i].block, mmaplist[i].first);
            }
        }
    }
    return 0;
}

uintptr_t AddNewDynarecMap(dynablock_t* db, size_t size)
{
    size_t i = mmapsize++;
    dynarec_log(LOG_DEBUG, "Ask for DynaRec Block Alloc #%zu/%zu\n", mmapsize, mmapcap);
    if(mmapsize>mmapcap) {
        mmapcap += 32;
        mmaplist = (mmaplist_t*)realloc(mmaplist, mmapcap*sizeof(mmaplist_t));
    }
    #ifndef USE_MMAP
    void *p = NULL;
    if(posix_memalign(&p, box64_pagesize, MMAPSIZE)) {
        dynarec_log(LOG_INFO, "Cannot create memory map of %d byte for dynarec block #%zu\n", MMAPSIZE, i);
        --mmapsize;
        return 0;
    }
    mprotect(p, MMAPSIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
    #else
    void* p = mmap(NULL, MMAPSIZE, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if(p==(void*)-1) {
        dynarec_log(LOG_INFO, "Cannot create memory map of %d byte for dynarec block #%zu\n", MMAPSIZE, i);
        --mmapsize;
        return 0;
    }
    #endif
    setProtection((uintptr_t)p, MMAPSIZE, PROT_READ | PROT_WRITE | PROT_EXEC);

    mmaplist[i].locked = 1;
    mmaplist[i].block = p;
    mmaplist[i].size = MMAPSIZE;
    mmaplist[i].helper = (uint8_t*)calloc(1, MMAPSIZE);
    mmaplist[i].first = p;
    // setup marks
    blockmark_t* m = (blockmark_t*)p;
    m->prev.x32 = 0;
    m->next.fill = 0;
    m->next.size = MMAPSIZE-2*sizeof(blockmark_t);
    blockmark_t* n = NEXT_BLOCK(m);
    n->next.x32 = 0;
    n->prev.fill = 0;
    n->prev.size = m->next.size;
    // alloc 1st block
    uintptr_t sub  = (uintptr_t)allocBlock(mmaplist[i].block, p, size, &mmaplist[i].first);
    mmaplist[i].maxfree = getMaxFreeBlock(mmaplist[i].block, mmaplist[i].size, mmaplist[i].first);
    kh_dynablocks_t *blocks = mmaplist[i].dblist = kh_init(dynablocks);
    kh_resize(dynablocks, blocks, 64);
    khint_t k;
    int ret;
    k = kh_put(dynablocks, blocks, (uintptr_t)sub, &ret);
    kh_value(blocks, k) = db;
    for(size_t j=0; j<size; ++j)
        mmaplist[i].helper[(uintptr_t)sub-(uintptr_t)mmaplist[i].block + j] = (j<256)?j:255;
    mmaplist[i].locked = 0;
    return sub;
}

void ActuallyFreeDynarecMap(dynablock_t* db, uintptr_t addr, size_t size)
{
    (void)db;
    if(!addr || !size)
        return;
    for(size_t i=0; i<mmapsize; ++i) {
        if ((addr>(uintptr_t)mmaplist[i].block) 
         && (addr<((uintptr_t)mmaplist[i].block+mmaplist[i].size))) {
            void* sub = (void*)(addr-sizeof(blockmark_t));
            size_t newfree = freeBlock(mmaplist[i].block, sub, &mmaplist[i].first);
            if(mmaplist[i].maxfree < newfree) mmaplist[i].maxfree = newfree;
            kh_dynablocks_t *blocks = mmaplist[i].dblist;
            if(blocks) {
                khint_t k = kh_get(dynablocks, blocks, (uintptr_t)sub);
                if(k!=kh_end(blocks))
                    kh_del(dynablocks, blocks, k);
                memset(&mmaplist[i].helper[(uintptr_t)sub-(uintptr_t)mmaplist[i].block], 0, size);
            }
            if(mmaplist[i].locked) {
                printf_log(LOG_INFO, "BOX64: Warning, Free a chunk in a locked mmaplist[%zu]\n", i);
                ++mmaplist[i].locked;
            }
            return;
        }
    }
    if(mmapsize)
        dynarec_log(LOG_NONE, "Warning, block %p (size %zu) not found in mmaplist for Free\n", (void*)addr, size);
}

dynablock_t* FindDynablockFromNativeAddress(void* addr)
{
    // look in actual list
    for(size_t i=0; i<mmapsize; ++i) {
        if ((uintptr_t)addr>=(uintptr_t)mmaplist[i].block 
        && ((uintptr_t)addr<(uintptr_t)mmaplist[i].block+mmaplist[i].size)) {
            if(!mmaplist[i].helper)
                return FindDynablockDynablocklist(addr, mmaplist[i].dblist);
            else {
                uintptr_t p = (uintptr_t)addr - (uintptr_t)mmaplist[i].block;
                while(mmaplist[i].helper[p]) p -= mmaplist[i].helper[p];
                khint_t k = kh_get(dynablocks, mmaplist[i].dblist, (uintptr_t)mmaplist[i].block + p);
                if(k!=kh_end(mmaplist[i].dblist))
                    return kh_value(mmaplist[i].dblist, k);
                return NULL;
            }
        }
    }
    // look in oversized
    return FindDynablockDynablocklist(addr, dblist_oversized);
}

uintptr_t AllocDynarecMap(dynablock_t* db, size_t size)
{
    if(!size)
        return 0;
    if(size>MMAPSIZE-2*sizeof(blockmark_t)) {
        //pthread_mutex_lock(&mutex_mmap);
        #ifndef USE_MMAP
        void *p = NULL;
        if(posix_memalign(&p, box64_pagesize, size)) {
            //pthread_mutex_unlock(&mutex_mmap);
            dynarec_log(LOG_INFO, "Cannot create dynamic map of %zu bytes\n", size);
            return 0;
        }
        mprotect(p, size, PROT_READ | PROT_WRITE | PROT_EXEC);
        #else
        void* p = mmap(NULL, size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        if(p==(void*)-1) {
            //pthread_mutex_unlock(&mutex_mmap);
            dynarec_log(LOG_INFO, "Cannot create dynamic map of %zu bytes\n", size);
            return 0;
        }
        #endif
        setProtection((uintptr_t)p, size, PROT_READ | PROT_WRITE | PROT_EXEC);
        kh_dynablocks_t *blocks = dblist_oversized;
        if(!blocks) {
            blocks = dblist_oversized = kh_init(dynablocks);
            kh_resize(dynablocks, blocks, 64);
        }
        khint_t k;
        int ret;
        k = kh_put(dynablocks, blocks, (uintptr_t)p, &ret);
        kh_value(blocks, k) = db;
        //pthread_mutex_unlock(&mutex_mmap);
        return (uintptr_t)p;
    }
    
    //pthread_mutex_lock(&mutex_mmap);

    uintptr_t ret = FindFreeDynarecMap(db, size);
    if(!ret)
        ret = AddNewDynarecMap(db, size);

    //pthread_mutex_unlock(&mutex_mmap);

    return ret;
}

void FreeDynarecMap(dynablock_t* db, uintptr_t addr, size_t size)
{
    if(!addr || !size)
        return;
    if(size>MMAPSIZE-2*sizeof(blockmark_t)) {
        //pthread_mutex_lock(&mutex_mmap);
        #ifndef USE_MMAP
        free((void*)addr);
        #else
        munmap((void*)addr, size);
        #endif
        kh_dynablocks_t *blocks = dblist_oversized;
        if(blocks) {
            khint_t k = kh_get(dynablocks, blocks, addr);
            if(k!=kh_end(blocks))
                kh_del(dynablocks, blocks, k);
        }
        //pthread_mutex_unlock(&mutex_mmap);
        return;
    }
    //pthread_mutex_lock(&mutex_mmap);
    ActuallyFreeDynarecMap(db, addr, size);
    //pthread_mutex_unlock(&mutex_mmap);
}

dynablocklist_t* getDB(uintptr_t idx)
{
    // already 16bits shifted
    uintptr_t idx3 = (idx>>32)&((1<<DYNAMAP_SHIFT)-1);
    uintptr_t idx2 = (idx>>16)&((1<<DYNAMAP_SHIFT)-1);
    uintptr_t idx1 = (idx    )&((1<<DYNAMAP_SHIFT)-1);

    if(!dynmap123[idx3])
        return NULL;
    if(!dynmap123[idx3][idx2])
        return NULL;
    return dynmap123[idx3][idx2][idx1];
}

// each dynmap is 64k of size

void addDBFromAddressRange(uintptr_t addr, size_t size)
{
    dynarec_log(LOG_DEBUG, "addDBFromAddressRange %p -> %p\n", (void*)addr, (void*)(addr+size-1));
    uintptr_t idx = (addr>>DYNAMAP_SHIFT);
    uintptr_t end = ((addr+size-1)>>DYNAMAP_SHIFT);
    for (uintptr_t i=idx; i<=end; ++i) {
        int idx3 = (i>>32)&((1<<DYNAMAP_SHIFT)-1);
        int idx2 = (i>>16)&((1<<DYNAMAP_SHIFT)-1);
        int idx1 = (i    )&((1<<DYNAMAP_SHIFT)-1);
        if(!dynmap123[idx3]) {
            dynablocklist_t*** p = (dynablocklist_t***)calloc(1<<DYNAMAP_SHIFT, sizeof(dynablocklist_t**));
            if(arm64_lock_storeifnull(&dynmap123[idx3], (uint64_t)p)!=(uint64_t)p)
                free(p);
        }
        if(!dynmap123[idx3][idx2]) {
            dynablocklist_t** p = (dynablocklist_t**)calloc(1<<DYNAMAP_SHIFT, sizeof(dynablocklist_t*));
            if(arm64_lock_storeifnull(&dynmap123[idx3][idx2], (uint64_t)p)!=(uint64_t)p)
                free(p);
        }
        if(!dynmap123[idx3][idx2][idx1]) {
            dynablocklist_t* p = NewDynablockList(i<<DYNAMAP_SHIFT, 1<<DYNAMAP_SHIFT, 0);
            if(arm64_lock_storeifnull(&dynmap123[idx3][idx2][idx1], (uint64_t)p)!=(uint64_t)p)
                FreeDynablockList(&p);
        }
    }
}

static int dynmapempty(void** mem)
{
    for (int i=0; i<(1<<DYNAMAP_SHIFT); ++i)
        if(mem[i])
            return 0;
    return 1;
}

void cleanDBFromAddressRange(uintptr_t addr, size_t size, int destroy)
{
    dynarec_log(LOG_DEBUG, "cleanDBFromAddressRange %p -> %p %s\n", (void*)addr, (void*)(addr+size-1), destroy?"destroy":"mark");
    uintptr_t idx = (addr>>DYNAMAP_SHIFT);
    uintptr_t end = ((addr+size-1)>>DYNAMAP_SHIFT);
    for (uintptr_t i=idx; i<=end; ++i) {
        int idx3 = (i>>32)&((1<<DYNAMAP_SHIFT)-1);
        int idx2 = (i>>16)&((1<<DYNAMAP_SHIFT)-1);
        int idx1 = (i    )&((1<<DYNAMAP_SHIFT)-1);
        if(dynmap123[idx3] && dynmap123[idx3][idx2]) {
            dynablocklist_t* dblist = dynmap123[idx3][idx2][idx1];
            if(dblist) {
                if(destroy) {
                    if(FreeRangeDynablock(dblist, addr, size) && 0) {    // dblist is empty, check if we can delete more...
                        // disabling this for now. It seems to cause random crash in Terraria
                        if(!arm64_lock_storeifref(&dynmap123[idx3][idx2][idx1], 0, (uint64_t)dblist)) {
                            dynablocklist_t** p = dynmap123[idx3][idx2];
                            if(dynmapempty((void**)p)) {
                                if(!arm64_lock_storeifref(&dynmap123[idx3][idx2], 0, (uint64_t)p)) {
                                    dynablocklist_t*** p2 = dynmap123[idx3];
                                    if(dynmapempty((void**)p2)) {
                                        if(!arm64_lock_storeifref(&dynmap123[idx3], 0, (uint64_t)p2)) {
                                            free(p2);
                                        }
                                    }
                                    free(p);
                                }
                            }
                            FreeDynablockList(&dblist);
                        }
                    }
                } else
                    MarkRangeDynablock(dblist, addr, size);
            }
        }
    }
}

#ifdef ARM64
void arm64_next(void);
#endif

void addJumpTableIfDefault64(void* addr, void* jmp)
{
    uintptr_t idx3, idx2, idx1, idx0;
    idx3 = (((uintptr_t)addr)>>48)&0xffff;
    idx2 = (((uintptr_t)addr)>>32)&0xffff;
    idx1 = (((uintptr_t)addr)>>16)&0xffff;
    idx0 = (((uintptr_t)addr)    )&0xffff;
    if(box64_jmptbl3[idx3] == box64_jmptbldefault2) {
        uintptr_t*** tbl = (uintptr_t***)malloc((1<<JMPTABL_SHIFT)*sizeof(uintptr_t**));
        for(int i=0; i<(1<<JMPTABL_SHIFT); ++i)
            tbl[i] = box64_jmptbldefault1;
        if(arm64_lock_storeifref(&box64_jmptbl3[idx3], (uint64_t)tbl, (uint64_t)box64_jmptbldefault2)!=(uint64_t)tbl)
            free(tbl);
    }
    if(box64_jmptbl3[idx3][idx2] == box64_jmptbldefault1) {
        uintptr_t** tbl = (uintptr_t**)malloc((1<<JMPTABL_SHIFT)*sizeof(uintptr_t*));
        for(int i=0; i<(1<<JMPTABL_SHIFT); ++i)
            tbl[i] = box64_jmptbldefault0;
        if(arm64_lock_storeifref(&box64_jmptbl3[idx3][idx2], (uint64_t)tbl, (uint64_t)box64_jmptbldefault1)!=(uint64_t)tbl)
            free(tbl);
    }
    if(box64_jmptbl3[idx3][idx2][idx1] == box64_jmptbldefault0) {
        uintptr_t* tbl = (uintptr_t*)malloc((1<<JMPTABL_SHIFT)*sizeof(uintptr_t));
        for(int i=0; i<(1<<JMPTABL_SHIFT); ++i)
            tbl[i] = (uintptr_t)arm64_next;
        if(arm64_lock_storeifref(&box64_jmptbl3[idx3][idx2][idx1], (uint64_t)tbl, (uint64_t)box64_jmptbldefault0)!=(uint64_t)tbl)
            free(tbl);
    }

    arm64_lock_storeifref(&box64_jmptbl3[idx3][idx2][idx1][idx0], (uint64_t)jmp, (uint64_t)arm64_next);
}
void setJumpTableDefault64(void* addr)
{
    uintptr_t idx3, idx2, idx1, idx0;
    idx3 = (((uintptr_t)addr)>>48)&0xffff;
    idx2 = (((uintptr_t)addr)>>32)&0xffff;
    idx1 = (((uintptr_t)addr)>>16)&0xffff;
    idx0 = (((uintptr_t)addr)    )&0xffff;
    if(box64_jmptbl3[idx3] == box64_jmptbldefault2)
        return;
    if(box64_jmptbl3[idx3][idx2] == box64_jmptbldefault1)
        return;
    if(box64_jmptbl3[idx3][idx2][idx1] == box64_jmptbldefault0)
        return;
    if(box64_jmptbl3[idx3][idx2][idx1][idx0]==(uintptr_t)arm64_next)
        return;
    box64_jmptbl3[idx3][idx2][idx1][idx0] = (uintptr_t)arm64_next;
}
int isJumpTableDefault64(void* addr)
{
    uintptr_t idx3, idx2, idx1, idx0;
    idx3 = (((uintptr_t)addr)>>48)&0xffff;
    idx2 = (((uintptr_t)addr)>>32)&0xffff;
    idx1 = (((uintptr_t)addr)>>16)&0xffff;
    idx0 = (((uintptr_t)addr)    )&0xffff;
    if(box64_jmptbl3[idx3] == box64_jmptbldefault2)
        return 1;
    if(box64_jmptbl3[idx3][idx2] == box64_jmptbldefault1)
        return 1;
    if(box64_jmptbl3[idx3][idx2][idx1] == box64_jmptbldefault0)
        return 1;
    if(box64_jmptbl3[idx3][idx2][idx1][idx0]==(uintptr_t)arm64_next)
        return 1;
    return (box64_jmptbl3[idx3][idx2][idx1][idx0]==(uintptr_t)arm64_next)?1:0;
}
uintptr_t getJumpTable64()
{
    return (uintptr_t)box64_jmptbl3;
}

uintptr_t getJumpTableAddress64(uintptr_t addr)
{
    uintptr_t idx3, idx2, idx1, idx0;
    idx3 = ((addr)>>48)&0xffff;
    idx2 = ((addr)>>32)&0xffff;
    idx1 = ((addr)>>16)&0xffff;
    idx0 = ((addr)    )&0xffff;
    if(box64_jmptbl3[idx3] == box64_jmptbldefault2) {
        uintptr_t*** tbl = (uintptr_t***)malloc((1<<JMPTABL_SHIFT)*sizeof(uintptr_t**));
        for(int i=0; i<(1<<JMPTABL_SHIFT); ++i)
            tbl[i] = box64_jmptbldefault1;
        if(arm64_lock_storeifref(&box64_jmptbl3[idx3], (uint64_t)tbl, (uint64_t)box64_jmptbldefault2)!=(uint64_t)tbl)
            free(tbl);
    }
    if(box64_jmptbl3[idx3][idx2] == box64_jmptbldefault1) {
        uintptr_t** tbl = (uintptr_t**)malloc((1<<JMPTABL_SHIFT)*sizeof(uintptr_t*));
        for(int i=0; i<(1<<JMPTABL_SHIFT); ++i)
            tbl[i] = box64_jmptbldefault0;
        if(arm64_lock_storeifref(&box64_jmptbl3[idx3][idx2], (uint64_t)tbl, (uint64_t)box64_jmptbldefault1)!=(uint64_t)tbl)
            free(tbl);
    }
    if(box64_jmptbl3[idx3][idx2][idx1] == box64_jmptbldefault0) {
        uintptr_t* tbl = (uintptr_t*)malloc((1<<JMPTABL_SHIFT)*sizeof(uintptr_t));
        for(int i=0; i<(1<<JMPTABL_SHIFT); ++i)
            tbl[i] = (uintptr_t)arm64_next;
        if(arm64_lock_storeifref(&box64_jmptbl3[idx3][idx2][idx1], (uint64_t)tbl, (uint64_t)box64_jmptbldefault0)!=(uint64_t)tbl)
            free(tbl);
    }

    return (uintptr_t)&box64_jmptbl3[idx3][idx2][idx1][idx0];
}

// Remove the Write flag from an adress range, so DB can be executed safely
void internalProtectDB(uintptr_t addr, uintptr_t size)
{
    dynarec_log(LOG_DEBUG, "internalProtectDB %p -> %p\n", (void*)addr, (void*)(addr+size-1));
    uintptr_t idx = (addr>>MEMPROT_SHIFT);
    uintptr_t end = ((addr+size-1LL)>>MEMPROT_SHIFT);
    int ret;
    for (uintptr_t i=idx; i<=end; ++i) {
        const uint32_t key = (i>>MEMPROT_SHIFT2)&0xffffffff;
        khint_t k = kh_put(memprot, memprot, key, &ret);
        if(ret) {
            uint8_t *m = (uint8_t*)calloc(1, MEMPROT_SIZE);
            kh_value(memprot, k) = m;
        }
        const uintptr_t ii = i&(MEMPROT_SIZE-1);
        uint8_t prot = kh_value(memprot, k)[ii];
        if(!(prot&PROT_DYNAREC)) {
            if(!prot)
                prot = PROT_READ | PROT_WRITE;    // comes from malloc & co, so should not be able to execute
            kh_value(memprot, k)[ii] = prot|PROT_DYNAREC;
            mprotect((void*)(i<<MEMPROT_SHIFT), 1<<MEMPROT_SHIFT, prot&~(PROT_WRITE|PROT_CUSTOM));
        }
    }
}

int internalIsprotectedDB(uintptr_t addr, size_t size)
{
    uintptr_t idx = (addr>>MEMPROT_SHIFT);
    uintptr_t end = ((addr+size-1LL)>>MEMPROT_SHIFT);
    int ret;
    for (uintptr_t i=idx; i<=end; ++i) {
        const uint32_t key = (i>>MEMPROT_SHIFT2)&0xffffffff;
        khint_t k = kh_put(memprot, memprot, key, &ret);
        if(ret) {
            uint8_t *m = (uint8_t*)calloc(1, MEMPROT_SIZE);
            kh_value(memprot, k) = m;
        }
        const uintptr_t ii = i&(MEMPROT_SIZE-1);
        uint8_t prot = kh_value(memprot, k)[ii];
        if(!(prot&PROT_DYNAREC)) {
            return 0;        
        }
    }
    return 1;
}

// Add the Write flag from an adress range, and mark all block as dirty
// no log, as it can be executed inside a signal handler
void internalUnprotectDB(uintptr_t addr, size_t size)
{
    dynarec_log(LOG_DEBUG, "internalUnprotectDB %p -> %p\n", (void*)addr, (void*)(addr+size-1));
    uintptr_t idx = (addr>>MEMPROT_SHIFT);
    uintptr_t end = ((addr+size-1LL)>>MEMPROT_SHIFT);
    int ret;
    for (uintptr_t i=idx; i<=end; ++i) {
        const uint32_t key = (i>>MEMPROT_SHIFT2)&0xffffffff;
        khint_t k = kh_put(memprot, memprot, key, &ret);
        if(ret) {
            uint8_t *m = (uint8_t*)calloc(1, MEMPROT_SIZE);
            kh_value(memprot, k) = m;
        }
        const uintptr_t ii = i&(MEMPROT_SIZE-1);
        uint8_t prot = kh_value(memprot, k)[ii];
        if(prot&PROT_DYNAREC) {
            kh_value(memprot, k)[ii] = prot&~PROT_DYNAREC;
            mprotect((void*)(i<<MEMPROT_SHIFT), 1<<MEMPROT_SHIFT, prot&~PROT_CUSTOM);
            cleanDBFromAddressRange((i<<MEMPROT_SHIFT), 1<<MEMPROT_SHIFT, 0);
        }
    }
}

#endif

void internalUpdateProtection(uintptr_t addr, size_t size, uint32_t prot)
{
    dynarec_log(LOG_DEBUG, "internalUpdateProtection %p:%p 0x%x\n", (void*)addr, (void*)(addr+size-1), prot);
    uintptr_t idx = (addr>>MEMPROT_SHIFT);
    uintptr_t end = ((addr+size-1LL)>>MEMPROT_SHIFT);
    int ret;
    uintptr_t last = idx<<MEMPROT_SHIFT;
    uint8_t oldprot = 0xff;
    for (uintptr_t i=idx; i<=end; ++i) {
        const uint32_t key = (i>>MEMPROT_SHIFT2)&0xffffffff;
        khint_t k = kh_put(memprot, memprot, key, &ret);
        if(ret) {
            uint8_t *m = (uint8_t*)calloc(1, MEMPROT_SIZE);
            kh_value(memprot, k) = m;
        }
        const uintptr_t start = i&(MEMPROT_SIZE-1);
        const uintptr_t finish = (((i|(MEMPROT_SIZE-1))<end)?(MEMPROT_SIZE-1):end)&(MEMPROT_SIZE-1);
        uint8_t* block = kh_value(memprot, k);
        for(uintptr_t ii = start; ii<=finish; ++ii) {
            uint32_t dyn = block[ii]&PROT_DYNAREC;
            if(dyn && (prot&PROT_WRITE)) {   // need to remove the write protection from this block
                if(oldprot!=prot) {
                    if(oldprot!=0xff)
                        mprotect((void*)last, (i<<MEMPROT_SHIFT)-last, oldprot&~PROT_CUSTOM); // need to optimize
                    last = i<<MEMPROT_SHIFT;
                    oldprot = prot;
                }
            } else if(prot!=0xff) {
                mprotect((void*)last, (i<<MEMPROT_SHIFT)-last, oldprot&~PROT_CUSTOM); // need to optimize
                last = i << MEMPROT_SHIFT;
                oldprot = 0xff;
            }
            block[ii] = prot|dyn|PROT_ALLOC;
        }
        i+=finish-start;    // +1 from the "for" loop
    }
    if(oldprot!=0xff)
        mprotect((void*)last, (end<<MEMPROT_SHIFT)-last, oldprot&~PROT_CUSTOM); // need to optimize
}

void internalSetProtection(uintptr_t addr, size_t size, uint32_t prot)
{
    dynarec_log(LOG_DEBUG, "internalSetProtection %p:%p 0x%x\n", (void*)addr, (void*)(addr+size-1), prot);
    uintptr_t idx = (addr>>MEMPROT_SHIFT);
    uintptr_t end = ((addr+size-1LL)>>MEMPROT_SHIFT);
    int ret;
    for (uintptr_t i=idx; i<=end; ++i) {
        const uint32_t key = (i>>MEMPROT_SHIFT2)&0xffffffff;
        khint_t k = kh_put(memprot, memprot, key, &ret);
        if(ret) {
            uint8_t *m = (uint8_t*)calloc(1, MEMPROT_SIZE);
            kh_value(memprot, k) = m;
        }
        const uintptr_t start = i&(MEMPROT_SIZE-1);
        const uintptr_t finish = (((i|(MEMPROT_SIZE-1))<end)?(MEMPROT_SIZE-1):end)&(MEMPROT_SIZE-1);
        memset(kh_value(memprot, k)+start, prot|PROT_ALLOC, finish-start+1);
        i+=finish-start;    // +1 from the "for" loop
    }
}

void internalAllocProtection(uintptr_t addr, size_t size, uint32_t prot)
{
    dynarec_log(LOG_DEBUG, "internalAllocProtection %p:%p 0x%x\n", (void*)addr, (void*)(addr+size-1), prot);
    uintptr_t idx = (addr>>MEMPROT_SHIFT);
    uintptr_t end = ((addr+size-1LL)>>MEMPROT_SHIFT);
    int ret;
    for (uintptr_t i=idx; i<=end; ++i) {
        const uint32_t key = (i>>MEMPROT_SHIFT2)&0xffffffff;
        khint_t k = kh_put(memprot, memprot, key, &ret);
        if(ret) {
            uint8_t *m = (uint8_t*)calloc(1, MEMPROT_SIZE);
            kh_value(memprot, k) = m;
        }
        const uintptr_t start = i&(MEMPROT_SIZE-1);
        const uintptr_t finish = (((i|(MEMPROT_SIZE-1))<end)?(MEMPROT_SIZE-1):end)&(MEMPROT_SIZE-1);
        uint8_t* block = kh_value(memprot, k);
        for(uintptr_t ii = start; ii<=finish; ++ii) {
            if(!block[ii])
                block[ii] = prot;
        }
        i+=finish-start;    // +1 from the "for" loop
    }
}

void internalLoadProtectionFromMap()
{
    char buf[500];
    FILE *f = fopen("/proc/self/maps", "r");
    if(!f)
        return;
    while(!feof(f)) {
        char* ret = fgets(buf, sizeof(buf), f);
        (void)ret;
        char r, w, x;
        uintptr_t s, e;
        if(sscanf(buf, "%lx-%lx %c%c%c", &s, &e, &r, &w, &x)==5) {
            int prot = ((r=='r')?PROT_READ:0)|((w=='w')?PROT_WRITE:0)|((x=='x')?PROT_EXEC:0);
            internalAllocProtection(s, e-s, prot);
        }
    }
    fclose(f);
}

static int blockempty(uint8_t* mem)
{
    for (int i=0; i<(MEMPROT_SIZE); ++i)
        if(mem[i])
            return 0;
    return 1;
}

void internalFreeProtection(uintptr_t addr, size_t size)
{
    dynarec_log(LOG_DEBUG, "internalFreeProtection %p:%p\n", (void*)addr, (void*)(addr+size-1));
    uintptr_t idx = (addr>>MEMPROT_SHIFT);
    uintptr_t end = ((addr+size-1LL)>>MEMPROT_SHIFT);
    for (uintptr_t i=idx; i<=end; ++i) {
        const uint32_t key = (i>>MEMPROT_SHIFT2)&0xffffffff;
        khint_t k = kh_get(memprot, memprot, key);
        if(k!=kh_end(memprot)) {
            const uintptr_t start = i&(MEMPROT_SIZE-1);
            const uintptr_t finish = (((i|(MEMPROT_SIZE-1))<end)?(MEMPROT_SIZE-1):end)&(MEMPROT_SIZE-1);
            uint8_t *block = kh_value(memprot, k);
            memset(block+start, 0, finish-start+1);
            if(blockempty(block)) {
                free(block);
                kh_del(memprot, memprot, k);
            }
            i+=finish-start;    // +1 from the "for" loop
        }
    }
}

uint32_t internalGetProtection(uintptr_t addr)
{
    const uint32_t key = (addr>>32)&0xffffffff;
    khint_t k = kh_get(memprot, memprot, key);
    if (k == kh_end(memprot)) return 0;
    const uintptr_t idx = ((addr&0xffffffff)>>MEMPROT_SHIFT);
    uint32_t ret = kh_val(memprot, k)[idx];
    return ret;
}

#define LOWEST (void*)0x20000
int availableBlock(uint8_t* p, size_t n)
{
    for (size_t i=0; i<n; ++i, ++p)
        if(*p)
            return 0;
    return 1;
}
static uintptr_t nextFree(uintptr_t addr)
{
    do {
        const uint32_t key = (addr>>32)&0xffffffff;
        khint_t k = kh_get(memprot, memprot, key);
        if(k==kh_end(memprot)) {
            return addr;
        }
        uint8_t *block = kh_value(memprot, k);
        for (uintptr_t i=(addr&0xffffffffLL)>>MEMPROT_SHIFT; i<MEMPROT_SIZE; ++i)
            if(!block[i]) {
                return (addr&~0xffffffffLL)+(i<<MEMPROT_SHIFT);
            }
        addr += 0x100000000LL;
        addr &= ~0xffffffffLL;
    } while(1);
}
static uintptr_t maxFree(uintptr_t addr, uintptr_t sz)
{
    uintptr_t mfree = 0;
    do {
        const uint32_t key = (addr>>32)&0xffffffff;
        khint_t k = kh_get(memprot, memprot, key);
        if(k==kh_end(memprot)) {
            mfree+=0x100000000LL;
            if(mfree>sz) {
                return addr;
            }
        } else {
            uint8_t *block = kh_value(memprot, k);
            for (uintptr_t i=(addr&0xffffffffLL)>>MEMPROT_SHIFT; i<MEMPROT_SIZE; ++i)
                if(!block[i]) {
                    mfree+=1<<MEMPROT_SHIFT;
                } else
                    return mfree;
        }
        addr += 0x100000000LL;
        addr &= ~0xffffffffLL;
    } while(1);
}
void* internalFind47bitBlock(size_t size)
{
    // slow iterative search... Would need something better one day
    uintptr_t addr = 0x100000000LL;
    do {
        addr = nextFree(addr);
        uintptr_t sz = maxFree(addr, size);
        if(sz>=size) {
            return (void*)addr;
        }
        addr += sz;
    } while(addr<0x800000000000LL);
    // search in 32bits as a backup
    addr = (uintptr_t)LOWEST;
    do {
        addr = nextFree(addr);
        uintptr_t sz = maxFree(addr, size);
        if(sz>=size) {
            return (void*)addr;
        }
        addr += sz;
    } while(addr<0x100000000LL);
    printf_log(LOG_NONE, "Warning: cannot find a 0x%zx block in 47bits address space\n", size);
    return NULL;
}
void* internalFind47bitBlockNearHint(void* hint, size_t size)
{
    // slow iterative search... Would need something better one day
    uintptr_t addr = (uintptr_t)hint;
    do {
        addr = nextFree(addr);
        uintptr_t sz = maxFree(addr, size);
        if(sz>=size) {
            return (void*)addr;
        }
        addr += sz;
    } while(addr<0x800000000000LL);
    printf_log(LOG_NONE, "Warning: cannot find a 0x%zx block in 32bits address space\n", size);
    return NULL;
}
void* internalFindBlockNearHint(void* hint, size_t size)
{
    // slow iterative search... Would need something better one day
    uintptr_t addr = (uintptr_t)hint;
    do {
        addr = nextFree(addr);
        uintptr_t sz = maxFree(addr, size);
        if(sz>=size) {
            return (void*)addr;
        }
        addr += sz;
    } while(addr<0x100000000LL);
    printf_log(LOG_NONE, "Warning: cannot find a 0x%zx block in 32bits address space\n", size);
    return NULL;
}
void* internalFind32bitBlock(size_t size)
{
    return internalFindBlockNearHint(LOWEST, size);
}
#undef LOWEST

int unlockCustommemMutex()
{
    int ret = 0;
    int i = 0;
    #define GO(A, B)                    \
        i = checkUnlockMutex(&A);       \
        if(i) {                         \
            ret|=(1<<B);                \
        }
    GO(mutex_blocks, 0)
    #ifdef DYNAREC
    GO(mutex_mmap, 2)
    #endif
    #undef GO
    return ret;
}

void relockCustommemMutex(int locks)
{
    #define GO(A, B)                    \
        if(locks&(1<<B))                \
            pthread_mutex_lock(&A);     \

    GO(mutex_blocks, 0)
    #ifdef DYNAREC
    GO(mutex_mmap, 2)
    #endif
    #undef GO
}

static void init_mutexes(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&mutex_blocks, &attr);
#ifdef DYNAREC
    pthread_mutex_init(&mutex_mmap, &attr);
#endif

    pthread_mutexattr_destroy(&attr);
}

static void atfork_child_custommem(void)
{
    // (re)init mutex if it was lock before the fork
    init_mutexes();
}

void init_custommem_helper(box64context_t* ctx)
{
    (void)ctx;
    if(inited) // already initialized
        return;
    inited = 1;
    memprot = kh_init(memprot);
    init_mutexes();
#ifdef DYNAREC
#ifdef ARM64
    if(box64_dynarec)
        for(int i=0; i<(1<<JMPTABL_SHIFT); ++i) {
            box64_jmptbldefault0[i] = (uintptr_t)arm64_next;
            box64_jmptbldefault1[i] = box64_jmptbldefault0;
            box64_jmptbldefault2[i] = box64_jmptbldefault1;
            box64_jmptbl3[i] = box64_jmptbldefault2;
        }
#else
#error Unsupported architecture!
#endif
#endif
    pthread_atfork(NULL, NULL, atfork_child_custommem);
}

void fini_custommem_helper(box64context_t *ctx)
{
    (void)ctx;
    if(!inited)
        return;
    inited = 0;
#ifdef DYNAREC
    if(box64_dynarec) {
        dynarec_log(LOG_DEBUG, "Free global Dynarecblocks\n");
        for (size_t i=0; i<mmapsize; ++i) {
            if(mmaplist[i].block)
                #ifdef USE_MMAP
                munmap(mmaplist[i].block, mmaplist[i].size);
                #else
                free(mmaplist[i].block);
                #endif
            if(mmaplist[i].dblist) {
                kh_destroy(dynablocks, mmaplist[i].dblist);
                mmaplist[i].dblist = NULL;
            }
            if(mmaplist[i].helper) {
                free(mmaplist[i].helper);
                mmaplist[i].helper = NULL;
            }
        }
        if(dblist_oversized) {
            kh_destroy(dynablocks, dblist_oversized);
            dblist_oversized = NULL;
        }
        mmapsize = 0;
        mmapcap = 0;
        dynarec_log(LOG_DEBUG, "Free dynamic Dynarecblocks\n");
        for (uintptr_t idx3=0; idx3<=0xffff; ++idx3)
            if(dynmap123[idx3]) {
                for (uintptr_t idx2=0; idx2<=0xffff; ++idx2)
                    if(dynmap123[idx3][idx2]) {
                        for (uintptr_t idx1=0; idx1<=0xffff; ++idx1)
                            if(dynmap123[idx3][idx2][idx1])
                                FreeDynablockList(&dynmap123[idx3][idx2][idx1]);
                        free(dynmap123[idx3][idx2]);
                    }
                free(dynmap123[idx3]);
            }

        free(mmaplist);
        pthread_mutex_destroy(&mutex_mmap);
        for (int i3=0; i3<(1<<DYNAMAP_SHIFT); ++i3)
            if(box64_jmptbl3[i3]!=box64_jmptbldefault2) {
                for (int i2=0; i2<(1<<DYNAMAP_SHIFT); ++i2)
                    if(box64_jmptbl3[i3][i2]!=box64_jmptbldefault1) {
                        for (int i1=0; i1<(1<<DYNAMAP_SHIFT); ++i1)
                            if(box64_jmptbl3[i3][i2][i1]!=box64_jmptbldefault0) {
                                free(box64_jmptbl3[i3][i2][i1]);
                            }
                        free(box64_jmptbl3[i3][i2]);
                    }
                free(box64_jmptbl3[i3]);
            }
    }
#endif
    uint8_t* m;
    kh_foreach_value(memprot, m,
        free(m);
    );
    kh_destroy(memprot, memprot);

    for(int i=0; i<n_blocks; ++i)
        #ifdef USE_MMAP
        munmap(p_blocks[i].block, p_blocks[i].size);
        #else
        free(p_blocks[i].block);
        #endif
    free(p_blocks);
    pthread_mutex_destroy(&mutex_blocks);
}
