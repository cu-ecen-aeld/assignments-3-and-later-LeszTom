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
#include <linux/fs.h>
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
#include "aesd_ioctl.h"
int aesd_major =   0;
int aesd_minor =   0;

MODULE_AUTHOR("LeszTom");
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
    PDEBUG("Read file f_fpos=%lld, function f_pos=%lld, count=%zu",filp->f_pos,*f_pos,count);
    ssize_t retval = 0;
    ssize_t rest = 0;
    ssize_t read_count = 0;
    loff_t lf_pos = *f_pos;
    struct aesd_buffer_entry* entry;

    struct aesd_dev *dev = (struct aesd_dev*)filp->private_data;

    if(dev->eof){
        dev->eof=false;
        return 0;
    }

    if (mutex_lock_interruptible(&(dev->lock))){
        PDEBUG("Error: aesd_read lock");
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

        PDEBUG("Iteration i=%d, in_offs=%zu, out_offs=%zu",i,dev->cirular_buffer.in_offs,dev->cirular_buffer.out_offs);

        entry = &(dev->cirular_buffer.entry[dev->cirular_buffer.out_offs]);

        if(entry->buffptr == NULL || dev->eof){
            PDEBUG("Break look. entry_NULL=%d, dev_eof=%d",entry->buffptr == NULL, dev->eof);
            break;
        }

        if(lf_pos < entry->size)
        {
            rest = copy_to_user(buf + read_count, &entry->buffptr[lf_pos], entry->size - lf_pos);
            PDEBUG("Read, copy_to_user. read_count=%zu, lf_pos=%lld, rest=%zu copied bytes=%zu",read_count,lf_pos,rest, entry->size - lf_pos); 

            if (rest)
                PDEBUG("Not all date read. read %zu bytes, expected %zu",entry->size - lf_pos - rest,count);

            read_count = read_count + entry->size - lf_pos - rest;
            lf_pos=0;
        }else
            lf_pos -=  entry->size;

        dev->cirular_buffer.out_offs++;
        dev->cirular_buffer.out_offs = dev->cirular_buffer.out_offs % (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);

        PDEBUG("Read %s, in_offs = %zu out_offs = %zu",&entry->buffptr[lf_pos],dev->cirular_buffer.in_offs,out_offs);
    }

    dev->eof=true;
    retval = read_count;
    *f_pos += read_count;
    if (*f_pos >= dev->total_size){
        *f_pos -=  dev->total_size;
    }
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
    PDEBUG("Write file f_fpos=%lld, function f_pos=%zu, count=%zu",filp->f_pos,*f_pos,count);
    struct aesd_dev *dev;
    dev = (struct aesd_dev*)filp->private_data;

    if (mutex_lock_interruptible(&(dev->lock))){
        PDEBUG("Error: aesd_write lock");
        return -ERESTARTSYS;
    }

    size_t rest = 0;
    struct aesd_buffer_entry* entry = &(dev->cirular_buffer.entry[dev->cirular_buffer.in_offs]);

    if(entry->buffptr != NULL && entry->buffptr[entry->size-1] == '\n'){
        dev->total_size -= entry->size;
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
    *f_pos = *f_pos + entry->size;
    dev->total_size += entry->size; 
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

static loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = (struct aesd_dev*)filp->private_data;

    if (mutex_lock_interruptible(&(dev->lock))){
        PDEBUG("Error: aesd_llseek lock");
        return -ERESTARTSYS;
    }

    loff_t new_pos = fixed_size_llseek(filp, offset, whence, dev->total_size);
    dev->cirular_buffer.out_offs=0;

    mutex_unlock(&dev->lock);

    PDEBUG("ases_llseek: new_pos=%lld, file f_fpos=%lld, total_size=%zu",new_pos,filp->f_pos,dev->total_size);
    return new_pos;
}

static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    off_t retval=0;
    PDEBUG("aesd_iocl: file f_fpos=%lld, cmd=%u",filp->f_pos,cmd);
    switch(cmd) {
        case AESDCHAR_IOCSEEKTO:
            struct aesd_seekto seekto;
            if(copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto))){
                PDEBUG("aesd_iocl: -EFAULT");
                return -EFAULT;
            }else{
                PDEBUG("aesd_ioct: write_cmd=%u, write_cmd_offset=%u",seekto.write_cmd,seekto.write_cmd_offset);
                retval = aesd_adjust_file_offset(filp,seekto.write_cmd,seekto.write_cmd_offset);
                PDEBUG("aesd_iocl: offset set to %lld",retval);
                if(retval > 0)        
                    aesd_llseek(filp, retval, SEEK_SET);
            }
            break;
        default:
            PDEBUG("aesd_iocl: -ENOTTY");
            return -ENOTTY;
    }
    return retval;
}

static loff_t aesd_adjust_file_offset(struct file *filp, uint32_t write_cmd, uint32_t write_cmd_offset)
{
    struct aesd_dev *dev = (struct aesd_dev*)filp->private_data;

    if (write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED || dev->cirular_buffer.entry[write_cmd].buffptr== NULL){
        PDEBUG("Error: aesd_adjust_file_offset write_cmd out of range");
        return -ERESTARTSYS;
    }

    if (write_cmd_offset >= dev->cirular_buffer.entry[write_cmd].size){
        PDEBUG("Error: aesd_adjust_file_offset write_cmd_offset out of range");
        return -ERESTARTSYS;
    }

    if (mutex_lock_interruptible(&(dev->lock))){
        PDEBUG("Error: aesd_adjust_file_offset lock");
        return -ERESTARTSYS;
    }

    loff_t tmp_pos = 0;

    for(int i=0; i<write_cmd; i++)
        tmp_pos += dev->cirular_buffer.entry[i].size;
    tmp_pos += write_cmd_offset;

    mutex_unlock(&dev->lock);
    
    return tmp_pos;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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
