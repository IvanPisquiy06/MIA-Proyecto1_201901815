#ifndef MOVE_H
#define MOVE_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include "../core/structures.h"
#include "mount.h"

namespace CommandMove {

    inline std::string execute(const std::string& path, const std::string& destino) {
        try {
            if (path.empty() || destino.empty()) return "Error: Parámetros -path y -destino son obligatorios.";
            if (!::getSession().is_logged_in) return "Error: No hay sesión activa.";

            std::string id = ::getSession().partition_id;
            MountedPartition partition;
            bool encontrada = false;
            for (const auto& p : CommandMount::mountedPartitions) {
                if (p.second.id == id) { partition = p.second; encontrada = true; break; }
            }
            if (!encontrada) return "Error: Partición no montada.";

            std::fstream file(partition.path, std::ios::in | std::ios::out | std::ios::binary);
            if (!file.is_open()) return "Error: No se pudo abrir el disco.";

            Superblock sb;
            file.seekg(partition.start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            // --- 1. BUSCAR Y DESCONECTAR DEL ORIGEN ---
            std::vector<std::string> srcDirs;
            std::stringstream ssSrc(path);
            std::string item;
            while (std::getline(ssSrc, item, '/')) if (!item.empty()) srcDirs.push_back(item);
            
            std::string targetName = srcDirs.back();
            srcDirs.pop_back();

            // Navegación al padre origen (reutilizando tu lógica)
            int srcParentIdx = 0;
            Inode srcParentInode;
            file.seekg(sb.s_inode_start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&srcParentInode), sizeof(Inode));

            for (const std::string& d : srcDirs) {
                bool f = false;
                for (int i=0; i<15; i++) {
                    if (srcParentInode.i_block[i] != -1) {
                        FolderBlock fb;
                        file.seekg(sb.s_block_start + (srcParentInode.i_block[i]*sizeof(FolderBlock)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                        for (int j=0; j<4; j++) {
                            if (fb.b_content[j].b_inodo != -1 && strcmp(fb.b_content[j].b_name, d.c_str()) == 0) {
                                srcParentIdx = fb.b_content[j].b_inodo;
                                file.seekg(sb.s_inode_start + (srcParentIdx*sizeof(Inode)), std::ios::beg);
                                file.read(reinterpret_cast<char*>(&srcParentInode), sizeof(Inode));
                                f = true; break;
                            }
                        }
                    }
                    if (f) break;
                }
            }

            int movedInodeIdx = -1;
            bool disconnected = false;
            for (int i=0; i<15; i++) {
                if (srcParentInode.i_block[i] != -1) {
                    FolderBlock fb;
                    file.seekg(sb.s_block_start + (srcParentInode.i_block[i]*sizeof(FolderBlock)), std::ios::beg);
                    file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                    for (int j=0; j<4; j++) {
                        if (fb.b_content[j].b_inodo != -1 && strcmp(fb.b_content[j].b_name, targetName.c_str()) == 0) {
                            movedInodeIdx = fb.b_content[j].b_inodo;
                            fb.b_content[j].b_inodo = -1; // Desconectamos
                            strcpy(fb.b_content[j].b_name, "");
                            file.seekp(sb.s_block_start + (srcParentInode.i_block[i]*sizeof(FolderBlock)), std::ios::beg);
                            file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                            disconnected = true; break;
                        }
                    }
                }
                if (disconnected) break;
            }

            if (movedInodeIdx == -1) { file.close(); return "Error: No se encontró el origen."; }

            // --- 2. CONECTAR EN EL DESTINO ---
            std::vector<std::string> destDirs;
            std::stringstream ssDest(destino);
            while (std::getline(ssDest, item, '/')) if (!item.empty()) destDirs.push_back(item);

            int destInodeIdx = 0;
            Inode destInode;
            file.seekg(sb.s_inode_start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&destInode), sizeof(Inode));

            for (const std::string& d : destDirs) {
                bool f = false;
                for (int i=0; i<15; i++) {
                    if (destInode.i_block[i] != -1) {
                        FolderBlock fb;
                        file.seekg(sb.s_block_start + (destInode.i_block[i]*sizeof(FolderBlock)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                        for (int j=0; j<4; j++) {
                            if (fb.b_content[j].b_inodo != -1 && strcmp(fb.b_content[j].b_name, d.c_str()) == 0) {
                                destInodeIdx = fb.b_content[j].b_inodo;
                                file.seekg(sb.s_inode_start + (destInodeIdx*sizeof(Inode)), std::ios::beg);
                                file.read(reinterpret_cast<char*>(&destInode), sizeof(Inode));
                                f = true; break;
                            }
                        }
                    }
                    if (f) break;
                }
            }

            // Buscar espacio en los bloques de la carpeta destino para insertar el inodo
            bool connected = false;
            for (int i=0; i<15; i++) {
                if (destInode.i_block[i] != -1) {
                    FolderBlock fb;
                    file.seekg(sb.s_block_start + (destInode.i_block[i]*sizeof(FolderBlock)), std::ios::beg);
                    file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                    for (int j=0; j<4; j++) {
                        if (fb.b_content[j].b_inodo == -1) {
                            fb.b_content[j].b_inodo = movedInodeIdx;
                            strncpy(fb.b_content[j].b_name, targetName.c_str(), 11);
                            file.seekp(sb.s_block_start + (destInode.i_block[i]*sizeof(FolderBlock)), std::ios::beg);
                            file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                            connected = true; break;
                        }
                    }
                }
                if (connected) break;
            }

            file.close();
            if (!connected) return "Error: Carpeta destino llena o no encontrada.";
            
            return "Elemento movido exitosamente a: " + destino;

        } catch (const std::exception& e) {
            return std::string("Error fatal en move: ") + e.what();
        }
    }
}

#endif