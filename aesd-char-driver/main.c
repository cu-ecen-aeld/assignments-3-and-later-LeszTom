/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("LeszTom"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("aesd device open");      
    struct aesd_dev *dev; /* device information */

	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev; /* for other methods */

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("aesd device release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    ssize_t rest = 0;
    ssize_t read_count = 0;
    struct aesd_buffer_entry* entry;

    struct aesd_dev *dev = (struct aesd_dev*)filp->private_data;

    if(dev->eof)
        return 0;

    if (mutex_lock_interruptible(&(dev->lock))){
        PDEBUG("Error: aesd_read lock");
        pr_err("Błąd dla procesu %d (%s)\n", current->pid, current->comm);
        return -ERESTARTSYS;
    }

    uint8_t iterations;
    if (dev->cirular_buffer.full){
        iterations = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }else{
        iterations=dev->cirular_buffer.in_offs;
        dev->cirular_buffer.out_offs=0;
    }
    uint8_t out_offs=dev->cirular_buffer.out_offs;

    for(int i=0; i<iterations; i++){
        entry = &(dev->cirular_buffer.entry[dev->cirular_buffer.out_offs]);

        if(entry->buffptr == NULL || dev->eof)
            break;

        rest = copy_to_user(buf + read_count, entry->buffptr, entry->size);

        if (rest) {
            PDEBUG("Not all date read. read %zu bytes, expected %zu",entry->size-rest,count);
        }

        read_count = read_count + entry->size - rest;
        dev->cirular_buffer.out_offs++;
        dev->cirular_buffer.out_offs = dev->cirular_buffer.out_offs % (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
        PDEBUG("Read %s, in_offs = %zu out_offs = %zu",entry->buffptr,dev->cirular_buffer.in_offs,out_offs);
    }

    dev->eof=true;
    retval = read_count;
    mutex_unlock(&dev->lock);

    return read_count;

    empty:
        PDEBUG("No data found. Returning 0. Bufer ptr = %p, eof=%d",entry->buffptr,dev->eof);
        PDEBUG("in_offs = %zu. out_offs = %zu",dev->cirular_buffer.in_offs,dev->cirular_buffer.out_offs);
        mutex_unlock(&dev->lock);
        return 0;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,loff_t *f_pos)
{
    struct aesd_dev *dev;
    dev = (struct aesd_dev*)filp->private_data;

    if (mutex_lock_interruptible(&(dev->lock))){
        PDEBUG("Error: aesd_write lock");
        return -ERESTARTSYS;
    }

    size_t rest = 0;
    struct aesd_buffer_entry* entry = &(dev->cirular_buffer.entry[dev->cirular_buffer.in_offs]);

    if(entry->buffptr != NULL && entry->buffptr[entry->size-1] == '\n'){
        kfree(entry->buffptr);
        entry->buffptr = NULL;
        entry->size=0;
        if (dev->cirular_buffer.full && dev->cirular_buffer.out_offs == dev->cirular_buffer.in_offs){
            dev->cirular_buffer.out_offs++;
            dev->cirular_buffer.out_offs = dev->cirular_buffer.out_offs % (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
        }
    }

    if(entry->size + count > MAXDATASIZE){
        PDEBUG("can't fill the entry, buffer to small");
        goto error;
    }

    if(entry->buffptr == NULL ){
        entry->buffptr = kmalloc(MAXDATASIZE,GFP_KERNEL);
        if (!entry->buffptr){
            PDEBUG("Error: aesd_write, kmalloc");
            goto error;
        }
        memset(entry->buffptr,0,MAXDATASIZE);
        entry->size=0;
    }

    PDEBUG("Writing to: in_offs = %zu. out_offs = %zu",dev->cirular_buffer.in_offs,dev->cirular_buffer.out_offs);
    rest = copy_from_user(entry->buffptr+entry->size, buf, count);

    entry->size = entry->size + count - rest;
    if(count - rest > 0)
        dev->eof=false;

    if(entry->buffptr[entry->size-1] == '\n'){
        dev->cirular_buffer.in_offs++;
        if (dev->cirular_buffer.in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
            dev->cirular_buffer.full = true;

        dev->cirular_buffer.in_offs = dev->cirular_buffer.in_offs % (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
    }

    if (rest)
        PDEBUG("Not all date writen. Writen %zu bytes, expected %zu",count-rest,count);

    mutex_unlock(&dev->lock);
    PDEBUG("Written %s, in_offs = %zu out_offs = %zu rest=%zu",entry->buffptr,dev->cirular_buffer.in_offs,dev->cirular_buffer.out_offs,rest);
    return count - rest;

    error:
        mutex_unlock(&dev->lock);
        return -ENOMEM;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    PDEBUG("aesd_setup_cdev");
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        PDEBUG(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    PDEBUG("aesd_init_module");
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        PDEBUG(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&(aesd_device.cirular_buffer));
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        PDEBUG("Error: aesd_init_module: device allocation");
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    PDEBUG("aesd_cleanup_module");
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    uint8_t index;
    struct aesd_buffer_entry *entry;
    
    AESD_CIRCULAR_BUFFER_FOREACH(entry,&(aesd_device.cirular_buffer),index) {
        kfree(entry->buffptr);
        entry->buffptr = NULL;
        entry->size = 0;
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
