Public Class UI_Main
    ''' <summary>
    ''' This code occurs whenever the program is started.
    ''' It does nothing but call the Update routine that populates
    ''' the Form Textbox with data from the MUXSrvc.CONF file.
    ''' This sub should never be called directly.
    ''' </summary>
    Private Sub UI_Main_Load(ByVal sender As System.Object, ByVal e As System.EventArgs) Handles MyBase.Load
        'Set up the blank CONF file if it doesn't exist.
        If Not System.IO.File.Exists(Application.StartupPath & "\MUXSrvc.conf") Then System.IO.File.CreateText(Application.StartupPath & "\MUXSrvc.conf").Close()
        Me.UI_TextBox_Update()
    End Sub

    ''' <summary>
    ''' This code exists to populate the TextBox with data from the MUXSrvc.CONF file.
    ''' This code may be called directly at any time that the text file on disk changes.
    ''' </summary>
    Private Sub UI_TextBox_Update()
        'Create a filereader.
        Dim r_MUXSrvcConf As System.IO.StreamReader = My.Computer.FileSystem.OpenTextFileReader(Application.StartupPath & "\MUXSrvc.conf")
        'Create an array of lines.
        Dim ConfLines() As String = Nothing
        'Read til there's nothing left to read.
        While Not r_MUXSrvcConf.EndOfStream
            Dim line As String = r_MUXSrvcConf.ReadLine
            'Redundant directory check.
            If System.IO.Directory.Exists(line) Then
                If ConfLines Is Nothing Then
                    'If conflines is NULL then this is the first element.
                    ReDim ConfLines(0)
                Else
                    'Otherwise add a new element.
                    ReDim Preserve ConfLines(UBound(ConfLines))
                End If
                'Add the confirmed directory entry to the array.
                ConfLines(UBound(ConfLines)) = line
            End If
            'If the array isn't empty (file isn't blank)
            'link each array entry together with a carriage return and print to the textbox.
            If Not ConfLines Is Nothing Then
                Me.TextBox.Text = System.String.Join(vbNewLine, ConfLines)
            End If
        End While
        'Kick the reader out of memory.
        r_MUXSrvcConf.Close()
        r_MUXSrvcConf.Dispose()
    End Sub

    ''' <summary>
    ''' This code exists to write the contents of the UI TextBox to the MUXSrvc.CONF file.
    ''' This code may be called directly at any time that the contents of the UI TextBox changes.
    ''' </summary>
    ''' <remarks></remarks>
    Private Sub MUXSrvcConf_SAVE()
        'Create a filewriter.
        Dim w_MUXSrvcCONF As System.IO.StreamWriter = My.Computer.FileSystem.OpenTextFileWriter(Application.StartupPath & "\MUXSrvc.conf", False)
        w_MUXSrvcCONF.Write(Me.TextBox.Text)
        'Kick the writer out of memory.
        w_MUXSrvcCONF.Close()
        w_MUXSrvcCONF.Dispose()

        'Call the service refresh command.
        'If you change the name of the Service IN the Service project you
        'need to change it here too.
        Dim srvc As New System.ServiceProcess.ServiceController("TinyMUX.Win32")
        If srvc.Status = ServiceProcess.ServiceControllerStatus.Running Then srvc.ExecuteCommand(128)
        srvc.Close()
        srvc.Dispose()
    End Sub

    ''' <summary>
    ''' This code exists as an Event Handler for a UI Click event.
    ''' This should never be called directly.
    ''' </summary>
    ''' <param name="sender"></param>
    ''' <param name="e"></param>
    ''' <remarks></remarks>
    Private Sub AddExistingTinyMUXToolStripMenuItem_Click(ByVal sender As System.Object, ByVal e As System.EventArgs) Handles AddExistingTinyMUXToolStripMenuItem.Click
        'Create a FolderBrowser Control to use to navigate to and select
        'a TinyMUX Game Directory.
        Dim FldrBrwsr As New System.Windows.Forms.FolderBrowserDialog
        FldrBrwsr.Description = "Select the GAME directory for the TinyMUX to Add to Win32 Service control."
        FldrBrwsr.RootFolder = Environment.SpecialFolder.Desktop
        Dim GameName As String = Nothing
        Dim PIDFile As String = Nothing

        'Show the control and handle events that occur if/when the user selects a folder and clicks OK.
        If FldrBrwsr.ShowDialog() = Windows.Forms.DialogResult.OK Then
            Dim SelPath As String = FldrBrwsr.SelectedPath & "\"
            'Redundant check for existence of path selected by FolderBrowserDialog
            If System.IO.Directory.Exists(SelPath) Then

                'Before we allow the automated process to add the designated folder to the CONF,
                'it would be nice to know that it's a valid TinyMUX folder with all the requisite
                'files.
                If System.IO.File.Exists(SelPath & "startmux.wsf") Then
                    'Check for existence of MUXCONFIG.VBS in selected folder.
                    If System.IO.File.Exists(SelPath & "muxconfig.vbs") Then
                        'Instantiate text reader for VBScript if it exists.
                        Dim MuxConf As System.IO.StreamReader = My.Computer.FileSystem.OpenTextFileReader(SelPath & "muxconfig.vbs")
                        'Load script content into MS Script Control
                        Dim MuxconfigVBS As New MSScriptControl.ScriptControl
                        MuxconfigVBS.Language = "VBScript"
                        MuxconfigVBS.AddCode(MuxConf.ReadToEnd)
                        'Release memory resources dedicated to script content.
                        MuxConf.Close()
                        MuxConf.Dispose()
                        GameName = MuxconfigVBS.Eval("gamename")
                        MuxconfigVBS.Reset()
                        MuxconfigVBS = Nothing
                        'GameName should be set.  If it is still nothing or if it is equal to an empty string "" then the MUXConfig.VBS File is corrupt, the GameName value is commented, or the file is invalid for other reasons.
                        If (Not (GameName = "")) And (Not (GameName Is Nothing)) Then
                            'Check for the existence of <GAMENAME>.CONF in the folder specified as the MUX Game Folder.  If it does exist that's great - otherwise let the user know an error occurred.
                            If Not System.IO.File.Exists(SelPath & GameName & ".conf") Then
                                MsgBox("Selected Path Does Not Contain A NETMUX.CONF (Or <GAMENAME.CONF> If You Changed The GameName In MUXCONFIG.VBS)")
                                Exit Sub
                            End If
                        Else
                            'Tell the user if GameName is invalid for some reason.
                            MsgBox("MUXConfig.VBS Returned No Valid GameName")
                            Exit Sub
                        End If
                    Else
                        'Tell the user if no MuxConfig.VBS file was found.
                        MsgBox("Selected Path Does Not Contain A MUXCONFIG.VBS File.")
                        Exit Sub
                    End If
                Else
                    'Tell the user if no StartMUX.WSF file was found.
                    MsgBox("Selected Path Does Not Contain A STARTMUX.WSF File.")
                    Exit Sub
                End If
                'Now let's make sure this particular Game Directory isn't already in the file.
                Dim r_MUXSrvcCONF As System.IO.StreamReader = My.Computer.FileSystem.OpenTextFileReader(Application.StartupPath & "\MUXSrvc.conf")
                While Not r_MUXSrvcCONF.EndOfStream
                    Dim line As String = r_MUXSrvcCONF.ReadLine
                    If line = SelPath Then
                        MsgBox(SelPath & " already exists in the TinyMUX Service Configuration File.")
                        r_MUXSrvcCONF.Close()
                        r_MUXSrvcCONF.Dispose()
                        Exit Sub
                    End If
                End While
                r_MUXSrvcCONF.Close()
                r_MUXSrvcCONF.Dispose()
                'Now we're past all the error checking... if we're still running, write to the local database.
                'This writes to the CONF file on disk and then updates the UI.
                If Me.TextBox.Text.Length > 0 Then
                    Me.TextBox.Text = Me.TextBox.Text & vbNewLine & SelPath
                Else
                    Me.TextBox.Text = SelPath
                End If

                Me.MUXSrvcConf_SAVE()
            Else
                'Tell the user if the redundant folder.exists check failed.
                MsgBox("Selected Path Does Not Exist.")
                Exit Sub
            End If
        End If


    End Sub

    ''' <summary>
    ''' This code exists as an Event Handler for a UI Click event.
    ''' This should never be called directly.
    ''' </summary>
    Private Sub SaveMUXSrvcCONFToolStripMenuItem_Click(ByVal sender As System.Object, ByVal e As System.EventArgs) Handles SaveMUXSrvcCONFToolStripMenuItem.Click
        'Write the contents of the UI TextBox to the MUXSrvc.CONF File on disk.
        Me.MUXSrvcConf_SAVE()
    End Sub

    ''' <summary>
    ''' This code exists as an Event Handler for the UI FormClosing event.
    ''' This should never be called directly.
    ''' </summary>
    Private Sub UI_Main_FormClosing(ByVal sender As System.Object, ByVal e As System.Windows.Forms.FormClosingEventArgs) Handles MyBase.FormClosing
        'Uncomment the next line to auto-save the content of the textbox to the CONF file at every exit.
        'Me.MUXSrvcConf_SAVE()

        'Uncomment the next 3 lines to prompt for save on every exit.
        'If MsgBox("Save CONF File to Disk?", MsgBoxStyle.YesNo) = MsgBoxResult.Yes Then
        'Me.MUXSrvcConf_SAVE()
        'End If
    End Sub
End Class
