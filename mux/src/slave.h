/*! \file slave.h
 * \brief Shared definitions between the main server and the dns / ident slave.
 *
 * This enum doesn't actually appear to be used for anything.
 */

enum {
    SLAVE_IDENTQ = 'i',
    SLAVE_IPTONAME = 'h'
};
