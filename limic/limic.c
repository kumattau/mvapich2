/* 
 * limic.c
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

#include <linux/init.h>
#include <linux/module.h> 
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include "limic_internal.h"


MODULE_AUTHOR("Hyun-Wook Jin <jinh@konkuk.ac.kr>");
MODULE_DESCRIPTION("LiMIC2: Linux Kernel Module for High-Performance MPI Intra-Node Communication");
MODULE_VERSION("0.5");
MODULE_LICENSE("Dual BSD/GPL"); /* BSD only */


struct cdev *limic_cdev;
static dev_t limic_devnum;


int limic_ioctl( 
                  struct file *fp, 
                  unsigned int op_code, 
                  unsigned long arg)
{
    limic_request req;
    int err;
    struct page **maplist;
    limic_user *lu;

    switch( op_code ){

        case LIMIC_TX:

            if( copy_from_user((void *)&req,(void *)arg,sizeof(limic_request)) )
                return -EFAULT;

            err = limic_get_info(req.buf, req.len, req.lu);
            if ( err )
                return err;

            return LIMIC_TX_DONE;

        case LIMIC_RX:

            if( copy_from_user((void *)&req,(void *)arg,sizeof(limic_request)) )
                return -EFAULT;
            lu = req.lu;

            maplist = limic_get_pages( &req, READ );
            if( !maplist )
                return -EINVAL;
            err = limic_map_and_rxcopy( &req, maplist );
            if ( err ){
                limic_release_pages(maplist, lu->nr_pages);
                return err;
            }
            limic_release_pages(maplist, lu->nr_pages);
            return LIMIC_RX_DONE;
	/*
        case OCK_RESET:
            while(module_refcount(THIS_MODULE) )
		module_put(THIS_MODULE);
	    try_module_get(THIS_MODULE);  
	    return OCK_RESETTED;
	*/
        default:
            return -ENOTTY;
    }

    return -EFAULT;
}


static int limic_open(struct inode *inode, struct file *fp)
{
    try_module_get(THIS_MODULE); 
    return 0;
}


static int limic_release(struct inode *inode, struct file *fp)
{
    module_put(THIS_MODULE); 
    return 0;
}


static struct file_operations limic_fops={
    unlocked_ioctl: limic_ioctl,
    open: limic_open,
    release: limic_release
};


int limic_init(void)
{
    int err;

    err = alloc_chrdev_region(&limic_devnum, 0, 1, "limic");
    if( err < 0 ){
        printk ("LiMIC: can't get a major number\n");
        return err;
    }

    limic_cdev = cdev_alloc();
    limic_cdev->ops = &limic_fops;
    limic_cdev->owner = THIS_MODULE;
    err = cdev_add(limic_cdev, limic_devnum, 1);
    if ( err < 0 ) {
        printk ("LiMIC: can't register the device\n");
        unregister_chrdev_region(limic_devnum, 1);
        return err;
    }
    
    printk("LiMIC: module is successfuly loaded.\n");
    printk("LiMIC: device major number: %d.\n", MAJOR(limic_devnum));

    return 0;
}


void limic_exit(void)
{
    cdev_del(limic_cdev);
    unregister_chrdev_region(limic_devnum, 1);
    printk("LiMIC: module is cleaned up.\n");
}


module_init(limic_init);
module_exit(limic_exit);
