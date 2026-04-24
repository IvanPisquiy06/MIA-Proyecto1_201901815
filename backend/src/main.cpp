#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cctype>
#include "../core/structures.h"
#include "../commands/mkdisk.h"
#include "../commands/rmdisk.h"
#include "../commands/fdisk.h"
#include "../commands/mount.h"
#include "../commands/mkfs.h"
#include "../commands/login.h"
#include "../commands/logout.h"
#include "../commands/cat.h"
#include "../commands/mkgrp.h"
#include "../commands/mkusr.h"
#include "../commands/rmgrp.h"
#include "../commands/rmusr.h"
#include "../commands/chgrp.h"
#include "../commands/mkfile.h"
#include "../commands/mkdir.h"
#include "../commands/rep.h"
#include "../commands/unmount.h"
#include "../commands/remove.h"
#include "../commands/rename.h"
#include "../commands/copy.h"
#include "../commands/move.h"
#include "../commands/chown.h"
#include "../commands/chmod.h"
#include "../../libs/httplib.h"
#include "../../libs/json.hpp"
#include "../commands/find.h"


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
        std::string addParam = parseParameter(commandLine, "-add");
        
        // Parámetros de creación
        std::string sizeStr = parseParameter(commandLine, "-size");
        std::string unit = parseParameter(commandLine, "-unit");
        std::string type = parseParameter(commandLine, "-type");
        std::string fit = parseParameter(commandLine, "-fit");
        
        if (path.empty()) return "Error: fdisk requiere parámetro -path";

        int size = 0;
        int add = 0;
        
        if (!sizeStr.empty()) {
            try { size = std::stoi(sizeStr); } catch (...) { return "Error: -size debe ser número."; }
        }
        
        if (!addParam.empty()) {
            try { add = std::stoi(addParam); } catch (...) { return "Error: -add debe ser número."; }
        }

        if (unit.empty()) unit = "K"; // Default: kilobytes

        return CommandFdisk::execute(size, unit, path, type, fit, deleteName, name, add);
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
        std::string fs = parseParameter(commandLine, "-fs");

        if (id.empty()) {
            return "Error: mfks requiere el parámetro -id\n"
                   "Uso: mfks -id=ID -type=[full] -fs=[2fs|3fs] (opcional)";
        }

        if (!type.empty() && type != "full") {
            return "Error: El parámetro -type debe ser 'full'";
        }

        if (fs.empty()) {
            fs = "2fs";
        }

        if (fs != "2fs" && fs != "3fs") {
            return "Error: El parámetro -fs debe ser '2fs' o '3fs'";
        }

        return CommandMkfs::execute(id, type, fs);

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
        std::string group = parseParameter(commandLine, "-grp");

        if (user.empty() || pass.empty() || group.empty()) {
            return "Error: mkusr requiere los parámetros -user, -pass y -grp\n"
                   "Uso: mkusr -user=usuario -pass=contraseña -grp=grupo";
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
        std::string group = parseParameter(commandLine, "-grp");

        if (user.empty() || group.empty()) {
            return "Error: chgrp requiere los parámetros -user y -group\n"
                   "Uso: chgrp -user=usuario -group=grupo";
        }

        return CommandChgrp::execute(user, group);
    } else if (cmd == "mkfile") {
        std::string path = parseParameter(commandLine, "-path");
        std::string sizeStr = parseParameter(commandLine, "-size");
        std::string rStr = parseParameter(commandLine, "-r"); 
        std::string cont = parseParameter(commandLine, "-cont");
        
        int size = sizeStr.empty() ? 0 : std::stoi(sizeStr);
        bool isRecursive = !rStr.empty() || commandLine.find("-r") != std::string::npos; 
        
        return CommandMkfile::execute(path, isRecursive, size, cont);
    } else if (cmd == "mkdir") {
        std::string path = parseParameter(commandLine, "-path");
        
        if (commandLine.find("-p=") != std::string::npos) {
            return "Error: El parámetro -p no debe recibir ningún valor.";
        }

        bool hasP = commandLine.find("-p") != std::string::npos; 
        
        return CommandMkdir::execute(path, hasP);
    } else if( cmd == "rep") {
        std::string name = parseParameter(commandLine, "-name");
        std::string path = parseParameter(commandLine, "-path");
        std::string id = parseParameter(commandLine, "-id");
        std::string pathFileLs = parseParameter(commandLine, "-filels");

        if (name.empty() || path.empty() || id.empty()) {
            return "Error: rep requiere los parámetros -name, -path e -id\n"
                   "Uso: rep -name=nombre_del_reporte -path=ruta_de_salida -id=ID";
        }

        return CommandRep::execute(name, path, id, pathFileLs);
    } else if (cmd == "unmount") {
        std::string id = parseParameter(commandLine, "-id");

        if (id.empty()) {
            return "Error: unmount requiere el parámetro -id\n"
                   "Uso: unmount -id=ID";
        }

        return CommandUnmount::execute(id);
    } else if (cmd == "remove") {
        std::string path = parseParameter(commandLine, "-path");

        if (path.empty()) {
            return "Error: remove requiere el parámetro -path\n"
                   "Uso: remove -path=ruta_del_elemento";
        }

        return CommandRemove::execute(path);
    } else if (cmd == "rename")
    {
        std::string path = parseParameter(commandLine, "-path");
        std::string name = parseParameter(commandLine, "-name");

        if (path.empty() || name.empty()) {
            return "Error: rename requiere los parámetros -path y -name\n"
                   "Uso: rename -path=ruta_del_elemento -name=nombre_nuevo";
        }

        return CommandRename::execute(path, name);
    } else if (cmd == "copy") {
        std::string path = parseParameter(commandLine, "-path");
        std::string destination = parseParameter(commandLine, "-destination");

        if (path.empty() || destination.empty()) {
            return "Error: copy requiere los parámetros -path y -destination\n"
                   "Uso: copy -path=ruta_origen -destination=ruta_destino";
        }

        return CommandCopy::execute(path, destination);
    } else if (cmd == "move") {
        std::string path = parseParameter(commandLine, "-path");
        std::string destination = parseParameter(commandLine, "-destination");

        if (path.empty() || destination.empty()) {
            return "Error: move requiere los parámetros -path y -destination\n"
                   "Uso: move -path=ruta_origen -destination=ruta_destino";
        }

        return CommandMove::execute(path, destination);
    } else if (cmd == "find") {
        std::string path = parseParameter(commandLine, "-path");
        std::string name = parseParameter(commandLine, "-name");

        if (path.empty() || name.empty()) {
            return "Error: find requiere los parámetros -path y -name\n"
                   "Uso: find -path=ruta_inicial -name=nombre_a_buscar";
        }

        return CommandFind::execute(path, name);
    } else if (cmd == "chown") {
        std::string path = parseParameter(commandLine, "-path");
        std::string user = parseParameter(commandLine, "-user");
        std::string recursiveStr = parseParameter(commandLine, "-r");
        bool recursive = !recursiveStr.empty() || commandLine.find("-r") != std::string::npos;

        if (path.empty() || user.empty()) {
            return "Error: chown requiere los parámetros -path y -user\n"
                   "Uso: chown -path=ruta_del_elemento -user=nombre_usuario [-r]";
        }

        return CommandChown::execute(path, user, recursive);
    } else if (cmd == "chmod") {
        std::string path = parseParameter(commandLine, "-path");
        std::string ugo = parseParameter(commandLine, "-ugo"); 
        
        bool recursive = (commandLine.find("-r") != std::string::npos || commandLine.find("-R") != std::string::npos);
        
        if (path.empty() || ugo.empty()) {
            return "Error: chmod requiere los parámetros -path y -ugo\n"
                   "Uso: chmod -path=/ruta/archivo -ugo=777 [-r]";
        }
        
        return CommandChmod::execute(path, ugo, recursive);
    }
    else if (cmd == "exit" || cmd == "quit") {
        return "EXIT";
    } else if (cmd.empty()) {
        return "";
    } else {
        return "Error: Comando no reconocido";
    }
}

using json = nlohmann::json;

int main() {
    httplib::Server svr;

    svr.Post("/api/execute", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");

        std::string comando = "";
        try {
            auto body_json = json::parse(req.body);
            comando = body_json["comando"];
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content("{\"error\": \"Formato JSON inválido\"}", "application/json");
            return;
        }

        std::cout << "Comando recibido desde la web: " << comando << std::endl;
        
        std::string salida = executeCommand(comando);

        json respuesta_json;
        respuesta_json["salida"] = salida;

        res.set_content(respuesta_json.dump(), "application/json");
    });

    svr.Options("/(.*)", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 200;
    });

    // Arrancar el servidor en el puerto 3000
    std::cout << "Servidor C++ iniciado en http://localhost:3000" << std::endl;
    svr.listen("0.0.0.0", 3000);

    return 0;
}