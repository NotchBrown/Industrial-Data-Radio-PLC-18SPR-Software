/****************************************************************************
** IDR Configurator - CH340 driver component script.
** Adds a "CH340 Driver" wizard page with an install checkbox. When checked,
** the bundled (complete, unmodified) WCH driver is installed with pnputil.
**
** Path strategy (QtIFW best practice, see the "modify extract" example):
** The component's data is explicitly extracted to the fixed, well-known
** location "@TargetDir@/ch340driver" via createOperationsForArchive() instead
** of relying on QtIFW's default components/<name> extraction path. Both the
** extract target and the executed batch file use the same path, so the driver
** files and install_driver.bat are always found together.
**
** Privilege strategy:
** The batch runs at NORMAL privilege (addOperation, not addElevatedOperation,
** which has QtIFW quoting bugs and an unexpected %TEMP% context). Inside the
** batch, pnputil is launched with Start-Process -Verb RunAs, requesting a
** single UAC elevation prompt just for the driver install.
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

// Extract this component's data to a fixed path instead of QtIFW's default
// @TargetDir@/components/<name> so the batch script always finds the driver.
Component.prototype.createOperationsForArchive = function(archive)
{
    component.addOperation("Extract", archive, "@TargetDir@/ch340driver");
};

Component.prototype.createOperations = function()
{
    // Creates the default operations; createOperationsForArchive() above
    // redirects the data extraction to "@TargetDir@/ch340driver".
    component.createOperations();

    if (installer.isUninstaller())
        return;

    var ui = component.userInterface("DriverPage");
    if (ui && ui.ch340DriverBox && ui.ch340DriverBox.checked) {
        // Run the wrapper batch at normal privilege; it elevates pnputil itself.
        component.addOperation("Execute", "cmd.exe", "/C",
            "@TargetDir@/ch340driver/install_driver.bat");
    }
};