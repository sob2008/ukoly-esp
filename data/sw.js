// Service worker – cache-first pro shell appky, aby appka po prvním navštívení
// fungovala i úplně offline (mimo domácí síť, nainstalovaná jako PWA).

// Zvyš při každé zmene tohoto souboru (nebo obsahu SHELL_FILES) – prohlížeč
// detekuje aktualizaci service workeru jen podle zmeny bajtu v tomto souboru,
// a zmena jmena cache zaroven zahodi pripadnou starou/nekompletni cache
// (viz "activate" nize).
const CACHE_NAME = "ukoly-shell-v2";
const SHELL_FILES = [
  "/",
  "/index.html",
  "/app.js",
  "/style.css",
  "/manifest.json",
  "/icons/icon-192.png",
  "/icons/icon-512.png",
];

// Jednoduchá offline stránka pro případ, že appka jeste vubec nemela
// prilezitost se nacachovat (prvni spusteni bez pripojeni k ESP32) - misto
// uplne prazdne/bile obrazovky aspon srozumitelna zprava.
const OFFLINE_FALLBACK_HTML = `<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<body style="background:#12151a;color:#e4e7ec;font-family:-apple-system,Segoe UI,Roboto,sans-serif;
  display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0;padding:24px;
  text-align:center;box-sizing:border-box;">
  <div>
    <p style="font-size:17px;font-weight:600;margin:0 0 8px;">Appka zatím není uložená pro offline použití</p>
    <p style="font-size:14px;color:#8b93a3;margin:0;">Připoj se k WiFi, na které běží ESP32, a appku znovu otevři –
      pak bude fungovat i bez připojení.</p>
  </div>
</body>`;

self.addEventListener("install", (event) => {
  event.waitUntil(
    (async () => {
      const cache = await caches.open(CACHE_NAME);
      // Soubory se cachují postupne (ne caches.addAll, ktere je "vse nebo
      // nic" a posle vsechny pozadavky najednou) - ESP32 ma omezeny pocet
      // soucasnych spojeni, takze jeden pretizeny/neuspesny pozadavek by
      // jinak shodil cachovani uplne vseho a appka by offline nefungovala
      // vubec.
      for (const url of SHELL_FILES) {
        try {
          await cache.add(url);
        } catch (e) {
          console.warn("[SW] Nepodarilo se nacachovat", url, e);
        }
      }
    })()
  );
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) =>
        Promise.all(
          keys
            .filter((key) => key !== CACHE_NAME)
            .map((key) => caches.delete(key))
        )
      )
  );
  self.clients.claim();
});

self.addEventListener("fetch", (event) => {
  const req = event.request;

  // Na API dotazy se service worker vůbec neplete – appka řeší online/offline sama.
  if (req.url.includes("/api/")) {
    return;
  }

  event.respondWith(
    caches.match(req).then((cached) => {
      if (cached) {
        return cached;
      }
      return fetch(req)
        .then((res) => {
          if (res && res.status === 200 && req.method === "GET") {
            const resClone = res.clone();
            caches.open(CACHE_NAME).then((cache) => cache.put(req, resClone));
          }
          return res;
        })
        .catch(async () => {
          if (req.mode === "navigate") {
            const cachedShell = await caches.match("/index.html");
            if (cachedShell) return cachedShell;
            return new Response(OFFLINE_FALLBACK_HTML, {
              headers: { "Content-Type": "text/html; charset=utf-8" },
            });
          }
        });
    })
  );
});
