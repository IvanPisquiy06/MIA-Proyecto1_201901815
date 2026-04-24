#ifndef CHMOD_H
#define CHMOD_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include "../core/structures.h"
#include "mount.h"

namespace CommandChmod {

    inline void chmodRecursivo(std::fstream& file, const Superblock& sb, int inodeIdx, int nuevosPermisos) {
        Inode currentInode;
        file.seekg(sb.s_inode_start + (inodeIdx * sizeof(Inode)), std::ios::beg);
        file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

        currentInode.i_perm = nuevosPermisos;
        file.seekp(sb.s_inode_start + (inodeIdx * sizeof(Inode)), std::ios::beg);
        file.write(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

        if (currentInode.i_type == '0') {
            for (int i = 0; i < 15; i++) {
                if (currentInode.i_block[i] != -1) {
                    FolderBlock fb;
                    file.seekg(sb.s_block_start + (currentInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                    file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                    for (int j = 0; j < 4; j++) {
                        if (fb.b_content[j].b_inodo != -1) {
                            std::string entryName = fb.b_content[j].b_name;
                            
                            if (entryName == "." || entryName == "..") continue;

                            chmodRecursivo(file, sb, fb.b_content[j].b_inodo, nuevosPermisos);
                        }
                    }
                }
            }
        }
    }

    inline std::string execute(const std::string& path, const std::string& ugo, bool recursive) {
        try {
            if (path.empty() || ugo.empty()) return "Error: Parámetros -path y -ugo son obligatorios.";
            if (!::getSession().is_logged_in) return "Error: No hay sesión activa.";
            
            if (ugo.length() != 3 || !isdigit(ugo[0]) || !isdigit(ugo[1]) || !isdigit(ugo[2])) {
                return "Error: El parámetro -ugo debe ser un número de 3 dígitos (ej. 777 o 664).";
            }

            int nuevosPermisos = std::stoi(ugo);

            if (::getSession().username != "root") {
                return "Error: Solo el usuario 'root' puede ejecutar el comando chmod.";
            }

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

            std::vector<std::string> dirs;
            std::stringstream pathStream(path);
            std::string item;
            while (std::getline(pathStream, item, '/')) if (!item.empty()) dirs.push_back(item);

            int targetInodeIdx = 0; // Empezamos en la raíz
            Inode targetInode;
            file.seekg(sb.s_inode_start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&targetInode), sizeof(Inode));

            for (const std::string& dirName : dirs) {
                bool found = false;
                for (int i = 0; i < 15; i++) {
                    if (targetInode.i_block[i] != -1) {
                        FolderBlock fb;
                        file.seekg(sb.s_block_start + (targetInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                        for (int j = 0; j < 4; j++) {
                            if (fb.b_content[j].b_inodo != -1 && strcmp(fb.b_content[j].b_name, dirName.c_str()) == 0) {
                                targetInodeIdx = fb.b_content[j].b_inodo;
                                file.seekg(sb.s_inode_start + (targetInodeIdx * sizeof(Inode)), std::ios::beg);
                                file.read(reinterpret_cast<char*>(&targetInode), sizeof(Inode));
                                found = true; break;
                            }
                        }
                    }
                    if (found) break;
                }
                if (!found) {
                    file.close();
                    return "Error: La ruta '" + path + "' no existe.";
                }
            }

            if (recursive) {
                // Si es recursivo, llamamos a la función que recorre el árbol
                chmodRecursivo(file, sb, targetInodeIdx, nuevosPermisos);
            } else {
                // Si no es recursivo, solo cambiamos el de este inodo
                targetInode.i_perm = nuevosPermisos;
                file.seekp(sb.s_inode_start + (targetInodeIdx * sizeof(Inode)), std::ios::beg);
                file.write(reinterpret_cast<char*>(&targetInode), sizeof(Inode));
            }

            file.close();
            
            std::string msg = "Se cambiaron los permisos a '" + ugo + "' para la ruta " + path;
            if (recursive) msg += " (de forma recursiva).";
            return msg;

        } catch (const std::exception& e) {
            return std::string("Error fatal en chmod: ") + e.what();
        }
    }
}

#endif // CHMOD_H