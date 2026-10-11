// Dead Man's Boots report relay: a Cloudflare Worker (free plan, no card, no bucket).
//
// The launcher POSTs a bug report (a zip) to /report. The worker passes the zip on, as it arrives, to the developers' PRIVATE reports repository as a release file
// (one release per day, tag reports-YYYYMMDD). The only key is GITHUB_TOKEN, a Worker secret on Cloudflare's side: a fine-grained GitHub token with Contents
// write on that one repository. Nothing on a player's PC can read, list or delete anything. Reading the reports back is done with the developers' own GitHub sign-in
// (fetch_reports.py), so the relay has no read or admin address at all.
//
// Limits: 45 MB, application/zip, a name made by the launcher (DMB-report-YYYYMMDD-HHMMSS-xxxxxx.zip) with a day within one of today's, and the file must begin with
// the zip signature. Not limited: the number of reports from one address unless the LIMITER binding is set (it is approximate); anyone who finds the address can
// send junk until the worker is deleted in the Cloudflare dashboard or the feed's reportEndpoint is removed. A release holds at most 1,000 files, so a full one is followed by reports-YYYYMMDD-2 and so on (five a day, 4,500 files at most).
//
// Reports are deleted after 30 days by a daily cron trigger (wrangler.toml; KEEP_DAYS in vars changes it).
//
// Settings (wrangler.toml / Worker secrets): GITHUB_TOKEN (secret), REPORTS_REPO (e.g. DeadMansBoots/dmb-reports); GITHUB_API and GITHUB_UPLOADS only for tests.

const MAX_BYTES = 45 * 1024 * 1024;
const NAME = /^DMB-report-(\d{8})-\d{6}-[0-9a-f]{1,6}\.zip$/;
// GitHub allows 1,000 files on a release (more gives HTTP 422), so a day's reports go into further releases when one fills: reports-YYYYMMDD, reports-YYYYMMDD-2, ...
const SHARD_FILES = 900;
const MAX_SHARDS = 5;

function json(body, status = 200) {
  return new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });
}

function github(env, path, init = {}, uploads = false) {
  const base = uploads ? env.GITHUB_UPLOADS || "https://uploads.github.com" : env.GITHUB_API || "https://api.github.com";
  return fetch(base + path, {
    ...init,
    headers: {
      authorization: `Bearer ${env.GITHUB_TOKEN}`,
      accept: "application/vnd.github+json",
      "user-agent": "dmb-report-relay",
      ...(init.headers || {}),
    },
  });
}

async function releaseFor(env, day, shard = 1) {
  const tag = shard === 1 ? `reports-${day}` : `reports-${day}-${shard}`;
  const found = await github(env, `/repos/${env.REPORTS_REPO}/releases/tags/${tag}`);
  if (found.ok) return (await found.json()).id;
  if (found.status !== 404) throw new Error(`release lookup ${found.status}`);
  const made = await github(env, `/repos/${env.REPORTS_REPO}/releases`, {
    method: "POST",
    body: JSON.stringify({ tag_name: tag, name: `Reports ${day}${shard === 1 ? "" : ` (${shard})`}`, prerelease: true }),
  });
  if (made.ok) return (await made.json()).id;
  // two uploads can race to create it: the loser looks again
  const again = await github(env, `/repos/${env.REPORTS_REPO}/releases/tags/${tag}`);
  if (again.ok) return (await again.json()).id;
  throw new Error(`release create ${made.status}`);
}

// how many files a release holds: with one file to a page, the last page's number (the Link header) is the count
async function fileCount(env, release) {
  const listed = await github(env, `/repos/${env.REPORTS_REPO}/releases/${release}/assets?per_page=1`);
  if (!listed.ok) throw new Error(`file count ${listed.status}`);
  const last = /[?&]page=(\d+)>; rel="last"/.exec(listed.headers.get("link") || "");
  return last ? Number(last[1]) : (await listed.json()).length;
}

// The body, checked and handed on: its first four bytes must be the zip signature (read, then written first into the stream that goes to GitHub); the rest is piped
// on by the runtime itself (a JavaScript read per chunk would spend the Free plan's 10 ms of CPU a request on a 45 MB file; time spent waiting for the network is not
// CPU). On Cloudflare the stream is a FixedLengthStream, so GitHub is sent a Content-Length and not chunked encoding, and a body shorter or longer than the
// Content-Length it announced is an error. Outside Cloudflare (the tests under node) a plain stream stands in.
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
  })().catch(() => {}); // a failure here ends the upload that reads `readable`, which reports it
  return readable;
}

async function upload(request, env) {
  const name = request.headers.get("X-DMB-Report-Name") || "";
  const match = NAME.exec(name);
  if (!match) return json({ ok: false, error: "bad name" }, 400);
  // the day in the name makes the release tag: only yesterday, today and tomorrow (players' clocks and time zones differ), so nobody can make tags at will
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
  if (!env.GITHUB_TOKEN || !env.REPORTS_REPO) return json({ ok: false, error: "relay not set up" }, 500);
  const body = await checkedBody(request, length);
  if (!body) return json({ ok: false, error: "not a zip" }, 400);

  try {
    let release = 0;
    for (let shard = 1; shard <= MAX_SHARDS; shard++) {
      release = await releaseFor(env, match[1], shard);
      if ((await fileCount(env, release)) < (Number(env.SHARD_FILES) || SHARD_FILES)) break; // SHARD_FILES in env is for tests
      release = 0;
    }
    if (!release) return json({ ok: false, error: "full for today" }, 503);
    const stored = await github(
      env,
      `/repos/${env.REPORTS_REPO}/releases/${release}/assets?name=${encodeURIComponent(name)}`,
      { method: "POST", body, headers: { "content-type": "application/zip", "content-length": String(length) }, duplex: "half" },
      true
    );
    if (!stored.ok) return json({ ok: false, error: `store ${stored.status}` }, 502);
  } catch (e) {
    return json({ ok: false, error: String(e.message || e).slice(0, 100) }, 502);
  }
  return json({ ok: true });
}

// Once a day (the cron trigger in wrangler.toml) the reports older than KEEP_DAYS are deleted from the repository: they hold players' logs and are no use after the
// developers have read them (fetch_reports.py keeps its own copy). Only releases named reports-YYYYMMDD[-N] are touched.
const KEEP_DAYS = 30;

async function expire(env) {
  const keep = Number(env.KEEP_DAYS) || KEEP_DAYS;
  const cutoff = new Date(Date.now() - keep * 86400000).toISOString().slice(0, 10).replaceAll("-", "");
  const old = [];
  for (let page = 1; page <= 20; page++) {
    const listed = await github(env, `/repos/${env.REPORTS_REPO}/releases?per_page=100&page=${page}`);
    if (!listed.ok) throw new Error(`release list ${listed.status}`);
    const items = await listed.json();
    for (const r of items) {
      const m = /^reports-(\d{8})(-\d+)?$/.exec(r.tag_name);
      if (m && m[1] < cutoff) old.push(r);
    }
    if (items.length < 100) break;
  }
  for (const r of old) {
    await github(env, `/repos/${env.REPORTS_REPO}/releases/${r.id}`, { method: "DELETE" });
    await github(env, `/repos/${env.REPORTS_REPO}/git/refs/tags/${r.tag_name}`, { method: "DELETE" });
  }
  return old.length;
}

export default {
  async scheduled(event, env, ctx) {
    ctx.waitUntil(expire(env));
  },
  expire,
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "POST" && url.pathname === "/report") return upload(request, env);
    return new Response("Dead Man's Boots report relay", { status: 200 });
  },
};
