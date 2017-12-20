/* 
 * Author; Trong-Dat Nguyen
 * MySQL REDO log with NVDIMM
 * Using libpmemobj
 * Copyright (c) 2017 VLDB Lab - Sungkyunkwan University
 * */


#ifndef __PMEMOBJ_H__
#define __PMEMOBJ_H__


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>                                                                      
#include <sys/time.h> //for struct timeval, gettimeofday()
#include <string.h>
#include <stdint.h> //for uint64_t
#include <math.h> //for log()
#include <assert.h>
#include <wchar.h>
#include <unistd.h> //for access()

#include "univ.i"
#include "ut0byte.h"
#include "ut0rbt.h"
//#include "hash0hash.h" //for hashtable
#include "buf0buf.h" //for page_id_t
#include "page0types.h"
#include "ut0dbg.h"
#include "ut0new.h"

//#include "pmem_log.h"
#include <libpmemobj.h>
#include "my_pmem_common.h"
//#include "pmem0buf.h"
//cc -std=gnu99 ... -lpmemobj -lpmem
struct __pmem_buf_block_t;
typedef struct __pmem_buf_block_t PMEM_BUF_BLOCK;

struct __pmem_buf_block_list_t;
typedef struct __pmem_buf_block_list_t PMEM_BUF_BLOCK_LIST;

struct __pmem_dbw;
typedef struct __pmem_dbw PMEM_DBW;

struct __pmem_log_buf;
typedef struct __pmem_log_buf PMEM_LOG_BUF;

struct __pmem_buf;
typedef struct __pmem_buf PMEM_BUF;

struct __pmem_wrapper;
typedef struct __pmem_wrapper PMEM_WRAPPER;


POBJ_LAYOUT_BEGIN(my_pmemobj);
POBJ_LAYOUT_TOID(my_pmemobj, char);
POBJ_LAYOUT_TOID(my_pmemobj, PMEM_LOG_BUF);
POBJ_LAYOUT_TOID(my_pmemobj, PMEM_DBW);
POBJ_LAYOUT_TOID(my_pmemobj, PMEM_BUF);
POBJ_LAYOUT_TOID(my_pmemobj, PMEM_BUF_BLOCK_LIST);
POBJ_LAYOUT_TOID(my_pmemobj, TOID(PMEM_BUF_BLOCK_LIST));
POBJ_LAYOUT_TOID(my_pmemobj, PMEM_BUF_BLOCK);
POBJ_LAYOUT_END(my_pmemobj);


////////////////////////// THE WRAPPER ////////////////////////
/*The global wrapper*/
struct __pmem_wrapper {
	char name[PMEM_MAX_FILE_NAME_LENGTH];
	PMEMobjpool* pop;
	PMEM_LOG_BUF* plogbuf;
	PMEM_DBW* pdbw;
	PMEM_BUF* pbuf;
	bool is_new;
};



/* FUNCTIONS*/

PMEM_WRAPPER* pm_wrapper_create(const char* path, const size_t pool_size);
void pm_wrapper_free(PMEM_WRAPPER* pmw);


PMEMoid pm_pop_alloc_bytes(PMEMobjpool* pop, size_t size);
void pm_pop_free(PMEMobjpool* pop);


////////////////////// LOG BUFFER /////////////////////////////

struct __pmem_log_buf {
	size_t				size;
	PMEM_OBJ_TYPES		type;	
	PMEMoid				data; //log data
    uint64_t			lsn; 	
	uint64_t			buf_free; /* first free offset within the log buffer */
	bool				need_recv; /*need recovery, it is set to false when init and when the server shutdown
					  normally. Whenever a log record is copy to log buffer, this flag is set to true
	*/
	uint64_t			last_tsec_buf_free; /*the buf_free updated in previous t seconds, update this value in srv_sync_log_buffer_in_background() */
};

void* pm_wrapper_logbuf_get_logdata(PMEM_WRAPPER* pmw);
int pm_wrapper_logbuf_alloc(PMEM_WRAPPER* pmw, const size_t size);
int pm_wrapper_logbuf_realloc(PMEM_WRAPPER* pmw, const size_t size);
PMEM_LOG_BUF* pm_pop_get_logbuf(PMEMobjpool* pop);
PMEM_LOG_BUF* pm_pop_logbuf_alloc(PMEMobjpool* pop, const size_t size);
PMEM_LOG_BUF* pm_pop_logbuf_realloc(PMEMobjpool* pop, const size_t size);
ssize_t  pm_wrapper_logbuf_io(PMEM_WRAPPER* pmw, 
							const int type,
							void* buf, 
							const uint64_t offset,
							unsigned long int n);

///////////// DOUBLE WRITE BUFFER //////////////////////////


struct __pmem_dbw {
	size_t size;
	PMEM_OBJ_TYPES type;	
	PMEMoid  data; //dbw data
	uint64_t s_first_free;
	uint64_t b_first_free;
	bool is_new;
};
void* pm_wrapper_dbw_get_dbwdata(PMEM_WRAPPER* pmw);
int pm_wrapper_dbw_alloc(PMEM_WRAPPER* pmw, const size_t size);
PMEM_DBW* pm_pop_get_dbw(PMEMobjpool* pop);
PMEM_DBW* pm_pop_dbw_alloc(PMEMobjpool* pop, const size_t size);
ssize_t  pm_wrapper_dbw_io(PMEM_WRAPPER* pmw, 
							const int type,
							void* buf, 
							const uint64_t offset,
							unsigned long int n);


/////// PMEM BUF  //////////////////////

//This struct is used only for POBJ_LIST_INSERT_NEW_HEAD
//modify this struct according to struct __pmem_buf_block_t
struct list_constr_args{
//	uint64_t		id;
	page_id_t		id;
	size_t			size;
	int				check;
	buf_page_t*		bpage;
	TOID(PMEM_BUF_BLOCK_LIST) list;
	uint64_t		pmemaddr;
};

/*
 *A unit page in pmem
 It wrap buf_page_t and an address in pmem
 * */
struct __pmem_buf_block_t{
	POBJ_LIST_ENTRY(PMEM_BUF_BLOCK) entries;
//	uint64_t		id;
	page_id_t		id;
	size_t			size;
	int				check;
	buf_page_t*		bpage;
	TOID(PMEM_BUF_BLOCK_LIST) list;
	uint64_t		pmemaddr; /*
						  the offset of the page in pmem
						  note that the size of page can be got from page
						*/
};

struct __pmem_buf_block_list_t {
	POBJ_LIST_HEAD(block_list, PMEM_BUF_BLOCK) head;
	TOID(PMEM_BUF_BLOCK_LIST) pext_list;
	size_t				max_pages; //max number of pages
	size_t				cur_pages; // current buffered pages
	bool				is_flush;
};

struct __pmem_buf {
	size_t size;
	PMEM_OBJ_TYPES type;	
	PMEMoid  data; //pmem data
	bool is_new;
	TOID(PMEM_BUF_BLOCK_LIST) free;
	TOID_ARRAY(TOID(PMEM_BUF_BLOCK_LIST)) buckets;
};

int pm_wrapper_buf_alloc(PMEM_WRAPPER* pmw, const size_t size, const size_t page_size);
PMEM_BUF* pm_pop_get_buf(PMEMobjpool* pop);
PMEM_BUF* pm_pop_buf_alloc(PMEMobjpool* pop, const size_t size, const size_t page_size);
int 
pm_buf_block_init(PMEMobjpool *pop, void *ptr, void *arg);

void
pm_buf_list_init(PMEMobjpool* pop, PMEM_BUF* buf, const size_t size, const size_t page_size);

int
pm_buf_write(PMEMobjpool* pop, PMEM_BUF* buf, buf_page_t* bpage, void* data);

size_t
pm_buf_read(PMEMobjpool* pop, PMEM_BUF* buf, page_id_t page_id, void* data);

void
pm_buf_write_list_to_datafile(PMEMobjpool* pop, PMEM_BUF* buf, TOID(PMEM_BUF_BLOCK_LIST) list_new, PMEM_BUF_BLOCK_LIST* plist);

void
pm_buf_write_aio_complete(PMEMobjpool* pop, PMEM_BUF* buf, TOID(PMEM_BUF_BLOCK) toid_block);

PMEM_BUF* pm_pop_get_buf(PMEMobjpool* pop);
//DEBUG functions

void pm_buf_print_lists_info(PMEM_BUF* buf);

#define PMEM_BUF_LIST_INSERT(pop, list, entries, type, func, args) do {\
	POBJ_LIST_INSERT_NEW_HEAD(pop, &list.head, entries, sizeof(type), func, &args); \
	list.cur_size++;\
}while (0)

#define PMEM_HASH_KEY(hashed, key, n) do {\
	hashed = key ^ PMEM_HASH_MASK;\
	hashed = hashed % n;\
}while(0)


#endif /*__PMEMOBJ_H__ */
