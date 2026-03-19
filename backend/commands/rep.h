#ifndef REP_H
#define REP_H

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <vector>
#include <iomanip>
#include <sys/stat.h>
#include <libgen.h>
#include <map>

#include "../core/structures.h"
#include "mount.h"

namespace CommandRep {
    
    inline std::string toLowerCase(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }
    
    // Crear directorios recursivamente si no existen
    inline bool createDirectories(const std::string& path) {
        char tmp[1024];
        char *p = nullptr;
        size_t len;
        
        snprintf(tmp, sizeof(tmp), "%s", path.c_str());
        len = strlen(tmp);
        if (tmp[len - 1] == '/') {
            tmp[len - 1] = 0;
        }
        
        for (p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = 0;
                mkdir(tmp, S_IRWXU);
                *p = '/';
            }
        }
        mkdir(tmp, S_IRWXU);
        return true;
    }
    
    // Obtener el directorio padre de una ruta
    inline std::string getParentPath(const std::string& path) {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", path.c_str());
        return std::string(dirname(tmp));
    }
    
    // Obtener extensión del archivo
    inline std::string getExtension(const std::string& path) {
        size_t pos = path.find_last_of('.');
        if (pos != std::string::npos) {
            return toLowerCase(path.substr(pos + 1));
        }
        return "jpg"; // Por defecto
    }
    
    inline std::string reportMBR(const std::string& path, const std::string& diskPath) {
        std::ifstream file(diskPath, std::ios::binary);
        if (!file.is_open()) {
            return "Error: no se pudo abrir el disco '" + diskPath + "'";
        }
        
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        std::ostringstream dot;
        dot << "digraph MBR_Report {\n";
        dot << "    node [shape=plaintext]\n";
        dot << "    rankdir=TB;\n\n";
        dot << "    mbr [label=<<TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
        dot << "        <TR><TD COLSPAN=\"2\" BGCOLOR=\"#000000\"><FONT COLOR=\"white\"><B>MBR</B></FONT></TD></TR>\n";
        dot << "        <TR><TD><B>mbr_tamano</B></TD><TD>" << mbr.mbr_size << "</TD></TR>\n";
        
        char dateStr[100];
        struct tm* timeinfo = localtime(&mbr.mbr_creation_date);
        strftime(dateStr, sizeof(dateStr), "%d/%m/%Y %H:%M:%S", timeinfo);
        dot << "        <TR><TD><B>mbr_fecha_creacion</B></TD><TD>" << dateStr << "</TD></TR>\n";
        dot << "        <TR><TD><B>mbr_dsk_signature</B></TD><TD>" << mbr.mbr_disk_signature << "</TD></TR>\n";
        dot << "        <TR><TD><B>dsk_fit</B></TD><TD>" << mbr.disk_fit << "</TD></TR>\n";
        
        int partNum = 1;
        for (int i = 0; i < 4; i++) {
            Partition& part = mbr.mbr_partitions[i];
            if (part.part_status == '1') {
                dot << "        <TR><TD COLSPAN=\"2\" BGCOLOR=\"#000000\"><FONT COLOR=\"white\"><B>Partition " << partNum << "</B></FONT></TD></TR>\n";
                dot << "        <TR><TD><B>status</B></TD><TD>" << part.part_status << "</TD></TR>\n";
                dot << "        <TR><TD><B>type</B></TD><TD>" << part.part_type << "</TD></TR>\n";
                dot << "        <TR><TD><B>fit</B></TD><TD>" << part.part_fit << "</TD></TR>\n";
                dot << "        <TR><TD><B>start</B></TD><TD>" << part.part_start << "</TD></TR>\n";
                dot << "        <TR><TD><B>size</B></TD><TD>" << part.part_size << "</TD></TR>\n";
                dot << "        <TR><TD><B>name</B></TD><TD>" << part.part_name << "</TD></TR>\n";
                partNum++;
            }
        }
        
        dot << "    </TABLE>>];\n}\n";
        file.close();
        
        createDirectories(getParentPath(path));
        
        std::string dotPath = path + ".dot";
        std::ofstream dotFile(dotPath);
        if (!dotFile.is_open()) return "Error: no se pudo crear el archivo .dot";
        dotFile << dot.str();
        dotFile.close();
        
        std::string cmd = "dot -T" + getExtension(path) + " \"" + dotPath + "\" -o \"" + path + "\"";
        int result = system(cmd.c_str());
        remove(dotPath.c_str());
        
        if (result != 0) return "Error: no se pudo generar el reporte con Graphviz (¿Está instalado 'dot'?)";
        
        return "Reporte MBR generado exitosamente en: " + path;
    }
    
    inline std::string reportDISK(const std::string& path, const std::string& diskPath) {
        std::ifstream file(diskPath, std::ios::binary);
        if (!file.is_open()) {
            return "Error: no se pudo abrir el disco '" + diskPath + "'";
        }
        
        // Leer MBR
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        int diskSize = mbr.mbr_size;
        
        // Estructura para secciones del disco
        struct DiskSection {
            std::string type;
            std::string name;
            int start;
            int size;
            double percent;
            bool isExtended;
        };
        
        std::vector<DiskSection> allSections;
        
        // Agregar MBR
        DiskSection mbrSec;
        mbrSec.type = "mbr";
        mbrSec.name = "MBR";
        mbrSec.start = 0;
        mbrSec.size = sizeof(MBR);
        mbrSec.percent = (sizeof(MBR) * 100.0) / diskSize;
        mbrSec.isExtended = false;
        allSections.push_back(mbrSec);
        
        // Procesar particiones
        for (int i = 0; i < 4; i++) {
            Partition& part = mbr.mbr_partitions[i];
            if (part.part_status == '1') {
                if (part.part_type == 'E' || part.part_type == 'e') {
                    // Partición extendida - expandir con EBR y lógicas
                    int ebr_start = part.part_start;
                    int ext_end = part.part_start + part.part_size;
                    int current_pos = part.part_start;
                    
                    while (ebr_start != -1 && ebr_start < ext_end) {
                        EBR ebr;
                        file.seekg(ebr_start, std::ios::beg);
                        file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                        
                        // Espacio libre antes del EBR
                        if (ebr_start > current_pos) {
                            DiskSection freeSec;
                            freeSec.type = "free";
                            freeSec.name = "Libre";
                            freeSec.start = current_pos;
                            freeSec.size = ebr_start - current_pos;
                            freeSec.percent = (freeSec.size * 100.0) / diskSize;
                            freeSec.isExtended = true;
                            allSections.push_back(freeSec);
                        }
                        
                        if (ebr.part_status == '1') {
                            // EBR
                            DiskSection ebrSec;
                            ebrSec.type = "ebr";
                            ebrSec.name = "EBR";
                            ebrSec.start = ebr_start;
                            ebrSec.size = sizeof(EBR);
                            ebrSec.percent = (sizeof(EBR) * 100.0) / diskSize;
                            ebrSec.isExtended = true;
                            allSections.push_back(ebrSec);
                            
                            // Partición Lógica
                            DiskSection logSec;
                            logSec.type = "logical";
                            logSec.name = std::string(ebr.part_name);
                            logSec.start = ebr_start + sizeof(EBR);
                            logSec.size = ebr.part_size;
                            logSec.percent = (ebr.part_size * 100.0) / diskSize;
                            logSec.isExtended = true;
                            allSections.push_back(logSec);
                            
                            current_pos = ebr_start + sizeof(EBR) + ebr.part_size;
                        }
                        
                        ebr_start = ebr.part_next;
                        if (ebr_start <= 0) break;
                    }
                    
                    // Espacio libre al final de la extendida
                    if (current_pos < ext_end) {
                        DiskSection freeSec;
                        freeSec.type = "free";
                        freeSec.name = "Libre";
                        freeSec.start = current_pos;
                        freeSec.size = ext_end - current_pos;
                        freeSec.percent = (freeSec.size * 100.0) / diskSize;
                        freeSec.isExtended = true;
                        allSections.push_back(freeSec);
                    }
                } else {
                    // Partición primaria
                    DiskSection primSec;
                    primSec.type = "partition";
                    primSec.name = std::string(part.part_name);
                    primSec.start = part.part_start;
                    primSec.size = part.part_size;
                    primSec.percent = (part.part_size * 100.0) / diskSize;
                    primSec.isExtended = false;
                    allSections.push_back(primSec);
                }
            }
        }
        
        // Ordenar por posición
        std::sort(allSections.begin(), allSections.end(), 
                  [](const DiskSection& a, const DiskSection& b) { return a.start < b.start; });
        
        // Calcular espacios libres entre secciones (fuera de extendida)
        std::vector<DiskSection> finalSections;
        int currentPos = 0;
        
        for (const auto& sec : allSections) {
            if (!sec.isExtended && sec.start > currentPos && sec.type != "mbr") {
                DiskSection freeSec;
                freeSec.type = "free";
                freeSec.name = "Libre";
                freeSec.start = currentPos;
                freeSec.size = sec.start - currentPos;
                freeSec.percent = (freeSec.size * 100.0) / diskSize;
                freeSec.isExtended = false;
                finalSections.push_back(freeSec);
            }
            
            finalSections.push_back(sec);
            
            if (!sec.isExtended) {
                currentPos = sec.start + sec.size;
            }
        }
        
        // Espacio libre al final
        if (currentPos < diskSize) {
            DiskSection freeSec;
            freeSec.type = "free";
            freeSec.name = "Libre";
            freeSec.start = currentPos;
            freeSec.size = diskSize - currentPos;
            freeSec.percent = (freeSec.size * 100.0) / diskSize;
            freeSec.isExtended = false;
            finalSections.push_back(freeSec);
        }
        
        // Generar código DOT para Graphviz
        std::ostringstream dot;
        dot << "digraph DISK_Report {\n";
        dot << "    node [shape=plaintext]\n";
        dot << "    rankdir=LR;\n\n";
        
        // Crear tabla con dos filas: encabezado "Extendida" y contenido
        dot << "    disk [label=<<TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
        
        // Primera fila: encabezados (solo "Extendida" sobre las secciones extendidas)
        dot << "        <TR>\n";
        bool inExtended = false;
        int extendedCols = 0;
        
        // Contar columnas de extendida
        for (const auto& sec : finalSections) {
            if (sec.isExtended) {
                extendedCols++;
            }
        }
        
        for (const auto& sec : finalSections) {
            if (sec.isExtended && !inExtended) {
                // Inicio de sección extendida
                inExtended = true;
                dot << "            <TD COLSPAN=\"" << extendedCols << "\" BGCOLOR=\"#FFFFFF\"><B>Extendida</B></TD>\n";
            } else if (!sec.isExtended && inExtended) {
                inExtended = false;
                dot << "            <TD></TD>\n";
            } else if (!sec.isExtended && !inExtended) {
                dot << "            <TD></TD>\n";
            }
        }
        dot << "        </TR>\n";
        
        // Segunda fila: contenido real
        dot << "        <TR>\n";
        
        for (const auto& sec : finalSections) {
            std::string color;
            
            if (sec.type == "mbr") {
                color = "#CCCCCC";
                dot << "            <TD BGCOLOR=\"" << color << "\"><B>" << sec.name << "</B></TD>\n";
            } else if (sec.type == "free") {
                color = "#FFFFFF";
                dot << "            <TD BGCOLOR=\"" << color << "\"><B>" << sec.name << "<BR/>"
                    << std::fixed << std::setprecision(0) << sec.percent << "% del disco</B></TD>\n";
            } else if (sec.type == "ebr") {
                color = "#B0BEC5";  // Gris más oscuro para EBR
                dot << "            <TD BGCOLOR=\"" << color << "\"><B>" << sec.name << "</B></TD>\n";
            } else if (sec.type == "logical") {
                color = "#E3F2FD";  // Azul claro para lógicas
                dot << "            <TD BGCOLOR=\"" << color << "\"><B>Lógica<BR/>"
                    << std::fixed << std::setprecision(0) << sec.percent << "% Del Disco</B></TD>\n";
            } else if (sec.type == "partition") {
                color = "#C8E6C9";  // Verde claro para primarias
                dot << "            <TD BGCOLOR=\"" << color << "\"><B>Primaria<BR/>"
                    << std::fixed << std::setprecision(0) << sec.percent << "% del disco</B></TD>\n";
            }
        }
        
        dot << "        </TR>\n";
        dot << "    </TABLE>>];\n\n";
        dot << "}\n";
        
        file.close();
        
        // Crear directorio si no existe
        std::string parentPath = getParentPath(path);
        createDirectories(parentPath);
        
        // Guardar archivo .dot
        std::string dotPath = path + ".dot";
        std::ofstream dotFile(dotPath);
        if (!dotFile.is_open()) {
            return "Error: no se pudo crear el archivo .dot";
        }
        dotFile << dot.str();
        dotFile.close();
        
        // Ejecutar Graphviz para generar la imagen
        std::string ext = getExtension(path);
        std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\"";
        int result = system(cmd.c_str());
        
        // Eliminar archivo .dot temporal
        remove(dotPath.c_str());
        
        if (result != 0) {
            return "Error: no se pudo generar el reporte con Graphviz";
        }
        
        return "Reporte DISK generado exitosamente en: " + path;
        return "Reporte DISK generado exitosamente en: " + path; 
    }
    
    inline std::string reportINODE(const std::string& path, const std::string& diskPath, int partStart, const std::string& pathFileLs) {
        std::ifstream file(diskPath, std::ios::binary);
        if (!file.is_open()) {
            return "Error: no se pudo abrir el disco '" + diskPath + "'";
        }
        
        // Leer Superblock
        Superblock sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));
        
        // Leer bitmap de inodos para saber cuáles están en uso
        file.seekg(sb.s_bm_inode_start, std::ios::beg);
        std::vector<char> bitmap(sb.s_inodes_count);
        file.read(bitmap.data(), sb.s_inodes_count);
        
        // Leer todos los inodos en uso
        std::vector<std::pair<int, Inode>> usedInodes;
        for (int i = 0; i < sb.s_inodes_count; i++) {
            if (bitmap[i] == '1') {
                Inode inode;
                file.seekg(sb.s_inode_start + (i * sizeof(Inode)), std::ios::beg);
                file.read(reinterpret_cast<char*>(&inode), sizeof(Inode));
                usedInodes.push_back({i, inode});
            }
        }
        file.close();
        
        if (usedInodes.empty()) {
            return "No hay inodos en uso";
        }
        
        // Generar el DOT para Graphviz
        std::ostringstream dot;
        dot << "digraph INODE_Report {\n";
        dot << "    node [shape=plaintext]\n";
        dot << "    rankdir=LR;\n";
        dot << "    splines=line;\n\n";
        
        // Crear una tabla para cada inodo en uso
        for (size_t idx = 0; idx < usedInodes.size(); idx++) {
            int inodeNum = usedInodes[idx].first;
            Inode& inode = usedInodes[idx].second;
            
            // Determinar el color según el tipo
            std::string headerColor = (inode.i_type == '1') ? "#FF9800" : "#E53935";  // Naranja para directorio, Rojo para archivo
            std::string tipoStr = (inode.i_type == '1') ? "Directorio" : "Archivo";
            
            dot << "    inode" << inodeNum << " [label=<<TABLE BORDER=\"2\" CELLBORDER=\"0\" CELLSPACING=\"4\" CELLPADDING=\"8\" STYLE=\"rounded\">\n";
            
            // Cabecera con tipo
            std::string icon = (inode.i_type == '1') ? "[DIR]" : "[FILE]";
            dot << "        <TR><TD COLSPAN=\"2\" BGCOLOR=\"" << headerColor << "\">"
                << "<FONT COLOR=\"white\" POINT-SIZE=\"12\"><B>" << icon << " Inodo " << inodeNum << "</B></FONT></TD></TR>\n";
            
            // i_uid
            dot << "        <TR><TD ALIGN=\"LEFT\"><FONT POINT-SIZE=\"10\">i_uid:</FONT></TD>"
                << "<TD ALIGN=\"RIGHT\"><FONT POINT-SIZE=\"10\" COLOR=\"#1976D2\"><B>" << inode.i_uid << "</B></FONT></TD></TR>\n";
            
            // i_size
            dot << "        <TR><TD ALIGN=\"LEFT\"><FONT POINT-SIZE=\"10\">i_size:</FONT></TD>"
                << "<TD ALIGN=\"RIGHT\"><FONT POINT-SIZE=\"10\" COLOR=\"#1976D2\"><B>" << inode.i_size << " bytes</B></FONT></TD></TR>\n";
            
            // Fechas
            char dateStr[100];
            struct tm* timeinfo;
            
            // i_atime
            timeinfo = localtime(&inode.i_atime);
            strftime(dateStr, sizeof(dateStr), "%d/%m/%Y %H:%M", timeinfo);
            dot << "        <TR><TD ALIGN=\"LEFT\"><FONT POINT-SIZE=\"10\">i_atime:</FONT></TD>"
                << "<TD ALIGN=\"RIGHT\"><FONT POINT-SIZE=\"9\" COLOR=\"#7B8894\">" << dateStr << "</FONT></TD></TR>\n";
            
            // i_ctime
            timeinfo = localtime(&inode.i_ctime);
            strftime(dateStr, sizeof(dateStr), "%d/%m/%Y %H:%M", timeinfo);
            dot << "        <TR><TD ALIGN=\"LEFT\"><FONT POINT-SIZE=\"10\">i_ctime:</FONT></TD>"
                << "<TD ALIGN=\"RIGHT\"><FONT POINT-SIZE=\"9\" COLOR=\"#7B8894\">" << dateStr << "</FONT></TD></TR>\n";
            
            // i_mtime
            timeinfo = localtime(&inode.i_mtime);
            strftime(dateStr, sizeof(dateStr), "%d/%m/%Y %H:%M", timeinfo);
            dot << "        <TR><TD ALIGN=\"LEFT\"><FONT POINT-SIZE=\"10\">i_mtime:</FONT></TD>"
                << "<TD ALIGN=\"RIGHT\"><FONT POINT-SIZE=\"9\" COLOR=\"#7B8894\">" << dateStr << "</FONT></TD></TR>\n";
            
            // i_type
            dot << "        <TR><TD ALIGN=\"LEFT\"><FONT POINT-SIZE=\"10\">i_type:</FONT></TD>"
                << "<TD ALIGN=\"RIGHT\"><FONT POINT-SIZE=\"10\" COLOR=\"#5E35B1\"><B>" << tipoStr << "</B></FONT></TD></TR>\n";
            
            // i_perm (con badge)
            dot << "        <TR><TD ALIGN=\"LEFT\"><FONT POINT-SIZE=\"10\">i_perm:</FONT></TD>"
                << "<TD ALIGN=\"RIGHT\" BGCOLOR=\"#26A69A\"><FONT POINT-SIZE=\"10\" COLOR=\"white\"><B>" << inode.i_perm << "</B></FONT></TD></TR>\n";
            
            // Espaciador
            dot << "        <TR><TD COLSPAN=\"2\" HEIGHT=\"10\"></TD></TR>\n";
            
            // Bloques asignados
            dot << "        <TR><TD COLSPAN=\"2\" ALIGN=\"LEFT\"><FONT POINT-SIZE=\"10\"><B>Bloques asignados:</B></FONT></TD></TR>\n";
            
            // Contar bloques asignados
            std::vector<int> assignedBlocks;
            for (int i = 0; i < 15; i++) {
                if (inode.i_block[i] != -1) {
                    assignedBlocks.push_back(inode.i_block[i]);
                }
            }
            
            // Mostrar bloques en círculos morados
            if (!assignedBlocks.empty()) {
                dot << "        <TR><TD COLSPAN=\"2\" ALIGN=\"CENTER\">";
                for (size_t i = 0; i < assignedBlocks.size(); i++) {
                    dot << "<FONT POINT-SIZE=\"14\" COLOR=\"#7E57C2\">&#9679;</FONT>"
                        << "<FONT POINT-SIZE=\"10\" COLOR=\"#7E57C2\"><B>" << assignedBlocks[i] << "</B></FONT>";
                    if (i < assignedBlocks.size() - 1) {
                        dot << "  ";
                    }
                }
                dot << "</TD></TR>\n";
                
                // Total de bloques
                dot << "        <TR><TD COLSPAN=\"2\" ALIGN=\"CENTER\">"
                    << "<FONT POINT-SIZE=\"9\" COLOR=\"#9E9E9E\">Total: " << assignedBlocks.size() 
                    << " bloques asignados</FONT></TD></TR>\n";
            } else {
                dot << "        <TR><TD COLSPAN=\"2\" ALIGN=\"CENTER\">"
                    << "<FONT POINT-SIZE=\"10\" COLOR=\"#9E9E9E\"><I>Sin bloques asignados</I></FONT></TD></TR>\n";
            }
            
            dot << "    </TABLE>>];\n\n";
        }
        
        // Crear conexiones entre inodos con flechas visibles
        if (usedInodes.size() > 1) {
            dot << "    // Conexiones entre inodos\n";
            for (size_t i = 0; i < usedInodes.size() - 1; i++) {
                int currentInode = usedInodes[i].first;
                int nextInode = usedInodes[i + 1].first;
                dot << "    inode" << currentInode << " -> inode" << nextInode 
                    << " [color=\"#2196F3\", penwidth=2, arrowsize=1.2];\n";
            }
        }
        
        dot << "}\n";
        
        // Crear directorios si no existen
        std::string parentPath = getParentPath(path);
        createDirectories(parentPath);
        
        // Guardar archivo .dot
        std::string dotPath = path + ".dot";
        std::ofstream dotFile(dotPath);
        if (!dotFile.is_open()) {
            return "Error: no se pudo crear el archivo .dot";
        }
        dotFile << dot.str();
        dotFile.close();
        
        // Ejecutar Graphviz para generar la imagen
        std::string ext = getExtension(path);
        std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\" 2>&1";
        int result = system(cmd.c_str());
        
        if (result != 0) {
            // No eliminar el .dot para poder depurar
            return "Error: no se pudo generar el reporte con Graphviz.\n"
                   "Archivo DOT guardado en: " + dotPath + "\n"
                   "Puedes revisarlo o ejecutar manualmente: " + cmd;
        }
        
        remove(dotPath.c_str());
        
        return "Reporte INODE generado exitosamente en: " + path + " (" 
               + std::to_string(usedInodes.size()) + " inodos utilizados)";
    }

    inline std::string reportBM_INODE(const std::string& path, const std::string& diskPath, int partStart) {
        std::ifstream file(diskPath, std::ios::binary);
        if (!file.is_open()) return "Error: no se pudo abrir el disco.";

        Superblock sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

        std::vector<char> bitmap(sb.s_inodes_count);
        file.seekg(sb.s_bm_inode_start, std::ios::beg);
        file.read(bitmap.data(), sb.s_inodes_count);
        file.close();

        createDirectories(getParentPath(path));

        std::ofstream txtFile(path);
        if (!txtFile.is_open()) return "Error: no se pudo crear el archivo txt.";

        int count = 0;
        for (int i = 0; i < sb.s_inodes_count; i++) {
            txtFile << bitmap[i] << " ";
            count++;
            if (count == 20) {
                txtFile << "\n";
                count = 0;
            }
        }
        txtFile.close();

        return "Reporte BM_INODE generado exitosamente en: " + path;
    }

    inline std::string reportBM_BLOCK(const std::string& path, const std::string& diskPath, int partStart) {
        std::ifstream file(diskPath, std::ios::binary);
        if (!file.is_open()) return "Error: no se pudo abrir el disco.";

        Superblock sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

        std::vector<char> bitmap(sb.s_blocks_count);
        file.seekg(sb.s_bm_block_start, std::ios::beg);
        file.read(bitmap.data(), sb.s_blocks_count);
        file.close();

        createDirectories(getParentPath(path));

        std::ofstream txtFile(path);
        if (!txtFile.is_open()) return "Error: no se pudo crear el archivo txt.";

        int count = 0;
        for (int i = 0; i < sb.s_blocks_count; i++) {
            txtFile << bitmap[i] << " ";
            count++;
            if (count == 20) {
                txtFile << "\n";
                count = 0;
            }
        }
        txtFile.close();

        return "Reporte BM_BLOCK generado exitosamente en: " + path;
    }

    inline std::string escapeHtml(const std::string& data) {
        std::string buffer;
        buffer.reserve(data.size());
        for(size_t pos = 0; pos != data.size(); ++pos) {
            switch(data[pos]) {
                case '&':  buffer.append("&amp;");       break;
                case '\"': buffer.append("&quot;");      break;
                case '\'': buffer.append("&apos;");      break;
                case '<':  buffer.append("&lt;");        break;
                case '>':  buffer.append("&gt;");        break;
                case '\n': buffer.append("<br/>");       break;
                default:   buffer.append(&data[pos], 1); break;
            }
        }
        return buffer;
    }

    inline std::string reportBLOCK(const std::string& path, const std::string& diskPath, int partStart) {
        std::ifstream file(diskPath, std::ios::binary);
        if (!file.is_open()) return "Error: no se pudo abrir el disco.";

        Superblock sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

        std::vector<char> inodeBitmap(sb.s_inodes_count);
        file.seekg(sb.s_bm_inode_start, std::ios::beg);
        file.read(inodeBitmap.data(), sb.s_inodes_count);

        std::map<int, std::string> blocksDot;

        for (int i = 0; i < sb.s_inodes_count; i++) {
            if (inodeBitmap[i] == '1') {
                Inode inode;
                file.seekg(sb.s_inode_start + (i * sizeof(Inode)), std::ios::beg);
                file.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

                for (int b = 0; b < 12; b++) {
                    int blockNum = inode.i_block[b];
                    if (blockNum != -1) {
                        std::ostringstream bDot;
                        
                        if (inode.i_type == '0') { 
                            FolderBlock fb;
                            file.seekg(sb.s_block_start + (blockNum * sizeof(FolderBlock)), std::ios::beg);
                            file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                            bDot << "<TABLE BORDER=\"2\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\" STYLE=\"rounded\">\n";
                            bDot << "    <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4CAF50\"><FONT COLOR=\"white\"><B>Bloque Carpeta " << blockNum << "</B></FONT></TD></TR>\n";
                            bDot << "    <TR><TD BGCOLOR=\"#E8F5E9\"><B>b_name</B></TD><TD BGCOLOR=\"#E8F5E9\"><B>b_inodo</B></TD></TR>\n";
                            for (int c = 0; c < 4; c++) {
                                std::string name(fb.b_content[c].b_name);
                                if (name.empty()) name = "-";
                                bDot << "    <TR><TD>" << escapeHtml(name) << "</TD><TD>" << fb.b_content[c].b_inodo << "</TD></TR>\n";
                            }
                            bDot << "</TABLE>";
                            
                        } else if (inode.i_type == '1') {
                            FileBlock fb;
                            file.seekg(sb.s_block_start + (blockNum * sizeof(FileBlock)), std::ios::beg);
                            file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));

                            std::string content(fb.b_content, 64);
                            content.erase(std::remove(content.begin(), content.end(), '\0'), content.end());
                            
                            bDot << "<TABLE BORDER=\"2\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\" STYLE=\"rounded\">\n";
                            bDot << "    <TR><TD BGCOLOR=\"#FF9800\"><FONT COLOR=\"white\"><B>Bloque Archivo " << blockNum << "</B></FONT></TD></TR>\n";
                            bDot << "    <TR><TD>" << escapeHtml(content) << "</TD></TR>\n";
                            bDot << "</TABLE>";
                        }
                        
                        if (!bDot.str().empty()) {
                            blocksDot[blockNum] = bDot.str();
                        }
                    }
                }
            }
        }
        file.close();

        if (blocksDot.empty()) return "No hay bloques en uso para generar el reporte.";

        std::ostringstream dot;
        dot << "digraph BLOCK_Report {\n";
        dot << "    node [shape=plaintext]\n";
        dot << "    rankdir=LR;\n\n";

        for (const auto& [bNum, bContent] : blocksDot) {
            dot << "    block" << bNum << " [label=<" << bContent << ">];\n";
        }

        dot << "\n    // Conexiones invisibles para mantener el orden visual\n";
        auto it = blocksDot.begin();
        int prevBlock = it->first;
        it++;
        while (it != blocksDot.end()) {
            dot << "    block" << prevBlock << " -> block" << it->first << " [style=invis];\n";
            prevBlock = it->first;
            it++;
        }

        dot << "}\n";

        createDirectories(getParentPath(path));

        std::string dotPath = path + ".dot";
        std::ofstream dotFile(dotPath);
        if (!dotFile.is_open()) return "Error: no se pudo crear el archivo .dot";
        dotFile << dot.str();
        dotFile.close();

        std::string ext = getExtension(path);
        std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\" 2>&1";
        int result = system(cmd.c_str());

        if (result != 0) {
            return "Error: no se pudo generar el reporte con Graphviz.\nArchivo DOT guardado en: " + dotPath;
        }

        remove(dotPath.c_str());
        return "Reporte BLOCK generado exitosamente en: " + path + " (" + std::to_string(blocksDot.size()) + " bloques)";
    }

    inline std::string reportTREE(const std::string& path, const std::string& diskPath, int partStart) {
        std::ifstream file(diskPath, std::ios::binary);
        if (!file.is_open()) return "Error: no se pudo abrir el disco.";

        Superblock sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

        std::vector<char> inodeBitmap(sb.s_inodes_count);
        file.seekg(sb.s_bm_inode_start, std::ios::beg);
        file.read(inodeBitmap.data(), sb.s_inodes_count);

        // AQUÍ ESTÁ LA MAGIA: Dos flujos separados
        std::ostringstream dotNodes;
        std::ostringstream dotEdges;

        dotNodes << "digraph TREE_Report {\n";
        dotNodes << "    node [shape=plaintext]\n";
        dotNodes << "    rankdir=LR;\n"; 
        dotNodes << "    nodesep=0.5;\n";
        dotNodes << "    ranksep=1.5;\n\n";

        // Mapa para evitar dibujar el mismo bloque dos veces
        std::map<int, bool> processedBlocks;

        for (int i = 0; i < sb.s_inodes_count; i++) {
            if (inodeBitmap[i] == '1') {
                Inode inode;
                file.seekg(sb.s_inode_start + (i * sizeof(Inode)), std::ios::beg);
                file.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

                std::string tipoStr = (inode.i_type == '0') ? "Carpeta" : "Archivo";
                std::string headerColor = (inode.i_type == '0') ? "#1976D2" : "#00796B"; 

                // --- TABLA DEL INODO ---
                dotNodes << "    inode" << i << " [label=<<TABLE BORDER=\"2\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\" STYLE=\"rounded\">\n";
                dotNodes << "        <TR><TD COLSPAN=\"2\" BGCOLOR=\"" << headerColor << "\"><FONT COLOR=\"white\"><B>Inodo " << i << " (" << tipoStr << ")</B></FONT></TD></TR>\n";
                dotNodes << "        <TR><TD>i_uid</TD><TD>" << inode.i_uid << "</TD></TR>\n";
                dotNodes << "        <TR><TD>i_size</TD><TD>" << inode.i_size << " bytes</TD></TR>\n";
                dotNodes << "        <TR><TD>i_type</TD><TD>" << inode.i_type << "</TD></TR>\n";
                
                for (int b = 0; b < 12; b++) {
                    dotNodes << "        <TR><TD>i_block[" << b << "]</TD><TD PORT=\"f" << b << "\">" << inode.i_block[b] << "</TD></TR>\n";
                }
                dotNodes << "    </TABLE>>];\n\n";

                // --- BLOQUES APUNTADOS POR EL INODO ---
                for (int b = 0; b < 12; b++) {
                    int blockNum = inode.i_block[b];
                    if (blockNum != -1) {
                        
                        // FLECHA: Inodo -> Bloque (¡Va directo a dotEdges!)
                        dotEdges << "    inode" << i << ":f" << b << " -> block" << blockNum << " [color=\"#D32F2F\", penwidth=1.5];\n";

                        if (!processedBlocks[blockNum]) {
                            processedBlocks[blockNum] = true;

                            if (inode.i_type == '0') { 
                                // ES UN BLOQUE CARPETA
                                FolderBlock fb;
                                file.seekg(sb.s_block_start + (blockNum * sizeof(FolderBlock)), std::ios::beg);
                                file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                                dotNodes << "    block" << blockNum << " [label=<<TABLE BORDER=\"2\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\" STYLE=\"rounded\">\n";
                                dotNodes << "        <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4CAF50\"><FONT COLOR=\"white\"><B>Bloque Carpeta " << blockNum << "</B></FONT></TD></TR>\n";
                                dotNodes << "        <TR><TD BGCOLOR=\"#E8F5E9\"><B>b_name</B></TD><TD BGCOLOR=\"#E8F5E9\"><B>b_inodo</B></TD></TR>\n";

                                for (int c = 0; c < 4; c++) {
                                    std::string name(fb.b_content[c].b_name);
                                    if (name.empty()) name = "-";
                                    int pointedInode = fb.b_content[c].b_inodo;
                                    
                                    dotNodes << "        <TR><TD>" << escapeHtml(name) << "</TD><TD PORT=\"c" << c << "\">" << pointedInode << "</TD></TR>\n";
                                    
                                    // FLECHA: Bloque Carpeta -> Inodo hijo (¡Va directo a dotEdges!)
                                    if (pointedInode != -1 && name != "." && name != "..") {
                                        dotEdges << "    block" << blockNum << ":c" << c << " -> inode" << pointedInode << " [color=\"#1976D2\", penwidth=1.5];\n";
                                    }
                                }
                                dotNodes << "    </TABLE>>];\n\n";
                                
                            } else if (inode.i_type == '1') { 
                                // ES UN BLOQUE ARCHIVO
                                FileBlock fb;
                                file.seekg(sb.s_block_start + (blockNum * sizeof(FileBlock)), std::ios::beg);
                                file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));

                                std::string content(fb.b_content, 64);
                                content.erase(std::remove(content.begin(), content.end(), '\0'), content.end());
                                
                                dotNodes << "    block" << blockNum << " [label=<<TABLE BORDER=\"2\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\" STYLE=\"rounded\">\n";
                                dotNodes << "        <TR><TD BGCOLOR=\"#FF9800\"><FONT COLOR=\"white\"><B>Bloque Archivo " << blockNum << "</B></FONT></TD></TR>\n";
                                dotNodes << "        <TR><TD>" << escapeHtml(content) << "</TD></TR>\n";
                                dotNodes << "    </TABLE>>];\n\n";
                            }
                        }
                    }
                }
            }
        }

        std::ostringstream finalDot;
        finalDot << dotNodes.str();
        finalDot << "\n    // =====================================\n";
        finalDot << "    // CONEXIONES (Aisladas para evitar errores)\n";
        finalDot << "    // =====================================\n";
        finalDot << dotEdges.str();
        finalDot << "}\n";

        file.close();

        createDirectories(getParentPath(path));
        std::string dotPath = path + ".dot";
        std::ofstream dotFile(dotPath);
        if (!dotFile.is_open()) return "Error: no se pudo crear el archivo .dot";
        dotFile << finalDot.str();
        dotFile.close();

        std::string ext = getExtension(path);
        std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\" 2>&1";
        int result = system(cmd.c_str());

        if (result != 0) {
            return "Error: no se pudo generar el reporte con Graphviz.\nArchivo DOT guardado en: " + dotPath;
        }

        remove(dotPath.c_str());
        return "Reporte TREE generado exitosamente en: " + path;
    }

    inline std::string reportSB(const std::string& path, const std::string& diskPath, int partStart) {
        std::ifstream file(diskPath, std::ios::binary);
        if (!file.is_open()) return "Error: no se pudo abrir el disco.";

        // Leer el superbloque
        Superblock sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));
        file.close();

        auto formatDate = [](time_t time_val) {
            char dateStr[100];
            struct tm* timeinfo = localtime(&time_val);
            if (timeinfo) {
                strftime(dateStr, sizeof(dateStr), "%d/%m/%Y %H:%M:%S", timeinfo);
                return std::string(dateStr);
            }
            return std::string("Sin fecha");
        };

        std::ostringstream dot;
        dot << "digraph SB_Report {\n";
        dot << "    node [shape=plaintext]\n";
        dot << "    rankdir=TB;\n\n";

        dot << "    sb [label=<<TABLE BORDER=\"2\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"6\" STYLE=\"rounded\">\n";
        dot << "        <TR><TD COLSPAN=\"2\" BGCOLOR=\"#004D40\"><FONT COLOR=\"white\"><B>Reporte de Superbloque</B></FONT></TD></TR>\n";
        
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_filesystem_type</B></TD><TD>" << sb.s_filesystem_type << " (EXT2/EXT3)</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_inodes_count</B></TD><TD>" << sb.s_inodes_count << "</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_blocks_count</B></TD><TD>" << sb.s_blocks_count << "</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_free_blocks_count</B></TD><TD>" << sb.s_free_blocks_count << "</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_free_inodes_count</B></TD><TD>" << sb.s_free_inodes_count << "</TD></TR>\n";
        
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_mtime</B></TD><TD>" << formatDate(sb.s_mtime) << "</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_umtime</B></TD><TD>" << formatDate(sb.s_umtime) << "</TD></TR>\n";
        
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_mnt_count</B></TD><TD>" << sb.s_mnt_count << "</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_magic</B></TD><TD>0x" << std::hex << sb.s_magic << std::dec << "</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_inode_size</B></TD><TD>" << sb.s_inode_size << " bytes</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_block_size</B></TD><TD>" << sb.s_block_size << " bytes</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_first_ino</B></TD><TD>" << sb.s_first_ino << "</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_first_blo</B></TD><TD>" << sb.s_first_blo << "</TD></TR>\n";
        
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_bm_inode_start</B></TD><TD>" << sb.s_bm_inode_start << "</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_bm_block_start</B></TD><TD>" << sb.s_bm_block_start << "</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_inode_start</B></TD><TD>" << sb.s_inode_start << "</TD></TR>\n";
        dot << "        <TR><TD BGCOLOR=\"#E0F2F1\"><B>s_block_start</B></TD><TD>" << sb.s_block_start << "</TD></TR>\n";

        dot << "    </TABLE>>];\n";
        dot << "}\n";

        createDirectories(getParentPath(path));
        std::string dotPath = path + ".dot";
        std::ofstream dotFile(dotPath);
        if (!dotFile.is_open()) return "Error: no se pudo crear el archivo .dot";
        dotFile << dot.str();
        dotFile.close();

        std::string ext = getExtension(path);
        std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\" 2>&1";
        int result = system(cmd.c_str());

        if (result != 0) {
            return "Error: no se pudo generar el reporte con Graphviz.\nArchivo DOT guardado en: " + dotPath;
        }

        remove(dotPath.c_str());
        return "Reporte SUPERBLOQUE generado exitosamente en: " + path;
    }

    inline int findInodeByPath(const std::string& targetPath, std::ifstream& diskFile, const Superblock& sb) {
        if (targetPath == "/" || targetPath.empty()) return 0;
        
        std::vector<std::string> tokens;
        std::stringstream ss(targetPath);
        std::string token;
        while (std::getline(ss, token, '/')) {
            if (!token.empty()) tokens.push_back(token);
        }

        int currentInodeId = 0;
        for (const std::string& dir : tokens) {
            Inode currentInode;
            diskFile.seekg(sb.s_inode_start + (currentInodeId * sizeof(Inode)), std::ios::beg);
            diskFile.read(reinterpret_cast<char*>(&currentInode), sizeof(Inode));

            if (currentInode.i_type != '0') return -1;

            bool found = false;
            for (int b = 0; b < 12; b++) { 
                int blockNum = currentInode.i_block[b];
                if (blockNum != -1) {
                    FolderBlock fb;
                    diskFile.seekg(sb.s_block_start + (blockNum * sizeof(FolderBlock)), std::ios::beg);
                    diskFile.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                    for (int c = 0; c < 4; c++) {
                        if (std::string(fb.b_content[c].b_name) == dir) {
                            currentInodeId = fb.b_content[c].b_inodo;
                            found = true;
                            break;
                        }
                    }
                }
                if (found) break;
            }
            if (!found) return -1; 
        }
        return currentInodeId;
    }

    inline std::string reportFILE(const std::string& path, const std::string& diskPath, int partStart, const std::string& fileFsPath) {
        if (fileFsPath.empty()) return "Error: el reporte 'file' requiere indicar la ruta del archivo interno (parámetro ruta).";

        std::ifstream file(diskPath, std::ios::binary);
        if (!file.is_open()) return "Error: no se pudo abrir el disco.";

        Superblock sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

        int inodeId = findInodeByPath(fileFsPath, file, sb);
        if (inodeId == -1) {
            file.close();
            return "Error: no se encontró el archivo '" + fileFsPath + "' en el sistema de archivos.";
        }

        Inode inode;
        file.seekg(sb.s_inode_start + (inodeId * sizeof(Inode)), std::ios::beg);
        file.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

        if (inode.i_type != '1') {
            file.close();
            return "Error: la ruta '" + fileFsPath + "' pertenece a una carpeta, no a un archivo.";
        }

        std::string fileContent = "";
        for (int b = 0; b < 12; b++) {
            int blockNum = inode.i_block[b];
            if (blockNum != -1) {
                FileBlock fb;
                file.seekg(sb.s_block_start + (blockNum * sizeof(FileBlock)), std::ios::beg);
                file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
                
                std::string blockData(fb.b_content, 64);
                blockData.erase(std::remove(blockData.begin(), blockData.end(), '\0'), blockData.end());
                fileContent += blockData;
            }
        }
        file.close();

        createDirectories(getParentPath(path));
        std::ofstream outFile(path);
        if (!outFile.is_open()) return "Error: no se pudo crear el archivo de destino en " + path;
        
        outFile << fileContent;
        outFile.close();

        return "Reporte FILE generado exitosamente en: " + path;
    }

    inline std::string reportLS(const std::string& path, const std::string& diskPath, int partStart, const std::string& targetDir) {
        if (targetDir.empty()) return "Error: el reporte 'ls' requiere el parámetro de ruta interna.";

        std::ifstream file(diskPath, std::ios::binary);
        if (!file.is_open()) return "Error: no se pudo abrir el disco.";

        Superblock sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

        // 1. Buscar el inodo de la carpeta objetivo
        int inodeId = findInodeByPath(targetDir, file, sb);
        if (inodeId == -1) {
            file.close();
            return "Error: no se encontró la ruta '" + targetDir + "'.";
        }

        // 2. Leer el inodo y verificar que sea carpeta
        Inode dirInode;
        file.seekg(sb.s_inode_start + (inodeId * sizeof(Inode)), std::ios::beg);
        file.read(reinterpret_cast<char*>(&dirInode), sizeof(Inode));

        if (dirInode.i_type != '0') {
            file.close();
            return "Error: la ruta '" + targetDir + "' pertenece a un archivo, no a una carpeta.";
        }

        // Función lambda para formatear fechas
        auto formatDate = [](time_t time_val) {
            char dateStr[100];
            struct tm* timeinfo = localtime(&time_val);
            if (timeinfo) {
                strftime(dateStr, sizeof(dateStr), "%d/%m/%Y %H:%M:%S", timeinfo);
                return std::string(dateStr);
            }
            return std::string("Sin fecha");
        };

        // 3. Empezar a dibujar la tabla
        std::ostringstream dot;
        dot << "digraph LS_Report {\n";
        dot << "    node [shape=plaintext]\n";
        dot << "    rankdir=TB;\n\n";

        dot << "    ls [label=<<TABLE BORDER=\"2\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"6\" STYLE=\"rounded\">\n";
        dot << "        <TR><TD COLSPAN=\"7\" BGCOLOR=\"#1565C0\"><FONT COLOR=\"white\"><B>Contenido de: " << targetDir << "</B></FONT></TD></TR>\n";
        dot << "        <TR BGCOLOR=\"#90CAF9\">\n";
        dot << "            <TD><B>Permisos</B></TD>\n";
        dot << "            <TD><B>Propietario</B></TD>\n";
        dot << "            <TD><B>Grupo</B></TD>\n";
        dot << "            <TD><B>Tamaño</B></TD>\n";
        dot << "            <TD><B>Fecha Modificación</B></TD>\n";
        dot << "            <TD><B>Tipo</B></TD>\n";
        dot << "            <TD><B>Nombre</B></TD>\n";
        dot << "        </TR>\n";

        // 4. Recorrer los apuntadores de la carpeta para leer su contenido
        for (int b = 0; b < 12; b++) {
            int blockNum = dirInode.i_block[b];
            if (blockNum != -1) {
                FolderBlock fb;
                file.seekg(sb.s_block_start + (blockNum * sizeof(FolderBlock)), std::ios::beg);
                file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                // Revisar los 4 espacios de cada bloque carpeta
                for (int c = 0; c < 4; c++) {
                    int pointedInode = fb.b_content[c].b_inodo;
                    if (pointedInode != -1) {
                        std::string name(fb.b_content[c].b_name);
                        if (name.empty()) continue;

                        // Leer el inodo hijo para sacar su información
                        Inode itemInode;
                        file.seekg(sb.s_inode_start + (pointedInode * sizeof(Inode)), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&itemInode), sizeof(Inode));

                        std::string tipoStr = (itemInode.i_type == '0') ? "Carpeta" : "Archivo";
                        std::string color = (itemInode.i_type == '0') ? "#E3F2FD" : "#E8F5E9"; // Azul claro para carpetas, verde claro para archivos

                        dot << "        <TR BGCOLOR=\"" << color << "\">\n";
                        dot << "            <TD>" << itemInode.i_perm << "</TD>\n";
                        dot << "            <TD>" << itemInode.i_uid << "</TD>\n";
                        dot << "            <TD>" << itemInode.i_gid << "</TD>\n";
                        dot << "            <TD>" << itemInode.i_size << " bytes</TD>\n";
                        dot << "            <TD>" << formatDate(itemInode.i_mtime) << "</TD>\n";
                        dot << "            <TD>" << tipoStr << "</TD>\n";
                        dot << "            <TD>" << escapeHtml(name) << "</TD>\n";
                        dot << "        </TR>\n";
                    }
                }
            }
        }
        
        dot << "    </TABLE>>];\n";
        dot << "}\n";
        file.close();

        createDirectories(getParentPath(path));
        std::string dotPath = path + ".dot";
        std::ofstream dotFile(dotPath);
        if (!dotFile.is_open()) return "Error: no se pudo crear el archivo .dot";
        dotFile << dot.str();
        dotFile.close();

        std::string ext = getExtension(path);
        std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\" 2>&1";
        int result = system(cmd.c_str());

        if (result != 0) {
            return "Error: no se pudo generar el reporte con Graphviz.\nArchivo DOT guardado en: " + dotPath;
        }

        remove(dotPath.c_str());
        return "Reporte LS generado exitosamente en: " + path;
    }
    
    inline std::string execute(const std::string& name, const std::string& path, 
                               const std::string& id, const std::string& pathFileLs) {
        if (name.empty()) return "Error: rep requiere el parámetro -name";
        if (path.empty()) return "Error: rep requiere el parámetro -path";
        if (id.empty()) return "Error: rep requiere el parámetro -id";
        
        std::string reportType = toLowerCase(name);
        
        if (reportType != "mbr" && reportType != "disk" && reportType != "inode" 
            && reportType != "bm_inode" && reportType != "bm_block"
            && reportType != "block" && reportType != "tree" && reportType != "sb"
            && reportType != "file" && reportType != "ls") {
            return "Error: tipo de reporte no válido o aún no implementado.";
        }
        
        MountedPartition partition;
        if (!CommandMount::getMountedPartition(id, partition)) {
            return "Error: la partición con ID '" + id + "' no está montada";
        }
        
        std::ostringstream result;
        result << "Generando reporte '" << reportType << "'...\n";
        
        if (reportType == "mbr") {
            result << reportMBR(path, partition.path);
        } else if (reportType == "disk") {
            result << reportDISK(path, partition.path);
        } else if (reportType == "inode") {
            result << reportINODE(path, partition.path, partition.start, pathFileLs);
        } else if (reportType == "bm_inode") {
            result << reportBM_INODE(path, partition.path, partition.start);
        } else if (reportType == "bm_block") {
            result << reportBM_BLOCK(path, partition.path, partition.start);
        } else if (reportType == "block") {
            result << reportBLOCK(path, partition.path, partition.start);
        } else if (reportType == "tree") {
            result << reportTREE(path, partition.path, partition.start);
        } else if (reportType == "sb") {
            result << reportSB(path, partition.path, partition.start);
        } else if (reportType == "file") { 
            result << reportFILE(path, partition.path, partition.start, pathFileLs);
        } else if (reportType == "ls") {
            result << reportLS(path, partition.path, partition.start, pathFileLs);
        }
        
        return result.str();
    }
    
}

#endif // REP_H