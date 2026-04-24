#ifndef REMOVE_H
#define REMOVE_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include "../core/structures.h"
#include "mount.h"
#include "../core/utilities.h"

namespace CommandRemove {

    inline std::string execute(const std::string& path) {
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

            // 1. Separar la ruta para aislar al "padre" y al "objetivo"
            std::vector<std::string> dirs;
            std::stringstream ss(path);
            std::string item;
            while (std::getline(ss, item, '/')) {
                if (!item.empty()) dirs.push_back(item);
            }

            if (dirs.empty()) return "Error: Ruta inválida.";

            // El último elemento es el que vamos a borrar (ej: "users.txt" o "carpeta1")
            std::string targetName = dirs.back();
            dirs.pop_back();

            // 2. Navegar hasta el Inodo Padre
            int currentInodeIdx = 0; // Raíz
            Inode currentInode;
            file.seekg(sb.s_inode_start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

            for (const std::string& dirName : dirs) {
                bool found = false;
                for (int i = 0; i < 15; i++) {
                    if (currentInode.i_block[i] != -1) {
                        FolderBlock fb;
                        file.seekg(sb.s_block_start + (currentInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                        for (int j = 0; j < 4; j++) {
                            if (fb.b_content[j].b_inodo != -1 && strcmp(fb.b_content[j].b_name, dirName.c_str()) == 0) {
                                currentInodeIdx = fb.b_content[j].b_inodo;
                                file.seekg(sb.s_inode_start + (currentInodeIdx * sizeof(Inode)), std::ios::beg);
                                file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));
                                found = true;
                                break;
                            }
                        }
                    }
                    if (found) break;
                }
                if (!found) {
                    file.close();
                    return "Error: La ruta padre no existe.";
                }
            }

            // 3. Buscar el Objetivo dentro de los bloques de la Carpeta Padre
            int targetInodeIdx = -1;
            bool targetFound = false;

            for (int i = 0; i < 15; i++) {
                if (currentInode.i_block[i] != -1) {
                    FolderBlock fb;
                    file.seekg(sb.s_block_start + (currentInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                    file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                    for (int j = 0; j < 4; j++) {
                        if (fb.b_content[j].b_inodo != -1 && strcmp(fb.b_content[j].b_name, targetName.c_str()) == 0) {
                            targetInodeIdx = fb.b_content[j].b_inodo;
                            
                            //Desvinculamos el inodo
                            fb.b_content[j].b_inodo = -1;
                            strcpy(fb.b_content[j].b_name, "");
                            
                            // Guardamos el bloque padre modificado
                            file.seekp(sb.s_block_start + (currentInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                            file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                            
                            targetFound = true;
                            break;
                        }
                    }
                }
                if (targetFound) break;
            }

            if (!targetFound) {
                file.close();
                return "Error: El archivo o carpeta '" + targetName + "' no existe.";
            }

            // 4. Leer el Inodo del Objetivo para liberar sus recursos
            Inode targetInode;
            file.seekg(sb.s_inode_start + (targetInodeIdx * sizeof(Inode)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&targetInode), sizeof(Inode));

            char zero = '0';
            
            // 4.1 Liberar los bloques de datos usados por el objetivo en el Bitmap de Bloques
            for (int i = 0; i < 15; i++) {
                if (targetInode.i_block[i] != -1) {
                    file.seekp(sb.s_bm_block_start + targetInode.i_block[i], std::ios::beg);
                    file.write(&zero, 1);
                    sb.s_free_blocks_count++;
                }
            }

            // 4.2 Liberar el Inodo en el Bitmap de Inodos
            file.seekp(sb.s_bm_inode_start + targetInodeIdx, std::ios::beg);
            file.write(&zero, 1);
            sb.s_free_inodes_count++;

            // 5. Actualizar el Superbloque
            file.seekp(partition.start, std::ios::beg);
            file.write(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            // 6. Opcional: Registrar en el Journaling para EXT3
            // registrarEnJournal(partition.path, partition.start, "remove", targetInode.i_type, path, "");

            file.close();
            return "Elemento '" + path + "' eliminado exitosamente.";

        } catch (const std::exception& e) {
            return std::string("Error fatal en remove: ") + e.what();
        }
    }
}

#endif // REMOVE_H