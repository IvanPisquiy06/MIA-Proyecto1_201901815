#ifndef RENAME_H
#define RENAME_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include "../core/structures.h"
#include "mount.h"
#include "../core/utilities.h"

// #include "../utils/journaling.h" // Si tienes Journaling

namespace CommandRename {

    inline std::string execute(const std::string& path, const std::string& newName) {
        try {
            if (path.empty() || newName.empty()) return "Error: Faltan parámetros obligatorios (-path, -name).";
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

            // 1. Separar la ruta para aislar al "padre" y al "viejo nombre"
            std::vector<std::string> dirs;
            std::stringstream ss(path);
            std::string item;
            while (std::getline(ss, item, '/')) {
                if (!item.empty()) dirs.push_back(item);
            }

            if (dirs.empty()) return "Error: Ruta inválida.";

            // El último elemento es el que queremos renombrar
            std::string oldName = dirs.back();
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

            // 3. Buscar el archivo/carpeta y RENOMBRARLO
            bool renamed = false;

            for (int i = 0; i < 15; i++) {
                if (currentInode.i_block[i] != -1) {
                    FolderBlock fb;
                    file.seekg(sb.s_block_start + (currentInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                    file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                    for (int j = 0; j < 4; j++) {
                        if (fb.b_content[j].b_inodo != -1 && strcmp(fb.b_content[j].b_name, oldName.c_str()) == 0) {
                            
                            // ¡LA MAGIA OCURRE AQUÍ! 
                            // Reemplazamos el string viejo por el nuevo
                            strncpy(fb.b_content[j].b_name, newName.c_str(), 11); 
                            fb.b_content[j].b_name[11] = '\0'; // Asegurar el fin de cadena por seguridad
                            
                            // Guardamos el bloque de la carpeta de vuelta en el disco
                            file.seekp(sb.s_block_start + (currentInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                            file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                            
                            renamed = true;
                            break;
                        }
                    }
                }
                if (renamed) break;
            }

            if (!renamed) {
                file.close();
                return "Error: El archivo o carpeta '" + oldName + "' no existe en esa ruta.";
            }

            registrarEnJournal(partition.path, partition.start, "rename", '-', path, newName);

            file.close();
            return "Renombrado exitoso: '" + oldName + "' ahora se llama '" + newName + "'.";

        } catch (const std::exception& e) {
            return std::string("Error fatal en rename: ") + e.what();
        }
    }
}

#endif // RENAME_H