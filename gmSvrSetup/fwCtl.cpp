#include "fwCtl.h"

static BOOL _fwIsAppEnabled(INetFwProfile *fwProfile, const WCHAR *szFileName)
{
    HRESULT hr;
    BSTR bstrFWProcessImageFileName = NULL;
    VARIANT_BOOL bFWEnabled;
    INetFwAuthorizedApplication* pFWApp = NULL;
    INetFwAuthorizedApplications* pFWApps = NULL;
    BOOL bEnabled = FALSE;

    hr = fwProfile->get_AuthorizedApplications(&pFWApps);
    if (FAILED(hr)) return FALSE;

    bstrFWProcessImageFileName = SysAllocString(szFileName);
    if (SysStringLen(bstrFWProcessImageFileName) == 0)
    {
        pFWApps->Release();
        return FALSE;
    }

    hr = pFWApps->Item(bstrFWProcessImageFileName, &pFWApp);
    if (SUCCEEDED(hr))
    {
        hr = pFWApp->get_Enabled(&bFWEnabled);

        if (FAILED(hr))
        {
            SysFreeString(bstrFWProcessImageFileName);
            pFWApp->Release();
            pFWApps->Release();
            return FALSE;
        }

        if (bFWEnabled == VARIANT_TRUE) bEnabled = TRUE;

        pFWApp->Release();
    }

    SysFreeString(bstrFWProcessImageFileName);

    pFWApps->Release();

    return bEnabled;
}

static BOOL _fwAddExe(INetFwProfile *fwProfile, const WCHAR *szFileName, const WCHAR *szRegisterName)
{
    HRESULT hr;
    BSTR bstrProcessImageFileName = NULL;
    BSTR bstrRegisterName = NULL;
    INetFwAuthorizedApplication* pFWApp = NULL;
    INetFwAuthorizedApplications* pFWApps = NULL;

    if (_fwIsAppEnabled(fwProfile, szFileName)) return TRUE;

    hr = fwProfile->get_AuthorizedApplications(&pFWApps);
    if (FAILED(hr)) return FALSE;

    hr = CoCreateInstance(__uuidof(NetFwAuthorizedApplication), NULL, CLSCTX_INPROC_SERVER, __uuidof(INetFwAuthorizedApplication), (void**)&pFWApp);
    if (FAILED(hr))
    {
        pFWApps->Release();
        return FALSE;
    }

    bstrProcessImageFileName = SysAllocString(szFileName);
    if (SysStringLen(bstrProcessImageFileName) == 0)
    {
        pFWApp->Release();
        pFWApps->Release();
        return FALSE;
    }

    hr = pFWApp->put_ProcessImageFileName(bstrProcessImageFileName);
    if (FAILED(hr))
    {
        SysFreeString(bstrProcessImageFileName);
        pFWApp->Release();
        pFWApps->Release();
        return FALSE;
    }

    bstrRegisterName = SysAllocString(szRegisterName);
    if (SysStringLen(bstrRegisterName) == 0)
    {
        SysFreeString(bstrProcessImageFileName);
        pFWApp->Release();
        pFWApps->Release();
        return FALSE;
    }
    hr = pFWApp->put_Name(bstrRegisterName);
    if (FAILED(hr))
    {
        SysFreeString(bstrProcessImageFileName);
        SysFreeString(bstrRegisterName);
        pFWApp->Release();
        pFWApps->Release();
        return FALSE;
    }

    hr = pFWApps->Add(pFWApp);
    if (FAILED(hr))
    {
        SysFreeString(bstrProcessImageFileName);
        SysFreeString(bstrRegisterName);
        pFWApp->Release();
        pFWApps->Release();
        return FALSE;
    }

    SysFreeString(bstrProcessImageFileName);
    SysFreeString(bstrRegisterName);

    pFWApp->Release();
    pFWApps->Release();

    return TRUE;
}

void _fwRemoveApp(const WCHAR *szFileName)
{
    INetFwMgr *fwMgr = NULL;
    INetFwPolicy *fwPolicy = NULL;
    INetFwProfile *fwProfile = NULL;
    HRESULT hr;
    BSTR bstrProcessImageFileName = NULL;
    INetFwAuthorizedApplications* pFWApps = NULL;

    hr = CoCreateInstance(__uuidof(NetFwMgr), NULL, CLSCTX_INPROC_SERVER, __uuidof(INetFwMgr), (void**)&fwMgr);
    if (FAILED(hr)) return;

    hr = fwMgr->get_LocalPolicy(&fwPolicy);
    if (FAILED(hr))
    {
        fwMgr->Release();
        return;
    }

    hr = fwPolicy->get_CurrentProfile(&fwProfile);
    if (FAILED(hr))
    {
        fwPolicy->Release();
        fwMgr->Release();
        return;
    }

    hr = fwProfile->get_AuthorizedApplications(&pFWApps);
    if (FAILED(hr))
    {
        fwProfile->Release();
        fwPolicy->Release();
        fwMgr->Release();
        return;
    }

    bstrProcessImageFileName = SysAllocString(szFileName);
    if (SysStringLen(bstrProcessImageFileName) == 0)
    {
        pFWApps->Release();
        fwProfile->Release();
        fwPolicy->Release();
        fwMgr->Release();
        return;
    }
    pFWApps->Remove(bstrProcessImageFileName);

    SysFreeString(bstrProcessImageFileName);
    pFWApps->Release();

    fwProfile->Release();
    fwPolicy->Release();
    fwMgr->Release();
}

BOOL _fwAddApp(const WCHAR *szFileName, const WCHAR *szRegisterName)
{
    INetFwProfile *fwProfile = NULL;
    HRESULT hr = S_FALSE;
    INetFwMgr *fwMgr = NULL;
    INetFwPolicy *fwPolicy = NULL;
    BOOL bResult;

    hr = CoCreateInstance(__uuidof(NetFwMgr), NULL, CLSCTX_INPROC_SERVER, __uuidof(INetFwMgr), (void**)&fwMgr);
    if (FAILED(hr)) return FALSE;

    hr = fwMgr->get_LocalPolicy(&fwPolicy);
    if (FAILED(hr)) { fwMgr->Release(); return FALSE; }

    hr = fwPolicy->get_CurrentProfile(&fwProfile);
    if (FAILED(hr)) { fwPolicy->Release(); fwMgr->Release(); return FALSE; }

    bResult = _fwAddExe(fwProfile, szFileName, szRegisterName);

    fwProfile->Release();
    fwPolicy->Release();
    fwMgr->Release();

    return bResult;
}

extern "C"
BOOL fwAddApp(const WCHAR *szFileName, const WCHAR *szRegisterName)
{
    return _fwAddApp(szFileName, szRegisterName);
}

extern "C"
void fwRemoveApp(const WCHAR *szFileName)
{
    _fwRemoveApp(szFileName);
}

