/* 
 * Author; Trong-Dat Nguyen
 * MySQL REDO log with NVDIMM
 * Using libpmemobj
 * Copyright (c) 2017 VLDB Lab - Sungkyunkwan University
 * */

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


#include "my_pmem_common.h"
#include "my_pmemobj.h"
//#include "pmem0buf.h"

//global variable
PMEM_WRAPPER* gb_pmw = NULL;


PMEM_WRAPPER* pm_wrapper_create(const char* path){
	PMEM_WRAPPER* pmw;
	PMEMobjpool* pop = NULL;
	size_t size;

	pmw =  (PMEM_WRAPPER*) malloc(sizeof(PMEM_WRAPPER));
	if (!pmw)
		goto err;
	pmw->is_new = true;

	/*create new or open existed PMEMobjpool*/
	size =  PMEM_MAX_LOG_BUF_SIZE +
				   (PMEM_MAX_DBW_PAGES + 1) * PMEM_PAGE_SIZE  ;

	if (file_exists(path) != 0) {
		if ((pop = pmemobj_create(path, POBJ_LAYOUT_NAME(my_pmemobj),
						size, S_IWRITE | S_IREAD)) == NULL) {
			printf("[PMEMOBJ_ERROR] failed to create pool\n");
			goto err;
		}
	} else {
		pmw->is_new = false;

		if ((pop = pmemobj_open(path, POBJ_LAYOUT_NAME(my_pmemobj)))
				== NULL) {
			printf("[PMEMOBJ_ERROR] failed to open pool\n");
			goto err;
		}
	}
	pmw->pop = pop;

//	pmemw->pop = pm_create_PMEMobjpool(path);
	/*name */
	strncpy(pmw->name, path, PMEM_MAX_FILE_NAME_LENGTH);
	pmw->name[PMEM_MAX_FILE_NAME_LENGTH - 1] = '\0';

	/* log buffer */
	pmw->plogbuf = NULL;
	pmw->pdbw = NULL;
	pmw->pbuf = NULL;

	/*If we have persistent data structures, get them*/
	if(!pmw->is_new) {
		pmw->plogbuf = pm_pop_get_logbuf(pop);
		if(!pmw->plogbuf){
			printf("[PMEMOBJ_INFO] the pmem log buffer is empty. The server've shutdown normally\n");
		}
		pmw->pdbw = pm_pop_get_dbw(pop);
		if(!pmw->pdbw){
			printf("[PMEMOBJ_INFO] the pmem double write buffer is empty. The database is new or the previous double write buffer is in disk instead of PMEM\n");
		}
		pmw->pbuf = pm_pop_get_buf(pop);
		if(!pmw->pbuf){
			printf("[PMEMOBJ_INFO] the pmem buf is empty. The database is new\n");
		}	
	}
	return pmw;

err:
	printf("PMEMOBJ_ERROR: error in pm_create_PMEMwrapper");
	if(pmw)
		pm_wrapper_free(pmw);
	return NULL;
}

void pm_wrapper_free(PMEM_WRAPPER* pmw){
	if(pmw->pop)
		pm_pop_free(pmw->pop);
	pmw->plogbuf = NULL;
	pmw->pop = NULL;
	printf("PMEMOBJ_INFO: free PMEM_WRAPPER from heap allocated\n");
	free(pmw);

}

/*
 * Allocate a range of persistent memory 
 * */
PMEMoid pm_pop_alloc_bytes(PMEMobjpool* pop, size_t size){
	TOID(char) array;

	POBJ_ALLOC(pop, &array, char, sizeof(char) * size,	NULL, NULL);

	if (TOID_IS_NULL(array)) {
		fprintf(stderr, "POBJ_ALLOC\n");
		return OID_NULL;
	}

	pmemobj_persist(pop, D_RW(array), size * sizeof(*D_RW(array)));
	printf("PMEMOBJ_INFO: allocate PMEMobjpool from pmem with size %zu MB\n", (size/1024));

	//Check
	char* p = (char*) pmemobj_direct(array.oid);
	if(!p){
			printf("PMEMOBJ_ERROR: message: %s\n",  pmemobj_errormsg() );
	}
	return array.oid;
}
void pm_pop_free(PMEMobjpool* pop){
	TOID(PMEM_LOG_BUF) logbuf;
	TOID(PMEM_DBW) dbw;
	TOID(char) data1;
	TOID(char) data2;
	
	/* Free log buffer*/
	POBJ_FOREACH_TYPE(pop, logbuf) {
		TOID_ASSIGN(data1, D_RW(logbuf)->data);
		POBJ_FREE(&data1);	

		D_RW(logbuf)->lsn = 0;
		D_RW(logbuf)->buf_free = 0;
		D_RW(logbuf)->need_recv = false;

		POBJ_FREE(&logbuf);	
	}
	printf("PMEMOBJ_INFO: free PMEMobjpool from pmem\n");

	/*Free DBW*/
	/*We kept the double write buffer in PMEM when the server shutdown
	 *It use for recovery tone pages when the server start up
	 * */
	/*
	POBJ_FOREACH_TYPE(pop, dbw) {
		TOID_ASSIGN(data2, D_RW(dbw)->data);
		POBJ_FREE(&data2);	

		D_RW(dbw)->block1 = 0;
		D_RW(dbw)->block2 = 0;

		POBJ_FREE(&logbuf);	
	}
	*/

	pmemobj_close(pop);
}
