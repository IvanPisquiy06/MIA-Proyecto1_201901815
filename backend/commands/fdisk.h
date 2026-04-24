#ifndef FDISK_H  
#define FDISK_H   

#include <string>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include "../core/structures.h"

namespace CommandFdisk {
    
    inline std::string expandPath(const std::string& path) {
        if (path.empty() || path[0] != '~') {
            return path;
        }
        
        const char* home = std::getenv("HOME");
        if (!home) {
            std::cerr << "Error: No se pudo obtener el directorio HOME" << std::endl;
            return path;
        }
        
        return std::string(home) + path.substr(1);
    }

    inline std::string deletePartition(const std::string& path, const std::string& name) {
        std::fstream diskFile(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!diskFile.is_open()) return "Error: No se pudo abrir el disco para eliminar.";

        MBR mbr;
        diskFile.seekg(0, std::ios::beg);
        diskFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

        int targetIndex = -1;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' && 
                strcmp(mbr.mbr_partitions[i].part_name, name.c_str()) == 0) {
                targetIndex = i;
                break;
            }
        }

        if (targetIndex == -1) {
            diskFile.close();
            return "Error: No se encontró la partición '" + name + "' en el disco.";
        }

        mbr.mbr_partitions[targetIndex].part_status = '0';
        mbr.mbr_partitions[targetIndex].part_type = '0';
        mbr.mbr_partitions[targetIndex].part_fit = '0';
        mbr.mbr_partitions[targetIndex].part_start = -1;
        mbr.mbr_partitions[targetIndex].part_size = 0;
        strcpy(mbr.mbr_partitions[targetIndex].part_name, "");

        diskFile.seekp(0, std::ios::beg);
        diskFile.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        diskFile.close();

        return "Partición '" + name + "' eliminada exitosamente.";
    }

    inline std::string addPartitionSpace(const std::string& path, const std::string& name, int addValue, const std::string& unit) {
        if (addValue == 0) return "Error: El valor de -add no puede ser 0.";

        std::fstream diskFile(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!diskFile.is_open()) return "Error: No se pudo abrir el disco.";

        MBR mbr;
        diskFile.seekg(0, std::ios::beg);
        diskFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

        int targetIndex = -1;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' && 
                strcmp(mbr.mbr_partitions[i].part_name, name.c_str()) == 0) {
                targetIndex = i;
                break;
            }
        }

        if (targetIndex == -1) {
            diskFile.close();
            return "Error: No se encontró la partición '" + name + "'.";
        }

        // Calcular en bytes
        int bytesToAdd = addValue;
        if (unit == "k" || unit == "K") bytesToAdd *= 1024;
        else if (unit == "m" || unit == "M") bytesToAdd *= 1024 * 1024;

        int currentSize = mbr.mbr_partitions[targetIndex].part_size;
        int newSize = currentSize + bytesToAdd;

        // Si recorta la partición
        if (bytesToAdd < 0) {
            if (newSize <= 0) {
                diskFile.close();
                return "Error: No se puede reducir a tamaño 0 o menor.";
            }
            mbr.mbr_partitions[targetIndex].part_size = newSize;
        } 
        // Si expande la partición
        else {
            int partitionEnd = mbr.mbr_partitions[targetIndex].part_start + currentSize;
            int maxAvailableSpace = mbr.mbr_size - partitionEnd; 

            for (int i = 0; i < 4; i++) {
                if (i != targetIndex && mbr.mbr_partitions[i].part_status == '1') {
                    if (mbr.mbr_partitions[i].part_start >= partitionEnd) {
                        int spaceBetween = mbr.mbr_partitions[i].part_start - partitionEnd;
                        if (spaceBetween < maxAvailableSpace) maxAvailableSpace = spaceBetween;
                    }
                }
            }

            if (bytesToAdd > maxAvailableSpace) {
                diskFile.close();
                return "Error: No hay suficiente espacio libre adyacente para expandir.";
            }
            mbr.mbr_partitions[targetIndex].part_size = newSize;
        }

        diskFile.seekp(0, std::ios::beg);
        diskFile.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        diskFile.close();

        std::string action = (bytesToAdd > 0) ? "aumentado" : "reducido";
        return "Espacio " + action + " exitosamente. Nuevo tamaño: " + std::to_string(newSize) + " bytes.";
    }

    inline std::string createPrimaryOrExtendedPartition(const std::string& path, int size, char type, char fit, const std::string& name) {
        std::fstream diskFile(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!diskFile.is_open()) return "Error: No se pudo abrir el disco";

        MBR mbr;
        diskFile.seekg(0, std::ios::beg);
        diskFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' && strcmp(mbr.mbr_partitions[i].part_name, name.c_str()) == 0) {
                diskFile.close(); return "Error: Ya existe una partición con ese nombre";
            }
        }

        int partCount = 0;
        bool hasExtended = false;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1') {
                partCount++;
                if (mbr.mbr_partitions[i].part_type == 'E') hasExtended = true;
            }
        }

        if (partCount >= 4) { diskFile.close(); return "Error: Ya existen 4 particiones (máximo permitido)"; }
        if (type == 'E' && hasExtended) { diskFile.close(); return "Error: Ya existe una partición extendida"; }

        int selectedSlot = -1, bestStart = -1;
        if (fit == 'F') { 
            for (int i = 0; i < 4; i++) {
                if (mbr.mbr_partitions[i].part_status == '0') {
                    int currentPos = sizeof(MBR);
                    for (int j = 0; j < 4; j++) {
                        if (mbr.mbr_partitions[j].part_status == '1' && mbr.mbr_partitions[j].part_start == currentPos)
                            currentPos = mbr.mbr_partitions[j].part_start + mbr.mbr_partitions[j].part_size;
                    }
                    int availableSpace = mbr.mbr_size - currentPos;
                    if (availableSpace >= size) { selectedSlot = i; bestStart = currentPos; break; }
                }
            }
        } else if (fit == 'B') { 
            int minWaste = mbr.mbr_size;
            for (int i = 0; i < 4; i++) {
                if (mbr.mbr_partitions[i].part_status == '0') {
                    int currentPos = sizeof(MBR);
                    for (int j = 0; j < 4; j++) {
                        if (mbr.mbr_partitions[j].part_status == '1' && mbr.mbr_partitions[j].part_start == currentPos)
                            currentPos = mbr.mbr_partitions[j].part_start + mbr.mbr_partitions[j].part_size;
                    }
                    int availableSpace = mbr.mbr_size - currentPos;
                    int waste = availableSpace - size;
                    if (availableSpace >= size && waste < minWaste) { selectedSlot = i; bestStart = currentPos; minWaste = waste; }
                }
            }
        } else { 
            int maxSpace = 0;
            for (int i = 0; i < 4; i++) {
                if (mbr.mbr_partitions[i].part_status == '0') {
                    int currentPos = sizeof(MBR);
                    for (int j = 0; j < 4; j++) {
                        if (mbr.mbr_partitions[j].part_status == '1' && mbr.mbr_partitions[j].part_start == currentPos)
                            currentPos = mbr.mbr_partitions[j].part_start + mbr.mbr_partitions[j].part_size;
                    }
                    int availableSpace = mbr.mbr_size - currentPos;
                    if (availableSpace >= size && availableSpace > maxSpace) { selectedSlot = i; bestStart = currentPos; maxSpace = availableSpace; }
                }
            }
        }

        if (selectedSlot == -1 || bestStart == -1) { diskFile.close(); return "Error: No hay espacio suficiente en el disco"; }

        mbr.mbr_partitions[selectedSlot].part_status = '1';
        mbr.mbr_partitions[selectedSlot].part_type = type;
        mbr.mbr_partitions[selectedSlot].part_fit = fit;
        mbr.mbr_partitions[selectedSlot].part_start = bestStart;
        mbr.mbr_partitions[selectedSlot].part_size = size;
        strncpy(mbr.mbr_partitions[selectedSlot].part_name, name.c_str(), 16);

        if (type == 'E') {
            EBR ebr;
            ebr.part_status = '0';
            ebr.part_fit = fit;
            ebr.part_start = -1;
            ebr.part_size = 0;
            ebr.part_next = -1;
            memset(ebr.part_name, 0, 16);

            diskFile.seekp(bestStart, std::ios::beg);
            diskFile.write(reinterpret_cast<char*>(&ebr), sizeof(EBR));
        }

        diskFile.seekp(0, std::ios::beg);
        diskFile.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        diskFile.close();

        return "Partición " + std::string(1, type) + " '" + name + "' creada exitosamente.";
    }

    inline std::string createLogicalPartition(const std::string& path, int size, char fit, const std::string& name) {
        std::fstream diskFile(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!diskFile.is_open()) return "Error: No se pudo abrir el disco";

        MBR mbr;
        diskFile.seekg(0, std::ios::beg);
        diskFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

        int extendedIndex = -1;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '1' && mbr.mbr_partitions[i].part_type == 'E') {
                extendedIndex = i; break;
            }
        }

        if (extendedIndex == -1) { diskFile.close(); return "Error: No existe partición extendida."; }

        Partition& extended = mbr.mbr_partitions[extendedIndex];
        int extStart = extended.part_start;
        int extEnd = extended.part_start + extended.part_size;

        EBR currentEBR;
        diskFile.seekg(extStart, std::ios::beg);
        diskFile.read(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));

        if (currentEBR.part_status == '0') {
            currentEBR.part_status = '1';
            currentEBR.part_fit = fit;
            currentEBR.part_start = extStart + sizeof(EBR);
            currentEBR.part_size = size;
            currentEBR.part_next = -1;
            strncpy(currentEBR.part_name, name.c_str(), 16);

            diskFile.seekp(extStart, std::ios::beg);
            diskFile.write(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));
            diskFile.close();
            return "Partición lógica '" + name + "' creada exitosamente.";
        }

        int currentEBRPos = extStart;
        while (true) {
            diskFile.seekg(currentEBRPos, std::ios::beg);
            diskFile.read(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));

            if (currentEBR.part_status == '1' && strcmp(currentEBR.part_name, name.c_str()) == 0) {
                diskFile.close(); return "Error: Ya existe una partición lógica con ese nombre";
            }

            if (currentEBR.part_next == -1) {
                int nextEBRPos = currentEBR.part_start + currentEBR.part_size;
                int availableSpace = extEnd - nextEBRPos - sizeof(EBR);

                if (availableSpace < size) { diskFile.close(); return "Error: No hay espacio en la extendida."; }

                EBR newEBR;
                newEBR.part_status = '1';
                newEBR.part_fit = fit;
                newEBR.part_start = nextEBRPos + sizeof(EBR);
                newEBR.part_size = size;
                newEBR.part_next = -1;
                strncpy(newEBR.part_name, name.c_str(), 16);

                currentEBR.part_next = nextEBRPos;
                diskFile.seekp(currentEBRPos, std::ios::beg);
                diskFile.write(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));

                diskFile.seekp(nextEBRPos, std::ios::beg);
                diskFile.write(reinterpret_cast<char*>(&newEBR), sizeof(EBR));
                diskFile.close();
                return "Partición lógica '" + name + "' creada exitosamente.";
            }
            currentEBRPos = currentEBR.part_next;
        }
    }

    inline std::string execute(int size, const std::string& unit, const std::string& path, 
                               const std::string& type, const std::string& fit, 
                               const std::string& deleteName, const std::string& name, int add = 0) {
        try {
            std::string expandedPath = expandPath(path);

            std::ifstream checkFile(expandedPath);
            if (!checkFile.good()) { checkFile.close(); return "Error: El disco no existe"; }
            checkFile.close();

            // 1. FLUJO DE ELIMINACIÓN
            if (!deleteName.empty()) {
                return deletePartition(expandedPath, deleteName);
            }

            // 2. FLUJO DE AGREGAR/QUITAR ESPACIO
            if (add != 0) {
                if (name.empty()) return "Error: Se requiere el parámetro -name para hacer -add.";
                std::string u = unit.empty() ? "K" : unit;
                return addPartitionSpace(expandedPath, name, add, u);
            }

            // 3. FLUJO DE CREACIÓN DE PARTICIÓN
            if (name.empty()) return "Error: Se requiere el parámetro -name";
            if (size <= 0) return "Error: El tamaño debe ser mayor a 0";

            int sizeInBytes = size;
            if (unit == "k" || unit == "K") sizeInBytes = size * 1024;
            else if (unit == "m" || unit == "M") sizeInBytes = size * 1024 * 1024;
            else return "Error: Unidad no válida. Use 'k' para KB o 'm' para MB";

            char partType = 'P';  
            if (!type.empty()) {
                if (type == "p" || type == "P") partType = 'P';
                else if (type == "e" || type == "E") partType = 'E';
                else if (type == "l" || type == "L") partType = 'L';
                else return "Error: Tipo inválido. Use P, E o L";
            }

            char partFit = 'W';  
            if (!fit.empty()) {
                if (fit == "bf" || fit == "BF") partFit = 'B';
                else if (fit == "ff" || fit == "FF") partFit = 'F';
                else if (fit == "wf" || fit == "WF") partFit = 'W';
                else return "Error: Fit inválido. Use BF, FF o WF";
            }

            if (partType == 'L') return createLogicalPartition(expandedPath, sizeInBytes, partFit, name);
            else return createPrimaryOrExtendedPartition(expandedPath, sizeInBytes, partType, partFit, name);

        } catch (const std::exception& e) {
            return std::string("Error en fdisk: ") + e.what();
        }
    }

}

#endif // FDISK_H