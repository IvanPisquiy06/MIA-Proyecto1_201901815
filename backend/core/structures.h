#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <ctime>
#include <cstring>
#include <string>

struct Partition {
    char part_status;
    char part_type;
    char part_fit;
    int part_start;
    int part_size;
    char part_name[16];

    Partition() {
        part_status = '0';
        part_type = '\0';
        part_fit = '\0';
        part_start = -1;
        part_size = 0;
        memset(part_name, 0, sizeof(part_name));
    }
};

struct MBR {
    int mbr_size;
    time_t mbr_creation_date;
    int mbr_disk_signature;
    char disk_fit;
    Partition mbr_partitions[4];

    MBR() {
        mbr_size = 0;
        mbr_creation_date = time(nullptr);
        mbr_disk_signature = rand();
        disk_fit = 'F';
    }
};

struct EBR {
    char part_status;
    char part_fit;
    int part_start;
    int part_size;
    int part_next;
    char part_name[16];

    EBR() {
        part_status = '0';
        part_fit = 'F';
        part_start = -1;
        part_size = 0;
        part_next = -1;
        memset(part_name, 0, sizeof(part_name));
    }
};

struct MountedPartition {
        std::string path;
        std::string name;
        std::string id;
        char type;
        int start;
        int size;
};

struct Superblock {
    int s_filesystem_type;
    int s_inodes_count;
    int s_blocks_count;
    int s_free_blocks_count;
    int s_free_inodes_count;
    time_t s_mtime;
    time_t s_umtime;
    int s_mnt_count;
    int s_magic;
    int s_inode_size;
    int s_block_size; 
    int s_first_ino;   
    int s_first_blo;
    int s_bm_inode_start;
    int s_bm_block_start;
    int s_inode_start;
    int s_block_start;
};

struct Inode {
    int i_uid;
    int i_gid;
    int i_size;
    time_t i_atime;
    time_t i_ctime;
    time_t i_mtime;
    int i_block[15];
    char i_type;     
    int i_perm;     
};

struct Content {
    char b_name[12];
    int b_inodo;
};

struct FolderBlock {
    Content b_content[4];
};

struct FileBlock {
    char b_content[64];
};

struct PointerBlock {
    int b_pointers[16];
};

struct ActiveSession {
    bool is_logged_in = false;
    std::string username;
    int uid;
    int gid;
    std::string partition_id;
};

inline ActiveSession& getSession() {
    static ActiveSession session;
    return session;
}

inline ActiveSession currentSession;

#endif // STRUCTURES_H