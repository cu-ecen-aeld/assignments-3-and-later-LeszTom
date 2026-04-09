/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

//#include <stdlib.h>
#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    uint8_t out_offs = buffer->out_offs;
    size_t buffer_size_sum = buffer->entry[out_offs].size;
    uint8_t read_operations=1;

    while (char_offset > buffer_size_sum - 1 && read_operations < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ){
        out_offs++;
        read_operations++;

        if ( out_offs > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED -1 ){
            out_offs=0;
        }
        buffer_size_sum += buffer->entry[out_offs].size;
    }

    size_t last_entry_char_offset = buffer->entry[out_offs].size + char_offset - buffer_size_sum;
    (*entry_offset_byte_rtn)=last_entry_char_offset;
    
    if (buffer_size_sum > char_offset){
        return &(buffer->entry[out_offs]);
    }
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
//    int empty=-1;
//    if ( buffer->in_offs <= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1 ){
        buffer->entry[buffer->in_offs]=*add_entry;
        buffer->in_offs++;
        if (buffer->in_offs - buffer->out_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
            buffer->full = true;
//        else
//            buffer->full = false;

        buffer->in_offs = buffer->in_offs % (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
//    }



    if ( buffer->full &&  buffer->out_offs < buffer->in_offs)
            buffer->out_offs=buffer->in_offs;

//    if (abs(buffer->in_offs - buffer->out_offs) == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
//    buffer->full = true;

}

struct aesd_buffer_entry *aesd_circular_buffer_add_entry_overload(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
   //    int empty=-1;
//    if ( buffer->in_offs <= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1 ){
        buffer->entry[buffer->in_offs]=*add_entry;
        buffer->in_offs++;
        if (buffer->in_offs - buffer->out_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
            buffer->full = true;
//        else
//            buffer->full = false;

        buffer->in_offs = buffer->in_offs % (AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
//    }



    if ( buffer->full &&  buffer->out_offs < buffer->in_offs)
            buffer->out_offs=buffer->in_offs;

//    if (abs(buffer->in_offs - buffer->out_offs) == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
//    buffer->full = true;

    if (buffer->full)
        return &(buffer->entry[buffer->in_offs]);
    return NULL;
}

struct aesd_buffer_entry *aesd_circular_buffer_get_entry(struct aesd_circular_buffer *buffer){
    if (buffer->out_offs > buffer->in_offs || buffer->in_offs == 0){
        buffer->in_offs=0;
        buffer->out_offs=0;
        return NULL;
    }
    
    buffer->out_offs++;
    
    return &(buffer->entry[buffer->out_offs-1]);

}

struct aesd_buffer_entry *aesd_circular_buffer_get_last_entry(struct aesd_circular_buffer *buffer){
    if(buffer->in_offs > 0){
        return &(buffer->entry[buffer->in_offs]);
    }
    return NULL;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
