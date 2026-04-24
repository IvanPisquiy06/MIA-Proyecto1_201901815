#ifndef MKFS_H
#define MKFS_H

#include <iostream>
#include <string>
#include <fstream>
#include <cmath>
#include <ctime>
#include <cstring>
#include "../core/structures.h"
#include "mount.h" 

namespace CommandMkfs {

    inline bool findPartitionByID(const std::string& id, MountedPartition& outPartition) {
        for (const auto& particion : CommandMount::mountedPartitions) {
            if (particion.second.id == id) {
                outPartition = particion.second;
                return true;
            }
        }
        return false;
    }

    inline std::string execute(const std::string& id, const std::string& type, const std::string& fs) {
        try {
            if(id.empty()){
                return "Error: El parámetro -id es obligatorio";
            }
            std::string typeStr = (type.empty() ? "full" : type);
            if(typeStr != "full"){
                return "Error: El parámetro -type debe ser 'full'";
            }

            std::string fsStr = (fs.empty() ? "2fs" : fs);
            if(fsStr != "2fs" && fsStr != "3fs"){
                return "Error: El parámetro -fs debe ser '2fs' o '3fs'";
            }

            MountedPartition partition;
            if (!findPartitionByID(id, partition)) {
                return "Error: No se encontró la partición montada con ID: " + id;
            }

            int n = 0;
            if(fsStr == "2fs") {
                n = floor((partition.size - sizeof(Superblock)) / 
                          (4 + sizeof(Inode) + 3 * sizeof(FolderBlock)));
            } else if (fsStr == "3fs") {
                n = floor((partition.size - sizeof(Superblock)) / 
                          (sizeof(Journal) + 4 + sizeof(Inode) + 3 * sizeof(FileBlock)));
            }

            if (n <= 0) {
                return "Error: La partición es demasiado pequeña.";
            }

            Superblock sb;
            sb.s_filesystem_type = (fsStr == "2fs") ? 2 : 3;
            sb.s_inodes_count = n;
            sb.s_blocks_count = 3 * n;
            sb.s_free_blocks_count = (3 * n) - 2;
            sb.s_free_inodes_count = n - 2;
            sb.s_mtime = time(nullptr);
            sb.s_umtime = 0;
            sb.s_mnt_count = 1;
            sb.s_magic = 0xEF53;
            sb.s_inode_size = sizeof(Inode);
            sb.s_block_size = sizeof(FolderBlock);
            sb.s_first_ino = 2; 
            sb.s_first_blo = 2;

            if(fsStr == "2fs"){
                sb.s_bm_inode_start = partition.start + sizeof(Superblock);
            } else if (fsStr == "3fs") {
                sb.s_bm_inode_start = partition.start + sizeof(Superblock) + (n * sizeof(Journal));
            }

            sb.s_bm_block_start = sb.s_bm_inode_start + n; 
            sb.s_inode_start = sb.s_bm_block_start + (3 * n); 
            sb.s_block_start = sb.s_inode_start + (n * sizeof(Inode));

            std::fstream file(partition.path, std::ios::in | std::ios::out | std::ios::binary);
            if (!file.is_open()) return "Error: No se pudo abrir el disco.";

            if(fsStr == "3fs"){
                Journal emptyJournal;
                emptyJournal.journal_estado = 0;
                memset(emptyJournal.journal_tipo_operacion, 0, sizeof(emptyJournal.journal_tipo_operacion));
                emptyJournal.journal_tipo = '-';
                memset(emptyJournal.journal_nombre, 0, sizeof(emptyJournal.journal_nombre));
                memset(emptyJournal.journal_contenido, 0, sizeof(emptyJournal.journal_contenido));
                emptyJournal.journal_fecha = time(nullptr);

                file.seekp(partition.start + sizeof(Superblock), std::ios::beg);
                for(int i = 0; i < n; i++) {
                    file.write(reinterpret_cast<char*>(&emptyJournal), sizeof(Journal));
                }
            }

            char zero = '0';
            file.seekp(sb.s_bm_inode_start, std::ios::beg);
            for(int i = 0; i < n; i++) file.write(&zero, 1);
            file.seekp(sb.s_bm_block_start, std::ios::beg);
            for(int i = 0; i < 3 * n; i++) file.write(&zero, 1);

            Inode rootInode;
            rootInode.i_uid = 1;
            rootInode.i_gid = 1;
            rootInode.i_size = 0; 
            rootInode.i_atime = sb.s_mtime;
            rootInode.i_ctime = sb.s_mtime;
            rootInode.i_mtime = sb.s_mtime;
            for(int i = 0; i < 15; i++) rootInode.i_block[i] = -1;
            rootInode.i_type = '0'; 
            rootInode.i_perm = 664;
            rootInode.i_block[0] = 0; 

            FolderBlock rootBlock;
            strcpy(rootBlock.b_content[0].b_name, ".");
            rootBlock.b_content[0].b_inodo = 0;
            strcpy(rootBlock.b_content[1].b_name, "..");
            rootBlock.b_content[1].b_inodo = 0;
            strcpy(rootBlock.b_content[2].b_name, "users.txt");
            rootBlock.b_content[2].b_inodo = 1;               
            strcpy(rootBlock.b_content[3].b_name, "");
            rootBlock.b_content[3].b_inodo = -1;

            std::string usersContent = "1,G,root\n1,U,root,root,123\n";
            
            Inode usersInode;
            usersInode.i_uid = 1;
            usersInode.i_gid = 1;
            usersInode.i_size = usersContent.length();
            usersInode.i_atime = sb.s_mtime;
            usersInode.i_ctime = sb.s_mtime;
            usersInode.i_mtime = sb.s_mtime;
            for(int i = 0; i < 15; i++) usersInode.i_block[i] = -1;
            usersInode.i_type = '1'; // '1' = Archivo
            usersInode.i_perm = 664;
            usersInode.i_block[0] = 1; // Apunta al bloque 1

            FileBlock usersBlock;
            memset(usersBlock.b_content, 0, sizeof(usersBlock.b_content)); // Limpiar basura
            strcpy(usersBlock.b_content, usersContent.c_str());

            char ocupado = '1';
            file.seekp(sb.s_bm_inode_start, std::ios::beg);
            file.write(&ocupado, 1); // Inodo 0
            file.write(&ocupado, 1); // Inodo 1
            
            file.seekp(sb.s_bm_block_start, std::ios::beg); 
            file.write(&ocupado, 1); // Bloque 0
            file.write(&ocupado, 1); // Bloque 1

            file.seekp(partition.start, std::ios::beg);
            file.write(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            file.seekp(sb.s_inode_start, std::ios::beg);
            file.write(reinterpret_cast<char*>(&rootInode), sizeof(Inode));
            file.write(reinterpret_cast<char*>(&usersInode), sizeof(Inode)); // Se escribe justo después

            file.seekp(sb.s_block_start, std::ios::beg);
            file.write(reinterpret_cast<char*>(&rootBlock), sizeof(FolderBlock));
            file.write(reinterpret_cast<char*>(&usersBlock), sizeof(FileBlock)); // Se escribe justo después

            file.close();

            std::string tipoFormateado = (fsStr == "2fs") ? "EXT2" : "EXT3";
            return "MKFS completado: Sistema de archivos " + tipoFormateado + " creado con /users.txt.";

        } catch (const std::exception& e) {
            return std::string("Error fatal en mkfs: ") + e.what();
        }
    }
}

#endif // MKFS_H