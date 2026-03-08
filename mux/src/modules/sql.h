/*! \file sql.h
 * \brief Definitions for SQLProxy and SQLSlave Modules
 *
 */

#ifndef SQL_H
#define SQL_H

class CQueryServerFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CQueryServerFactory(void);
    virtual ~CQueryServerFactory();

private:
    uint32_t m_cRef;
};

#endif // SQL_H
