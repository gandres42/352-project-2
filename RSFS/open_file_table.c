/*
    global variable open_file_table and its guarding mutex; 
    routines for open file entry
*/

#include "def.h"

struct open_file_entry open_file_table[NUM_OPEN_FILE];
pthread_mutex_t open_file_table_mutex;

//allocate an available entry in open file table and return fd (file descriptor);
//return -1 if no entry is found
int allocate_open_file_entry(int access_flag, struct dir_entry *dir_entry){
    
    int fd=-1;
    
    pthread_mutex_lock(&open_file_table_mutex);
    for(int i=0; i<NUM_OPEN_FILE; i++){
        struct open_file_entry *entry = &open_file_table[i];
        pthread_mutex_lock(&entry->entry_mutex);
        if(!entry->used && fd == -1){
            fd=i; //find a valid empty fd in case the file is not already open
        }
        else if (entry->used && entry->dir_entry->inode_number == dir_entry->inode_number)
        {
            fd=i;
        }
        pthread_mutex_unlock(&entry->entry_mutex);
    }

    struct open_file_entry * entry = &open_file_table[fd];
    if (entry->used)
    {
        if (access_flag == RSFS_RDONLY && entry->readers >= 0)
        {
            entry->readers++;
        }
        else
        {
            fd = -1;
        }
    }
    else
    {
        entry->used = 1; //mark it as used
        //set up the entry
        entry->access_flag = access_flag;
        entry->dir_entry = dir_entry;
        //init position
        entry->position = 0;
        // set read/write protection
        if (access_flag == RSFS_RDONLY)
        {
            entry->readers = 1;
        }
        else if (access_flag == RSFS_RDWR)
        {
            entry->readers = -1;
        }
    }
    // printf("entry readers opened set: %d\n", entry->readers);
    pthread_mutex_unlock(&open_file_table_mutex);
    return fd;
}


//free the open file entry at index fd 
void free_open_file_entry(int fd)
{
    pthread_mutex_lock(&open_file_table_mutex);
    if (open_file_table[fd].readers > 1)
    {
        open_file_table[fd].readers--;
    }
    else
    {
        open_file_table[fd].used = 0;
        open_file_table[fd].readers = 0;
    }
    // printf("entry readers closed set: %d\n", open_file_table[fd].readers);
    pthread_mutex_unlock(&open_file_table_mutex);
}