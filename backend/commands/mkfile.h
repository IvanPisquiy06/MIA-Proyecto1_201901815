#ifndef MKFILE_H
#define MKFILE_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cmath>
#include <ctime>
#include <cstring>
#include "../core/structures.h"
#include "mount.h"

namespace CommandMkfile {

    inline std::string execute(const std::string& path, bool r, int size = 0, const std::string& cont = "") {
        try {
            if (path.empty()) return "Error: El parámetro -path es obligatorio.";
            if (!::getSession().is_logged_in) return "Error: No hay una sesión activa. Use login primero.";

            // --- NUEVA LÓGICA PARA -cont ---
            std::string fileContent = "";
            if (!cont.empty()) {
                std::ifstream physicalFile(cont);
                if (!physicalFile.is_open()) {
                    return "Error: No se pudo abrir el archivo físico en la ruta: " + cont;
                }
                std::stringstream buffer;
                buffer << physicalFile.rdbuf();
                fileContent = buffer.str();
                physicalFile.close();

                // Si el tamaño del archivo físico es mayor que el -size, ajustamos el size
                if (fileContent.length() > size) {
                    size = fileContent.length();
                }
            }
            // -------------------------------

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

            // 1. Separar la ruta
            std::string parentPath, fileName;
            size_t pos = path.find_last_of('/');
            if (pos != std::string::npos) {
                parentPath = path.substr(0, pos);
                fileName = path.substr(pos + 1);
            } else { return "Error: Ruta inválida."; }

            if (fileName.length() > 12) return "Error: El nombre del archivo excede 12 caracteres.";

            std::vector<std::string> dirs;
            std::stringstream ss(parentPath);
            std::string item;
            while (std::getline(ss, item, '/')) {
                if (!item.empty()) dirs.push_back(item);
            }

            // 2. Navegar/Crear el árbol de directorios
            int currentInodeIdx = 0; 
            Inode currentInode;
            file.seekg(sb.s_inode_start + (currentInodeIdx * sizeof(Inode)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

            for (const std::string& dirName : dirs) {
                bool found = false;
                int nextInodeIdx = -1;

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

                if (!found) {
                    if (!r) return "Error: El directorio '" + dirName + "' no existe y no se uso -r.";

                    // Crear Directorio
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

                    FolderBlock newFb;
                    for (int k = 0; k < 4; k++) newFb.b_content[k].b_inodo = -1;
                    strcpy(newFb.b_content[0].b_name, "."); newFb.b_content[0].b_inodo = newDirInodeIdx;
                    strcpy(newFb.b_content[1].b_name, ".."); newFb.b_content[1].b_inodo = currentInodeIdx;

                    file.seekp(sb.s_block_start + (newDirBlockIdx * sizeof(FolderBlock)), std::ios::beg);
                    file.write(reinterpret_cast<char*>(&newFb), sizeof(FolderBlock));

                    Inode newDirInode;
                    newDirInode.i_uid = ::getSession().uid;
                    newDirInode.i_gid = ::getSession().gid;
                    newDirInode.i_size = 0; newDirInode.i_type = '0';
                    newDirInode.i_atime = newDirInode.i_ctime = newDirInode.i_mtime = time(nullptr);
                    for (int k = 0; k < 15; k++) newDirInode.i_block[k] = -1;
                    newDirInode.i_block[0] = newDirBlockIdx;

                    file.seekp(sb.s_inode_start + (newDirInodeIdx * sizeof(Inode)), std::ios::beg);
                    file.write(reinterpret_cast<char*>(&newDirInode), sizeof(Inode));

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
                currentInodeIdx = nextInodeIdx;
                file.seekg(sb.s_inode_start + (currentInodeIdx * sizeof(Inode)), std::ios::beg);
                file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));
            }

            // 3. Crear el Archivo
            int newFileInodeIdx = -1; char bit;
            for (int i = 0; i < sb.s_inodes_count; i++) {
                file.seekg(sb.s_bm_inode_start + i, std::ios::beg); file.read(&bit, 1);
                if (bit == '0') { newFileInodeIdx = i; break; }
            }
            if (newFileInodeIdx == -1) return "Error: No hay Inodos libres.";

            char ocupado = '1';
            file.seekp(sb.s_bm_inode_start + newFileInodeIdx, std::ios::beg); file.write(&ocupado, 1);
            sb.s_free_inodes_count--;

            Inode newFileInode;
            newFileInode.i_uid = ::getSession().uid;
            newFileInode.i_gid = ::getSession().gid;
            newFileInode.i_size = size;
            newFileInode.i_type = '1'; 
            newFileInode.i_atime = newFileInode.i_ctime = newFileInode.i_mtime = time(nullptr);
            for (int i = 0; i < 15; i++) newFileInode.i_block[i] = -1;

            int requiredBlocks = std::ceil((double)size / 64.0);
            if (requiredBlocks > 12) return "Error: Archivo muy grande para bloques directos (por ahora).";

            int bytesWritten = 0; char textChar = '0';
            for (int b = 0; b < requiredBlocks; b++) {
                int newBlockIdx = -1;
                for (int i = 0; i < sb.s_blocks_count; i++) {
                    file.seekg(sb.s_bm_block_start + i, std::ios::beg); file.read(&bit, 1);
                    if (bit == '0') { newBlockIdx = i; break; }
                }
                file.seekp(sb.s_bm_block_start + newBlockIdx, std::ios::beg); file.write(&ocupado, 1);
                sb.s_free_blocks_count--;

                FileBlock fileBlock; memset(fileBlock.b_content, 0, 64);
                
                // --- APLICANDO EL CONTENIDO DEL ARCHIVO FÍSICO ---
                for (int c = 0; c < 64 && bytesWritten < size; c++) {
                    if (bytesWritten < fileContent.length()) {
                        fileBlock.b_content[c] = fileContent[bytesWritten];
                    } else {
                        // Si nos sobra -size, rellenamos con patrón numérico
                        fileBlock.b_content[c] = textChar;
                        textChar = (textChar == '9') ? '0' : textChar + 1;
                    }
                    bytesWritten++;
                }
                
                file.seekp(sb.s_block_start + (newBlockIdx * sizeof(FileBlock)), std::ios::beg);
                file.write(reinterpret_cast<char*>(&fileBlock), sizeof(FileBlock));
                newFileInode.i_block[b] = newBlockIdx;
            }

            file.seekp(sb.s_inode_start + (newFileInodeIdx * sizeof(Inode)), std::ios::beg);
            file.write(reinterpret_cast<char*>(&newFileInode), sizeof(Inode));

            // Enlazar al Padre
            bool slotFound = false;
            for (int i = 0; i < 12; i++) {
                int blockIdx = currentInode.i_block[i];
                if (blockIdx != -1) {
                    FolderBlock folderBlock;
                    file.seekg(sb.s_block_start + (blockIdx * sizeof(FolderBlock)), std::ios::beg);
                    file.read(reinterpret_cast<char*>(&folderBlock), sizeof(FolderBlock));
                    for (int j = 0; j < 4; j++) {
                        if (folderBlock.b_content[j].b_inodo == -1) {
                            strcpy(folderBlock.b_content[j].b_name, fileName.c_str());
                            folderBlock.b_content[j].b_inodo = newFileInodeIdx;
                            file.seekp(sb.s_block_start + (blockIdx * sizeof(FolderBlock)), std::ios::beg);
                            file.write(reinterpret_cast<char*>(&folderBlock), sizeof(FolderBlock));
                            slotFound = true; break;
                        }
                    }
                }
                if (slotFound) break;
            }

            if (!slotFound) {
                for (int i = 0; i < 12; i++) {
                    if (currentInode.i_block[i] == -1) {
                        int newDirBlockIdx = -1;
                        for (int k = 0; k < sb.s_blocks_count; k++) {
                            file.seekg(sb.s_bm_block_start + k, std::ios::beg); file.read(&bit, 1);
                            if (bit == '0') { newDirBlockIdx = k; break; }
                        }
                        file.seekp(sb.s_bm_block_start + newDirBlockIdx, std::ios::beg); file.write(&ocupado, 1);
                        sb.s_free_blocks_count--;

                        FolderBlock newFolderBlock;
                        for (int k = 0; k < 4; k++) newFolderBlock.b_content[k].b_inodo = -1;
                        strcpy(newFolderBlock.b_content[0].b_name, fileName.c_str());
                        newFolderBlock.b_content[0].b_inodo = newFileInodeIdx;

                        file.seekp(sb.s_block_start + (newDirBlockIdx * sizeof(FolderBlock)), std::ios::beg);
                        file.write(reinterpret_cast<char*>(&newFolderBlock), sizeof(FolderBlock));
                        currentInode.i_block[i] = newDirBlockIdx;
                        break;
                    }
                }
            }

            currentInode.i_mtime = time(nullptr);
            file.seekp(sb.s_inode_start + (currentInodeIdx * sizeof(Inode)), std::ios::beg);
            file.write(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

            file.seekp(partition.start, std::ios::beg);
            file.write(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            file.close();

            return "Archivo '" + fileName + "' creado exitosamente. Bytes escritos: " + std::to_string(size);

        } catch (const std::exception& e) {
            return std::string("Error fatal en mkfile: ") + e.what();
        }
    }
}

#endif // MKFILE_H