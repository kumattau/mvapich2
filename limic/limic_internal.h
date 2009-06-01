/* 
 * limic_internal.h
 *  
 * LiMIC2:  Linux Kernel Module for High-Performance MPI Intra-Node 
 *          Communication
 * 
 * Author:  Hyun-Wook Jin <jinh@konkuk.ac.kr>
 *          System Software Laboratory
 *          Department of Computer Science and Engineering
 *          Konkuk University
 *
 * History: Jul 15 2007 Launch
 */

#ifndef _LIMIC_INTERNAL_INCLUDED_
#define _LIMIC_INTERNAL_INCLUDED_

#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <asm/pgtable.h>
#include "limic.h"


typedef enum{
    CPY_TX,
    CPY_RX
}limic_copy_flag;


int limic_map_and_copy( limic_request *req, 
                        struct page **maplist, 
                        limic_copy_flag flag )
{
    limic_user *lu = req->lu;
    int pg_num = 0, offset = lu->offset, ret = 0;
    size_t pcount, len = (lu->length>req->len)?req->len:lu->length;
    void *kaddr, *buf = req->buf;
	
    lu->length = len; 
    while( ( pg_num <= lu->nr_pages ) && ( len > 0 ) ){
        pcount = PAGE_SIZE - offset;
        if (pcount > len)
            pcount = len;
	
        kaddr = kmap(maplist[pg_num]);

        if( flag == CPY_TX ){
            if( copy_from_user(kaddr+offset, buf, pcount) ){
                printk("LiMIC: (limic_map_and_copy) copy_from_user() is failed\n");
                return -EFAULT;
            }
        }
        else if( flag == CPY_RX ){
            if( copy_to_user(buf, kaddr+offset, pcount) ){
                printk("LiMIC: (limic_map_and_copy) copy_to_user() is failed\n");
                return -EFAULT;
            }
        }
	/* flush_dcache_page(maplist[pg_num]); */
        kunmap(maplist[pg_num]);

        len -= pcount;
        buf += pcount;
        ret += pcount;
        pg_num++;
        offset = 0;
    }

    return 0;
}


int limic_map_and_txcopy(limic_request *req, struct page **maplist)
{
    return limic_map_and_copy(req, maplist, CPY_TX);
}


int limic_map_and_rxcopy(limic_request *req, struct page **maplist)
{
    return limic_map_and_copy(req, maplist, CPY_RX);
}


void limic_release_pages(struct page **maplist, int pgcount)
{
    int i;
    struct page *map;
	
    for (i = 0; i < pgcount; i++) {
        map = maplist[i];
        if (map) {
            /* FIXME: cache flush missing for rw==READ
             * FIXME: call the correct reference counting function
             */
            page_cache_release(map); 
         }
    }

    kfree(maplist);
}


struct page **limic_get_pages(limic_request *req, int rw) 
{
    int err, pgcount;
    struct mm_struct *mm;
    struct page **maplist;
    limic_user *lu = req->lu;

    mm = lu->mm;
    pgcount = lu->nr_pages;

    maplist = kmalloc(pgcount * sizeof(struct page **), GFP_KERNEL);
    if (unlikely(!maplist)) 
        return NULL;
	 
    /* Try to fault in all of the necessary pages */
    down_read(&mm->mmap_sem); 
    err = get_user_pages(lu->tsk, mm, lu->va, pgcount,
                         (rw==READ), 0, maplist, NULL); 
    up_read(&mm->mmap_sem);

    if (err < 0) { 
        limic_release_pages(maplist, pgcount); 
        return NULL;
    }
    lu->nr_pages = err;
 
    while (pgcount--) {
        /* FIXME: flush superflous for rw==READ,
         * probably wrong function for rw==WRITE
         */
        /* flush_dcache_page(maplist[pgcount]); */
    }
	
    return maplist;
}


int limic_get_info(void *buf, int len, limic_user *lu)
{
    limic_user limic_u;
    unsigned long va;
    int pgcount;

    va = (unsigned long)buf;
    limic_u.va = va;
    limic_u.mm = (void *)current->mm;
    limic_u.tsk = (void *)current;

    pgcount = (va + len + PAGE_SIZE - 1)/PAGE_SIZE - va/PAGE_SIZE;
    if( !pgcount ){
        printk("LiMIC: (limic_get_info) number of pages is 0\n");
        return -EINVAL; 
    }       
    limic_u.nr_pages = pgcount;
    limic_u.offset = va & (PAGE_SIZE-1);
    limic_u.length = len;

    if( copy_to_user(lu, &limic_u, sizeof(limic_user)) ){
        printk("LiMIC: (limic_get_info) copy_to_user fail\n");
        return -EINVAL; 
    }       

    return 0;
}

#endif
