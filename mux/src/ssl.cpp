/*! \file ssl.cpp
 * \brief TLS context management for STARTTLS.
 *
 * Initialization and shutdown of the OpenSSL tls_ctx used by the telnet
 * STARTTLS negotiation path.  Implicit-SSL connections are handled by
 * GANL's SecureTransport layer with its own context.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#ifdef UNIX_SSL
extern SSL_CTX *tls_ctx;

static int pem_passwd_callback(char *buf, int size, int rwflag, void *userdata)
{
    UNUSED_PARAMETER(rwflag);
    const char *passwd = (const char *)userdata;
    int passwdLen = strlen(passwd);
    strncpy(buf, passwd, size);
    return ((passwdLen > size) ? size : passwdLen);
}

bool initialize_ssl()
{
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    OpenSSL_add_all_digests();
    tls_ctx = SSL_CTX_new(TLS_server_method());

    if (!SSL_CTX_use_certificate_file(tls_ctx, (char *)mudconf.ssl_certificate_file, SSL_FILETYPE_PEM))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Unable to load SSL certificate file "));
        log_text(mudconf.ssl_certificate_file);
        ENDLOG;
        SSL_CTX_free(tls_ctx);
        tls_ctx = nullptr;
        return false;
    }

    SSL_CTX_set_default_passwd_cb(tls_ctx, pem_passwd_callback);
    SSL_CTX_set_default_passwd_cb_userdata(tls_ctx, (void *)mudconf.ssl_certificate_password);

    if (!SSL_CTX_use_PrivateKey_file(tls_ctx, (char *)mudconf.ssl_certificate_key, SSL_FILETYPE_PEM))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Unable to load SSL private key: "));
        log_text(mudconf.ssl_certificate_key);
        ENDLOG;
        SSL_CTX_free(tls_ctx);
        tls_ctx = nullptr;
        return false;
    }

    if (!SSL_CTX_check_private_key(tls_ctx))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Key, certificate or password does not match."));
        ENDLOG;
        SSL_CTX_free(tls_ctx);
        tls_ctx = nullptr;
        return false;
    }

    SSL_CTX_set_mode(tls_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_CTX_set_mode(tls_ctx, SSL_MODE_AUTO_RETRY);
    SSL_CTX_set_mode(tls_ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    STARTLOG(LOG_ALWAYS, "NET", "SSL");
    log_text(T("initialize_ssl: SSL engine initialized successfully."));
    ENDLOG;

    return true;
}

void shutdown_ssl()
{
    if (nullptr != tls_ctx)
    {
        SSL_CTX_free(tls_ctx);
        tls_ctx = nullptr;
    }
}

#endif
