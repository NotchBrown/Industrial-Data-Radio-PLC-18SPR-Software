/****************************************************************************
** IDR Configurator - CH340 driver component script.
** Adds a "CH340 Driver" wizard page with an install checkbox. When checked,
** the bundled (architecture-matched) WCH driver is registered with pnputil
** using an elevated (UAC-prompted) operation.
**
** The driver INF is installed into the Windows DriverStore and made to match
** the genuine CH340 chip, so the radio shows up as a normal COM port.
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
    // Extract this component's data directory (the architecture-matched INF).
    component.createOperations();

    if (installer.isUninstaller())
        return;

    // The driver INF lives in this component's unpacked data directory: it is
    // extracted to <root>/components/ch340driver/CH341SER.INF.
    var inf = "@TargetDir@/components/ch340driver/CH341SER.INF";

    var ui = component.userInterface("DriverPage");
    if (ui && ui.ch340DriverBox && ui.ch340DriverBox.checked) {
        // pnputil requires elevation; addElevatedOperation triggers the UAC
        // prompt. /install also binds the package to a connected CH340 device
        // if one is present; otherwise the driver is staged for the next plug-in.
        component.addElevatedOperation("Execute", "pnputil.exe",
            "/add-driver", inf, "/install");
    }
};