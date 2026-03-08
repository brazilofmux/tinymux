/*! \file comsys_mod.h
 * \brief Comsys Module — Channel system as a loadable module
 *
 * This module implements the MUX channel system (@channel, addcom,
 * delcom, etc.) as a dynamically loaded module rather than compiled
 * into the netmux binary.
 */

#ifndef COMSYS_MOD_H
#define COMSYS_MOD_H

const MUX_CID CID_ComsysMod = UINT64_C(0x00000002C5A2F193);

class CComsysMod : public mux_IComsysControl, mux_IServerEventsSink
{
private:
    mux_ILog                  *m_pILog;
    mux_IServerEventsControl  *m_pIServerEventsControl;
    mux_INotify               *m_pINotify;
    mux_IObjectInfo           *m_pIObjectInfo;
    mux_IAttributeAccess      *m_pIAttributeAccess;
    mux_IEvaluator            *m_pIEvaluator;
    mux_IPermissions          *m_pIPermissions;

public:
    // mux_IUnknown
    //
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    uint32_t   AddRef(void) override;
    uint32_t   Release(void) override;

    // mux_IComsysControl
    //
    MUX_RESULT PlayerConnect(dbref player) override;
    MUX_RESULT PlayerDisconnect(dbref player) override;
    MUX_RESULT PlayerNuke(dbref player) override;
    MUX_RESULT ProcessCommand(dbref executor, const UTF8 *pCmd,
        bool *pbHandled) override;

    // mux_IServerEventsSink
    //
    void startup(void) override;
    void presync_database(void) override;
    void presync_database_sigsegv(void) override;
    void dump_database(int dump_type) override;
    void dump_complete_signal(void) override;
    void shutdown(void) override;
    void dbck(void) override;
    void connect(dbref player, int isnew, int num) override;
    void disconnect(dbref player, int num) override;
    void data_create(dbref object) override;
    void data_clone(dbref clone, dbref source) override;
    void data_free(dbref object) override;

    CComsysMod(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CComsysMod();

private:
    uint32_t m_cRef;
};

class CComsysModFactory : public mux_IClassFactory
{
public:
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    uint32_t   AddRef(void) override;
    uint32_t   Release(void) override;

    MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv) override;
    MUX_RESULT LockServer(bool bLock) override;

    CComsysModFactory(void);
    virtual ~CComsysModFactory();

private:
    uint32_t m_cRef;
};

#endif // COMSYS_MOD_H
