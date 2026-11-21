#pragma once

#include <cstdlib>
#include <initializer_list>
#include <unistd.h>

namespace cadence {

// The AppImage's TLS libraries may have been built with a CA path that
// doesn't exist on the host. Keep verification on and use the host bundle.
inline const char* host_ca_bundle() {
    if (const char* path = std::getenv("SSL_CERT_FILE"); path && *path) return path;
    for (const char* path : {"/etc/ssl/certs/ca-certificates.crt",
                             "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem"}) {
        if (access(path, R_OK) == 0) return path;
    }
    return nullptr;
}

} // namespace cadence
