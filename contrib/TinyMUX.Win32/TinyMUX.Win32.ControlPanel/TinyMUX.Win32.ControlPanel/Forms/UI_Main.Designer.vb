<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class UI_Main
    Inherits System.Windows.Forms.Form

    'Form overrides dispose to clean up the component list.
    <System.Diagnostics.DebuggerNonUserCode()> _
    Protected Overrides Sub Dispose(ByVal disposing As Boolean)
        Try
            If disposing AndAlso components IsNot Nothing Then
                components.Dispose()
            End If
        Finally
            MyBase.Dispose(disposing)
        End Try
    End Sub

    'Required by the Windows Form Designer
    Private components As System.ComponentModel.IContainer

    'NOTE: The following procedure is required by the Windows Form Designer
    'It can be modified using the Windows Form Designer.  
    'Do not modify it using the code editor.
    <System.Diagnostics.DebuggerStepThrough()> _
    Private Sub InitializeComponent()
        Dim resources As System.ComponentModel.ComponentResourceManager = New System.ComponentModel.ComponentResourceManager(GetType(UI_Main))
        Me.MenuStrip = New System.Windows.Forms.MenuStrip
        Me.FileToolStripMenuItem = New System.Windows.Forms.ToolStripMenuItem
        Me.AddExistingTinyMUXToolStripMenuItem = New System.Windows.Forms.ToolStripMenuItem
        Me.SaveMUXSrvcCONFToolStripMenuItem = New System.Windows.Forms.ToolStripMenuItem
        Me.TextBox = New System.Windows.Forms.TextBox
        Me.Label1 = New System.Windows.Forms.Label
        Me.MenuStrip.SuspendLayout()
        Me.SuspendLayout()
        '
        'MenuStrip
        '
        Me.MenuStrip.Items.AddRange(New System.Windows.Forms.ToolStripItem() {Me.FileToolStripMenuItem})
        Me.MenuStrip.Location = New System.Drawing.Point(0, 0)
        Me.MenuStrip.Name = "MenuStrip"
        Me.MenuStrip.Size = New System.Drawing.Size(614, 24)
        Me.MenuStrip.TabIndex = 0
        Me.MenuStrip.Text = "MenuStrip1"
        '
        'FileToolStripMenuItem
        '
        Me.FileToolStripMenuItem.DropDownItems.AddRange(New System.Windows.Forms.ToolStripItem() {Me.AddExistingTinyMUXToolStripMenuItem, Me.SaveMUXSrvcCONFToolStripMenuItem})
        Me.FileToolStripMenuItem.Name = "FileToolStripMenuItem"
        Me.FileToolStripMenuItem.Size = New System.Drawing.Size(35, 20)
        Me.FileToolStripMenuItem.Text = "File"
        '
        'AddExistingTinyMUXToolStripMenuItem
        '
        Me.AddExistingTinyMUXToolStripMenuItem.Name = "AddExistingTinyMUXToolStripMenuItem"
        Me.AddExistingTinyMUXToolStripMenuItem.Size = New System.Drawing.Size(188, 22)
        Me.AddExistingTinyMUXToolStripMenuItem.Text = "Add Existing TinyMUX"
        '
        'SaveMUXSrvcCONFToolStripMenuItem
        '
        Me.SaveMUXSrvcCONFToolStripMenuItem.Name = "SaveMUXSrvcCONFToolStripMenuItem"
        Me.SaveMUXSrvcCONFToolStripMenuItem.Size = New System.Drawing.Size(188, 22)
        Me.SaveMUXSrvcCONFToolStripMenuItem.Text = "Save MUXSrvc.CONF"
        '
        'TextBox
        '
        Me.TextBox.AcceptsReturn = True
        Me.TextBox.Dock = System.Windows.Forms.DockStyle.Fill
        Me.TextBox.Location = New System.Drawing.Point(0, 71)
        Me.TextBox.Multiline = True
        Me.TextBox.Name = "TextBox"
        Me.TextBox.Size = New System.Drawing.Size(614, 481)
        Me.TextBox.TabIndex = 2
        '
        'Label1
        '
        Me.Label1.Dock = System.Windows.Forms.DockStyle.Top
        Me.Label1.Location = New System.Drawing.Point(0, 24)
        Me.Label1.Name = "Label1"
        Me.Label1.Size = New System.Drawing.Size(614, 47)
        Me.Label1.TabIndex = 3
        Me.Label1.Text = resources.GetString("Label1.Text")
        '
        'UI_Main
        '
        Me.AutoScaleDimensions = New System.Drawing.SizeF(6.0!, 13.0!)
        Me.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font
        Me.ClientSize = New System.Drawing.Size(614, 552)
        Me.Controls.Add(Me.TextBox)
        Me.Controls.Add(Me.Label1)
        Me.Controls.Add(Me.MenuStrip)
        Me.MainMenuStrip = Me.MenuStrip
        Me.Name = "UI_Main"
        Me.Text = "TinyMUX Win32 Control Panel"
        Me.MenuStrip.ResumeLayout(False)
        Me.MenuStrip.PerformLayout()
        Me.ResumeLayout(False)
        Me.PerformLayout()

    End Sub
    Friend WithEvents MenuStrip As System.Windows.Forms.MenuStrip
    Friend WithEvents FileToolStripMenuItem As System.Windows.Forms.ToolStripMenuItem
    Friend WithEvents AddExistingTinyMUXToolStripMenuItem As System.Windows.Forms.ToolStripMenuItem
    Friend WithEvents TextBox As System.Windows.Forms.TextBox
    Friend WithEvents Label1 As System.Windows.Forms.Label
    Friend WithEvents SaveMUXSrvcCONFToolStripMenuItem As System.Windows.Forms.ToolStripMenuItem

End Class
