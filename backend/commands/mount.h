#ifndef MOUNT_H
#define MOUNT_H

#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <cstring>
#include <algorithm>
#include "../core/structures.h"

namespace CommandMount {
    
    inline static std::map<std::string, MountedPartition> mountedPartitions;
    
    static std::map<std::string, char> diskLetters;
    
    // Contador para la siguiente letra de disco disponible
    static char nextDiskLetter = 'a';
    
    // Función auxiliar para expandir ~ a home directory
    inline std::string expandPath(const std::string& path) {
        if (path.empty() || path[0] != '~') {
            return path;
        }
        
        const char* home = std::getenv("HOME");
        if (!home) {
            home = std::getenv("USERPROFILE"); // Windows
        }
        
        if (home) {
            return std::string(home) + path.substr(1);
        }
        return path;
    }
    
    // Función para buscar una partición en el MBR
    inline bool findPartitionInMBR(const std::string& path, const std::string& name, 
                                    char& type, int& start, int& size) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // Leer MBR
        MBR mbr;
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        // Buscar en particiones primarias y extendidas
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1') {
                std::string partName(mbr.mbr_partitions[i].part_name);
                // Remover espacios en blanco del nombre
                partName.erase(std::remove_if(partName.begin(), partName.end(), ::isspace), partName.end());
                partName.erase(std::find(partName.begin(), partName.end(), '\0'), partName.end());
                
                if (partName == name) {
                    type = mbr.mbr_partitions[i].part_type;
                    start = mbr.mbr_partitions[i].part_start;
                    size = mbr.mbr_partitions[i].part_size;
                    file.close();
                    return true;
                }
                
                // Si es extendida, buscar en particiones lógicas
                if (mbr.mbr_partitions[i].part_type == 'E') {
                    int ebrPos = mbr.mbr_partitions[i].part_start;
                    while (ebrPos != -1) {
                        EBR ebr;
                        file.seekg(ebrPos, std::ios::beg);
                        file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                        
                        if (ebr.part_status == '1') {
                            std::string ebrName(ebr.part_name);
                            ebrName.erase(std::remove_if(ebrName.begin(), ebrName.end(), ::isspace), ebrName.end());
                            ebrName.erase(std::find(ebrName.begin(), ebrName.end(), '\0'), ebrName.end());
                            
                            if (ebrName == name) {
                                type = 'L';
                                start = ebr.part_start;
                                size = ebr.part_size;
                                file.close();
                                return true;
                            }
                        }
                        
                        ebrPos = ebr.part_next;
                    }
                }
            }
        }
        
        file.close();
        return false;
    }
    
    // Función para generar el ID de montaje
    inline std::string generateMountID(const std::string& path) {
        char diskLetter;
        
        // Verificar si el disco ya tiene una letra asignada
        auto it = diskLetters.find(path);
        if (it != diskLetters.end()) {
            diskLetter = it->second;
        } else {
            diskLetter = nextDiskLetter++;
            diskLetters[path] = diskLetter;
        }
        
        int partitionNumber = 1;
        for (const auto& [id, partition] : mountedPartitions) {
            if (partition.path == path) {
                partitionNumber++;
            }
        }
        
        std::string mountID = "15";
        mountID += diskLetter;
        mountID += std::to_string(partitionNumber);
        
        return mountID;
    }
    
    // Función para verificar si una partición ya está montada
    inline bool isPartitionMounted(const std::string& path, const std::string& name) {
        for (const auto& [id, partition] : mountedPartitions) {
            if (partition.path == path && partition.name == name) {
                return true;
            }
        }
        return false;
    }
    
    inline void execute(const std::map<std::string, std::string>& params) {
        std::cout << "\n=== MOUNT ===" << std::endl;
        
        auto pathIt = params.find("-path");
        auto nameIt = params.find("-name");
        
        if (pathIt == params.end() || nameIt == params.end()) {
            std::cerr << "Error: faltan parámetros obligatorios (-path y -name)" << std::endl;
            return;
        }
        
        std::string path = expandPath(pathIt->second);
        std::string name = nameIt->second;
        
        std::ifstream file(path);
        if (!file.good()) {
            std::cerr << "Error: el disco '" << path << "' no existe" << std::endl;
            return;
        }
        file.close();
        
        // Verificar si la partición ya está montada
        if (isPartitionMounted(path, name)) {
            std::cerr << "Error: la partición '" << name << "' en '" << path 
                      << "' ya está montada" << std::endl;
            return;
        }
        
        // Buscar la partición en el disco
        char type;
        int start, size;
        if (!findPartitionInMBR(path, name, type, start, size)) {
            std::cerr << "Error: no se encontró la partición '" << name 
                      << "' en el disco '" << path << "'" << std::endl;
            return;
        }
        
        // Generar ID de montaje
        std::string mountID = generateMountID(path);
        
        // Crear estructura de partición montada
        MountedPartition mounted;
        mounted.path = path;
        mounted.name = name;
        mounted.id = mountID;
        mounted.type = type;
        mounted.start = start;
        mounted.size = size;
        
        // Agregar al mapa de particiones montadas
        mountedPartitions[mountID] = mounted;
        
        std::cout << "Partición montada exitosamente" << std::endl;
        std::cout << "  ID: " << mountID << std::endl;
        std::cout << "  Disco: " << path << std::endl;
        std::cout << "  Partición: " << name << std::endl;
        std::cout << "  Tipo: " << type << std::endl;
        std::cout << "  Inicio: " << start << " bytes" << std::endl;
        std::cout << "  Tamaño: " << size << " bytes" << std::endl;
    }
    
    // Sobrecarga de execute() que devuelve std::string (para compatibilidad con main.cpp)
    inline std::string execute(const std::string& pathParam, const std::string& nameParam) {
        std::string path = expandPath(pathParam);
        std::string name = nameParam;
        
        // Verificar que el archivo del disco existe
        std::ifstream file(path);
        if (!file.good()) {
            return "Error: el disco '" + path + "' no existe";
        }
        file.close();
        
        // Verificar si la partición ya está montada
        if (isPartitionMounted(path, name)) {
            return "Error: la partición '" + name + "' en '" + path + "' ya está montada";
        }
        
        // Buscar la partición en el disco
        char type;
        int start, size;
        if (!findPartitionInMBR(path, name, type, start, size)) {
            return "Error: no se encontró la partición '" + name + "' en el disco '" + path + "'";
        }
        
        // Generar ID de montaje
        std::string mountID = generateMountID(path);
        
        // Crear estructura de partición montada
        MountedPartition mounted;
        mounted.path = path;
        mounted.name = name;
        mounted.id = mountID;
        mounted.type = type;
        mounted.start = start;
        mounted.size = size;
        
        // Agregar al mapa de particiones montadas
        mountedPartitions[mountID] = mounted;
        
        std::ostringstream result;
        result << "\n=== MOUNT ===\n";
        result << "Partición montada exitosamente\n";
        result << "  ID: " << mountID << "\n";
        result << "  Disco: " << path << "\n";
        result << "  Partición: " << name << "\n";
        result << "  Tipo: " << type << "\n";
        result << "  Inicio: " << start << " bytes\n";
        result << "  Tamaño: " << size << " bytes";
        
        return result.str();
    }
    
    // Función para listar todas las particiones montadas (retorna string)
    inline std::string listMountedPartitions() {
        if (mountedPartitions.empty()) {
            return "No hay particiones montadas";
        }
        
        std::ostringstream result;
        result << "\n=== PARTICIONES MONTADAS ===\n";
        for (const auto& [id, partition] : mountedPartitions) {
            result << "ID: " << id << "\n";
            result << "  Disco: " << partition.path << "\n";
            result << "  Partición: " << partition.name << "\n";
            result << "  Tipo: " << partition.type << "\n";
            result << "  Inicio: " << partition.start << " bytes\n";
            result << "  Tamaño: " << partition.size << " bytes\n";
            result << "---\n";
        }
        return result.str();
    }
    
    // Función para mostrar todas las particiones montadas (imprime en consola)
    inline void showMountedPartitions() {
        std::cout << listMountedPartitions() << std::endl;
    }
    
    // Función auxiliar para obtener información de una partición montada por su ID
    inline bool getMountedPartition(const std::string& id, MountedPartition& partition) {
        auto it = mountedPartitions.find(id);
        if (it != mountedPartitions.end()) {
            partition = it->second;
            return true;
        }
        return false;
    }

} // namespace CommandMount

#endif // MOUNT_H