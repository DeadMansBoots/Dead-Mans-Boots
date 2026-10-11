// Dead Man's Boots report relay: a Cloudflare Worker on the free plan (no card, no GitHub, no key the players or K have to make).
//
// The launcher POSTs a bug report (a zip) to /report. The worker stores it in a Workers KV namespace (binding REPORTS). The developers read the reports back from the
// same worker: GET /reports lists them, GET /reports/<name> gives one, DELETE /reports/<name> removes it, all behind the Bearer secret READ_SECRET (a random secret that
// only the developers' PC and the worker's own settings hold; fetch_reports.py deletes each report right after it has been copied, which is what keeps the free 1 GB
// empty). Nothing on a player's PC can read, list or delete anything.
//
// Limits: 24 MB (a KV value holds 25 MiB), application/zip, a name made by the launcher (DMB-report-YYYYMMDD-HHMMSS-xxxxxx.zip) with a day within one of today's, and the
// file must begin with the zip signature. Not limited: junk from one address beyond the LIMITER binding (it is approximate); anyone who finds the address can send junk
// until the worker is deleted in the Cloudflare dashboard or the feed's reportEndpoint is removed. KV Free allows 1,000 writes a day and 1 GB; a report stays at most
// KEEP_SECONDS (a backstop: the fetch tool deletes reports as soon as it has them).
//
// Settings (wrangler.toml / Worker secrets): REPORTS (KV namespace binding), READ_SECRET (secret). Optional: LIMITER (rate-limit binding), KEEP_DAYS.

const MAX_BYTES = 24 * 1024 * 1024;
const NAME = /^DMB-report-(\d{8})-\d{6}-[0-9a-f]{1,6}\.zip$/;
const KEEP_DAYS = 60;

function json(body, status = 200) {
  return new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });
}

// the Bearer secret, compared without stopping at the first difference
function authorized(request, env) {
  const given = (request.headers.get("authorization") || "").replace(/^Bearer /, "");
  const secret = env.READ_SECRET || "";
  if (!secret || given.length !== secret.length) return false;
  let diff = 0;
  for (let i = 0; i < secret.length; i++) diff |= given.charCodeAt(i) ^ secret.charCodeAt(i);
  return diff === 0;
}

// The body, checked and handed on: its first four bytes must be the zip signature (read, then written first into the stream that goes to KV); the rest is piped on by
// the runtime itself (a JavaScript read per chunk would spend the Free plan's 10 ms of CPU a request on a 24 MB file; time spent waiting for the network is not CPU).
// On Cloudflare the stream is a FixedLengthStream, so the store knows the length and a body shorter or longer than the Content-Length it announced is an error.
// Outside Cloudflare (the tests under node) a plain stream stands in.
async function checkedBody(request, length) {
  const reader = request.body.getReader();
  let head = new Uint8Array(0);
  while (head.length < 4) {
    const next = await reader.read();
    if (next.done) return null;
    const joined = new Uint8Array(head.length + next.value.length);
    joined.set(head);
    joined.set(next.value, head.length);
    head = joined;
  }
  if (head[0] !== 0x50 || head[1] !== 0x4b || head[2] !== 0x03 || head[3] !== 0x04) return null;
  const { readable, writable } = typeof FixedLengthStream !== "undefined" ? new FixedLengthStream(length) : new TransformStream();
  const writer = writable.getWriter();
  (async () => {
    await writer.write(head);
    writer.releaseLock();
    reader.releaseLock();
    await request.body.pipeTo(writable);
  })().catch(() => {}); // a failure here ends the store that reads `readable`, which reports it
  return readable;
}

async function upload(request, env) {
  const name = request.headers.get("X-DMB-Report-Name") || "";
  const match = NAME.exec(name);
  if (!match) return json({ ok: false, error: "bad name" }, 400);
  // the day in the name: only yesterday, today and tomorrow (players' clocks and time zones differ)
  const dayOf = (offset) => new Date(Date.now() + offset * 86400000).toISOString().slice(0, 10).replaceAll("-", "");
  if (![dayOf(-1), dayOf(0), dayOf(1)].includes(match[1])) return json({ ok: false, error: "bad day" }, 400);
  // an optional Cloudflare rate-limiting binding named LIMITER (wrangler.toml, [[ratelimits]]): one address, a few reports a minute
  if (env.LIMITER) {
    const { success } = await env.LIMITER.limit({ key: request.headers.get("cf-connecting-ip") || "unknown" });
    if (!success) return json({ ok: false, error: "slow down" }, 429);
  }
  if (request.headers.get("content-type") !== "application/zip") return json({ ok: false, error: "not a zip" }, 400);
  const length = Number(request.headers.get("content-length") || "0");
  if (!length || length > MAX_BYTES) return json({ ok: false, error: "size" }, 413);
  if (!env.REPORTS) return json({ ok: false, error: "relay not set up" }, 500);
  const body = await checkedBody(request, length);
  if (!body) return json({ ok: false, error: "not a zip" }, 400);
  try {
    await env.REPORTS.put(name, body, {
      expirationTtl: (Number(env.KEEP_DAYS) || KEEP_DAYS) * 86400,
      metadata: { bytes: length, build: (request.headers.get("X-DMB-Build") || "").slice(0, 60), at: new Date().toISOString() },
    });
  } catch (e) {
    return json({ ok: false, error: String(e.message || e).slice(0, 100) }, 502);
  }
  return json({ ok: true });
}

async function list(env) {
  const reports = [];
  let cursor;
  for (let page = 0; page < 20; page++) {
    const found = await env.REPORTS.list({ prefix: "DMB-report-", cursor, limit: 1000 });
    for (const key of found.keys) reports.push({ name: key.name, ...(key.metadata || {}) });
    if (found.list_complete) break;
    cursor = found.cursor;
  }
  return reports;
}

async function reading(request, env, url) {
  if (!authorized(request, env)) return new Response("not found", { status: 404 }); // the same answer as for any other address: nothing to learn
  if (!env.REPORTS) return json({ ok: false, error: "relay not set up" }, 500);
  if (url.pathname === "/reports" && request.method === "GET") return json({ ok: true, reports: await list(env) });
  const name = decodeURIComponent(url.pathname.slice("/reports/".length));
  if (!NAME.test(name)) return json({ ok: false, error: "bad name" }, 400);
  if (request.method === "GET") {
    const value = await env.REPORTS.get(name, { type: "stream" });
    return value ? new Response(value, { headers: { "content-type": "application/zip" } }) : json({ ok: false, error: "no such report" }, 404);
  }
  if (request.method === "DELETE") {
    await env.REPORTS.delete(name);
    return json({ ok: true });
  }
  return json({ ok: false, error: "method" }, 405);
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "POST" && url.pathname === "/report") return upload(request, env);
    if (url.pathname === "/reports" || url.pathname.startsWith("/reports/")) return reading(request, env, url);
    return new Response("Dead Man's Boots report relay", { status: 200 });
  },
};
