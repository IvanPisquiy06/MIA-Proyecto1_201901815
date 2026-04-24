#ifndef COPY_H
#define COPY_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include <ctime>
#include "../core/structures.h"
#include "mount.h"
#include "../core/utilities.h"

namespace CommandCopy {

    inline std::string execute(const std::string& path, const std::string& destino) {
        try {
            if (path.empty() || destino.empty()) return "Error: Faltan parámetros obligatorios (-path, -destino).";
            if (!::getSession().is_logged_in) return "Error: No hay una sesión activa.";

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

            // ==========================================
            // 1. BUSCAR EL ARCHIVO ORIGEN
            // ==========================================
            std::vector<std::string> srcDirs;
            std::stringstream ssSrc(path);
            std::string item;
            while (std::getline(ssSrc, item, '/')) {
                if (!item.empty()) srcDirs.push_back(item);
            }
            if (srcDirs.empty()) return "Error: Ruta origen inválida.";

            std::string targetName = srcDirs.back(); // El nombre del archivo a copiar
            srcDirs.pop_back();

            int srcParentInodeIdx = 0; 
            Inode srcParentInode;
            file.seekg(sb.s_inode_start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&srcParentInode), sizeof(Inode));

            // Navegar al padre del origen
            for (const std::string& dirName : srcDirs) {
                bool found = false;
                for (int i = 0; i < 15; i++) {
                    if (srcParentInode.i_block[i] != -1) {
                        FolderBlock fb;
                        file.seekg(sb.s_block_start + (srcParentInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                        for (int j = 0; j < 4; j++) {
                            if (fb.b_content[j].b_inodo != -1 && strcmp(fb.b_content[j].b_name, dirName.c_str()) == 0) {
                                srcParentInodeIdx = fb.b_content[j].b_inodo;
                                file.seekg(sb.s_inode_start + (srcParentInodeIdx * sizeof(Inode)), std::ios::beg);
                                file.read(reinterpret_cast<char*>(&srcParentInode), sizeof(Inode));
                                found = true; break;
                            }
                        }
                    }
                    if (found) break;
                }
                if (!found) { file.close(); return "Error: La ruta origen no existe."; }
            }

            // Obtener el Inodo del archivo original
            int srcInodeIdx = -1;
            for (int i = 0; i < 15; i++) {
                if (srcParentInode.i_block[i] != -1) {
                    FolderBlock fb;
                    file.seekg(sb.s_block_start + (srcParentInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                    file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                    for (int j = 0; j < 4; j++) {
                        if (fb.b_content[j].b_inodo != -1 && strcmp(fb.b_content[j].b_name, targetName.c_str()) == 0) {
                            srcInodeIdx = fb.b_content[j].b_inodo;
                            break;
                        }
                    }
                }
                if (srcInodeIdx != -1) break;
            }
            if (srcInodeIdx == -1) { file.close(); return "Error: El archivo origen no existe."; }

            Inode srcInode;
            file.seekg(sb.s_inode_start + (srcInodeIdx * sizeof(Inode)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&srcInode), sizeof(Inode));

            // ==========================================
            // 2. BUSCAR LA CARPETA DESTINO
            // ==========================================
            std::vector<std::string> destDirs;
            std::stringstream ssDest(destino);
            while (std::getline(ssDest, item, '/')) {
                if (!item.empty()) destDirs.push_back(item);
            }

            int destInodeIdx = 0;
            Inode destInode;
            file.seekg(sb.s_inode_start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&destInode), sizeof(Inode));

            for (const std::string& dirName : destDirs) {
                bool found = false;
                for (int i = 0; i < 15; i++) {
                    if (destInode.i_block[i] != -1) {
                        FolderBlock fb;
                        file.seekg(sb.s_block_start + (destInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                        for (int j = 0; j < 4; j++) {
                            if (fb.b_content[j].b_inodo != -1 && strcmp(fb.b_content[j].b_name, dirName.c_str()) == 0) {
                                destInodeIdx = fb.b_content[j].b_inodo;
                                file.seekg(sb.s_inode_start + (destInodeIdx * sizeof(Inode)), std::ios::beg);
                                file.read(reinterpret_cast<char*>(&destInode), sizeof(Inode));
                                found = true; break;
                            }
                        }
                    }
                    if (found) break;
                }
                if (!found) { file.close(); return "Error: La carpeta destino no existe."; }
            }

            // ==========================================
            // 3. CLONAR (Nuevo Inodo y Nuevos Bloques)
            // ==========================================
            int newInodeIdx = -1;
            char bit;
            // Buscar un Inodo libre
            for (int i = 0; i < sb.s_inodes_count; i++) {
                file.seekg(sb.s_bm_inode_start + i, std::ios::beg);
                file.read(&bit, 1);
                if (bit == '0') { newInodeIdx = i; break; }
            }
            if (newInodeIdx == -1) { file.close(); return "Error: No hay inodos libres."; }

            // Marcar inodo como ocupado
            char ocupado = '1';
            file.seekp(sb.s_bm_inode_start + newInodeIdx, std::ios::beg);
            file.write(&ocupado, 1);
            sb.s_free_inodes_count--;

            Inode newInode = srcInode; // Copiamos toda la metadata (tamaño, tipo, permisos)
            newInode.i_ctime = time(nullptr); // Actualizamos la fecha de creación de la copia
            for(int i=0; i<15; i++) newInode.i_block[i] = -1; // Limpiamos los bloques para asignar nuevos

            // Clonar los bloques de datos
            for (int i = 0; i < 15; i++) {
                if (srcInode.i_block[i] != -1) {
                    // Buscar bloque libre
                    int newBlockIdx = -1;
                    for (int j = 0; j < sb.s_blocks_count; j++) {
                        file.seekg(sb.s_bm_block_start + j, std::ios::beg);
                        file.read(&bit, 1);
                        if (bit == '0') { newBlockIdx = j; break; }
                    }
                    
                    if (newBlockIdx != -1) {
                        // Marcar bloque como ocupado
                        file.seekp(sb.s_bm_block_start + newBlockIdx, std::ios::beg);
                        file.write(&ocupado, 1);
                        sb.s_free_blocks_count--;

                        // Leer bloque original y escribirlo en el nuevo
                        FileBlock tempBlock;
                        file.seekg(sb.s_block_start + (srcInode.i_block[i] * sizeof(FileBlock)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&tempBlock), sizeof(FileBlock));

                        file.seekp(sb.s_block_start + (newBlockIdx * sizeof(FileBlock)), std::ios::beg);
                        file.write(reinterpret_cast<char*>(&tempBlock), sizeof(FileBlock));

                        newInode.i_block[i] = newBlockIdx; // Asignar el nuevo bloque al nuevo inodo
                    }
                }
            }

            // Guardar el nuevo inodo
            file.seekp(sb.s_inode_start + (newInodeIdx * sizeof(Inode)), std::ios::beg);
            file.write(reinterpret_cast<char*>(&newInode), sizeof(Inode));

            // ==========================================
            // 4. VINCULAR EN LA CARPETA DESTINO
            // ==========================================
            bool linked = false;
            for (int i = 0; i < 15; i++) {
                if (destInode.i_block[i] != -1) {
                    FolderBlock fb;
                    file.seekg(sb.s_block_start + (destInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                    file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                    
                    for (int j = 0; j < 4; j++) {
                        if (fb.b_content[j].b_inodo == -1) {
                            fb.b_content[j].b_inodo = newInodeIdx;
                            strncpy(fb.b_content[j].b_name, targetName.c_str(), 11);
                            fb.b_content[j].b_name[11] = '\0';
                            
                            file.seekp(sb.s_block_start + (destInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                            file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                            linked = true; break;
                        }
                    }
                }
                if (linked) break;
            }

            // Guardar superbloque actualizado
            file.seekp(partition.start, std::ios::beg);
            file.write(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            file.close();
            return "Archivo copiado exitosamente a '" + destino + "'.";

        } catch (const std::exception& e) {
            return std::string("Error fatal en copy: ") + e.what();
        }
    }
}

#endif // COPY_H