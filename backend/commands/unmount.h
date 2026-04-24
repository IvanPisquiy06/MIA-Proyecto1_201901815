#ifndef UNMOUNT_H
#define UNMOUNT_H

#include <iostream>
#include <string>
#include "mount.h"

namespace CommandUnmount {

    inline std::string execute(const std::string& id) {
        if (id.empty()) {
            return "Error: unmount requiere el parámetro -id\nUso: unmount -id=ID";
        }

        // Buscamos si el ID existe en el mapa/diccionario de particiones montadas
        auto it = CommandMount::mountedPartitions.find(id);
        
        if (it != CommandMount::mountedPartitions.end()) {
            // Si lo encuentra, lo borramos de la memoria RAM
            CommandMount::mountedPartitions.erase(it);
            return "Partición desmontada con éxito. (ID: " + id + ")";
        } else {
            return "Error: No existe ninguna partición montada con el ID '" + id + "'.";
        }
    }
}

#endif // UNMOUNT_H