#ifndef LOGOUT_H
#define LOGOUT_H

#include <string>
#include "login.h"

namespace CommandLogout {
    inline std::string execute() {
        if (!CommandLogin::currentSession.is_logged_in) {
            return "Error: No hay ningún usuario logueado actualmente.";
        }

        std::string username = CommandLogin::currentSession.username;
        CommandLogin::currentSession = CommandLogin::ActiveSession(); // Reiniciar sesión

        return "Logout exitoso. Usuario '" + username + "' ha cerrado sesión.";
    }
}

#endif // LOGOUT_H