# AuraSense

A Windows service to turn the Asus Aura LED into a soothing CPU usage indicator.  At 0% usage LEDs slowly pulsate green.  As CPU load increases, LEDs pulsate quicker and transition to full red at 100%.  Handles resume from sleep properly.  The service itself uses roughly 1% CPU.

### Prerequisites

* Asus motherboard with Aura Sync feature, either builtin LED or header to attached LED fans etc.
* The Asus [Aura SDK](https://www.asus.com/campaign/aura/us/SDK.html) to obtain AURA_SDK.DLL.

### Installing

First, if you have the official Asus Aura Sync Utility installed, you will need to disable 'LightingService' service by setting it's Startup Type to Disabled in the Windows Service Control panel.  Otherwise the services will be fighting for control.

Place AURA_SDK.DLL and AuraSense.exe in the same folder of your choosing.  Open an Administrator command prompt to install as a service:
```
AuraSense.exe -service
```

Configure the service to automatically start when Windows boots (the **space** after "start=" is important):
```
sc.exe config AuraSense start= auto
```

### Uninstalling

To uninstall and return control back to the hardware default:
```
AuraSense.exe -unregserver
```

## Authors

* Kevin Bagnall

## Acknowledgments

* Asus for the [Aura SDK](https://www.asus.com/campaign/aura/us/SDK.html)
