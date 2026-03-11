#ifndef RMUSR_H
#define RMUSR_H

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
#include "cat.h"

namespace CommandRmusr {

    inline std::string execute(const std::string& user) {
        try {
            // 1. Validaciones iniciales
            if (user.empty()) return "Error: El parámetro -user es obligatorio.";
            
            // Regla de oro: No se puede eliminar a root
            if (user == "root") return "Error: No se puede eliminar al usuario 'root'.";

            if (!::getSession().is_logged_in) {
                return "Error: No hay una sesión activa. Use login primero.";
            }
            if (::getSession().username != "root") {
                return "Error: Solo el usuario 'root' puede eliminar usuarios.";
            }

            // 2. Obtener partición actual
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

            std::fstream file(partition.path, std::ios::in | std::ios::out | std::ios::binary);
            if (!file.is_open()) return "Error: No se pudo abrir el disco.";

            Superblock sb;
            file.seekg(partition.start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            // 3. Buscar y cargar el Inodo de users.txt
            int usersInodeIndex = CommandCat::getInodeByPath(file, sb, "users.txt");
            if (usersInodeIndex == -1) return "Error Crítico: No se encontró /users.txt.";

            Inode usersInode;
            file.seekg(sb.s_inode_start + (usersInodeIndex * sizeof(Inode)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

            // 4. Leer todo el contenido de users.txt
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

            // 5. Analizar y reconstruir el contenido (Baja lógica)
            std::stringstream ss(content);
            std::string line;
            std::string newContent = "";
            bool userFound = false;

            while (std::getline(ss, line, '\n')) {
                if (line.empty()) continue;

                std::vector<std::string> tokens;
                std::stringstream lineStream(line);
                std::string token;
                while (std::getline(lineStream, token, ',')) {
                    tokens.push_back(token);
                }

                // Formato de Usuario: UID,Tipo(U),Grupo,Usuario,Contraseña
                if (tokens.size() >= 5 && tokens[1] == "U" && tokens[3] == user) {
                    if (tokens[0] == "0") {
                        return "Error: El usuario '" + user + "' ya fue eliminado previamente.";
                    }
                    // Aplicar la baja lógica: Cambiar el UID original por un 0
                    newContent += "0,U," + tokens[2] + "," + tokens[3] + "," + tokens[4] + "\n";
                    userFound = true;
                } else {
                    // Si no es el usuario que buscamos, mantener la línea intacta
                    newContent += line + "\n";
                }
            }

            if (!userFound) {
                return "Error: El usuario '" + user + "' no existe.";
            }

            // 6. Escribir el nuevo texto de regreso a los bloques
            int bytesWritten = 0;
            int totalBytes = newContent.length();
            int requiredBlocks = std::ceil((double)totalBytes / 64.0);

            if (requiredBlocks > 12) {
                return "Error: Archivo users.txt excedió los 12 bloques directos.";
            }

            for (int b = 0; b < requiredBlocks; b++) {
                int blockIndex = usersInode.i_block[b];
                
                // Si por alguna razón se necesita un bloque nuevo al reescribir
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
                    newBlock.b_content[c] = newContent[bytesWritten++];
                }

                file.seekp(sb.s_block_start + (blockIndex * sizeof(FileBlock)), std::ios::beg);
                file.write(reinterpret_cast<char*>(&newBlock), sizeof(FileBlock));
            }

            // 7. Guardar el inodo actualizado
            usersInode.i_size = totalBytes;
            usersInode.i_mtime = time(nullptr);
            file.seekp(sb.s_inode_start + (usersInodeIndex * sizeof(Inode)), std::ios::beg);
            file.write(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

            file.close();

            return "Usuario '" + user + "' eliminado exitosamente (Baja lógica).";

        } catch (const std::exception& e) {
            return std::string("Error fatal en rmusr: ") + e.what();
        }
    }
}

#endif // RMUSR_H