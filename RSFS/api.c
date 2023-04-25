/*
    Implementation of API. 
    Treat the comments inside a function as friendly hints; but you are free to implement in your own way as long as it meets the functionality expectation. 
*/

#include "def.h"

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

pthread_mutex_t mutex_for_fs_stat;

//initialize file system - should be called as the first thing in application code
int RSFS_init(){

    //initialize data blocks
    for(int i=0; i<NUM_DBLOCKS; i++){
      void *block = malloc(BLOCK_SIZE); //a data block is allocated from memory
      if(block==NULL){
        printf("[sys_init] fails to init data_blocks\n");
        return -1;
      }
      data_blocks[i] = block;  
    } 

    //initialize bitmaps
    for(int i=0; i<NUM_DBLOCKS; i++) data_bitmap[i]=0;
    pthread_mutex_init(&data_bitmap_mutex,NULL);
    for(int i=0; i<NUM_INODES; i++) inode_bitmap[i]=0;
    pthread_mutex_init(&inode_bitmap_mutex,NULL);    

    //initialize inodes
    for(int i=0; i<NUM_INODES; i++){
        inodes[i].length=0;
        for(int j=0; j<NUM_POINTER; j++) 
            inodes[i].block[j]=-1; //pointer value -1 means the pointer is not used
        
    }
    pthread_mutex_init(&inodes_mutex,NULL); 

    //initialize open file table
    for(int i=0; i<NUM_OPEN_FILE; i++){
        struct open_file_entry entry=open_file_table[i];
        entry.used=0; //each entry is not used initially
        pthread_mutex_init(&entry.entry_mutex,NULL);
        entry.position=0;
        entry.access_flag=-1;
    }
    pthread_mutex_init(&open_file_table_mutex,NULL); 

    //initialize root directory
    root_dir.head = root_dir.tail = NULL;

    //initialize mutex_for_fs_stat
    pthread_mutex_init(&mutex_for_fs_stat,NULL);

    //return 0 means success
    return 0;
}


//create file with the provided file name
//if file does not exist, create the file and return 0;
//if file_name already exists, return -1; 
//otherwise, return -2.
int RSFS_create(char *file_name){

    //search root_dir for dir_entry matching provided file_name
    struct dir_entry *dir_entry = search_dir(file_name);

    if(dir_entry){//already exists
        printf("[create] file (%s) already exists.\n", file_name);
        return -1;
    }else{

        if(DEBUG) printf("[create] file (%s) does not exist.\n", file_name);

        //construct and insert a new dir_entry with given file_name
        dir_entry = insert_dir(file_name);
        if(DEBUG) printf("[create] insert a dir_entry with file_name:%s.\n", dir_entry->name);
        
        //access inode-bitmap to get a free inode 
        int inode_number = allocate_inode();
        if(inode_number<0){
            printf("[create] fail to allocate an inode.\n");
            return -2;
        } 
        if(DEBUG) printf("[create] allocate inode with number:%d.\n", inode_number);

        //save inode-number to dir-entry
        dir_entry->inode_number = inode_number;
        
        return 0;
    }
}

//open a file with RSFS_RDONLY or RSFS_RDWR flags
//return the file descriptor (i.e., the index of the open file entry in the open file table) if succeed, or -1 in case of error
int RSFS_open(char *file_name, int access_flag)
{
    //sanity test: access_flag should be either RSFS_RDONLY or RSFS_RDWR
    if (access_flag != RSFS_RDONLY && access_flag != RSFS_RDWR)
    {
        return -1;
    }
    
    //find dir_entry matching file_name
    struct dir_entry * mydir = search_dir(file_name);
    
    //find the corresponding inode
    //find an unused open-file-entry in open-file-table and use it
    //return the file descriptor (i.e., the index of the open file entry in the open file table)
    return allocate_open_file_entry(access_flag, mydir);
}



//read from file: read up to size bytes from the current position of the file of descriptor fd to buf;
//read will not go beyond the end of the file; 
//return the number of bytes actually read if succeed, or -1 in case of error.
int RSFS_read(int fd, void *buf, int size)
{
    //sanity test of fd and size
    if (fd < 0 || fd >= NUM_OPEN_FILE || !open_file_table[fd].used || size < 0)
    {
        return -1;
    }

    //get the open file entry of fd
    struct open_file_entry * myentry = &open_file_table[fd];
    int rig_pos = myentry->position;

    //lock the open file entry
    pthread_mutex_lock(&myentry->entry_mutex);
    
    //get the dir entry
    //get the inode
    struct inode * mynode = &inodes[myentry->dir_entry->inode_number];

    //copy data from the data block(s) to buf and update current position
    for (int i = 0; i < min(size, mynode->length - myentry->position); i++)
    {
        int block = (i + myentry->position) / BLOCK_SIZE;
        int offset = (i + myentry->position) % BLOCK_SIZE;
        memcpy(buf + i, data_blocks[mynode->block[block]] + offset, 1);
    }
    int read_delta = min(size, mynode->length - myentry->position);
    myentry->position += read_delta;
    
    //unlock the open file entry
    pthread_mutex_unlock(&myentry->entry_mutex);

    //return the current position
    return read_delta;
}

//write file: write size bytes from buf to the file with fd
//return the number of bytes that have been written if succeed; return -1 in case of error
int RSFS_write(int fd, void *buf, int size)
{
    //sanity test of fd and size
    if (fd < 0 || fd >= NUM_OPEN_FILE || !open_file_table[fd].used || size < 0)
    {
        return -1;
    }

    //get the open file entry
    struct open_file_entry * myentry = &open_file_table[fd];

    //lock the open file entry
    pthread_mutex_lock(&myentry->entry_mutex);

    //get the dir entry
    //get the inode
    struct inode * mynode = &inodes[myentry->dir_entry->inode_number];

    //copy data from buf to the data block(s) and update current position;
    //new data blocks may be allocated for the file if needed
    for (int i = 0; i < min(size, (NUM_POINTER * BLOCK_SIZE) - myentry->position); i++)
    {
        int block = (i + myentry->position) / BLOCK_SIZE;
        int offset = (i + myentry->position) % BLOCK_SIZE;
        

        if (mynode->block[block] < 0)
        {
            int block_num = allocate_data_block();
            if (block_num == -1)
            {
                // fs out of data blocks, return error
                mynode->length += (i - (myentry->position - mynode->length));
                myentry->position += i;
                pthread_mutex_unlock(&myentry->entry_mutex);
                return -1;
            }
            data_blocks[block_num] = (void *)malloc(BLOCK_SIZE);
            mynode->block[block] = block_num;
        }
        void * pos = data_blocks[mynode->block[block]];
        memcpy(data_blocks[mynode->block[block]] + offset, buf + i, 1);
    }

    int size_delta = min(size, (NUM_POINTER * BLOCK_SIZE) - myentry->position);
    mynode->length += size_delta - (myentry->position - mynode->length);
    myentry->position += size_delta;

    //unlock the open file entry
    pthread_mutex_unlock(&myentry->entry_mutex);

    //return the current position
    return size_delta;
}

//update current position: return the current position
// if the position is not updated, return the original position
//if whence == RSFS_SEEK_SET, change the position to offset
//if whence == RSFS_SEEK_CUR, change the position to position+offset
//if whence == RSFS_SEEK_END, change hte position to END-OF-FILE-Position + offset
//position cannot be out of the file's range; otherwise, it is not updated
int RSFS_fseek(int fd, int offset, int whence){

    //sanity test of fd and whence    
    if ((fd < 0 || fd >= NUM_OPEN_FILE || !open_file_table[fd].used) || (whence != RSFS_SEEK_SET && whence != RSFS_SEEK_CUR && whence != RSFS_SEEK_END))
    {
        return -1;
    }

    //get the open file entry of fd
    struct open_file_entry * myentry = &open_file_table[fd];
    
    //lock the entry
    pthread_mutex_lock(&myentry->entry_mutex);
    
    //get the current position
    int pos = myentry->position;
    
    //get the dir entry    
    //get the inode 
    struct inode * mynode = &inodes[myentry->dir_entry->inode_number];

    //change the position
    switch(whence) 
    {
        case RSFS_SEEK_SET:
            myentry->position = offset;
            break;
        case RSFS_SEEK_CUR:
            myentry->position += offset;
            break;
        case RSFS_SEEK_END:
            myentry->position = mynode->length + offset;
            break;
    }
    
    //unlock the entry
    pthread_mutex_unlock(&myentry->entry_mutex);
    
    //return the current position
    return myentry->position;
}


//close file: return 0 if succeed, or -1 if fd is invalid
int RSFS_close(int fd){

    //sanity test of fd and whence
    if (fd < 0 || fd >= NUM_OPEN_FILE || !open_file_table[fd].used)
    {
        return -1;
    }

    //free the open file entry
    free_open_file_entry(fd);
    return 0;
}


//delete file with provided file name: return 0 if succeed, or -1 in case of error
int RSFS_delete(char *file_name){

    //find the dir_entry; if find, continue, otherwise, return -1. 
    struct dir_entry * myentry = search_dir(file_name);
    if (myentry == NULL)
    {
        return -1;
    }
    
    //find the inode
    struct inode * mynode = &inodes[myentry->inode_number];

    //find the data blocks, free them in data-bitmap
    for (int i = 0; i < NUM_POINTER; i++)
    {
        if (mynode->block[i] >= 0)
        {
            free(data_blocks[mynode->block[i]]);
            free_data_block(mynode->block[i]);
            mynode->block[i] = -1;
        }
    }
    mynode->length = 0;

    //free the inode in inode-bitmap
    free_inode(myentry->inode_number);

    //free the dir_entry
    delete_dir(file_name);

    return 0;
}


//print status of the file system
void RSFS_stat(){

    pthread_mutex_lock(&mutex_for_fs_stat);

    printf("\nCurrent status of the file system:\n\n %16s%10s%10s\n", "File Name", "Length", "iNode #");

    //list files
    struct dir_entry *dir_entry = root_dir.head;
    while(dir_entry!=NULL){

        int inode_number = dir_entry->inode_number;
        struct inode *inode = &inodes[inode_number];
        
        printf("%16s%10d%10d\n", dir_entry->name, inode->length, inode_number);
        dir_entry = dir_entry->next;
    }
    
    //data blocks
    int db_used=0;
    for(int i=0; i<NUM_DBLOCKS; i++) db_used+=data_bitmap[i];
    printf("\nTotal Data Blocks: %4d,  Used: %d,  Unused: %d\n", NUM_DBLOCKS, db_used, NUM_DBLOCKS-db_used);

    //inodes
    int inodes_used=0;
    for(int i=0; i<NUM_INODES; i++) inodes_used+=inode_bitmap[i];
    printf("Total iNode Blocks: %3d,  Used: %d,  Unused: %d\n", NUM_INODES, inodes_used, NUM_INODES-inodes_used);

    //open files
    int of_num=0;
    for(int i=0; i<NUM_OPEN_FILE; i++) of_num+=open_file_table[i].used;
    printf("Total Opened Files: %3d\n\n", of_num);

    pthread_mutex_unlock(&mutex_for_fs_stat);
}
