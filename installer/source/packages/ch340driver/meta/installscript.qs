/****************************************************************************
** IDR Configurator - CH340 driver component script.
**
** Wizard page: a "Device Driver" page with two checkboxes:
**   1. ch340DriverBox - install the CH340 driver (always meaningful).
**   2. checkPatchBox   - install the Windows 7 SHA-2 support update KB3033929.
**
** Behaviour:
**   - KB3033929 is ONLY ever applied on Windows 7 (OS build 6.1) that lacks
**     SHA-2 support and only when checkPatchBox is checked. It is NEVER
**     triggered on Windows 8/8.1/10/11 (the check is gated on OS version here
**     AND again inside the batch file, belt and braces).
**   - On Windows 7 needing the patch, the patch is installed first (which
**     requires a restart) and the driver is deferred: the driver's SHA-256
**     signature cannot be verified until the PC is rebooted. A message asks
**     the user to restart and re-run the installer.
**   - Otherwise the driver is imported into the DriverStore with /add-driver.
**
** Robustness / no-hang:
**   - Uses addOperation (NORMAL privilege) + the batch elevates the elevated
**     tool with Start-Process -Verb RunAs inside try/catch, so a canceled UAC
**     cannot hang the installer.
**   - The patch batch uses wusa /quiet /norestart and always exits cleanly.
****************************************************************************/

function Component()
{
    if (installer.isUninstaller())
        return;
    component.loaded.connect(this, Component.prototype.installerLoaded);
}

Component.prototype.installerLoaded = function()
{
    installer.addWizardPage(component, "DriverPage", QInstaller.ReadyForInstallation);
};

// Extract this component's data to a fixed path (see "modify extract" example).
Component.prototype.createOperationsForArchive = function(archive)
{
    component.addOperation("Extract", archive, "@TargetDir@/ch340driver");
};

function isWindows7()
{
    var os = systemInfo.osVersion().toString();
    // Windows 7 = OS build 6.1; 8/8.1=6.2/6.3, 10/11=10. Only 6.1 matches.
    return os.indexOf("6.1") === 0;
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (installer.isUninstaller())
        return;

    var ui = component.userInterface("DriverPage");
    var wantDriver = ui && ui.ch340DriverBox && ui.ch340DriverBox.checked;
    var wantPatch  = ui && ui.checkPatchBox  && ui.checkPatchBox.checked;

    // --- Windows 7: install the SHA-2 patch if requested (needs restart). ---
    if (isWindows7() && wantPatch) {
        component.addOperation("Execute", "cmd.exe", "/C",
            "@TargetDir@/ch340driver/install_driver.bat --patch");
        return; // defer driver until after the required restart
    }

    // --- all other cases: install the driver directly. ---
    if (wantDriver) {
        component.addOperation("Execute", "cmd.exe", "/C",
            "@TargetDir@/ch340driver/install_driver.bat --driver");
    }
};