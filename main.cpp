#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cctype>
#include "structures.h"
#include "mkdisk.h"
#include "rmdisk.h"
#include "fdisk.h"
#include "mount.h"
#include "mkfs.h"
#include "login.h"
#include "logout.h"
#include "cat.h"
#include "mkgrp.h"
#include "mkusr.h"
#include "rmgrp.h"
#include "rmusr.h"
#include "chgrp.h"
#include "mkfile.h"


// Función para convertir string a minúsculas
std::string toLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Función para remover comillas de una cadena
std::string removeQuotes(const std::string& str) {
    if (str.length() >= 2 && 
        ((str.front() == '"' && str.back() == '"') || 
         (str.front() == '\'' && str.back() == '\''))) {
        return str.substr(1, str.length() - 2);
    }
    return str;
}

// Función para parsear parámetros con soporte para comillas y cualquier orden
std::string parseParameter(const std::string& commandLine, const std::string& paramName) {
    // Buscar el parámetro
    std::string lowerCommandLine = toLowerCase(commandLine);
    std::string lowerParamName = toLowerCase(paramName);
    
    size_t pos = lowerCommandLine.find(lowerParamName + "=");
    if (pos == std::string::npos) {
        return "";
    }
    
    // Encontrar el inicio del valor
    size_t valueStart = pos + paramName.length() + 1;
    if (valueStart >= commandLine.length()) {
        return "";
    }
    
    // Determinar el final del valor
    size_t valueEnd = commandLine.length();
    
    if (commandLine[valueStart] == '"' || commandLine[valueStart] == '\'') {
        char quote = commandLine[valueStart];
        size_t quoteEnd = commandLine.find(quote, valueStart + 1);
        if (quoteEnd != std::string::npos) {
            return commandLine.substr(valueStart + 1, quoteEnd - valueStart - 1);
        } else {
 
            size_t spacePos = commandLine.find(' ', valueStart + 1);
            if (spacePos != std::string::npos) {
                valueEnd = spacePos;
            }
            return commandLine.substr(valueStart + 1, valueEnd - valueStart - 1);
        }
    } else {
        // Si no hay comillas, buscar el siguiente espacio o final de línea
        size_t spacePos = commandLine.find(' ', valueStart);
        if (spacePos != std::string::npos) {
            valueEnd = spacePos;
        }
        return commandLine.substr(valueStart, valueEnd - valueStart);
    }
}

// Función para parsear y ejecutar comandos
std::string executeCommand(const std::string& commandLine) {
    std::istringstream iss(commandLine);
    std::string cmd;
    iss >> cmd;
    
    // Convertir comando a minúsculas para comparación case-insensitive
    cmd = toLowerCase(cmd);

    if (cmd == "mkdisk") {
        // Parsear parámetros
        std::string sizeStr = parseParameter(commandLine, "-size");
        std::string unit = parseParameter(commandLine, "-unit");
        std::string path = parseParameter(commandLine, "-path");
        
        // Validar parámetros obligatorios
        if (sizeStr.empty() || path.empty()) {
            return "Error: mkdisk requiere parámetros -size y -path\n"
                   "Uso: mkdisk -size=N -unit=[k|m] -path=ruta\n"
                   "Los parámetros pueden estar en cualquier orden";
        }
        
        // Convertir size a entero
        int size;
        try {
            size = std::stoi(sizeStr);
        } catch (const std::exception& e) {
            return "Error: el valor de size debe ser un número entero positivo";
        }
        
        if (size <= 0) {
            return "Error: el tamaño debe ser un número positivo";
        }
        
        // Unit por defecto es megabytes si no se especifica
        if (unit.empty()) {
            unit = "m";
        } else {
            unit = toLowerCase(unit);
        }
        
        // Validar unidad
        if (unit != "k" && unit != "m") {
            return "Error: unit debe ser 'k' (kilobytes) o 'm' (megabytes)";
        }

        return CommandMkdisk::execute(size, unit, path);

    } else if (cmd == "rmdisk") {
        std::string path = parseParameter(commandLine, "-path");

        if (path.empty()) {
            return "Error: rmdisk requiere parámetro -path\n"
                   "Uso: rmdisk -path=ruta";
        }

        return CommandRmdisk::execute(path);

    } else if (cmd == "fdisk") {
        std::string path = parseParameter(commandLine, "-path");
        std::string name = parseParameter(commandLine, "-name");
        std::string deleteName = parseParameter(commandLine, "-delete");
        
        // Validar parámetros obligatorios
        if (path.empty()) {
            return "Error: fdisk requiere parámetro -path\n"
                   "Uso: fdisk -size=N -unit=[k|m] -path=ruta -type=[P|E|L] -fit=[BF|FF|WF] -name=nombre\n"
                   "      fdisk -delete=nombre -path=ruta";
        }

        // Si es operación de eliminación
        if (!deleteName.empty()) {
            return CommandFdisk::execute(0, "", path, "", "", deleteName, "");
        }

        // Si es operación de adición
        if (name.empty()) {
            return "Error: fdisk requiere parámetro -name o -delete\n"
                   "Uso: fdisk -size=N -unit=[k|m] -path=ruta -type=[P|E|L] -fit=[BF|FF|WF] -name=nombre\n"
                   "      fdisk -delete=nombre -path=ruta";
        }

        std::string sizeStr = parseParameter(commandLine, "-size");
        if (sizeStr.empty()) {
            return "Error: fdisk requiere parámetro -size para crear particiones\n"
                   "Uso: fdisk -size=N -unit=[k|m] -path=ruta -type=[P|E|L] -fit=[BF|FF|WF] -name=nombre";
        }

        int size;
        try {
            size = std::stoi(sizeStr);
        } catch (const std::exception& e) {
            return "Error: el valor de size debe ser un número entero positivo";
        }

        if (size <= 0) {
            return "Error: el tamaño debe ser un número positivo";
        }

        std::string unit = parseParameter(commandLine, "-unit");
        if (unit.empty()) {
            unit = "k";  // Default: kilobytes
        } else {
            unit = toLowerCase(unit);
        }

        if (unit != "k" && unit != "m") {
            return "Error: unit debe ser 'k' (kilobytes) o 'm' (megabytes)";
        }

        std::string type = parseParameter(commandLine, "-type");
        if (type.empty()) {
            type = "P";  // Default: primaria
        } else {
            type = toLowerCase(type);
        }

        std::string fit = parseParameter(commandLine, "-fit");
        if (fit.empty()) {
            fit = "WF";  // Default: Worst Fit
        } else {
            fit = toLowerCase(fit);
        }

        return CommandFdisk::execute(size, unit, path, type, fit, "", name);

    } else if (cmd == "mount") {
        std::string path = parseParameter(commandLine, "-path");
        std::string name = parseParameter(commandLine, "-name");
        
        if (path.empty() || name.empty()) {
            return "Error: mount requiere parámetros -path y -name\n"
                   "Uso: mount -path=ruta -name=nombre";
        }
        
        return CommandMount::execute(path, name);

    } else if (cmd == "mounted") {
        // Mostrar todas las particiones montadas
        return CommandMount::listMountedPartitions();

    } else if(cmd == "mkfs") {
        std::string id = parseParameter(commandLine, "-id");
        std::string type = parseParameter(commandLine, "-type");

        if (id.empty()) {
            return "Error: mfks requiere el parámetro -id\n"
                   "Uso: mfks -id=ID -type=[full]";
        }

        if (!type.empty() && type != "full") {
            return "Error: El parámetro -type debe ser 'full'";
        }

        return CommandMkfs::execute(id, type);

    } else if (cmd == "cat") {
        std::vector<std::string> files;
        int i = 1;

        while (true) {
            std::string paramName = "-file" + std::to_string(i);
            std::string filepath = parseParameter(commandLine, paramName);
            
            if (filepath.empty()) {
                break; 
            }
            
            files.push_back(filepath);
            i++;
        }

        if (files.empty()) {
            return "Error: cat requiere al menos un parámetro -file1\n"
                "Uso: cat -file1=/ruta/del/archivo [-file2=/ruta2 ...]";
        }

        return CommandCat::execute(files);
    } else if (cmd == "login") {
        std::string user = parseParameter(commandLine, "-user");
        std::string pass = parseParameter(commandLine, "-pass");
        std::string id = parseParameter(commandLine, "-id");

        if (user.empty() || pass.empty() || id.empty()) {
            return "Error: login requiere los parámetros -user, -pass e -id\n"
                   "Uso: login -user=usuario -pass=contraseña -id=ID";
        }

        return CommandLogin::execute(user, pass, id);

    } else if (cmd == "logout") {
        return CommandLogout::execute();

    } else if (cmd == "mkgrp") {
        std::string name = parseParameter(commandLine, "-name");

        if (name.empty()) {
            return "Error: mkgrp requiere el parámetro -name\n"
                   "Uso: mkgrp -name=nombre_del_grupo";
        }

        return CommandMkgrp::execute(name);
    } else if (cmd == "mkusr"){
        std::string user = parseParameter(commandLine, "-user");
        std::string pass = parseParameter(commandLine, "-pass");
        std::string group = parseParameter(commandLine, "-group");

        if (user.empty() || pass.empty() || group.empty()) {
            return "Error: mkusr requiere los parámetros -user, -pass y -group\n"
                   "Uso: mkusr -user=usuario -pass=contraseña -group=grupo";
        }

        return CommandMkusr::execute(user, pass, group);
    } else if (cmd == "rmgrp") {
        std::string name = parseParameter(commandLine, "-name");

        if (name.empty()) {
            return "Error: rmgrp requiere el parámetro -name\n"
                   "Uso: rmgrp -name=nombre_del_grupo";
        }

        return CommandRmgrp::execute(name);
    } else if (cmd == "rmusr") {
        std::string user = parseParameter(commandLine, "-user");

        if (user.empty()) {
            return "Error: rmusr requiere el parámetro -user\n"
                   "Uso: rmusr -user=nombre_del_usuario";
        }

        return CommandRmusr::execute(user);
    } else if (cmd == "chgrp") {
        std::string user = parseParameter(commandLine, "-user");
        std::string group = parseParameter(commandLine, "-group");

        if (user.empty() || group.empty()) {
            return "Error: chgrp requiere los parámetros -user y -group\n"
                   "Uso: chgrp -user=usuario -group=grupo";
        }

        return CommandChgrp::execute(user, group);
    } else if (cmd == "mkfile") {
        std::string path = parseParameter(commandLine, "-path");
        int size = std::stoi(parseParameter(commandLine, "-size"));
        std::string content = parseParameter(commandLine, "-cont");

        if (path.empty()) {
            return "Error: mkfile requiere el parámetro -path\n"
                   "Uso: mkfile -path=/ruta/del/archivo [-size=tamaño] [-cont=contenido]";
        }

        return CommandMkfile::execute(path, size, content);
    } else if (cmd == "exit" || cmd == "quit") {
        return "EXIT";
    } else if (cmd.empty()) {
        return "";
    } else {
        return "Error: Comando no reconocido";
    }
}

int main(int argc, char* argv[]) {
    // Inicializar semilla para números aleatorios
    srand(time(nullptr));

    std::cout << "C++ DISK\n";
    std::cout << "MIA Proyecto 1 - 2026\n";
    std::cout << "Escriba 'exit' para salir\n";

    std::string commandLine;
    
    while (true) {
        std::cout << "> ";
        std::cout.flush(); 
        
        if (!std::getline(std::cin, commandLine)) {

            std::cout << "\nSaliendo del programa...\n";
            break;
        }

        if (commandLine.empty()) {
            continue;
        }

        // Ejecutar comando
        std::string result = executeCommand(commandLine);

        if (result == "EXIT") {
            std::cout << "Saliendo del programa...\n";
            break;
        }

        // Mostrar resultado
        if (!result.empty()) {
            std::cout << result << "\n\n";
        }
    }

    return 0;
}