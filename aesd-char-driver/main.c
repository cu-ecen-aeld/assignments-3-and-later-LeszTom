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

    filp->private_data = NULL;

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    PDEBUG("Read %zu bytes with offset %lld",count,*f_pos);
    ssize_t retval = 0;
    ssize_t rest = 0;
    
    struct aesd_dev *dev;
    dev = (struct aesd_dev*)filp->private_data;

    if(dev->eof)
        return 0;

    if (mutex_lock_interruptible(&(dev->lock))){
        PDEBUG("Error: aesd_read lock");
        return -ERESTARTSYS;
    }

    struct aesd_buffer_entry* entry;
    size_t read_count = 0;

    for(int i=0; i<AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++){

        if (dev->cirular_buffer.entry[dev->cirular_buffer.out_offs].buffptr == NULL)
            break;

        entry = &(dev->cirular_buffer.entry[dev->cirular_buffer.out_offs]);

        rest = copy_to_user(buf + read_count, entry->buffptr, entry->size);
        read_count = read_count + entry->size - rest;

        dev->cirular_buffer.out_offs++;
        dev->cirular_buffer.out_offs = dev->cirular_buffer.out_offs % (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);

    }

    dev->eof=true;
    mutex_unlock(&dev->lock);

    return read_count;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    ssize_t retval = -ENOMEM;

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
        if (dev->cirular_buffer.full && dev->cirular_buffer.out_offs == dev->cirular_buffer.in_offs)
            dev->cirular_buffer.out_offs++;
            dev->cirular_buffer.out_offs = dev->cirular_buffer.out_offs % (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
    }

    if(entry->size + count > MAXDATASIZE){
            PDEBUG("can't fill the entry, buffer to small");
            mutex_unlock(&dev->lock);
            return -ENOMEM;
    }

    if(entry->buffptr == NULL ){
        entry->buffptr = kmalloc(MAXDATASIZE,GFP_KERNEL);
        memset(entry->buffptr,0,MAXDATASIZE);
    }
    rest = copy_from_user(entry->buffptr+entry->size, buf, count);
    entry->size = entry->size + count - rest;
    if(count - rest > 0)
        dev->eof=false;
  
    if(entry->buffptr[entry->size-1] == '\n'){
        dev->cirular_buffer.in_offs++;
        if (abs(dev->cirular_buffer.in_offs - dev->cirular_buffer.out_offs) == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
            dev->cirular_buffer.full = true;
        dev->cirular_buffer.in_offs = dev->cirular_buffer.in_offs % (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
    }
    if (rest) {
        PDEBUG("Not all date writen. Writen %zu bytes, expected %zu",count-rest,count);
    }

    mutex_unlock(&dev->lock);

    return count - rest;
}

void aesd_device_status(struct aesd_dev *device){

    uint8_t in_offs = device->cirular_buffer.in_offs;
    uint8_t out_offs = device->cirular_buffer.out_offs;
    bool full = device->cirular_buffer.full;
    printk("in_offs = %d\nout_offs = %d\nfull = %d\n",in_offs,out_offs,full);

    for(int i=0; i<AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++){
        printk("index = %d\nsize = %zu",i,device->cirular_buffer.entry[i].size);
        if (device->cirular_buffer.entry[i].buffptr)
            printk("content: %s",device->cirular_buffer.entry[i].buffptr);
    }
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
    printk("aesd_setup_cdev");
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    printk("aesd_init_module");
    PDEBUG("Module init");
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
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
    printk("aesd_cleanup_module");
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
