#ifndef CAT_H
#define CAT_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include "structures.h"
#include "mount.h"

namespace CommandCat {

    inline std::vector<std::string> splitPath(const std::string& path) {
        std::vector<std::string> result;
        std::stringstream ss(path);
        std::string item;
        while (std::getline(ss, item, '/')) {
            if (!item.empty()) {
                result.push_back(item);
            }
        }
        return result;
    }

    inline int getInodeByPath(std::fstream& file, const Superblock& sb, const std::string& path) {
        std::vector<std::string> steps = splitPath(path);
        int currentInodeIndex = 0; 
        Inode currentInode;

        for (size_t i = 0; i < steps.size(); i++) {
            std::string step = steps[i];
            
            file.seekg(sb.s_inode_start + (currentInodeIndex * sizeof(Inode)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

            if (currentInode.i_type != '0' && i < steps.size() - 1) return -1; 

            bool found = false;
            int nextInodeIndex = -1;

            for (int b = 0; b < 12; b++) {
                int blockIndex = currentInode.i_block[b];
                if (blockIndex != -1) {
                    FolderBlock folderBlock;
                    file.seekg(sb.s_block_start + (blockIndex * sizeof(FolderBlock)), std::ios::beg);
                    file.read(reinterpret_cast<char*>(&folderBlock), sizeof(FolderBlock));

                    for (int c = 0; c < 4; c++) {
                        if (std::string(folderBlock.b_content[c].b_name) == step) {
                            found = true;
                            nextInodeIndex = folderBlock.b_content[c].b_inodo;
                            break;
                        }
                    }
                }
                if (found) break;
            }

            if (!found) return -1; 
            currentInodeIndex = nextInodeIndex; 
        }
        return currentInodeIndex; 
    }

    // NUEVO: Función para validar permisos de lectura
    inline bool hasReadPermission(const Inode& inode, const ActiveSession& session) {
        // Root todo lo puede
        if (session.username == "root") return true;

        int perm = inode.i_perm; // Ej: 664
        int ownerPerm = perm / 100;          // 6
        int groupPerm = (perm / 10) % 10;    // 6
        int othersPerm = perm % 10;          // 4

        int targetPerm = othersPerm; // Por defecto asumimos que es "Otros"

        if (session.uid == inode.i_uid) {
            targetPerm = ownerPerm;  // Si es el propietario del archivo
        } else if (session.gid == inode.i_gid) {
            targetPerm = groupPerm;  // Si pertenece al mismo grupo
        }

        // Verificamos si el bit de lectura (4) está activo en el permiso resultante
        // Si targetPerm es 4 (Lectura), 5 (Lectura+Ejecución), 6 (Lectura+Escritura) o 7 (Todo), 
        // la operación binaria (targetPerm & 4) dará como resultado un número mayor a 0.
        return (targetPerm & 4) != 0;
    }

    inline std::string execute(const std::vector<std::string>& files) {
        try {
            if (files.empty()) {
                return "Error: CAT requiere al menos un parámetro -file.";
            }

            if (!getSession().is_logged_in) {
                return "Error: No hay una sesión activa. Debe hacer login primero.";
            }

            std::string id = getSession().partition_id;

            MountedPartition partition;
            bool encontrada = false;
            for (const auto& p : CommandMount::mountedPartitions) {
                if (p.second.id == id) {
                    partition = p.second;
                    encontrada = true;
                    break;
                }
            }

            if (!encontrada) return "Error: Partición con ID " + id + " ya no está montada.";

            std::fstream file(partition.path, std::ios::in | std::ios::binary);
            if (!file.is_open()) return "Error: No se pudo abrir el disco.";

            Superblock sb;
            file.seekg(partition.start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            std::string output = "";

            for (const std::string& filepath : files) {
                output += "========== " + filepath + " ==========\n";
                
                int targetInodeIndex = getInodeByPath(file, sb, filepath);
                if (targetInodeIndex == -1) {
                    output += "Error: El archivo no existe.\n\n";
                    continue; 
                }

                Inode targetInode;
                file.seekg(sb.s_inode_start + (targetInodeIndex * sizeof(Inode)), std::ios::beg);
                file.read(reinterpret_cast<char*>(&targetInode), sizeof(Inode));

                if (targetInode.i_type == '0') {
                    output += "Error: La ruta es una carpeta, no un archivo.\n\n";
                    continue;
                }

                // ==========================================
                // VALIDACIÓN DE PERMISOS ANTES DE LEER
                // ==========================================
                if (!hasReadPermission(targetInode, getSession())) {
                    output += "Error: El usuario '" + getSession().username + "' no tiene permisos de lectura para este archivo.\n\n";
                    continue;
                }

                std::string content = "";
                int bytesRead = 0;
                int totalSize = targetInode.i_size;

                for (int b = 0; b < 12; b++) {
                    int blockIndex = targetInode.i_block[b];
                    if (blockIndex != -1) {
                        FileBlock fileBlock;
                        file.seekg(sb.s_block_start + (blockIndex * sizeof(FileBlock)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&fileBlock), sizeof(FileBlock));

                        for (int c = 0; c < 64 && bytesRead < totalSize; c++) {
                            content += fileBlock.b_content[c];
                            bytesRead++;
                        }
                    }
                }
                
                output += content + "\n\n";
            }

            file.close();

            std::cout << output;
            return "Comando CAT ejecutado exitosamente.";

        } catch (const std::exception& e) {
            return std::string("Error fatal en cat: ") + e.what();
        }
    }
}

#endif // CAT_H