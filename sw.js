// Retires the service worker that the 2025 fractonaut app installed at the
// site root. The 2025 app now lives at /2025/ with its own worker; the
// current app does not use one. Browsers that still have the old root worker
// fetch this file, install it, and it removes itself.
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => {
  event.waitUntil((async () => {
    await self.registration.unregister();
    const clients = await self.clients.matchAll({ type: 'window' });
    clients.forEach((client) => client.navigate(client.url));
  })());
});
