/*! \file ssl.cpp
 * \brief SSL/TLS lifecycle management.
 *
 * Initialization, shutdown, and cleanup of OpenSSL contexts and connections.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#ifdef UNIX_SSL
extern SSL_CTX *ssl_ctx;
extern SSL_CTX *tls_ctx;
#endif

#ifdef UNIX_SSL
int pem_passwd_callback(char *buf, int size, int rwflag, void *userdata)
{
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
    ssl_ctx = SSL_CTX_new(TLS_server_method());
    tls_ctx = SSL_CTX_new(TLS_server_method());

    if (!SSL_CTX_use_certificate_file(ssl_ctx, (char *)mudconf.ssl_certificate_file, SSL_FILETYPE_PEM))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Unable to load SSL certificate file "));
        log_text(mudconf.ssl_certificate_file);
        ENDLOG;
        SSL_CTX_free(ssl_ctx);
        SSL_CTX_free(tls_ctx);
        ssl_ctx = nullptr;
        tls_ctx = nullptr;
        return false;
    }
    if (!SSL_CTX_use_certificate_file(tls_ctx, (char *)mudconf.ssl_certificate_file, SSL_FILETYPE_PEM))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Unable to load SSL certificate file "));
        log_text(mudconf.ssl_certificate_file);
        ENDLOG;
        SSL_CTX_free(ssl_ctx);
        SSL_CTX_free(tls_ctx);
        ssl_ctx = nullptr;
        tls_ctx = nullptr;
        return false;
    }

    SSL_CTX_set_default_passwd_cb(ssl_ctx, pem_passwd_callback);
    SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, (void *)mudconf.ssl_certificate_password);
    SSL_CTX_set_default_passwd_cb(tls_ctx, pem_passwd_callback);
    SSL_CTX_set_default_passwd_cb_userdata(tls_ctx, (void *)mudconf.ssl_certificate_password);

    if (!SSL_CTX_use_PrivateKey_file(ssl_ctx, (char *)mudconf.ssl_certificate_key, SSL_FILETYPE_PEM))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Unable to load SSL private key: "));
        log_text(mudconf.ssl_certificate_key);
        ENDLOG;
        SSL_CTX_free(ssl_ctx);
        SSL_CTX_free(tls_ctx);
        ssl_ctx = nullptr;
        tls_ctx = nullptr;
        return false;
    }

    /* Since we're reusing settings, we only need to check the key once.
     * We'll use the SSL ctx for that. */
    if (!SSL_CTX_check_private_key(ssl_ctx))
    {
        STARTLOG(LOG_ALWAYS, "NET", "SSL");
        log_text(T("initialize_ssl: Key, certificate or password does not match."));
        ENDLOG;
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
        return false;
    }


    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);
    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

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
    if (nullptr != ssl_ctx)
    {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
    }
    if (nullptr != tls_ctx)
    {
        SSL_CTX_free(tls_ctx);
        tls_ctx = nullptr;
    }
}

void CleanUpSSLConnections()
{
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); )
    {
        DESC* d = *it;
        ++it;
        if (d->ssl_session)
        {
            shutdownsock(d, R_RESTART);
        }
    }
}

#endif
