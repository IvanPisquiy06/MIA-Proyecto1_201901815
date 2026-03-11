#ifndef MKUSR_H
#define MKUSR_H

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
#include "login.h"

namespace CommandMkusr {

    inline std::string execute(const std::string& user, const std::string& pwd, const std::string& grp) {
        try {
            // 1. Validaciones iniciales de parámetros
            if (user.empty() || pwd.empty() || grp.empty()) {
                return "Error: Los parámetros -user, -pass y -grp son obligatorios.";
            }
            if (user.length() > 10) return "Error: El nombre de usuario no puede exceder 10 caracteres.";
            if (pwd.length() > 10) return "Error: La contraseña no puede exceder 10 caracteres.";
            if (grp.length() > 10) return "Error: El nombre del grupo no puede exceder 10 caracteres.";

            // 2. Validar sesión activa y permisos de root
            if (!getSession().is_logged_in) {
                return "Error: No hay una sesión activa. Use login primero.";
            }
            if (getSession().username != "root") {
                return "Error: Solo el usuario 'root' puede crear nuevos usuarios.";
            }

            // 3. Obtener la partición actual
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
            if (!encontrada) return "Error: Partición con ID " + id + " no montada.";

            // Abrir el disco
            std::fstream file(partition.path, std::ios::in | std::ios::out | std::ios::binary);
            if (!file.is_open()) return "Error: No se pudo abrir el disco.";

            // 4. Cargar Superbloque e Inodo de users.txt
            Superblock sb;
            file.seekg(partition.start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            int usersInodeIndex = CommandCat::getInodeByPath(file, sb, "users.txt");
            if (usersInodeIndex == -1) return "Error Crítico: No se encontró /users.txt.";

            Inode usersInode;
            file.seekg(sb.s_inode_start + (usersInodeIndex * sizeof(Inode)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

            // 5. Leer todo el contenido de users.txt
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

            // 6. Analizar el contenido: validar grupo, usuario y buscar próximo UID
            std::stringstream ss(content);
            std::string line;
            int maxUid = 0;
            bool groupExists = false;

            while (std::getline(ss, line, '\n')) {
                if (line.empty() || line[0] == '0') continue; // Ignorar eliminados

                std::vector<std::string> tokens;
                std::stringstream lineStream(line);
                std::string token;
                while (std::getline(lineStream, token, ',')) {
                    tokens.push_back(token);
                }

                // Verificar si el grupo existe (Tipo 'G')
                if (tokens.size() >= 3 && tokens[1] == "G" && tokens[2] == grp) {
                    groupExists = true;
                }

                // Verificar si el usuario ya existe y buscar el ID más alto (Tipo 'U')
                if (tokens.size() >= 5 && tokens[1] == "U") {
                    int currentUid = std::stoi(tokens[0]);
                    if (currentUid > maxUid) maxUid = currentUid;

                    if (tokens[3] == user) {
                        return "Error: El usuario '" + user + "' ya existe.";
                    }
                }
            }

            if (!groupExists) {
                return "Error: El grupo '" + grp + "' no existe. Debe crearlo primero con mkgrp.";
            }

            // 7. Construir la nueva línea y agregarla al texto
            // Formato: UID,Tipo,Grupo,Usuario,Contraseña
            std::string newUserLine = std::to_string(maxUid + 1) + ",U," + grp + "," + user + "," + pwd + "\n";
            content += newUserLine;

            // 8. Escribir el nuevo contenido en los bloques (asignando nuevos si es necesario)
            int bytesWritten = 0;
            int totalBytes = content.length();
            int requiredBlocks = std::ceil((double)totalBytes / 64.0);

            if (requiredBlocks > 12) {
                return "Error: Archivo users.txt excedió los 12 bloques directos (No soportado aún).";
            }

            for (int b = 0; b < requiredBlocks; b++) {
                int blockIndex = usersInode.i_block[b];
                
                // Si necesitamos un bloque nuevo
                if (blockIndex == -1) {
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

                    char ocupado = '1';
                    file.seekp(sb.s_bm_block_start + freeBlockIndex, std::ios::beg);
                    file.write(&ocupado, 1);

                    sb.s_free_blocks_count--;
                    file.seekp(partition.start, std::ios::beg);
                    file.write(reinterpret_cast<char*>(&sb), sizeof(Superblock));

                    blockIndex = freeBlockIndex;
                    usersInode.i_block[b] = blockIndex;
                }

                FileBlock newBlock;
                memset(newBlock.b_content, 0, 64);
                for (int c = 0; c < 64 && bytesWritten < totalBytes; c++) {
                    newBlock.b_content[c] = content[bytesWritten++];
                }

                file.seekp(sb.s_block_start + (blockIndex * sizeof(FileBlock)), std::ios::beg);
                file.write(reinterpret_cast<char*>(&newBlock), sizeof(FileBlock));
            }

            // 9. Actualizar y guardar el Inodo
            usersInode.i_size = totalBytes;
            usersInode.i_mtime = time(nullptr);
            file.seekp(sb.s_inode_start + (usersInodeIndex * sizeof(Inode)), std::ios::beg);
            file.write(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

            file.close();

            return "Usuario '" + user + "' creado exitosamente en el grupo '" + grp + "'.";

        } catch (const std::exception& e) {
            return std::string("Error fatal en mkusr: ") + e.what();
        }
    }
}

#endif // MKUSR_H