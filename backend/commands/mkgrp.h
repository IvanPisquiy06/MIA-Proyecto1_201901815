#ifndef MKGRP_H
#define MKGRP_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cmath>
#include <ctime>
#include "../core/structures.h"
#include "mount.h"
#include "cat.h"

namespace CommandMkgrp {

    inline std::string execute(const std::string& name) {
        try {
            // 1. Validaciones iniciales
            if (name.empty()) return "Error: El parámetro -name es obligatorio.";
            if (name.length() > 10) return "Error: El nombre del grupo no puede exceder 10 caracteres.";

            // 2. Validar sesión de 'root'
            if (!::getSession().is_logged_in) {
                return "Error: No hay una sesión activa. Use login primero.";
            }
            if (::getSession().username != "root") {
                return "Error: Solo el usuario 'root' puede crear grupos.";
            }

            // 3. Obtener partición actual
            std::string id = ::getSession().partition_id;
            MountedPartition partition;
            bool encontrada = false;
            for (const auto& p : CommandMount::mountedPartitions) {
                if (p.second.id == id) {
                    partition = p.second;
                    encontrada = true;
                    break;
                }
            }
            if (!encontrada) return "Error: Partición con ID " + id + " no montada.";

            // Abrir el disco
            std::fstream file(partition.path, std::ios::in | std::ios::out | std::ios::binary);
            if (!file.is_open()) return "Error: No se pudo abrir el disco.";

            // 4. Cargar Superbloque e Inodo de users.txt
            Superblock sb;
            file.seekg(partition.start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            int usersInodeIndex = CommandCat::getInodeByPath(file, sb, "users.txt");
            if (usersInodeIndex == -1) return "Error: No se encontró /users.txt.";

            Inode usersInode;
            file.seekg(sb.s_inode_start + (usersInodeIndex * sizeof(Inode)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

            // 5. Leer el contenido actual de users.txt
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

            // 6. Analizar el contenido para validar duplicados y buscar el próximo ID
            std::stringstream ss(content);
            std::string line;
            int maxId = 0;

            while (std::getline(ss, line, '\n')) {
                if (line.empty() || line[0] == '0') continue; // Ignorar vacías o eliminados

                std::vector<std::string> tokens;
                std::stringstream lineStream(line);
                std::string token;
                while (std::getline(lineStream, token, ',')) {
                    tokens.push_back(token);
                }

                if (tokens.size() >= 3) {
                    int currentId = std::stoi(tokens[0]);
                    if (currentId > maxId) maxId = currentId;

                    // Verificar si es grupo y si el nombre ya existe
                    if (tokens[1] == "G" && tokens[2] == name) {
                        return "Error: El grupo '" + name + "' ya existe.";
                    }
                }
            }

            // 7. Crear el nuevo texto y concatenarlo
            std::string newGroupLine = std::to_string(maxId + 1) + ",G," + name + "\n";
            content += newGroupLine;

            // 8. Escribir el nuevo contenido de regreso a los bloques
            int bytesWritten = 0;
            int totalBytes = content.length();
            int requiredBlocks = std::ceil((double)totalBytes / 64.0);

            if (requiredBlocks > 12) {
                return "Error: Archivo users.txt excedió los 12 bloques directos (No soportado aún).";
            }

            for (int b = 0; b < requiredBlocks; b++) {
                int blockIndex = usersInode.i_block[b];
                
                // Si el inodo no tiene bloque asignado aquí, hay que buscar uno libre
                if (blockIndex == -1) {
                    // Buscar bloque libre en el bitmap
                    char bit;
                    int freeBlockIndex = -1;
                    for (int i = 0; i < sb.s_blocks_count; i++) {
                        file.seekg(sb.s_bm_block_start + i, std::ios::beg);
                        file.read(&bit, 1);
                        if (bit == '0') {
                            freeBlockIndex = i;
                            break;
                        }
                    }

                    if (freeBlockIndex == -1) return "Error: No hay bloques libres en el sistema.";

                    // Marcarlo como ocupado ('1')
                    char ocupado = '1';
                    file.seekp(sb.s_bm_block_start + freeBlockIndex, std::ios::beg);
                    file.write(&ocupado, 1);

                    // Actualizar Superbloque
                    sb.s_free_blocks_count--;
                    file.seekp(partition.start, std::ios::beg);
                    file.write(reinterpret_cast<char*>(&sb), sizeof(Superblock));

                    // Asignarlo al inodo
                    blockIndex = freeBlockIndex;
                    usersInode.i_block[b] = blockIndex;
                }

                // Llenar el bloque con hasta 64 caracteres de nuestro texto
                FileBlock newBlock;
                memset(newBlock.b_content, 0, 64); // Limpiar con ceros
                for (int c = 0; c < 64 && bytesWritten < totalBytes; c++) {
                    newBlock.b_content[c] = content[bytesWritten++];
                }

                // Escribir el bloque físico al disco
                file.seekp(sb.s_block_start + (blockIndex * sizeof(FileBlock)), std::ios::beg);
                file.write(reinterpret_cast<char*>(&newBlock), sizeof(FileBlock));
            }

            // 9. Actualizar el tamaño y fecha del inodo y guardarlo
            usersInode.i_size = totalBytes;
            usersInode.i_mtime = time(nullptr);
            file.seekp(sb.s_inode_start + (usersInodeIndex * sizeof(Inode)), std::ios::beg);
            file.write(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

            file.close();

            return "Grupo '" + name + "' creado exitosamente.";

        } catch (const std::exception& e) {
            return std::string("Error fatal en mkgrp: ") + e.what();
        }
    }
}

#endif // MKGRP_H