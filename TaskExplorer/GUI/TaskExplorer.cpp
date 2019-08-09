#include "stdafx.h"
#include "TaskExplorer.h"
#ifdef WIN32
#include "../API/Windows/WindowsAPI.h"
#include "../API/Windows/ProcessHacker/RunAs.h"
#include "../API/Windows/WinAdmin.h"
#endif
#include "../Common/ExitDialog.h"
#include "../Common/HistoryGraph.h"
#include "NewService.h"
#include "RunAsDialog.h"
#include "../SVC/TaskService.h"
#include "GraphBar.h"
#include "SettingsWindow.h"
#include "CustomItemDelegate.h"
#include "Search/HandleSearch.h"
#include "Search/ModuleSearch.h"
#include "Search/MemorySearch.h"
#include "SystemInfo/SystemInfoWindow.h"
#include "../Common/CheckableMessageBox.h"

CSystemAPI*	theAPI = NULL;

QIcon g_ExeIcon;
QIcon g_DllIcon;

CSettings* theConf = NULL;
CTaskExplorer* theGUI = NULL;


#if defined(Q_OS_WIN)
#include <wtypes.h>
#include <QAbstractNativeEventFilter>
#include <dbt.h>

class CNativeEventFilter : public QAbstractNativeEventFilter
{
public:
	virtual bool nativeEventFilter(const QByteArray &eventType, void *message, long *result)
	{
		if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") 
		{
			MSG *msg = static_cast<MSG *>(message);
			//if(msg->message != 275 && msg->message != 1025)
			//	qDebug() << msg->message;
			if (msg->message == WM_NOTIFY) 
			{
				LRESULT ret;
				if (PhMwpOnNotify((NMHDR *)msg->lParam, &ret))
					*result = ret;
				return true;
			}
			else if (msg->message == WM_DEVICECHANGE) 
			{
				switch (msg->wParam)
				{
				/*case DBT_DEVICEARRIVAL: // Drive letter added
				case DBT_DEVICEREMOVECOMPLETE: // Drive letter removed
					{
						DEV_BROADCAST_HDR* deviceBroadcast = (DEV_BROADCAST_HDR*)msg->lParam;

						if (deviceBroadcast->dbch_devicetype == DBT_DEVTYP_VOLUME)
						{
							
						}
					}
					break;*/
				case DBT_DEVNODES_CHANGED: // hardware changed
					theAPI->NotifyHardwareChanged();
					break;
				}
			}
		}
		return false;
	}
};
#endif


CTaskExplorer::CTaskExplorer(QWidget *parent)
	: QMainWindow(parent)
{
	theGUI = this;

	theAPI = CSystemAPI::New();

	QMetaObject::invokeMethod(theAPI, "Init", Qt::BlockingQueuedConnection);

	QString appTitle = tr("TaskExplorer v%1").arg(GetVersion());
	if (theAPI->RootAvaiable())
#ifdef WIN32
		appTitle.append(tr(" (Administrator)"));
#else
		appTitle.append(tr(" (root)"));
#endif
	this->setWindowTitle(appTitle);

#if defined(Q_OS_WIN)
	PhMainWndHandle = (HWND)QWidget::winId();

    QApplication::instance()->installNativeEventFilter(new CNativeEventFilter);
#endif

	if (g_ExeIcon.isNull())
	{
		g_ExeIcon = QIcon(":/Icons/exe16.png");
		g_ExeIcon.addFile(":/Icons/exe32.png");
		g_ExeIcon.addFile(":/Icons/exe48.png");
		g_ExeIcon.addFile(":/Icons/exe64.png");
	}

	if (g_DllIcon.isNull())
	{
		g_DllIcon = QIcon(":/Icons/dll16.png");
		g_DllIcon.addFile(":/Icons/dll32.png");
		g_DllIcon.addFile(":/Icons/dll48.png");
		g_DllIcon.addFile(":/Icons/dll64.png");
	}

	m_bExit = false;

	// a shared item deleagate for all lists
	m_pCustomItemDelegate = new CCustomItemDelegate(GetCellHeight() + 1, this);

	m_pMainWidget = new QWidget();
	m_pMainLayout = new QVBoxLayout(m_pMainWidget);
	m_pMainLayout->setMargin(0);
	this->setCentralWidget(m_pMainWidget);

	m_pGraphSplitter = new QSplitter();
	m_pGraphSplitter->setOrientation(Qt::Vertical);
	m_pMainLayout->addWidget(m_pGraphSplitter);

	
	
	m_pGraphBar = new CGraphBar();
	//m_pMainLayout->addWidget(m_pGraphBar);
	m_pGraphSplitter->addWidget(m_pGraphBar);
	m_pGraphSplitter->setStretchFactor(0, 0);
	m_pGraphSplitter->setSizes(QList<int>() << 80); // default size of 80
	connect(m_pGraphBar, SIGNAL(Resized(int)), this, SLOT(OnGraphsResized(int)));

	m_pMainSplitter = new QSplitter();
	m_pMainSplitter->setOrientation(Qt::Horizontal);
	//m_pMainLayout->addWidget(m_pMainSplitter);
	m_pGraphSplitter->addWidget(m_pMainSplitter);
	m_pGraphSplitter->setStretchFactor(1, 1);

	m_pProcessTree = new CProcessTree(this);
	//m_pProcessTree->setMinimumSize(200, 200);
	m_pMainSplitter->addWidget(m_pProcessTree);
	m_pMainSplitter->setCollapsible(0, false);

	m_pPanelSplitter = new QSplitter();
	m_pPanelSplitter->setOrientation(Qt::Vertical);
	m_pMainSplitter->addWidget(m_pPanelSplitter);

	m_pSystemInfo = new CSystemInfoView();
	//m_pSystemInfo->setMinimumSize(200, 200);
	m_pPanelSplitter->addWidget(m_pSystemInfo);

	m_pTaskInfo = new CTaskInfoView();
	//m_pTaskInfo->setMinimumSize(200, 200);
	m_pPanelSplitter->addWidget(m_pTaskInfo);
	m_pPanelSplitter->setCollapsible(1, false);

	connect(m_pMainSplitter, SIGNAL(splitterMoved(int,int)), this, SLOT(OnSplitterMoved()));
	connect(m_pPanelSplitter, SIGNAL(splitterMoved(int,int)), this, SLOT(OnSplitterMoved()));

	connect(m_pProcessTree, SIGNAL(ProcessClicked(const CProcessPtr)), m_pTaskInfo, SLOT(ShowProcess(const CProcessPtr)));


#ifdef WIN32
	connect(qobject_cast<CWindowsAPI*>(theAPI)->GetSymbolProvider().data(), SIGNAL(StatusMessage(const QString&)), this, SLOT(OnStatusMessage(const QString&)));
#endif

	m_pMenuProcess = menuBar()->addMenu(tr("&Process"));
		m_pMenuProcess->addAction(tr("Run..."), this, SLOT(OnRun()));
		m_pMenuProcess->addAction(tr("Run as Administrator..."), this, SLOT(OnRunAdmin()));
		m_pMenuProcess->addAction(tr("Run as Limited User..."), this, SLOT(OnRunUser()));
		QAction* m_pMenuRunAs = m_pMenuProcess->addAction(tr("Run as..."), this, SLOT(OnRunAs()));
#ifdef WIN32
		QAction* m_pMenuRunSys = m_pMenuProcess->addAction(tr("Run as TrustedInstaller..."), this, SLOT(OnRunSys()));
#endif
		QAction* m_pElevate = m_pMenuProcess->addAction(tr("Restart Elevated"), this, SLOT(OnElevate()));
		m_pElevate->setIcon(QIcon(":/Icons/Shield.png"));
		m_pMenuProcess->addSeparator();
		m_pMenuProcess->addAction(tr("Exit"), this, SLOT(OnExit()));

#ifdef WIN32
		m_pElevate->setVisible(!theAPI->RootAvaiable());
#endif

	m_pMenuView = menuBar()->addMenu(tr("&View"));
		m_pMenuSysTabs = m_pMenuView->addMenu(tr("System Tabs"));
		for (int i = 0; i < m_pSystemInfo->GetTabCount(); i++)
		{
			QAction* pAction = m_pMenuSysTabs->addAction(m_pSystemInfo->GetTabLabel(i), this, SLOT(OnSysTab()));
			pAction->setCheckable(true);
			pAction->setChecked(m_pSystemInfo->IsTabVisible(i));
			m_Act2Tab[pAction] = i;
		}

/*#ifdef WIN32
		m_pMenuSysTabs->addSeparator();
		m_pMenuKernelServices = m_pMenuSysTabs->addAction(tr("Show Kernel Services"), this, SLOT(OnKernelServices()));
		m_pMenuKernelServices->setCheckable(true);
		m_pMenuKernelServices->setChecked(theConf->GetBool("MainWindow/ShowDrivers", true));
		OnKernelServices();
#endif*/

		m_pMenuTaskTabs = m_pMenuView->addMenu(tr("Task Tabs"));
		for (int i = 0; i < m_pTaskInfo->GetTabCount(); i++)
		{
			QAction* pAction = m_pMenuTaskTabs->addAction(m_pTaskInfo->GetTabLabel(i), this, SLOT(OnTaskTab()));
			pAction->setCheckable(true);
			pAction->setChecked(m_pTaskInfo->IsTabVisible(i));
			m_Act2Tab[pAction] = i;
		}

		m_pMenuView->addSeparator();
		m_pMenuSystemInfo = m_pMenuView->addAction(tr("System Info"), this, SLOT(OnSystemInfo()));
		m_pMenuView->addSeparator();
		m_pMenuPauseRefresh = m_pMenuView->addAction(tr("Pause Refresh"));
		m_pMenuPauseRefresh->setCheckable(true);
		m_pMenuRefreshNow = m_pMenuView->addAction(tr("Refresh Now"), this, SLOT(UpdateAll()));

	m_pMenuFind = menuBar()->addMenu(tr("&Find"));
		m_pMenuFindHandle = m_pMenuFind->addAction(tr("Find Handles"), this, SLOT(OnFindHandle()));
		m_pMenuFindDll = m_pMenuFind->addAction(tr("Find Module (dll)"), this, SLOT(OnFindDll()));
		m_pMenuFindMemory = m_pMenuFind->addAction(tr("Find String in Memory"), this, SLOT(OnFindMemory()));

	m_pMenuOptions = menuBar()->addMenu(tr("&Options"));
		m_pMenuSettings = m_pMenuOptions->addAction(tr("Settings"), this, SLOT(OnSettings()));
#ifdef WIN32
        m_pMenuOptions->addSeparator();
        m_pMenuAutoRun = m_pMenuOptions->addAction(tr("Auto Run"), this, SLOT(OnAutoRun()));
        m_pMenuAutoRun->setCheckable(true);
        m_pMenuAutoRun->setChecked(IsAutorunEnabled());
		m_pMenuUAC = m_pMenuOptions->addAction(tr("Skip UAC"), this, SLOT(OnSkipUAC()));
		m_pMenuUAC->setCheckable(true);
		m_pMenuUAC->setEnabled(theAPI->RootAvaiable());
		m_pMenuUAC->setChecked(SkipUacRun(true));
#endif

	m_pMenuTools = menuBar()->addMenu(tr("&Tools"));
		m_pMenuServices = m_pMenuTools->addMenu(tr("&Services"));
			m_pMenuCreateService = m_pMenuServices->addAction(tr("Create new Service"), this, SLOT(OnCreateService()));
			m_pMenuCreateService->setEnabled(theAPI->RootAvaiable());
			m_pMenuUpdateServices = m_pMenuServices->addAction(tr("ReLoad all Service"), this, SLOT(OnReloadService()));
#ifdef WIN32
			m_pMenuSCMPermissions = m_pMenuServices->addAction(tr("Service Control Manager Permissions"), this, SLOT(OnSCMPermissions()));

		m_pMenuFree = m_pMenuTools->addMenu(tr("&Free Memory"));
			m_pMenuFreeWorkingSet = m_pMenuFree->addAction(tr("Empty Working set"), this, SLOT(OnFreeMemory()));
			m_pMenuFreeModPages = m_pMenuFree->addAction(tr("Empty Modified pages"), this, SLOT(OnFreeMemory()));
			m_pMenuFreeStandby = m_pMenuFree->addAction(tr("Empty Standby list"), this, SLOT(OnFreeMemory()));
			m_pMenuFreePriority0 = m_pMenuFree->addAction(tr("Empty Priority 0 list"), this, SLOT(OnFreeMemory()));
			m_pMenuFree->addSeparator();
			m_pMenuCombinePages = m_pMenuFree->addAction(tr("Combine Pages"), this, SLOT(OnFreeMemory()));
#endif
		m_pMenuTools->addSeparator();
#ifdef WIN32
		m_pMenuETW = m_pMenuTools->addAction(tr("Monitor ETW Events"), this, SLOT(OnMonitorETW()));
		m_pMenuETW->setCheckable(true);
		m_pMenuETW->setChecked(((CWindowsAPI*)theAPI)->IsMonitoringETW());
#endif

	m_pMenuHelp = menuBar()->addMenu(tr("&Help"));
		m_pMenuSupport = m_pMenuHelp->addAction(tr("Support TaskExplorer on Patreon"), this, SLOT(OnAbout()));
		m_pMenuHelp->addSeparator();
#ifdef WIN32
		m_pMenuAboutPH = m_pMenuHelp->addAction(tr("About ProcessHacker Library"), this, SLOT(OnAbout()));
#endif
		m_pMenuAboutQt = m_pMenuHelp->addAction(tr("About the Qt Framework"), this, SLOT(OnAbout()));
		//m_pMenuHelp->addSeparator();
		m_pMenuAbout = m_pMenuHelp->addAction(QIcon(":/TaskExplorer.png"), tr("About TaskExplorer"), this, SLOT(OnAbout()));


	restoreGeometry(theConf->GetBlob("MainWindow/Window_Geometry"));
	m_pMainSplitter->restoreState(theConf->GetBlob("MainWindow/Window_Splitter"));
	m_pPanelSplitter->restoreState(theConf->GetBlob("MainWindow/Panel_Splitter"));
	m_pGraphSplitter->restoreState(theConf->GetBlob("MainWindow/Graph_Splitter"));

	OnSplitterMoved();


	bool bAutoRun = QApplication::arguments().contains("-autorun");

	QIcon Icon;
	Icon.addFile(":/TaskExplorer.png");
	m_pTrayIcon = new QSystemTrayIcon(Icon, this);
	m_pTrayIcon->setToolTip("TaskExplorer");
	connect(m_pTrayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(OnSysTray(QSystemTrayIcon::ActivationReason)));
	//m_pTrayIcon->setContextMenu(m_pNeoMenu);

	m_pTrayMenu = new QMenu();
	m_pTrayMenu->addAction(tr("Exit"), this, SLOT(OnExit()));

	m_pTrayIcon->show(); // Note: qt bug; without a first show hide does not work :/
	if(!bAutoRun && !theConf->GetBool("SysTray/Show", true))
		m_pTrayIcon->hide();

	m_pTrayGraph = NULL;

	m_pStausCPU	= new QLabel();
	statusBar()->addPermanentWidget(m_pStausCPU);
	m_pStausGPU	= new QLabel();
	statusBar()->addPermanentWidget(m_pStausGPU);
	m_pStausMEM	= new QLabel();
	statusBar()->addPermanentWidget(m_pStausMEM);
	m_pStausIO	= new QLabel();
	statusBar()->addPermanentWidget(m_pStausIO);
	m_pStausNET	= new QLabel();
	statusBar()->addPermanentWidget(m_pStausNET);


	m_pCustomItemDelegate->m_Grid = theConf->GetBool("Options/ShowGrid", true);
	m_pCustomItemDelegate->m_Color = QColor(theConf->GetString("Colors/GridColor", "#808080"));

	m_pGraphBar->UpdateLengths();

	if (!bAutoRun)
		show();

#ifdef WIN32
	if(KphIsConnected())
		statusBar()->showMessage(tr("TaskExplorer with %1 driver is ready...").arg(((CWindowsAPI*)theAPI)->GetDriverFileName()), 30000);
	else if (((CWindowsAPI*)theAPI)->HasDriverFailed() && theAPI->RootAvaiable())
	{
		QString Message = tr("Failed to load %1 driver, this could have various causes.\r\n"
			"The driver file may be missing, or is wrongfully detected as malicious by your anti-virus application and is being blocked. "
			"If this is the case you have the following options:\r\n"
			"1. Try to set an exception for the kprocesshacker.sys and/or disable your anti-virus's self defense mechanism, if it permits that (which many don't).\r\n"
			"2. Use a custom build driver xprocesshacker.sys. To do that you have two options, eider:\r\n"
			"2a. Use a driver signed with a leaked code signing certificate to do that un-pack xprocesshacker_hack-sign.zip into the application directory, using he password 'leaked'. "
			"Note however that due to the use of a leaked certificate the driver may be mistakenly flagged as a virus, so you will need to add an exemption for the file.\r\n"
			"2b. Use an driver signed with a test signing certificate, to do that un-pack xprocesshacker_test-sign.zip into the application directory, "
			"enable test signing mode using an elevated command prompt by entering 'Bcdedit.exe -set TESTSIGNING ON' and rebooting your system. "
			"Note however that enabling test signing mode will add a watermark to the left bottom corner of your desktop. There are tools to remove these though."
		).arg(((CWindowsAPI*)theAPI)->GetDriverFileName());

		bool State = false;
		CCheckableMessageBox::question(this, "TaskExplorer", Message
			, tr("Don't use the driver. WARNING: this will limit the aplications functionality!"), &State, QDialogButtonBox::Ok, QDialogButtonBox::Ok, QMessageBox::Warning);

		if (State)
			theConf->SetValue("Options/UseDriver", false);

		statusBar()->showMessage(tr("TaskExplorer failed to load driver %1").arg(((CWindowsAPI*)theAPI)->GetDriverFileName()), 180000);
	}
	else
#endif
		statusBar()->showMessage(tr("TaskExplorer is ready..."), 30000);


	//m_uTimerCounter = 0;
	m_uTimerID = startTimer(theConf->GetInt("Options/RefreshInterval", 1000));

	UpdateAll();
}

CTaskExplorer::~CTaskExplorer()
{
	killTimer(m_uTimerID);

	m_pTrayIcon->hide();

	theConf->SetBlob("MainWindow/Window_Geometry",saveGeometry());
	theConf->SetBlob("MainWindow/Window_Splitter",m_pMainSplitter->saveState());
	theConf->SetBlob("MainWindow/Panel_Splitter",m_pPanelSplitter->saveState());
	theConf->SetBlob("MainWindow/Graph_Splitter",m_pGraphSplitter->saveState());

	theAPI->deleteLater();
	theAPI = NULL;

	theGUI = NULL;
}

void CTaskExplorer::OnGraphsResized(int Size)
{
	QList<int> Sizes = m_pGraphSplitter->sizes();
	Sizes[1] += Sizes[0] - Size;
	Sizes[0] = Size;
	m_pGraphSplitter->setSizes(Sizes);
}

void CTaskExplorer::timerEvent(QTimerEvent* pEvent)
{
	if (pEvent->timerId() != m_uTimerID)
		return;

	if(!m_pMenuPauseRefresh->isChecked())
		UpdateAll();
}

void CTaskExplorer::closeEvent(QCloseEvent *e)
{
	if (m_bExit)
		return;

	if(m_pTrayIcon->isVisible() && theConf->GetBool("SysTray/CloseToTray", true))
	{
		hide();
		e->ignore();
	}
	else
	{
		CExitDialog ExitDialog(tr("Do you want to close TaskExplorer?"));
		if(ExitDialog.exec())
			return;

		e->ignore();
	}
}

void CTaskExplorer::UpdateAll()
{
	QTimer::singleShot(0, theAPI, SLOT(UpdateProcessList()));
	QTimer::singleShot(0, theAPI, SLOT(UpdateSocketList()));

	QTimer::singleShot(0, theAPI, SLOT(UpdateSysStats()));

	QTimer::singleShot(0, theAPI, SLOT(UpdateServiceList()));
	QTimer::singleShot(0, theAPI, SLOT(UpdateDriverList()));


	//if(m_pMainSplitter->sizes()[0] > 0)
		m_pGraphBar->UpdateGraphs();
	
	if(m_pMainSplitter->sizes()[1] > 0)
		m_pTaskInfo->Refresh();

	if (m_pMainSplitter->sizes()[1] > 0 && m_pPanelSplitter->sizes()[0] > 0)
	{
		m_pSystemInfo->Refresh();
		m_pSystemInfo->UpdateGraphs();
	}

	UpdateStatus();
}

void CTaskExplorer::UpdateStatus()
{
	m_pStausCPU->setText(tr("CPU: %1%    ").arg(int(100 * theAPI->GetCpuUsage())));
	m_pStausCPU->setToolTip(theAPI->GetCpuModel());

	QMap<QString, CGpuMonitor::SGpuInfo> GpuList = theAPI->GetGpuMonitor()->GetAllGpuList();

	QString GPU;
	QStringList GpuInfos;
	int i = 0;
	foreach(const CGpuMonitor::SGpuInfo &GpuInfo, GpuList)
	{
		GPU.append(tr("GPU-%1: %2%    ").arg(i).arg(int(100 * GpuInfo.TimeUsage)));
		GpuInfos.append(GpuInfo.Description);
		i++;
	}
	m_pStausGPU->setToolTip(GpuInfos.join("\r\n"));
	m_pStausGPU->setText(GPU);

	quint64 RamUsage = theAPI->GetPhysicalUsed();
	quint64 SwapedMemory = theAPI->GetSwapedOutMemory();
	quint64 CommitedMemory = theAPI->GetCommitedMemory();

	quint64 InstalledMemory = theAPI->GetInstalledMemory();
	quint64 TotalSwap = theAPI->GetTotalSwapMemory();

	quint64 TotalMemory = Max(theAPI->GetInstalledMemory(), theAPI->GetCommitedMemory()); // theAPI->GetMemoryLimit();

	if(TotalSwap > 0)
		m_pStausMEM->setText(tr("Memory: %1/%2/(%3 + %4)    ").arg(FormatSize(RamUsage)).arg(FormatSize(CommitedMemory)).arg(FormatSize(InstalledMemory)).arg(FormatSize(TotalSwap)));
	else
		m_pStausMEM->setText(tr("Memory: %1/%2/%3    ").arg(FormatSize(RamUsage)).arg(FormatSize(CommitedMemory)).arg(FormatSize(InstalledMemory)));

	QStringList MemInfo;
	MemInfo.append(tr("Installed: %1").arg(FormatSize(InstalledMemory)));
	MemInfo.append(tr("Swap: %1").arg(FormatSize(TotalSwap)));
	MemInfo.append(tr("Commited: %1").arg(FormatSize(CommitedMemory)));
	MemInfo.append(tr("Physical: %1").arg(FormatSize(RamUsage)));
	m_pStausMEM->setToolTip(MemInfo.join("\r\n"));


	SSysStats Stats = theAPI->GetStats();

	QString IO;
	IO += tr("R: %1").arg(FormatSize(qMax(Stats.Io.ReadRate.Get(), qMax(Stats.MMapIo.ReadRate.Get(), Stats.Disk.ReadRate.Get()))) + "/s");
	IO += " ";
	IO += tr("W: %1").arg(FormatSize(qMax(Stats.Io.WriteRate.Get(), qMax(Stats.MMapIo.WriteRate.Get(), Stats.Disk.WriteRate.Get()))) + "/s");
	m_pStausIO->setText(IO + "    ");

	QStringList IOInfo;
	IOInfo.append(tr("FileIO; Read: %1/s; Write: %2/s; Other: %3/s").arg(FormatSize(Stats.Io.ReadRate.Get())).arg(FormatSize(Stats.Io.WriteRate.Get())).arg(FormatSize(Stats.Io.OtherRate.Get())));
	IOInfo.append(tr("MMapIO; Read: %1/s; Write: %2/s").arg(FormatSize(Stats.MMapIo.ReadRate.Get())).arg(FormatSize(Stats.MMapIo.WriteRate.Get())));
#ifdef WIN32
	if(((CWindowsAPI*)theAPI)->IsMonitoringETW())
		IOInfo.append(tr("DiskIO; Read: %1/s; Write: %2/s").arg(FormatSize(Stats.Disk.ReadRate.Get())).arg(FormatSize(Stats.Disk.WriteRate.Get())));
#endif
	m_pStausIO->setToolTip(IOInfo.join("\r\n"));

	CNetMonitor* pNetMonitor = theAPI->GetNetMonitor();

	CNetMonitor::SDataRates NetRates = pNetMonitor->GetTotalDataRate(CNetMonitor::eNet);
	CNetMonitor::SDataRates RasRates = pNetMonitor->GetTotalDataRate(CNetMonitor::eRas);

	QString Net;
	Net += tr("D: %1").arg(FormatSize(NetRates.ReceiveRate) + "/s");
	Net += " ";
	Net += tr("U: %1").arg(FormatSize(NetRates.SendRate) + "/s");
	m_pStausNET->setText(Net + "    ");

	QStringList NetInfo;
	NetInfo.append(tr("TCP/IP; Download: %1/s; Upload: %2/s").arg(FormatSize(NetRates.ReceiveRate)).arg(FormatSize(NetRates.SendRate)));
	NetInfo.append(tr("VPN/RAS; Download: %1/s; Upload: %2/s").arg(FormatSize(RasRates.ReceiveRate)).arg(FormatSize(RasRates.SendRate)));
	m_pStausNET->setToolTip(NetInfo.join("\r\n"));



	if (!m_pTrayIcon->isVisible())
		return;

	QString TrayInfo = tr("Task Explorer\r\nCPU: %1%\r\nRam: %2%").arg(int(100 * theAPI->GetCpuUsage()))
		.arg(InstalledMemory > 0 ? (int)100 * RamUsage / InstalledMemory : 0);
	if (TotalSwap > 0)
		TrayInfo.append(tr("\r\nSwap: %1%").arg((int)100 * SwapedMemory / TotalSwap));

	m_pTrayIcon->setToolTip(TrayInfo);

	QString TrayGraphMode = theConf->GetString("SysTray/GraphMode", "CpuMem");

	int MemMode = 0;
	if (TrayGraphMode.compare("Cpu", Qt::CaseInsensitive) == 0)
		;
	else if (TrayGraphMode.compare("CpuMem", Qt::CaseInsensitive) == 0)
		MemMode = 3; // All in one bar
	else if (TrayGraphMode.compare("CpuMem1", Qt::CaseInsensitive) == 0)
		MemMode = 1; // ram only
	else if (TrayGraphMode.compare("CpuMem2", Qt::CaseInsensitive) == 0)
		MemMode = 2; // ram and swap in two columns
	else
	{
		if (m_pTrayGraph) 
		{
			m_pTrayGraph->deleteLater();
			m_pTrayGraph = NULL;

			QIcon Icon;
			Icon.addFile(":/TaskExplorer.png");
			m_pTrayIcon->setIcon(Icon);
		}
		return;
	}

	QImage TrayIcon = QImage(16, 16, QImage::Format_RGB32);
	{
		QPainter qp(&TrayIcon);

		int offset = 0;

		float hVal = TrayIcon.height();

		if (MemMode == 2 && InstalledMemory > 0 && TotalSwap > 0) // if mode == 2 but TotalSwap == 0 default to mode == 1
		{
			offset = 6;
			qp.fillRect(0, 0, offset, TrayIcon.height(), Qt::black);

			float used_x = hVal * theAPI->GetPhysicalUsed() / InstalledMemory;

			qp.setPen(QPen(Qt::cyan, 2));
			qp.drawLine(3, (hVal+1), 3, (hVal+1) - used_x);

			float swaped_x = hVal * SwapedMemory / TotalSwap;

			qp.setPen(QPen(Qt::yellow, 2));
			qp.drawLine(1, (hVal+1), 1, (hVal+1) - swaped_x);
		}
		else if (MemMode != 3 && InstalledMemory > 0)
		{
			offset = 3;
			qp.fillRect(0, 0, offset, TrayIcon.height(), Qt::black);

			float used_x = hVal * theAPI->GetPhysicalUsed() / InstalledMemory;

			qp.setPen(QPen(Qt::cyan, 2));
			qp.drawLine(1, (hVal+1), 1, (hVal+1) - used_x);
		}
		else if(TotalMemory > 0) // TaskExplorer Mode
		{
			offset = 3;
			qp.fillRect(0, 0, offset, TrayIcon.height(), Qt::black);

			float used_x = hVal * RamUsage / TotalMemory;
			float virtual_x = hVal * (RamUsage + SwapedMemory) / TotalMemory;
			float commited_x = hVal * CommitedMemory / TotalMemory;

			qp.setPen(QPen(Qt::yellow, 2));
			qp.drawLine(1, (hVal+1), 1, (hVal+1) - commited_x);

			qp.setPen(QPen(Qt::red, 2));
			qp.drawLine(1, (hVal+1), 1, (hVal+1) - virtual_x);

			qp.setPen(QPen(Qt::cyan, 2));
			qp.drawLine(1, (hVal+1), 1, (hVal+1) - used_x);
		}

		ASSERT(TrayIcon.width() > offset);

		if (m_pTrayGraph == NULL)
		{
			m_pTrayGraph = new CHistoryGraph(true, QColor(0, 128, 0), this);
			m_pTrayGraph->AddValue(0, Qt::green);
			m_pTrayGraph->AddValue(1, Qt::red);
			m_pTrayGraph->AddValue(2, Qt::blue);
		}

		// Note: we may add an cuttof show 0 below 10%
		m_pTrayGraph->SetValue(0, theAPI->GetCpuUsage());
		m_pTrayGraph->SetValue(1, theAPI->GetCpuKernelUsage());
		m_pTrayGraph->SetValue(2, theAPI->GetCpuDPCUsage());

		m_pTrayGraph->Update(TrayIcon.height(), TrayIcon.width() - offset);


		QImage TrayGraph = m_pTrayGraph->GetImage();
		qp.translate(TrayIcon.width() - TrayGraph.height(), TrayIcon.height());
		qp.rotate(270);
		qp.drawImage(0, 0, TrayGraph);
	}

	m_pTrayIcon->setIcon(QIcon(QPixmap::fromImage(TrayIcon)));
}

void CTaskExplorer::CheckErrors(QList<STATUS> Errors)
{
	if (Errors.isEmpty())
		return;

	// todo : show window with error list

	QMessageBox::warning(NULL, "TaskExplorer", tr("Operation failed for %1 item(s).").arg(Errors.size()));
}

void CTaskExplorer::OnRun()
{
#ifdef WIN32
    SelectedRunAsMode = 0;
    PhShowRunFileDialog(PhMainWndHandle, NULL, NULL, NULL, NULL, RFF_OPTRUNAS);
#endif
}

void CTaskExplorer::OnRunAdmin()
{
#ifdef WIN32
    SelectedRunAsMode = RUNAS_MODE_ADMIN;
    PhShowRunFileDialog(PhMainWndHandle, NULL, NULL, NULL, L"Type the name of a program that will be opened under alternate credentials.", 0);
#endif
}

void CTaskExplorer::OnRunUser()
{
#ifdef WIN32
    SelectedRunAsMode = RUNAS_MODE_LIMITED;
    PhShowRunFileDialog(PhMainWndHandle, NULL, NULL, NULL, L"Type the name of a program that will be opened under standard user privileges.", 0);
#endif
}

void CTaskExplorer::OnRunAs()
{
	CRunAsDialog* pWnd = new CRunAsDialog();
	pWnd->show();
}

void CTaskExplorer::OnRunSys()
{
#ifdef WIN32
    SelectedRunAsMode = RUNAS_MODE_SYS;
    PhShowRunFileDialog(PhMainWndHandle, NULL, NULL, NULL, L"Type the name of a program that will be opened as system with the TrustedInstaller's token.", 0);
	/*STATUS status = RunAsTrustedInstaller(L"");
	if(!NT_SUCCESS(status))
		QMessageBox::warning(NULL, "TaskExplorer", tr("Run As TristedInstaller failed, Error:").arg(status));*/
#endif
}

void CTaskExplorer::OnElevate()
{
#ifdef WIN32
	if (PhShellProcessHackerEx(NULL, NULL, L"", SW_SHOW, PH_SHELL_EXECUTE_ADMIN, 0, 0, NULL))
		OnExit();
#endif
}

void CTaskExplorer::OnExit()
{
	m_bExit = true;
	close();
}

void CTaskExplorer::OnSysTray(QSystemTrayIcon::ActivationReason Reason)
{
	switch(Reason)
	{
		case QSystemTrayIcon::Context:
			m_pTrayMenu->popup(QCursor::pos());	
			break;
		case QSystemTrayIcon::DoubleClick:
			if (isVisible())
				hide();
			else
			{
				show();
#ifdef WIN32
				WINDOWPLACEMENT placement = { sizeof(placement) };
				GetWindowPlacement(PhMainWndHandle, &placement);

				if (placement.showCmd == SW_MINIMIZE || placement.showCmd == SW_SHOWMINIMIZED)
					ShowWindowAsync(PhMainWndHandle, SW_RESTORE);
				else
					SetForegroundWindow(PhMainWndHandle);
#endif
			}
			break;
	}
}

void CTaskExplorer::OnSysTab()
{
	QAction* pAction = (QAction*)sender();
	int Index = m_Act2Tab.value(pAction);
	m_pSystemInfo->ShowTab(Index, pAction->isChecked());
}

/*void CTaskExplorer::OnKernelServices()
{
#ifdef WIN32
	theConf->SetValue("MainWindow/ShowDrivers", m_pMenuKernelServices->isChecked());
	m_pSystemInfo->SetShowKernelServices(m_pMenuKernelServices->isChecked());
#endif
}*/

void CTaskExplorer::OnTaskTab()
{
	QAction* pAction = (QAction*)sender();
	int Index = m_Act2Tab.value(pAction);
	m_pTaskInfo->ShowTab(Index, pAction->isChecked());
}

void CTaskExplorer::OnSystemInfo()
{
	CSystemInfoWindow* pSystemInfoWindow = new CSystemInfoWindow();
	pSystemInfoWindow->show();
}

void CTaskExplorer::OnSettings()
{
	CSettingsWindow* pSettingsWindow = new CSettingsWindow();
	connect(pSettingsWindow, SIGNAL(OptionsChanged()), this, SLOT(UpdateOptions()));
	pSettingsWindow->show();
}

void CTaskExplorer::UpdateOptions()
{
	m_pCustomItemDelegate->m_Grid = theConf->GetBool("Options/ShowGrid", true);
	m_pCustomItemDelegate->m_Color = QColor(theConf->GetString("Colors/GridColor", "#808080"));

	m_pGraphBar->UpdateLengths();

	killTimer(m_uTimerID);
	m_uTimerID = startTimer(theConf->GetInt("Options/RefreshInterval", 1000));

	emit ReloadAll();

	QTimer::singleShot(0, this, SLOT(UpdateAll()));
}

void CTaskExplorer::OnAutoRun()
{
#ifdef WIN32
	AutorunEnable(m_pMenuAutoRun->isChecked());
#endif
}

void CTaskExplorer::OnSkipUAC()
{
#ifdef WIN32
	SkipUacEnable(m_pMenuUAC->isChecked());
#endif
}

void CTaskExplorer::OnCreateService()
{
	CNewService* pWnd = new CNewService();
	pWnd->show();
}

void CTaskExplorer::OnReloadService()
{
	QMetaObject::invokeMethod(theAPI, "UpdateServiceList", Qt::QueuedConnection, Q_ARG(bool, true));
}

#ifdef WIN32
NTSTATUS PhpOpenServiceControlManager(_Out_ PHANDLE Handle, _In_ ACCESS_MASK DesiredAccess, _In_opt_ PVOID Context)
{
    SC_HANDLE serviceHandle;
    if (serviceHandle = OpenSCManager(NULL, NULL, DesiredAccess))
    {
        *Handle = serviceHandle;
        return STATUS_SUCCESS;
    }
    return PhGetLastWin32ErrorAsNtStatus();
}
#endif

void CTaskExplorer::OnSCMPermissions()
{
#ifdef WIN32
	PhEditSecurity(NULL, L"Service Control Manager", L"SCManager", (PPH_OPEN_OBJECT)PhpOpenServiceControlManager, NULL, NULL);
#endif
}

void CTaskExplorer::OnFreeMemory()
{
#ifdef WIN32
	SYSTEM_MEMORY_LIST_COMMAND command = MemoryCommandMax;

	if (sender() == m_pMenuFreeWorkingSet)
		command = MemoryEmptyWorkingSets;
	else if (sender() == m_pMenuFreeModPages)
		command = MemoryFlushModifiedList;
	else if (sender() == m_pMenuFreeStandby)
		command = MemoryPurgeStandbyList;
	else if (sender() == m_pMenuFreePriority0)
		command = MemoryPurgeLowPriorityStandbyList;
	
	QApplication::setOverrideCursor(Qt::WaitCursor);

	NTSTATUS status;
	if (command == MemoryCommandMax)
	{
		MEMORY_COMBINE_INFORMATION_EX combineInfo = { 0 };
		status = NtSetSystemInformation(SystemCombinePhysicalMemoryInformation, &combineInfo, sizeof(MEMORY_COMBINE_INFORMATION_EX));
	}
	else
	{
		status = NtSetSystemInformation(SystemMemoryListInformation, &command, sizeof(SYSTEM_MEMORY_LIST_COMMAND));
		if (status == STATUS_PRIVILEGE_NOT_HELD)
		{
			QString SocketName = CTaskService::RunWorker();
			if (!SocketName.isEmpty())
			{
				QVariantMap Parameters;
				Parameters["Command"] = (int)command;

				QVariantMap Request;
				Request["Command"] = "FreeMemory";
				Request["Parameters"] = Parameters;

				status = CTaskService::SendCommand(SocketName, Request).toInt();
			}
		}
	}

	QApplication::restoreOverrideCursor();

	if (!NT_SUCCESS(status)) 
		QMessageBox::warning(NULL, "TaskExplorer", tr("Memory opertion failed; Error: %1").arg(status));
#endif
}

void CTaskExplorer::OnFindHandle()
{
	CHandleSearch* pHandleSearch = new CHandleSearch();
	pHandleSearch->show();
}

void CTaskExplorer::OnFindDll()
{
	CModuleSearch* pModuleSearch = new CModuleSearch();
	pModuleSearch->show();
}

void CTaskExplorer::OnFindMemory()
{
	CMemorySearch* pMemorySearch = new CMemorySearch();
	pMemorySearch->show();
}

void CTaskExplorer::OnMonitorETW()
{
#ifdef WIN32
	theConf->SetValue("Options/MonitorETW", m_pMenuETW->isChecked());
	((CWindowsAPI*)theAPI)->MonitorETW(m_pMenuETW->isChecked());
#endif
}

void CTaskExplorer::OnSplitterMoved()
{
	m_pMenuTaskTabs->setEnabled(m_pMainSplitter->sizes()[1] > 0);
	m_pMenuSysTabs->setEnabled(m_pMainSplitter->sizes()[1] > 0 && m_pPanelSplitter->sizes()[0] > 0);
}

QStyledItemDelegate* CTaskExplorer::GetItemDelegate() 
{
	return m_pCustomItemDelegate; 
}

float CTaskExplorer::GetDpiScale()
{
	return QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96.0;// *100.0;
}

int CTaskExplorer::GetCellHeight()
{
	QFontMetrics fontMetrics(QApplication::font());
	int fontHeight = fontMetrics.height();
	
	return (fontHeight + 3) * GetDpiScale();
}

QVector<QColor> CTaskExplorer::GetPlotColors()
{
	static QVector<QColor> Colors;
	if (Colors.isEmpty())
	{
		Colors.append(Qt::red);
		Colors.append(Qt::green);
		Colors.append(Qt::blue);
		Colors.append(Qt::yellow);

		Colors.append("#f58231"); // Orange
		Colors.append("#911eb4"); // Purple
		Colors.append("#42d4f4"); // Cyan
		Colors.append("#f032e6"); // Magenta
		Colors.append("#bfef45"); // Lime
		Colors.append("#fabebe"); // Pink
		Colors.append("#469990"); // Teal
		Colors.append("#e6beff"); // Lavender
		Colors.append("#9A6324"); // Brown
		Colors.append("#fffac8"); // Beige
		Colors.append("#800000"); // Maroon
		Colors.append("#aaffc3"); // Mint
		Colors.append("#808000"); // Olive
		Colors.append("#ffd8b1"); // Acricot
		Colors.append("#000075"); // Navy

		Colors.append("#e6194B"); // Red
		Colors.append("#3cb44b"); // Green
		Colors.append("#4363d8"); // Yellow
		Colors.append("#ffe119"); // Blue
	}

	return Colors;
}

QColor CTaskExplorer::GetColor(int Color)
{
	QString ColorStr;
	switch (Color)
	{
	case eToBeRemoved:	ColorStr = theConf->GetString("Colors/ToBeRemoved", "#F08080"); break;
	case eAdded:		ColorStr = theConf->GetString("Colors/NewlyCreated", "#00FF7F"); break;
	
	case eSystem:		ColorStr = theConf->GetString("Colors/SystemProcess", "#AACCFF"); break;
	case eUser:			ColorStr = theConf->GetString("Colors/UserProcess", "#FFFF80"); break;
	case eService:		ColorStr = theConf->GetString("Colors/ServiceProcess", "#80FFFF"); break;
#ifdef WIN32
	case eJob:			ColorStr = theConf->GetString("Colors/JobProcess", "#D49C5C"); break;
	case ePico:			ColorStr = theConf->GetString("Colors/PicoProcess", "#42A0FF"); break;
	case eImmersive:	ColorStr = theConf->GetString("Colors/ImmersiveProcess", "#FFE6FF"); break;
	case eDotNet:		ColorStr = theConf->GetString("Colors/NetProcess", "#DCFF00"); break;
#endif
	case eElevated:		ColorStr = theConf->GetString("Colors/ElevatedProcess", "#FFBB30"); break;
#ifdef WIN32
	case eDriver:		ColorStr = theConf->GetString("Colors/KernelServices", "#FFC880"); break;
	case eGuiThread:	ColorStr = theConf->GetString("Colors/GuiThread", "#AACCFF"); break;
	case eIsInherited:	ColorStr = theConf->GetString("Colors/IsInherited", "#77FFFF"); break;
	case eIsProtected:	ColorStr = theConf->GetString("Colors/IsProtected", "#FF77FF"); break;
#endif
	case eExecutable:	ColorStr = theConf->GetString("Colors/Executable", "#FF90E0"); break;
	}

	StrPair ColorUse = Split2(ColorStr, ";");
	if (ColorUse.second.isEmpty() || ColorUse.second.compare("true", Qt::CaseInsensitive) == 0 || ColorUse.second.toInt() != 0)
		return QColor(ColorUse.first);

	return QColor(theConf->GetString("Colors/Background", "#FFFFFF"));
}

void CTaskExplorer::OnStatusMessage(const QString& Message)
{
	statusBar()->showMessage(Message, 5000); // show for 5 seconds
}

QString CTaskExplorer::GetVersion()
{
	QString Version = QString::number(VERSION_MJR) + "." + QString::number(VERSION_MIN) //.rightJustified(2, '0')
#if VERSION_REV > 0
		+ "." + QString::number(VERSION_REV)
#endif
#if VERSION_UPD > 0
		+ QString('a' + VERSION_UPD - 1)
#endif
		;
	return Version;
}

void CTaskExplorer::OnAbout()
{
	if (sender() == m_pMenuAbout)
	{
#ifdef Q_WS_MAC
		static QPointer<QMessageBox> oldMsgBox;

		if (oldMsgBox) {
			oldMsgBox->show();
			oldMsgBox->raise();
			oldMsgBox->activateWindow();
			return;
		}
#endif

		QString AboutCaption = tr(
			"<h3>About TaskExplorer</h3>"
			"<p>Version %1</p>"
			"<p>by DavidXanatos</p>"
			"<p>Copyright (c) 2019</p>"
		).arg(GetVersion());
		QString AboutText = tr(
			"<p>TaskExplorer is a powerfull multi-purpose Task Manager that helps you monitor system resources, debug software and detect malware.</p>"
			"<p></p>"
#ifdef WIN32
			"<p>On Windows TaskExplorer is powered by the ProsessHacker Library.</p>"
			"<p></p>"
#endif
			"<p>Visit <a href=\"https://github.com/DavidXanatos/TaskExplorer\">TaskExplorer on github</a> for more information.</p>"
			"<p></p>"
			"<p></p>"
			"<p></p>"
		);
		QMessageBox *msgBox = new QMessageBox(this);
		msgBox->setAttribute(Qt::WA_DeleteOnClose);
		msgBox->setWindowTitle(tr("About TaskExplorer"));
		msgBox->setText(AboutCaption);
		msgBox->setInformativeText(AboutText);

		QIcon ico(QLatin1String(":/TaskExplorer.png"));
		msgBox->setIconPixmap(ico.pixmap(128, 128));
#if defined(Q_WS_WINCE)
		msgBox->setDefaultButton(msgBox->addButton(QMessageBox::Ok));
#endif

		// should perhaps be a style hint
#ifdef Q_WS_MAC
		oldMsgBox = msgBox;
		msgBox->show();
#else
		msgBox->exec();
#endif
	}
	else if (sender() == m_pMenuSupport)
	{
		QDesktopServices::openUrl(QUrl("https://www.patreon.com/DavidXanatos"));
	}
#ifdef WIN32
	else if (sender() == m_pMenuAboutPH)
		PhShowAbout(this);
#endif
	else //if (sender() == m_pMenuAboutQt)
		QMessageBox::aboutQt(this);
}
