#ifndef LOGOUT_H
#define LOGOUT_H

#include <string>
#include "login.h"
#include "../core/structures.h"

namespace CommandLogout {
    inline std::string execute() {
        if (!getSession().is_logged_in) {
            return "Error: No hay ningún usuario logueado actualmente.";
        }

        std::string username = getSession().username;
        ::getSession() = ::ActiveSession(); // Reiniciar sesión

        return "Logout exitoso. Usuario '" + username + "' ha cerrado sesión.";
    }
}

#endif // LOGOUT_H