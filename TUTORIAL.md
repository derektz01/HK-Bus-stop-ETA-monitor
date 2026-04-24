# Web Settings Page — User Guide

A walkthrough of the captive web portal used to configure the HK Bus ETA Monitor: Wi-Fi, bus stops (multi-stop), cache rebuild, save, and reboot.

---

## 1. Opening the settings page

The device serves the settings page on port 80 in both modes:

- **First boot / after a Wi-Fi failure** — the device falls back to AP mode (see [src/Wireless.cpp:23-32](src/Wireless.cpp#L23-L32)). Connect your phone or laptop to the AP advertised by the device, then open `http://192.168.4.1` in a browser.
- **Normal operation** — the device joins your home Wi-Fi as a station. Open `http://<device-ip>` (the IP appears on the device's screen).

You should see the page below.

![Settings page overview](img/main-1.png)

---

## 2. Wi-Fi setup

Fill in the **SSID** and **Password** of the network you want the device to join. Click **Show** next to the password to reveal what you typed; click **Hide** to mask it again.

> Wi-Fi changes do **not** take effect immediately — you must press **Save** and then **Reboot** before the device tries the new credentials.

---

## 3. Building the bus stop list cache

To search stops by Chinese name, the page needs a local cache of stop IDs and names. The cache lives in your browser's `localStorage`, so it's a one-time download per browser.

### KMB cache

KMB exposes the entire stop list (~6,700 stops) at a single endpoint, so the build is fast — usually a few seconds.

![KMB cache loaded with timestamp](img/bus-stop-list-build-1.png)

### Citybus (CTB) cache

CTB has no single endpoint, so the page builds the list in three phases:

1. Fetch the route list (`/route/CTB`).
2. For each route × direction, fetch the route-stop list.
3. For each unique stop, fetch the stop name.

Expect this to take a few minutes. You can watch progress in the status line.

![CTB cache before build](img/bus-stop-list-build-3.png)

![CTB rebuild — Phase 2/3 in progress](img/bus-stop-list-build-4.png)

When a build finishes, the status line shows the total stop count and a timestamp:

![CTB cache loaded with timestamp](img/bus-stop-list-build-5.png)

The bus stop section starts collapsed — click **Search by name** to open it (and again to close it):

![Default collapsed state of the bus stop section](img/bus-stop-list-build-8.png)

---

## 4. Searching and selecting stops (multi-stop)

Open the search panel with **Search by name**, then type any part of the Chinese stop name. Matches appear live; click any row to add it to the selected list.

![KMB search results for 尖沙咀](img/bus-stop-search-1.png)

You can add **as many stops as you want** for both KMB and Citybus — the device will rotate through every selected stop on its display. Below, the user has already picked two KMB stops while still searching for more:

![Multiple KMB stops selected, search still active](img/bus-stop-search-2.png)

CTB works the same way:

![CTB search results](img/bus-stop-search-3.png)

To remove a stop, click the red **×** at the right of its row in the selected list.

---

## 5. Adding a stop manually

If you already know a stop ID (e.g. from a `data.etabus.gov.hk` URL or the Citybus website), you can skip the search:

1. Click **+ Add manually** under the relevant section.
2. Type the stop ID (KMB IDs look like `YT912`; CTB IDs are 6 digits like `001588`).
3. Click **Add**.

Manually-added stops display "(unresolved)" until the corresponding cache is built — but they still work; the firmware queries by ID, not by name.

---

## 6. Rebuild cooldown and rate-limit gate

The KMB and Citybus APIs both rate-limit aggressive callers. The page has two protections to keep you out of trouble.

### 60-second cooldown

After every rebuild — successful or failed — the **Rebuild stop list** button is disabled for 60 seconds and shows a live countdown:

![Rebuild button in cooldown — Rebuild stop list (49s)](img/bus-stop-list-build-7.png)

This applies to both KMB and CTB rebuild buttons independently.

### 2-hour cache-age gate

If you click **Rebuild stop list** while the cache is younger than two hours, the confirm dialog tells you exactly how recent the cache is and warns about API rate-limiting:

![First-time rebuild confirm](img/bus-stop-list-build-2.png)

![Cache-age warning — rebuilt 11 minutes ago](img/bus-stop-list-build-6.png)

> **Why this matters.** When the data.gov.hk gateway rate-limits a request it returns `HTTP 403` *without* CORS headers. The browser surfaces this as a confusing CORS error in the console even though the real cause is throttling. The cooldown and gate are there so you don't trip into that state by accident.

Stop lists rarely change — rebuild only when you genuinely need newer data (e.g. a new route opened).

---

## 7. Saving configuration

Click **Save** to persist Wi-Fi credentials and the selected stop lists to flash. The page POSTs to `/api/config` and the firmware writes to LittleFS ([src/WebPortal.cpp:63-104](src/WebPortal.cpp#L63-L104)).

- **Stop selection changes** are picked up on the next ETA refresh — no reboot required.
- **Wi-Fi changes** require a reboot to take effect.

---

## 8. Rebooting the device

Click **Reboot** and confirm:

![Reboot confirmation dialog](img/reboot-1.png)

The page then shows a 5-second countdown and refreshes itself when the device comes back up:

![Reboot countdown modal](img/reboot-2.png)

If you changed Wi-Fi credentials, the device may now be on a different network — re-open the page at the new IP shown on the device screen.
