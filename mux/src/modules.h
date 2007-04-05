/*! \file modules.h
 * \brief Module support
 *
 * $Id: game.cpp 1831 2007-04-04 18:50:05Z brazilofmux $
 *
 * To support loadable modules, we implement a poor man's COM. There is no
 * support for appartments, multiple threads, out of process servers, remote
 * servers, or marshalling.  There is no RPC or sockets, and most-likely,
 * no opportunitity to use any existing RPC tools for building interfaces
 * either.
 */

typedef int MUX_RESULT;

#define MUX_S_OK   (0)
#define MUX_E_FAIL (-1)

#define MUX_FAILED(x) ((MUX_RESULT)(x) < 0)
#define MUX_SUCCESS(x) (0 <= (MUX_RESULT)(x))
