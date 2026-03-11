#ifndef LOGIN_H
#define LOGIN_H

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include "../core/structures.h"
#include "mount.h"
#include "cat.h"

namespace CommandLogin {

    // Ejecución del comando
    inline std::string execute(const std::string& user, const std::string& pass, const std::string& id) {
        try {
            // Validaciones iniciales
            if (user.empty() || pass.empty() || id.empty()) {
                return "Error: Los parámetros -user, -pass e -id son obligatorios.";
            }

            if (getSession().is_logged_in) {
                return "Error: Ya hay un usuario logueado (" + getSession().username + "). Use el comando logout primero.";
            }

            // Buscar la partición
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

            std::fstream file(partition.path, std::ios::in | std::ios::binary);
            if (!file.is_open()) return "Error: No se pudo abrir el disco.";

            // Leer Superbloque
            Superblock sb;
            file.seekg(partition.start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

            // Buscar el archivo users.txt usando la función de cat.h
            int usersInodeIndex = CommandCat::getInodeByPath(file, sb, "users.txt");
            
            if (usersInodeIndex == -1) {
                return "Error Crítico: No se encontró el archivo /users.txt en el sistema de archivos. ¿El disco fue formateado correctamente con mkfs?";
            }

            // Leer el Inodo de users.txt
            Inode usersInode;
            file.seekg(sb.s_inode_start + (usersInodeIndex * sizeof(Inode)), std::ios::beg);
            file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

            // Extraer todo el texto de users.txt
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
            file.close();

            // Parsear users.txt para validar credenciales
            // Formato esperado por línea: UID,Tipo,Grupo,Usuario,Contraseña
            std::stringstream ss(content);
            std::string line;
            bool loginSuccess = false;

            while (std::getline(ss, line, '\n')) {
                if (line.empty() || line[0] == '0') continue; // Ignorar líneas vacías o usuarios eliminados (UID 0)

                std::vector<std::string> tokens;
                std::stringstream lineStream(line);
                std::string token;
                
                while (std::getline(lineStream, token, ',')) {
                    tokens.push_back(token);
                }

                // Si es un usuario (Tipo 'U') y tiene 5 elementos
                if (tokens.size() >= 5 && tokens[1] == "U") {
                    std::string fileUser = tokens[3];
                    std::string filePass = tokens[4];

                    // Limpiar posibles retornos de carro (\r) que a veces se cuelan
                    if (!filePass.empty() && filePass.back() == '\r') filePass.pop_back();

                    if (fileUser == user && filePass == pass) {
                        getSession().is_logged_in = true;
                        getSession().username = user;
                        getSession().uid = std::stoi(tokens[0]);
                        getSession().gid = std::stoi(tokens[0]); 
                        getSession().partition_id = id;
                        loginSuccess = true;
                        break;
                    }
                }
            }

            if (loginSuccess) {
                return "Login exitoso. Bienvenido, " + user + ".";
            } else {
                return "Error: Usuario o contraseña incorrectos.";
            }

        } catch (const std::exception& e) {
            return std::string("Error fatal en login: ") + e.what();
        }
    }
}

#endif // LOGIN_H