#include <iostream>
#include <fstream>
#include <cstring>
#include <ctime>
#include "../core/structures.h"

inline void registrarEnJournal(const std::string& path_disco, int part_start, const std::string& operacion, char tipo, const std::string& nombre, const std::string& contenido) {
    std::fstream file(path_disco, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) return;

    Superblock sb;
    file.seekg(part_start, std::ios::beg);
    file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    if (sb.s_filesystem_type != 3) {
        file.close();
        return;
    }

    int journal_start = part_start + sizeof(Superblock);
    file.seekg(journal_start, std::ios::beg);

    Journal temp;
    for (int i = 0; i < sb.s_inodes_count; i++) {
        file.read(reinterpret_cast<char*>(&temp), sizeof(Journal));
        
        if (temp.journal_estado == 0) {
            temp.journal_estado = 1;
            strncpy(temp.journal_tipo_operacion, operacion.c_str(), 9);
            temp.journal_tipo_operacion[9] = '\0'; 
            
            temp.journal_tipo = tipo;
            
            strncpy(temp.journal_nombre, nombre.c_str(), 99);
            temp.journal_nombre[99] = '\0';
            
            strncpy(temp.journal_contenido, contenido.c_str(), 99);
            temp.journal_contenido[99] = '\0';
            
            temp.journal_fecha = time(nullptr);

            file.seekp(journal_start + (i * sizeof(Journal)), std::ios::beg);
            file.write(reinterpret_cast<char*>(&temp), sizeof(Journal));
            break;
        }
    }
    file.close();
}