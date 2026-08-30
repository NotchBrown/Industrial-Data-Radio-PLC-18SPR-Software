/****************************************************************************
** IDR Configurator installer component script.
** - Adds a "Shortcut Options" page with a "Create desktop shortcut" checkbox.
** - Always creates a Start Menu shortcut.
** - Creates the desktop shortcut only when the checkbox is checked.
****************************************************************************/

function Component()
{
    if (installer.isUninstaller())
        return;
    // Wait until the component metadata (and its .ui files) are loaded before
    // adding the wizard page.
    component.loaded.connect(this, Component.prototype.installerLoaded);
}

Component.prototype.installerLoaded = function()
{
    // Insert the shortcut options page right before "Ready to Install".
    installer.addWizardPage(component, "ShortcutPage", QInstaller.ReadyForInstallation);
};

Component.prototype.createOperations = function()
{
    // Call default implementation to extract the component's data directory.
    component.createOperations();

    if (installer.isUninstaller())
        return;

    // Start menu shortcut (always).
    component.addOperation("CreateShortcut",
        "@TargetDir@/IDRConfigurator.exe",
        "@StartMenuDir@/IDR Configurator.lnk",
        "workingDirectory=@TargetDir@",
        "iconPath=@TargetDir@/resource/icon/main.ico",
        "description=IDR Configurator");

    // Desktop shortcut (optional, driven by the ShortcutPage checkbox).
    var ui = component.userInterface("ShortcutPage");
    if (ui && ui.desktopShortcutBox && ui.desktopShortcutBox.checked) {
        component.addOperation("CreateShortcut",
            "@TargetDir@/IDRConfigurator.exe",
            "@DesktopDir@/IDR Configurator.lnk",
            "workingDirectory=@TargetDir@",
            "iconPath=@TargetDir@/resource/icon/main.ico",
            "description=IDR Configurator");
    }
};
