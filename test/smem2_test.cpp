/*************************************************************************************
                    GNU GENERAL PUBLIC LICENSE
           		      Version 3, 29 June 2007

BWA-MEM2  (Sequence alignment using Burrows-Wheeler Transform),
Copyright (C) 2019  Vasimuddin Md, Sanchit Misra, Intel Corporation, Heng Li.
    
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License at https://www.gnu.org/licenses/ for more details.


TERMS AND CONDITIONS FOR DISTRIBUTION OF THE CODE
                                             
1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer. 
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution. 
3. Neither the name of Intel Corporation nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

Authors: Vasimuddin Md <vasimuddin.md@intel.com>; Sanchit Misra <sanchit.misra@intel.com>.
*****************************************************************************************/

#include<stdio.h>
#include<stdlib.h>
#include<stdint.h>
#include "fasta_file.h"
#include "FMI_search.h"
#include <omp.h>
#include <string.h>

#ifdef VTUNE_ANALYSIS
#include <ittnotify.h>
#endif

#define MAX_NUM_QUERIES 1000000
#define QUERY_DB_SIZE 500000000L

int myrank, num_ranks;

int32_t read_smem2_input(char *fname, char *query_seq, int16_t *query_pos_array, int32_t *min_intv_array, int32_t readlen)
{
    FILE *smem2_fp = fopen(fname, "r");
    assert(smem2_fp != NULL);
    int numReads = 0;
    char line[1024];
    while(fgets(line, 1024, smem2_fp) != NULL)
    {
        //printf("0.0\n");
        int32_t rlen = strlen(line);
        //printf("1.0\n");
        if(rlen != (readlen + 1))
        {
            printf("ERROR! len - 1 = %d, readlen = %d\n", rlen - 1, readlen);
            exit(1);
        }
        //printf("2.0\n");
        memcpy(query_seq + numReads * readlen, line, readlen);
        //printf("3.0\n");
        fscanf(smem2_fp, "%hd", query_pos_array + numReads);
        //printf("4.0\n");
        fscanf(smem2_fp, "%d", min_intv_array + numReads);
        //printf("4.1\n");
        //printf("numReads = %d, numReads * readlen = %d, query_x = %d, min_intv = %dhi\n", numReads, numReads * readlen, query_pos_array[numReads], min_intv_array[numReads]);
        numReads++;
        //printf("5.0\n");
        fgets(line, 1024, smem2_fp);
        //printf("6.0\n");
    }
    printf("numReads = %d\n", numReads);
    //printf("Exiting\n");
    fflush(stdout);
    fclose(smem2_fp);
    return numReads;
}


int main(int argc, char **argv) {
#ifdef VTUNE_ANALYSIS
    __itt_pause();
#endif

    if(argc!=7)
    {
        printf("Need seven arguments : ref_file query_set batch_size readlength minSeedLen n_threads\n");
        return 1;
    }

    char *query_seq=(char *)malloc(QUERY_DB_SIZE*sizeof(char));
    int16_t *query_pos_array = (int16_t *)_mm_malloc(MAX_NUM_QUERIES * sizeof(int16_t), 64);
    int32_t *min_intv_array = (int32_t *)_mm_malloc(MAX_NUM_QUERIES * sizeof(int32_t), 64);
    int32_t *rid_array = (int32_t *)_mm_malloc(MAX_NUM_QUERIES * sizeof(int32_t), 64);
    int readlength=atoi(argv[4]);
    assert(readlength > 0);
    assert(readlength < 10000);
    long numReads;
    numReads=read_smem2_input(argv[2], query_seq, query_pos_array, min_intv_array, readlength);

    assert(numReads > 0);
    assert(numReads * readlength < QUERY_DB_SIZE);

    FMI_search *fmiSearch = new FMI_search(argv[1]);

    uint8_t *enc_qdb=(uint8_t *)malloc(numReads*readlength*sizeof(uint8_t));

    long cind,st;
    uint64_t r;
    for (st=0; st < numReads; st++) {
        cind=st*readlength;
        for(r = 0; r < readlength; ++r) {
            switch(query_seq[r+cind])
            {
                case '0': enc_qdb[r+cind]=0;
                          break;
                case '1': enc_qdb[r+cind]=1;
                          break;
                case '2': enc_qdb[r+cind]=2;
                          break;
                case '3': enc_qdb[r+cind]=3;
                          break;
                default: enc_qdb[r+cind]=4;
            }
            //printf("%c %d\n", query_seq[r+cind], enc_qdb[r + cind]);
            //printf("%d", enc_qdb[r + cind]);
        }
    }

    int batch_size=0;
    batch_size=atoi(argv[3]);

    SMEM *matchArray = (SMEM *)_mm_malloc(numReads * readlength * sizeof(SMEM), 64);

    int32_t minSeedLen = atoi(argv[5]);
    int numthreads=atoi(argv[6]);
    int64_t numTotalSmem[numthreads];
#pragma omp parallel num_threads(numthreads)
    {
        int tid = omp_get_thread_num();

        if(tid == 0)
            printf("Running %d threads\n", omp_get_num_threads());
        numTotalSmem[tid] = 0;
    }

    int32_t i;
    for(i = 0; i < numReads; i++)
    {
        rid_array[i] = i;
    }
    int64_t startTick, endTick;
#ifdef VTUNE_ANALYSIS
    __itt_resume();
#endif
    printf("before getSMEM\n");
    fflush(stdout);
    startTick = __rdtsc();
    numthreads = 1;
    fmiSearch->getSMEMsOnePosOneThread(enc_qdb,
            query_pos_array,
            min_intv_array,
            rid_array,
            numReads,
            batch_size,
            readlength,
            minSeedLen,
            matchArray,
            numTotalSmem);
    endTick = __rdtsc();
#ifdef VTUNE_ANALYSIS
    __itt_pause();
#endif
    printf("Consumed: %ld cycles\n", endTick - startTick);

    int64_t totalSmem = 0;
    int tid;
    for(tid = 0; tid < numthreads; tid++)
    {
        totalSmem += numTotalSmem[tid];
    }
    printf("totalSmems = %ld\n", totalSmem);

    fmiSearch->sortSMEMs(matchArray,
            numTotalSmem,
            numReads,
            readlength,
            numthreads);

    int32_t perThreadQuota = (numReads + (numthreads - 1)) / numthreads;
    for(tid = 0; tid < numthreads; tid++)
    {
        int32_t first = tid * perThreadQuota;
        SMEM *myMatchArray = matchArray + first * readlength;
        int64_t i = 0;
        int64_t rid;
        for(rid = 0; rid < numReads; rid++)
        {
            while((i < numTotalSmem[tid]) && (myMatchArray[i].rid == rid))
            {
                SMEM smem = myMatchArray[i];
                //printf("\n%u: ", smem.rid);
                printf("[ %u %u %u %u %u ] ", smem.k, smem.l, smem.s, smem.m, smem.n + 1);
                i++;
            }
            printf("\n");
        }
    }

    free(query_seq);
    free(enc_qdb);
    _mm_free(rid_array);
    _mm_free(query_pos_array);
    _mm_free(matchArray);
    _mm_free(min_intv_array);
    delete fmiSearch;
    return 0;
}

