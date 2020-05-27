/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)
#define MAX(x,y) ((x)>(y)?(x):(y))
#define PACK(size, alloc) ((size)|(alloc))
#define GET(p)    (*(unsigned int *)(p))
#define PUT(p,val)     (*(unsigned int *)(p) = (unsigned int)(val))
#define GET_SIZE(p) (GET(p)&~0x7)
#define GET_ALLOC(p) (GET(p)& 0x1)
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
#define PRED(bp) ((char *)(bp))
#define SUCC(bp) ((char *)(bp) + WSIZE)
/* 
 * mm_init - initialize the malloc package.
 */
char *heap_listp;
static void *coalesce(void *bp)
{
	size_t prev_alloc = GET(HDRP(bp))&0x2;
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));
	if(prev_alloc && next_alloc)//插入空闲链表头部
	{
		PUT(PRED(bp), heap_listp);//前驱为heap_listp
		PUT(SUCC(bp), GET(heap_listp));
		if(GET(heap_listp)!=NULL)
		{
			PUT(PRED(GET(heap_listp)), bp);
		}
		PUT(heap_listp, (unsigned int)bp);
	}
	else if(prev_alloc && !next_alloc)
	{
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(PRED(bp), GET(PRED(NEXT_BLKP(bp))));
		PUT(SUCC(bp), GET(SUCC(NEXT_BLKP(bp))));
		if(GET(PRED(NEXT_BLKP(bp)))!=heap_listp)
		{
			PUT(SUCC(GET(PRED(NEXT_BLKP(bp)))), GET(SUCC(NEXT_BLKP(bp))));
		}
		else
		{
			PUT(heap_listp, GET(SUCC(NEXT_BLKP(bp))));
		}
		if(GET(SUCC(NEXT_BLKP(bp)))!=NULL)
		{
			PUT(PRED(GET(SUCC(NEXT_BLKP(bp)))), GET(PRED(NEXT_BLKP(bp))));
		}
		PUT(HDRP(bp), PACK(size, 2));
		PUT(FTRP(bp), PACK(size, 2));
		PUT(PRED(bp), heap_listp);//前驱为heap_listp
		PUT(SUCC(bp), GET(heap_listp));
		if(GET(heap_listp)!=NULL)
		{
			PUT(PRED(GET(heap_listp)), bp);
		}
		PUT(heap_listp, (unsigned int)bp);
	}
	else if(!prev_alloc && next_alloc)
	{
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		if(GET(PRED(PREV_BLKP(bp)))!=heap_listp)
		{
			PUT(SUCC(GET(PRED(PREV_BLKP(bp)))), GET(SUCC(PREV_BLKP(bp))));
		}
		else
		{
			PUT(heap_listp, GET(SUCC(PREV_BLKP(bp))));
		}
		if(GET(SUCC(PREV_BLKP(bp)))!=NULL)
		{
			PUT(PRED(GET(SUCC(PREV_BLKP(bp)))), GET(PRED(PREV_BLKP(bp))));
		}
		PUT(FTRP(bp), PACK(size, GET(HDRP(PREV_BLKP(bp)))&0x7));
		PUT(HDRP(PREV_BLKP(bp)), GET(FTRP(bp)));
		bp = PREV_BLKP(bp);
		PUT(PRED(bp), heap_listp);//前驱为heap_listp
		PUT(SUCC(bp), GET(heap_listp));
		if(GET(heap_listp)!=NULL)
		{
			PUT(PRED(GET(heap_listp)), bp);
		}
		PUT(heap_listp, (unsigned int)bp);
	}
	else
	{
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
		if(GET(PRED(PREV_BLKP(bp)))!=heap_listp)
		{
			PUT(SUCC(GET(PRED(PREV_BLKP(bp)))), GET(SUCC(PREV_BLKP(bp))));
		}
		else
		{
			PUT(heap_listp, GET(SUCC(PREV_BLKP(bp))));
		}
		if(GET(SUCC(PREV_BLKP(bp)))!=NULL)
		{
			PUT(PRED(GET(SUCC(PREV_BLKP(bp)))), GET(PRED(PREV_BLKP(bp))));
		}
		if(GET(PRED(NEXT_BLKP(bp)))!=heap_listp)
		{
			PUT(SUCC(GET(PRED(NEXT_BLKP(bp)))), GET(SUCC(NEXT_BLKP(bp))));
		}
		else
		{
			PUT(heap_listp, GET(SUCC(NEXT_BLKP(bp))));
		}
		if(GET(SUCC(NEXT_BLKP(bp)))!=NULL)
		{
			PUT(PRED(GET(SUCC(NEXT_BLKP(bp)))), GET(PRED(NEXT_BLKP(bp))));
		}
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, GET(HDRP(PREV_BLKP(bp)))&0x7));
		PUT(FTRP(NEXT_BLKP(bp)), GET(HDRP(PREV_BLKP(bp))));
		bp = PREV_BLKP(bp);
		PUT(PRED(bp), heap_listp);//前驱为heap_listp
		PUT(SUCC(bp), GET(heap_listp));
		if(GET(heap_listp)!=NULL)
		{
			PUT(PRED(GET(heap_listp)), bp);
		}
		PUT(heap_listp, (unsigned int)bp);
	}
	return bp;
}
static void *extend_heap(size_t words)//合并部分才考虑插入
{
	char *bp;
	size_t size;
	size = (words %2) ? (words+1) * WSIZE :words * WSIZE;
	if((long)(bp = mem_sbrk(size))== -1)
		return NULL;
	PUT(HDRP(bp), PACK(size,(GET(HDRP(bp))&0x2)));
	PUT(FTRP(bp), GET(HDRP(bp)));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1));
	if(GET(heap_listp)==bp)
	{
		PUT(bp, bp);
		PUT(bp+WSIZE, bp);
		return bp;
	}
	return coalesce(bp);
}
int mm_init(void)//空闲链表最开始指向NULL
{	
	if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
		return -1;
	PUT(heap_listp, 0);
	PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));
	PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));
	PUT(heap_listp + (3*WSIZE), PACK(0,3));
	if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
		return -1;
	return 0;
}
/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *find_fit(size_t size)
{
	char* temp;
	temp = GET(heap_listp);
	while(temp!=NULL)
	{
		if(size<=GET_SIZE(HDRP(temp)))
			return temp;
		temp=GET(SUCC(temp));
	}
	return NULL;
}

void place(void *bp,size_t size)
{
	size_t temp = GET_SIZE(HDRP(bp)) - size;
	if(GET(PRED(bp))!=heap_listp)
	{
		PUT(SUCC(GET(PRED(bp))), GET(SUCC(bp)));
	}
	else
	{
		PUT(heap_listp, GET(SUCC(bp)));
	}
	if(GET(SUCC(bp))!=NULL)
	{
		PUT(PRED(GET(SUCC(bp))), GET(PRED(bp)));
	}
	if(temp<4*WSIZE)
	{
		PUT(HDRP(bp),GET(HDRP(bp))|0x1);
		PUT(HDRP(NEXT_BLKP(bp)),GET(HDRP(NEXT_BLKP(bp)))|0x2);
	}
	else
	{
		PUT(HDRP(bp),((GET(HDRP(bp))|0x1)&0x7)|size);
		PUT(FTRP(bp),GET(HDRP(bp)));
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp),PACK(temp, 0x2));
		PUT(FTRP(bp),PACK(temp, 0x2));
		PUT(PRED(bp), heap_listp);//前驱为heap_listp
		PUT(SUCC(bp), GET(heap_listp));
		if(GET(heap_listp)!=NULL)
		{
			PUT(PRED(GET(heap_listp)), bp);
		}
		PUT(heap_listp, (unsigned int)bp);
	}
}

void *mm_malloc(size_t size)
{
	size_t asize;	
	size_t extendsize;
	char *bp;
	if(size == 0)
		return NULL;
	if(size <=DSIZE)
		asize = 2*DSIZE;
	else
		asize = DSIZE *((size + (DSIZE)+(DSIZE - 1))/DSIZE);
	if((bp = find_fit(asize))!= NULL)
	{
		place(bp, asize);
		return bp;
	}
	extendsize = MAX(asize,CHUNKSIZE);
	if((bp= extend_heap(extendsize/WSIZE))==NULL)
		return NULL;
	place(bp, asize);
	return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
	size_t size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, GET(HDRP(bp))&0x7));
	PUT(FTRP(bp), GET(HDRP(bp)));
	if(GET_SIZE(HDRP(NEXT_BLKP(bp)))==0)
		GET(HDRP(NEXT_BLKP(bp))) -=2;
	coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}














