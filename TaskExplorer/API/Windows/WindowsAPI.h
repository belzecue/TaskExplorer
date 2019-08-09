#pragma once
#include "../SystemAPI.h"
#include "WinProcess.h"
#include "WinSocket.h"
#include "Monitors/EventMonitor.h"
#include "SymbolProvider.h"
#include "WinService.h"
#include "WinDriver.h"

class CWindowsAPI : public CSystemAPI
{
	Q_OBJECT

public:
	CWindowsAPI(QObject *parent = nullptr);
	virtual ~CWindowsAPI();

	virtual bool Init();

	virtual bool RootAvaiable();

	virtual bool UpdateSysStats();

	virtual bool UpdateProcessList();

	virtual bool UpdateSocketList();

	virtual CSocketPtr FindSocket(quint64 ProcessId, ulong ProtocolType, const QHostAddress& LocalAddress, quint16 LocalPort, const QHostAddress& RemoteAddress, quint16 RemotePort, bool bStrict);

	virtual bool UpdateOpenFileList();

	virtual bool UpdateServiceList(bool bRefresh = false);

	virtual bool UpdateDriverList();

	virtual CSymbolProviderPtr GetSymbolProvider()		{ return m_pSymbolProvider; }

	virtual quint64	GetCpuIdleCycleTime(int index);

	virtual quint32 GetTotalGuiObjects() const			{ QReadLocker Locker(&m_StatsMutex); return m_TotalGuiObjects; }
	virtual quint32 GetTotalUserObjects() const			{ QReadLocker Locker(&m_StatsMutex); return m_TotalUserObjects; }
	virtual quint32 GetTotalWndObjects() const			{ QReadLocker Locker(&m_StatsMutex); return m_TotalWndObjects; }

	struct SWndInfo
	{
		quint64 hwnd;
		quint64 parent;
		quint64 processId;
        quint64 threadId;
	};

	virtual QMultiMap<quint64, SWndInfo> GetWindowByPID(quint64 ProcessId) const { QReadLocker Locker(&m_WindowMutex); return m_WindowMap[ProcessId]; }

	virtual QList<QString> GetServicesByPID(quint64 ProcessId) const			 { QReadLocker Locker(&m_ServiceMutex); return m_ServiceByPID.values(ProcessId); }

	virtual quint64 GetUpTime() const;

	bool		IsMonitoringETW() const					{ return m_pEventMonitor != NULL; }

	virtual bool IsTestSigning() const					{ QReadLocker Locker(&m_Mutex); return m_bTestSigning; }
	virtual bool HasDriverFailed() const				{ QReadLocker Locker(&m_Mutex); return m_bDriverFailed; }
	virtual QString GetDriverFileName() const			{ QReadLocker Locker(&m_Mutex); return m_DriverFileName; }


public slots:
	void		MonitorETW(bool bEnable);
	void		OnNetworkEvent(int Type, quint64 ProcessId, quint64 ThreadId, ulong ProtocolType, ulong TransferSize,
								const QHostAddress& LocalAddress, quint16 LocalPort, const QHostAddress& RemoteAddress, quint16 RemotePort);
	void		OnFileEvent(int Type, quint64 FileId, quint64 ProcessId, quint64 ThreadId, const QString& FileName);
	void		OnDiskEvent(int Type, quint64 FileId, quint64 ProcessId, quint64 ThreadId, ulong IrpFlags, ulong TransferSize, quint64 HighResResponseTime);

protected:
	virtual void OnHardwareChanged();

	quint32		EnumWindows();

	void		AddNetworkIO(int Type, ulong TransferSize);
	void		AddDiskIO(int Type, ulong TransferSize);

	bool		InitWindowsInfo();

	CEventMonitor*			m_pEventMonitor;


	mutable QReadWriteLock	m_FileNameMutex;
	QMap<quint64, QString>	m_FileNames;


	// Guard it with		m_OpenFilesMutex
	//QMultiMap<quint64, CHandleRef> m_HandleByObject;

	// Guard it with m_ServiceMutex
	QMultiMap<quint64, QString>	m_ServiceByPID;

	CSymbolProviderPtr		m_pSymbolProvider;

	// Guard it with		m_StatsMutex
	quint32					m_TotalGuiObjects;
	quint32					m_TotalUserObjects;
	quint32					m_TotalWndObjects;

	mutable QReadWriteLock	m_WindowMutex;
	QMap<quint64, QMultiMap<quint64, SWndInfo> > m_WindowMap;

private:
	void UpdatePerfStats();
	void UpdateNetStats();
	quint64 UpdateCpuStats(bool SetCpuUsage);
	quint64 UpdateCpuCycleStats();
	bool InitCpuCount();

	QString GetFileNameByID(quint64 FileId) const;

	bool m_bTestSigning;
	bool m_bDriverFailed;
	QString m_DriverFileName;

	struct SWindowsAPI* m;
};

extern ulong g_fileObjectTypeIndex;
