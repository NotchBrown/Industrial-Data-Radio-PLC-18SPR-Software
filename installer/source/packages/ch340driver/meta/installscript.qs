/****************************************************************************
** IDR Configurator - CH340 driver component script.
** Adds a "CH340 Driver" wizard page with an install checkbox. When checked,
** the bundled (complete, unmodified) WCH driver is registered with pnputil
** using an elevated (UAC-prompted) operation.
**
** Robustness notes:
** - QtIFW's "Execute" operation builds the command line itself and has known
**   quoting quirks, so we run via "cmd.exe /C" with the INF referenced by a
**   RELATIVE filename. The working directory (which may contain spaces, e.g.
**   "C:\Program Files\...") is set through the "workingDirectory=" parameter,
**   which QProcess handles natively without shell quoting issues.
****************************************************************************/

function Component()
{
    if (installer.isUninstaller())
        return;
    component.loaded.connect(this, Component.prototype.installerLoaded);
}

Component.prototype.installerLoaded = function()
{
    // Insert the driver options page right before "Ready to Install".
    installer.addWizardPage(component, "DriverPage", QInstaller.ReadyForInstallation);
};

Component.prototype.createOperations = function()
{
    // Extract this component's data directory (the complete INF file set).
    component.createOperations();

    if (installer.isUninstaller())
        return;

    var ui = component.userInterface("DriverPage");
    if (ui && ui.ch340DriverBox && ui.ch340DriverBox.checked) {
        // Run the bundled install_driver.bat at NORMAL (non-elevated) privilege.
        // The bat internally launches pnputil with Start-Process -Verb RunAs to
        // request a UAC elevation prompt just for the driver install. Running the
        // bat non-elevated keeps %TEMP% as the user's temp (so the diagnostic log
        // is where the user can find it) and avoids the elevated-Execute quoting
        // and working-directory bugs of QtIFW.
        component.addOperation("Execute", "cmd.exe", "/C",
            "@TargetDir@/components/ch340driver/install_driver.bat");
    }
};