#ifndef CHOWN_H
#define CHOWN_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include "../core/structures.h"
#include "mount.h"
#include "cat.h"

namespace CommandChown {

    inline void chownRecursivo(std::fstream& file, const Superblock& sb, int inodeIdx, int nuevoUID) {
        Inode currentInode;
        file.seekg(sb.s_inode_start + (inodeIdx * sizeof(Inode)), std::ios::beg);
        file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

        currentInode.i_uid = nuevoUID;
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

                            chownRecursivo(file, sb, fb.b_content[j].b_inodo, nuevoUID);
                        }
                    }
                }
            }
        }
    }

    inline std::string execute(const std::string& path, const std::string& user, bool recursive) {
        try {
            if (path.empty() || user.empty()) return "Error: Parámetros -path y -usr son obligatorios.";
            if (!::getSession().is_logged_in) return "Error: No hay sesión activa.";
            
            if (::getSession().username != "root") {
                return "Error: Solo el usuario 'root' puede ejecutar el comando chown.";
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

            int nuevoUID = -1;
            
            int usersInodeIndex = CommandCat::getInodeByPath(file, sb, "users.txt");
            if (usersInodeIndex == -1) {
                file.close();
                return "Error Crítico: No se encontró /users.txt.";
            }

            Inode usersInode;
            file.seekg(sb.s_inode_start + (usersInodeIndex * sizeof(Inode)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

            std::string content = "";
            int bytesRead = 0;
            for (int b = 0; b < 12; b++) {
                int blockIndex = usersInode.i_block[b];
                if (blockIndex != -1) {
                    FileBlock fileBlock;
                    file.seekg(sb.s_block_start + (blockIndex * sizeof(FileBlock)), std::ios::beg);
                    file.read(reinterpret_cast<char*>(&fileBlock), sizeof(FileBlock));
                    for (int c = 0; c < 64 && bytesRead < usersInode.i_size; c++) {
                        content += fileBlock.b_content[c];
                        bytesRead++;
                    }
                }
            }

            std::stringstream ss(content);
            std::string line;
            while (std::getline(ss, line, '\n')) {
                if (line.empty() || line[0] == '0') continue;

                std::vector<std::string> tokens;
                std::stringstream lineStream(line);
                std::string token;
                while (std::getline(lineStream, token, ',')) {
                    tokens.push_back(token);
                }

                if (tokens.size() >= 5 && tokens[1] == "U") {
                    if (tokens[3] == user) {
                        nuevoUID = std::stoi(tokens[0]);
                        break;
                    }
                }
            }

            if (nuevoUID == -1) {
                file.close();
                return "Error: El usuario '" + user + "' no existe en el sistema.";
            }

            std::vector<std::string> dirs;
            std::stringstream pathStream(path);
            std::string item;
            while (std::getline(pathStream, item, '/')) if (!item.empty()) dirs.push_back(item);

            int targetInodeIdx = 0; 
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
                chownRecursivo(file, sb, targetInodeIdx, nuevoUID);
            } else {
                targetInode.i_uid = nuevoUID;
                file.seekp(sb.s_inode_start + (targetInodeIdx * sizeof(Inode)), std::ios::beg);
                file.write(reinterpret_cast<char*>(&targetInode), sizeof(Inode));
            }

            file.close();
            
            std::string msg = "Se cambió el propietario a '" + user + "' para la ruta " + path;
            if (recursive) msg += " (de forma recursiva).";
            return msg;

        } catch (const std::exception& e) {
            return std::string("Error fatal en chown: ") + e.what();
        }
    }
}

#endif // CHOWN_H