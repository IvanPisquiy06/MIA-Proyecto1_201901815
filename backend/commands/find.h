#ifndef FIND_H
#define FIND_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include "../core/structures.h"
#include "mount.h"

namespace CommandFind {

    // Función auxiliar para comparar usando el comodín *
    inline bool coincideNombre(const std::string& fileName, const std::string& targetName) {
        if (targetName == "*" || targetName == "?") return true; // Trae todo
        
        if (targetName.front() == '*' && targetName.back() == '*') { // *nombre*
            std::string core = targetName.substr(1, targetName.length() - 2);
            return fileName.find(core) != std::string::npos;
        } 
        else if (targetName.back() == '*') { // nombre*
            std::string core = targetName.substr(0, targetName.length() - 1);
            return fileName.find(core) == 0;
        } 
        else if (targetName.front() == '*') { // *nombre
            std::string core = targetName.substr(1);
            if (fileName.length() >= core.length()) {
                return fileName.substr(fileName.length() - core.length()) == core;
            }
            return false;
        }
        
        return fileName == targetName; // Coincidencia exacta
    }

    // Función recursiva que viaja por el árbol de inodos
    inline void buscarRecursivo(std::fstream& file, const Superblock& sb, int inodeIdx, const std::string& currentPath, const std::string& targetName, std::vector<std::string>& resultados) {
        Inode currentInode;
        file.seekg(sb.s_inode_start + (inodeIdx * sizeof(Inode)), std::ios::beg);
        file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

        // Solo buscamos dentro de carpetas (tipo '0')
        if (currentInode.i_type != '0') return;

        for (int i = 0; i < 15; i++) {
            if (currentInode.i_block[i] != -1) {
                FolderBlock fb;
                file.seekg(sb.s_block_start + (currentInode.i_block[i] * sizeof(FolderBlock)), std::ios::beg);
                file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                for (int j = 0; j < 4; j++) {
                    if (fb.b_content[j].b_inodo != -1) {
                        std::string entryName = fb.b_content[j].b_name;
                        
                        // ¡VITAL! Evitar ciclo infinito
                        if (entryName == "." || entryName == "..") continue;

                        // Construir la ruta de este elemento
                        std::string fullPath = currentPath;
                        if (fullPath.back() != '/') fullPath += "/";
                        fullPath += entryName;

                        // Evaluar si es lo que estamos buscando
                        if (coincideNombre(entryName, targetName)) {
                            resultados.push_back(fullPath);
                        }

                        // Leer el inodo hijo. Si es otra carpeta, entramos recursivamente
                        Inode childInode;
                        file.seekg(sb.s_inode_start + (fb.b_content[j].b_inodo * sizeof(Inode)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&childInode), sizeof(Inode));

                        if (childInode.i_type == '0') {
                            buscarRecursivo(file, sb, fb.b_content[j].b_inodo, fullPath, targetName, resultados);
                        }
                    }
                }
            }
        }
    }

    inline std::string execute(const std::string& path, const std::string& name) {
        try {
            if (path.empty() || name.empty()) return "Error: Parámetros -path y -name son obligatorios.";
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

            // --- 1. NAVEGAR HASTA LA CARPETA DONDE INICIARÁ LA BÚSQUEDA (-path) ---
            std::vector<std::string> startDirs;
            std::stringstream ss(path);
            std::string item;
            while (std::getline(ss, item, '/')) if (!item.empty()) startDirs.push_back(item);

            int currentInodeIdx = 0; // Empezamos en la raíz
            Inode currentInode;
            file.seekg(sb.s_inode_start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

            for (const std::string& dirName : startDirs) {
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
                                found = true; break;
                            }
                        }
                    }
                    if (found) break;
                }
                if (!found) {
                    file.close();
                    return "Error: La ruta de inicio '" + path + "' no existe.";
                }
            }

            // --- 2. INICIAR LA BÚSQUEDA RECURSIVA ---
            std::vector<std::string> resultados;
            buscarRecursivo(file, sb, currentInodeIdx, path, name, resultados);

            file.close();

            // --- 3. FORMATEAR EL RESULTADO ---
            if (resultados.empty()) {
                return "No se encontraron coincidencias para '" + name + "' en la ruta '" + path + "'.";
            }

            std::string output = "Resultados de búsqueda para '" + name + "':\n";
            for (const std::string& res : resultados) {
                output += " -> " + res + "\n";
            }

            return output;

        } catch (const std::exception& e) {
            return std::string("Error fatal en find: ") + e.what();
        }
    }
}

#endif // FIND_H