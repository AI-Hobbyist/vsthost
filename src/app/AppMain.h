// AppMain.h : 应用类
/******************************************************************************/
#pragma once

#include <afxwin.h>

class CSingleHost;

class CVsthostApp : public CWinApp
{
public:
    CVsthostApp();
    virtual ~CVsthostApp();

    virtual BOOL InitInstance();
    virtual int  ExitInstance();

    // 单插件宿主核心
    CSingleHost *GetHost() { return m_pHost; }

protected:
    CSingleHost *m_pHost;
};

// 全局唯一应用对象
extern CVsthostApp theApp;
