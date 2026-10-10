// Fixed-origin gateway: generated games never choose an upstream destination.
export const ORIGIN = 'https://workshop-game-share.vercel.app';
export const MAX_UPLOAD_BYTES = 6 * 1024 * 1024;
const CSP = "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'";
const ASSETS = new Set([
  '/', '/index.html', '/player.js', '/player.css', '/worker.js',
  '/runtime/fake08.js', '/runtime/fake08.wasm',
  '/runtime/FAKE08-LICENSE.md', '/runtime/Z8LUA-LICENSE.txt',
  '/runtime/ERIS-LICENSE.txt', '/runtime/MINIZ-LICENSE.txt',
  '/runtime/LODEPNG-LICENSE.txt',
  '/fonts/workshop-pixel.woff2', '/fonts/LICENSE.txt',
]);

function responseHeaders() {
  return new Headers({
    'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer',
    'Content-Security-Policy': CSP,
  });
}
function error(status, message) {
  const headers = responseHeaders();
  headers.set('Content-Type', 'application/json; charset=utf-8');
  headers.set('Cache-Control', 'no-store');
  return new Response(JSON.stringify({error: message}), {status, headers});
}

async function boundedBody(request, signal) {
  const length = request.headers.get('content-length');
  if (length && (!/^\d+$/.test(length) || Number(length) > MAX_UPLOAD_BYTES)) return null;
  if (!request.body) return new Uint8Array();
  const reader = request.body.getReader(), chunks = [];
  const cancel = () => { void reader.cancel().catch(() => {}); };
  signal.addEventListener('abort', cancel, {once: true});
  let bytes = 0;
  try {
    for (;;) {
      if (signal.aborted) throw Error('cancelled');
      const {done, value} = await reader.read();
      if (signal.aborted) throw Error('cancelled');
      if (done) break;
      bytes += value.byteLength;
      if (bytes > MAX_UPLOAD_BYTES) { await reader.cancel(); return null; }
      chunks.push(value);
    }
  } finally { signal.removeEventListener('abort', cancel); reader.releaseLock(); }
  const body = new Uint8Array(bytes);
  let offset = 0;
  for (const chunk of chunks) { body.set(chunk, offset); offset += chunk.byteLength; }
  return body;
}

export async function handleRequest(request, upstreamFetch = fetch) {
  const url = new URL(request.url), upload = url.pathname === '/api/upload';
  if (url.pathname === '/health' && request.method === 'GET') {
    const headers = responseHeaders();
    headers.set('Content-Type', 'application/json'); headers.set('Cache-Control', 'no-store');
    return new Response('{"ok":true,"service":"workshop-share-gateway"}', {headers});
  }
  let targetPath;
  if (upload) {
    if (request.method !== 'POST') return error(405, '仅支持上传');
    if (url.search) return error(400, '分享地址无效');
    if (!/^Bearer .+/.test(request.headers.get('authorization') || '')) return error(401, '分享凭据无效');
    if (request.headers.get('content-type')?.split(';')[0].trim().toLowerCase() !== 'application/json') return error(415, '仅支持 JSON 游戏数据');
    targetPath = '/api/upload';
  } else {
    if (request.method !== 'GET' && request.method !== 'HEAD') return error(405, '仅支持读取');
    if (url.pathname === '/api/game') {
      const ids = url.searchParams.getAll('id');
      if (ids.length !== 1 || !/^[a-f0-9]{64}$/.test(ids[0]) || [...url.searchParams.keys()].some(k => k !== 'id')) return error(400, '分享地址无效');
      targetPath = '/api/game?id=' + ids[0];
    } else if (ASSETS.has(url.pathname) || /^\/player\.[a-f0-9]{16}\.(css|js)$/.test(url.pathname) || /^\/g\/[a-f0-9]{64}$/.test(url.pathname)) {
      if (url.search) return error(400, '分享地址无效');
      targetPath = url.pathname;
    } else return error(404, '分享地址不存在');
  }
  const headers = new Headers();
  if (upload) {
    // Vercel remains responsible for validating the existing upload secret.
    headers.set('Authorization', request.headers.get('authorization'));
    headers.set('Content-Type', 'application/json');
  }
  for (const name of ['accept', 'range', 'if-none-match', 'if-modified-since']) {
    const value = request.headers.get(name);
    if (!upload && value) headers.set(name, value);
  }
  const controller = new AbortController();
  const cancel = () => controller.abort();
  request.signal.addEventListener('abort', cancel, {once: true});
  if (request.signal.aborted) controller.abort();
  const timeout = setTimeout(cancel, 25000);
  try {
    const body = upload ? await boundedBody(request, controller.signal) : undefined;
    if (body === null) return error(413, '分享请求过大');
    if (controller.signal.aborted) throw Error('cancelled');
    const upstream = await upstreamFetch(ORIGIN + targetPath, {
      method: request.method, headers, body, redirect: 'manual', signal: controller.signal,
    });
    // A redirect would send the handheld back to the inaccessible origin.
    if (upstream.status >= 300 && upstream.status < 400 && upstream.status !== 304) {
      await upstream.body?.cancel();
      return error(503, '分享服务暂时无法连接，请重试');
    }
    const resultHeaders = responseHeaders();
    for (const name of ['content-type', 'cache-control', 'etag', 'last-modified', 'content-range', 'accept-ranges']) {
      const value = upstream.headers.get(name);
      if (value) resultHeaders.set(name, value);
    }
    if (upload || upstream.status >= 400) resultHeaders.set('Cache-Control', 'no-store');
    return new Response(request.method === 'HEAD' ? null : upstream.body, {
      status: upstream.status, headers: resultHeaders,
    });
  } catch {
    return error(503, '分享服务暂时无法连接，请重试');
  } finally {
    clearTimeout(timeout); request.signal.removeEventListener('abort', cancel);
  }
}

export default {fetch: handleRequestWrapper};
function handleRequestWrapper(request) { return handleRequest(request); }
