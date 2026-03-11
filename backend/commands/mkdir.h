#ifndef MKDIR_H
#define MKDIR_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <ctime>
#include <cstring>
#include "structures.h"
#include "mount.h"

namespace CommandMkdir {

    inline std::string execute(const std::string& path, bool p) {
        try {
            if (path.empty()) return "Error: El parámetro -path es obligatorio.";
            if (!::getSession().is_logged_in) return "Error: No hay una sesión activa. Use login primero.";

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

            // 1. Separar toda la ruta en carpetas
            std::vector<std::string> dirs;
            std::stringstream ss(path);
            std::string item;
            while (std::getline(ss, item, '/')) {
                if (!item.empty()) dirs.push_back(item);
            }

            if (dirs.empty()) return "Error: Ruta inválida.";

            // 2. Navegar y crear el árbol desde la Raíz (Inodo 0)
            int currentInodeIdx = 0; 
            Inode currentInode;
            file.seekg(sb.s_inode_start + (currentInodeIdx * sizeof(Inode)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

            for (size_t d = 0; d < dirs.size(); ++d) {
                std::string dirName = dirs[d];
                if (dirName.length() > 12) return "Error: El nombre del directorio '" + dirName + "' excede 12 caracteres.";

                bool isLast = (d == dirs.size() - 1); // ¿Es la carpeta final que queremos crear?
                bool found = false;
                int nextInodeIdx = -1;

                // Buscar la carpeta en el Inodo actual
                for (int i = 0; i < 12; i++) {
                    int blockIdx = currentInode.i_block[i];
                    if (blockIdx != -1) {
                        FolderBlock fb;
                        file.seekg(sb.s_block_start + (blockIdx * sizeof(FolderBlock)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                        for (int j = 0; j < 4; j++) {
                            if (fb.b_content[j].b_inodo != -1 && std::string(fb.b_content[j].b_name) == dirName) {
                                nextInodeIdx = fb.b_content[j].b_inodo;
                                found = true; break;
                            }
                        }
                    }
                    if (found) break;
                }

                if (found) {
                    if (isLast) {
                        return "Error: El directorio '" + path + "' ya existe.";
                    }
                } else {
                    if (!isLast && !p) {
                        return "Error: El directorio intermedio '" + dirName + "' no existe y no se uso -p.";
                    }

                    // --- CREAR EL NUEVO DIRECTORIO ---
                    int newDirInodeIdx = -1; char bit;
                    for (int i = 0; i < sb.s_inodes_count; i++) {
                        file.seekg(sb.s_bm_inode_start + i, std::ios::beg); file.read(&bit, 1);
                        if (bit == '0') { newDirInodeIdx = i; break; }
                    }
                    if (newDirInodeIdx == -1) return "Error: No hay Inodos libres.";
                    
                    char ocupado = '1';
                    file.seekp(sb.s_bm_inode_start + newDirInodeIdx, std::ios::beg); file.write(&ocupado, 1);
                    sb.s_free_inodes_count--;

                    int newDirBlockIdx = -1;
                    for (int i = 0; i < sb.s_blocks_count; i++) {
                        file.seekg(sb.s_bm_block_start + i, std::ios::beg); file.read(&bit, 1);
                        if (bit == '0') { newDirBlockIdx = i; break; }
                    }
                    if (newDirBlockIdx == -1) return "Error: No hay Bloques libres.";

                    file.seekp(sb.s_bm_block_start + newDirBlockIdx, std::ios::beg); file.write(&ocupado, 1);
                    sb.s_free_blocks_count--;

                    // Llenar el bloque de carpeta con "." y ".."
                    FolderBlock newFb;
                    for (int k = 0; k < 4; k++) newFb.b_content[k].b_inodo = -1;
                    strcpy(newFb.b_content[0].b_name, "."); newFb.b_content[0].b_inodo = newDirInodeIdx;
                    strcpy(newFb.b_content[1].b_name, ".."); newFb.b_content[1].b_inodo = currentInodeIdx;

                    file.seekp(sb.s_block_start + (newDirBlockIdx * sizeof(FolderBlock)), std::ios::beg);
                    file.write(reinterpret_cast<char*>(&newFb), sizeof(FolderBlock));

                    // Crear el Inodo
                    Inode newDirInode;
                    newDirInode.i_uid = ::getSession().uid;
                    newDirInode.i_gid = ::getSession().gid;
                    newDirInode.i_size = 0; 
                    newDirInode.i_type = '0'; // Tipo 0 = Carpeta
                    newDirInode.i_atime = newDirInode.i_ctime = newDirInode.i_mtime = time(nullptr);
                    for (int k = 0; k < 15; k++) newDirInode.i_block[k] = -1;
                    newDirInode.i_block[0] = newDirBlockIdx;

                    file.seekp(sb.s_inode_start + (newDirInodeIdx * sizeof(Inode)), std::ios::beg);
                    file.write(reinterpret_cast<char*>(&newDirInode), sizeof(Inode));

                    // Enlazar este nuevo directorio al directorio padre (currentInode)
                    bool linked = false;
                    for (int i = 0; i < 12; i++) {
                        int pBlockIdx = currentInode.i_block[i];
                        if (pBlockIdx != -1) {
                            FolderBlock pFb;
                            file.seekg(sb.s_block_start + (pBlockIdx * sizeof(FolderBlock)), std::ios::beg);
                            file.read(reinterpret_cast<char*>(&pFb), sizeof(FolderBlock));
                            for (int j = 0; j < 4; j++) {
                                if (pFb.b_content[j].b_inodo == -1) {
                                    strcpy(pFb.b_content[j].b_name, dirName.c_str());
                                    pFb.b_content[j].b_inodo = newDirInodeIdx;
                                    file.seekp(sb.s_block_start + (pBlockIdx * sizeof(FolderBlock)), std::ios::beg);
                                    file.write(reinterpret_cast<char*>(&pFb), sizeof(FolderBlock));
                                    linked = true; break;
                                }
                            }
                        } else {
                            // Si el bloque del padre no existe, le asignamos uno nuevo
                            int nBlockIdx = -1;
                            for (int k = 0; k < sb.s_blocks_count; k++) {
                                file.seekg(sb.s_bm_block_start + k, std::ios::beg); file.read(&bit, 1);
                                if (bit == '0') { nBlockIdx = k; break; }
                            }
                            file.seekp(sb.s_bm_block_start + nBlockIdx, std::ios::beg); file.write(&ocupado, 1);
                            sb.s_free_blocks_count--;

                            FolderBlock pFb;
                            for (int k = 0; k < 4; k++) pFb.b_content[k].b_inodo = -1;
                            strcpy(pFb.b_content[0].b_name, dirName.c_str());
                            pFb.b_content[0].b_inodo = newDirInodeIdx;
                            file.seekp(sb.s_block_start + (nBlockIdx * sizeof(FolderBlock)), std::ios::beg);
                            file.write(reinterpret_cast<char*>(&pFb), sizeof(FolderBlock));

                            currentInode.i_block[i] = nBlockIdx;
                            file.seekp(sb.s_inode_start + (currentInodeIdx * sizeof(Inode)), std::ios::beg);
                            file.write(reinterpret_cast<char*>(&currentInode), sizeof(Inode));
                            linked = true; break;
                        }
                        if (linked) break;
                    }

                    nextInodeIdx = newDirInodeIdx;
                }
                
                // Avanzamos al inodo que acabamos de encontrar o crear
                currentInodeIdx = nextInodeIdx;
                file.seekg(sb.s_inode_start + (currentInodeIdx * sizeof(Inode)), std::ios::beg);
                file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));
            }

            // Actualizar el superbloque al final de todo el proceso
            file.seekp(partition.start, std::ios::beg);
            file.write(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            file.close();

            return "Directorio '" + path + "' creado exitosamente.";

        } catch (const std::exception& e) {
            return std::string("Error fatal en mkdir: ") + e.what();
        }
    }
}

#endif // MKDIR_H