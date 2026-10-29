/**
 * Service worker of the installed app (PWA). Keeps it simple:
 * - build assets and icons: cache first (their URLs change with every build)
 * - pages and /api reads: network first, the last copy is shown when offline
 * Other origins (article photos) are left to the browser.
 */
const VERSION = 'v2'; // v2: new brand logo/icons (2026-10-22)
const STATIC = `static-${VERSION}`;
const PAGES = `pages-${VERSION}`;

self.addEventListener('install', () => self.skipWaiting());

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) =>
        Promise.all(
          keys.filter((key) => key !== STATIC && key !== PAGES).map((key) => caches.delete(key))
        )
      )
      .then(() => self.clients.claim())
  );
});

const isStatic = (url) =>
  url.pathname.startsWith('/_next/static/') ||
  url.pathname.startsWith('/pwa/') ||
  url.pathname.startsWith('/logo/') ||
  url.pathname.startsWith('/assets/');

async function cacheFirst(request) {
  const cached = await caches.match(request);
  if (cached) return cached;
  const response = await fetch(request);
  if (response.ok) (await caches.open(STATIC)).put(request, response.clone());
  return response;
}

async function networkFirst(request) {
  try {
    const response = await fetch(request);
    if (response.ok) (await caches.open(PAGES)).put(request, response.clone());
    return response;
  } catch (error) {
    const cached = await caches.match(request);
    if (cached) return cached;
    // Offline and never visited: the home page is better than the browser's error page.
    if (request.mode === 'navigate') {
      const home = await caches.match('/');
      if (home) return home;
    }
    throw error;
  }
}

self.addEventListener('fetch', (event) => {
  const { request } = event;
  const url = new URL(request.url);
  if (request.method !== 'GET' || url.origin !== self.location.origin) return;
  // Signed-in staff pages and their data are never cached.
  if (url.pathname.startsWith('/admin') || url.pathname.startsWith('/api/admin')) return;
  if (url.pathname.startsWith('/api/auth')) return;

  if (isStatic(url)) event.respondWith(cacheFirst(request));
  else if (request.mode === 'navigate' || url.pathname.startsWith('/api/'))
    event.respondWith(networkFirst(request));
});
