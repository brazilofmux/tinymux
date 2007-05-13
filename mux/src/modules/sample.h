/*! \file sample.h
 * \brief Sample Module
 *
 * $Id$
 *
 */

#ifndef SAMPLE_H
#define SAMPLE_H

#ifdef WIN32
const UINT64 CID_Sample        = 0x0000000265E759EFi64;
const UINT64 IID_ISample       = 0x00000002462F47F3i64;
#else
const UINT64 CID_Sample        = 0x0000000265E759EFull;
const UINT64 IID_ISample       = 0x00000002462F47F3ull;
#endif

interface ISample : public mux_IUnknown
{
public:
    virtual int Add(int a, int b) = 0;
};

class CSample : public ISample
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(UINT64 iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // ISample
    //
    virtual int Add(int a, int b);

    CSample(void);
    virtual ~CSample();

private:
    UINT32 m_cRef;
};

class CSampleFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(UINT64 iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, UINT64 iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CSampleFactory(void);
    virtual ~CSampleFactory();

private:
    UINT32 m_cRef;
};

#endif // SAMPLE_H
