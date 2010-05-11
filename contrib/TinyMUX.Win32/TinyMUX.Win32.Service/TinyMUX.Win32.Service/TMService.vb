Imports System.ServiceProcess

''' <summary>
''' A class designed for a Win32 Service Implementation.
''' </summary>
Public Class TMService
    Inherits System.ServiceProcess.ServiceBase


    'Set up an EventLog for Service Reporting and Debugging.
    'Messagebox debugging is great, but doesn't work with Services...
    'if you change this code, make sure to use the srvc_Eventlog.WriteEntry
    'method instead of messageboxing.
    Private srvc_EventLog As New System.Diagnostics.EventLog
    ''' <summary>
    ''' Constructor.
    ''' If this sub is commented or removed, the Service will not work.
    ''' </summary>
    Public Sub New()
        'The name that appears in the Control Panel -> Administrative Tools -> Services list.
        Me.ServiceName = "TinyMUX.Win32"
        'Can we stop the Service at any time.
        Me.CanStop = True
        'No, we can not pause or continue the Service.
        'Changing this value can cause problems with your TinyMUX servers.
        Me.CanPauseAndContinue = False
        'Report all starts and stops to the Event Log?
        Me.AutoLog = True
        'Set up the Service EventLog Entry point.
        If Not Diagnostics.EventLog.SourceExists("TinyMUX Win32 Service") Then
            Diagnostics.EventLog.CreateEventSource("TinyMUX Win32 Service", "TinyMUX")
        End If
    End Sub

    ''' <summary>
    ''' The primary function of the Class definition.
    ''' If this sub is commented or removed, the Service will not work.
    ''' </summary>
    Shared Sub Main()
        System.ServiceProcess.ServiceBase.Run(New TMService)
    End Sub

    ''' <summary>
    ''' The actions taken when the Service is entering the Started state.
    ''' The code here replicates the functions of STARTMUX.WSF to provide
    ''' .CRASH database checking and other functions during TinyMUX Server startup.
    ''' </summary>
    Protected Overrides Sub OnStart(ByVal args() As String)
        'Instantiate EventLog writing for error reporting.
        'If an attempt to convert this Service into an Application
        'or Class Library is made, it would be considered best
        'practice to convert this to a System.Exception.

        'Check for existence of MUXSrvc.conf
        If System.IO.File.Exists(System.Windows.Forms.Application.StartupPath & "\MUXSrvc.conf") Then
            'If the Service's .conf file exists, open it for reading.
            Dim r_MUXSrvcCONF As New System.IO.StreamReader(System.IO.File.OpenRead(System.Windows.Forms.Application.StartupPath & "\MUXSrvc.conf"))
            While Not r_MUXSrvcCONF.EndOfStream
                'Each entry should contain one TinyMUX Game Directory on its own line.
                Dim line As String = r_MUXSrvcCONF.ReadLine
                'Verify the directory specified exists.
                If System.IO.Directory.Exists(line) Then
                    'Check for a muxconfig.vbs file.
                    If System.IO.File.Exists(line & "startmux.wsf") Then
                        'Start up the startmux.wsf script.
                        Dim NetMUX As New System.Diagnostics.Process
                        NetMUX.StartInfo.WorkingDirectory = line
                        NetMUX.StartInfo.FileName = "cscript.exe"
                        NetMUX.StartInfo.Arguments = "startmux.wsf"
                        NetMUX.StartInfo.CreateNoWindow = False
                        NetMUX.StartInfo.WindowStyle = Diagnostics.ProcessWindowStyle.Hidden

                        NetMUX.Start()
                    Else
                        srvc_EventLog.WriteEntry(line & "muxconfig.vbs not found.")
                    End If
                Else
                    srvc_EventLog.WriteEntry(line & " folder not found.")
                End If
ENDLOOP:
            End While
            r_MUXSrvcCONF.Close()
            r_MUXSrvcCONF.Dispose()
        End If

    End Sub

    'Windows API declarations required for appropriately sending SIGTERM to TinyMUX
    'in Win32 environments.
    'These declarations were written and tested in Windows XP Professional, Service Pack 3.
    'Further testing was done in Windows 2000, Vista, and 2003 Server.
    'HOWEVER!  Direct API can be twitchy between 2 service packs of the same Windows version
    Private Declare Function SendMessage Lib "User32.dll" Alias "SendMessageA" (ByVal Handle As Int32, ByVal wMsg As Int32, ByVal wParam As Int32, ByVal lParam As Int32) As Int32
    Private Declare Function EnumThreadWindows Lib "User32.dll" (ByVal dwThreadId As UInteger, ByVal lpfn As EnumThreadDelegate, ByVal lParam As IntPtr) As Boolean
    Delegate Function EnumThreadDelegate(ByVal hWnd As IntPtr, ByVal lParam As IntPtr) As Boolean
    Private Const WM_CLOSE As UInt32 = &H10
    ''' <summary>
    ''' EnumThreadCallback is a callback for an API call.
    ''' This is used only by the OnStop procedure, to send appropriate SIGTERM signals to
    ''' Netmux.EXE processes.  This function should NEVER be called directly.
    ''' </summary>
    Function EnumThreadCallback(ByVal hWnd As IntPtr, ByVal lParam As IntPtr) As Boolean
        SendMessage(hWnd, WM_CLOSE, IntPtr.Zero, IntPtr.Zero)
        Return True
    End Function

    ''' <summary>
    ''' The OnStop event takes place whenever the Service is Stopped - either by user-interaction or
    ''' by Windows Shutdown or other direct, proper Stop procedure.
    ''' </summary>
    Protected Overrides Sub OnStop()
        'When the Service Stops, TinyMUX needs an appropriate SIGTERM signal from Windows 
        '(stopping the Service will be treated by netmux.exe as if Windows shut down while it was running).
        'Grab all the netmux processes currently running.  
        'YES!  If you run netmux.exe outside of this Service while this Service is installed, THIS WILL KILL IT!
        'It is expected that anyone using the Win32 Service for TinyMUX relies on it exclusively.
        For Each proc As System.Diagnostics.Process In System.Diagnostics.Process.GetProcessesByName("netmux")

            For Each pt As System.Diagnostics.ProcessThread In proc.Threads
                'The following eventlog entry is good for bughunting, but not necessary.
                'srvc_EventLog.WriteEntry("PROCESS: " & proc.ProcessName & vbNewLine & "PROCESS HANDLES CREATED: " & proc.HandleCount & vbNewLine & "PROCESS MAINWINDOWHANDLE STRING: " & proc.MainWindowHandle.ToString & vbNewLine & "PROCESS MAINWINDOWTITLE STRING" & proc.MainWindowTitle.ToString &  vbNewLine & pt.Id)
                EnumThreadWindows(CUInt(pt.Id), New EnumThreadDelegate(AddressOf Me.EnumThreadCallback), IntPtr.Zero)
            Next pt
        Next proc
    End Sub

    Protected Overrides Sub OnCustomCommand(ByVal command As Integer)
        If command = 128 Then Me.RefreshCONF()
    End Sub
    Private Sub RefreshCONF()
        If System.IO.File.Exists(System.Windows.Forms.Application.StartupPath & "\MUXSrvc.conf") Then
            'If the Service's .conf file exists, open it for reading.
            Dim r_MUXSrvcCONF As New System.IO.StreamReader(System.IO.File.OpenRead(System.Windows.Forms.Application.StartupPath & "\MUXSrvc.conf"))
            While Not r_MUXSrvcCONF.EndOfStream
                'Each entry should contain one TinyMUX Game Directory on its own line.
                Dim line As String = r_MUXSrvcCONF.ReadLine
                'Verify the directory specified exists.
                If System.IO.Directory.Exists(line) Then
                    'Check for a muxconfig.vbs file.
                    If System.IO.File.Exists(line & "startmux.wsf") Then
                        'Check if this entry is already running.
                        For Each proc As System.Diagnostics.Process In System.Diagnostics.Process.GetProcessesByName("netmux")
                            If proc.StartInfo.WorkingDirectory = line Then GoTo endloop
                        Next

                        'Start up the startmux.wsf script if it isn't running yet.
                        Dim NetMUX As New System.Diagnostics.Process
                        NetMUX.StartInfo.WorkingDirectory = line
                        NetMUX.StartInfo.FileName = "cscript.exe"
                        NetMUX.StartInfo.Arguments = "startmux.wsf"
                        NetMUX.StartInfo.CreateNoWindow = False
                        NetMUX.StartInfo.WindowStyle = Diagnostics.ProcessWindowStyle.Hidden

                        NetMUX.Start()
                    Else
                        srvc_EventLog.WriteEntry(line & "muxconfig.vbs not found.")
                    End If
                Else
                    srvc_EventLog.WriteEntry(line & " folder not found.")
                End If
ENDLOOP:
            End While
            r_MUXSrvcCONF.Close()
            r_MUXSrvcCONF.Dispose()
        End If
    End Sub


End Class
