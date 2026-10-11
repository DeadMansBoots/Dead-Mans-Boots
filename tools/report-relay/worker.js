// Dead Man's Boots report relay: a Cloudflare Worker (free plan, no card, no bucket).
//
// The launcher POSTs a bug report (a zip) to /report. The worker passes the zip on, as it arrives, to the developers' PRIVATE reports repository as a release file
// (one release per day, tag reports-YYYYMMDD). The only key is GITHUB_TOKEN, a Worker secret on Cloudflare's side: a fine-grained GitHub token with Contents
// write on that one repository. Nothing on a player's PC can read, list or delete anything. Reading the reports back is done with the developers' own GitHub sign-in
// (fetch_reports.py), so the relay has no read or admin address at all.
//
// Limits: 45 MB, application/zip, a name made by the launcher (DMB-report-YYYYMMDD-HHMMSS-xxxxxx.zip), and the file must begin with the zip signature.
//
// Settings (wrangler.toml / Worker secrets): GITHUB_TOKEN (secret), REPORTS_REPO (e.g. DeadMansBoots/dmb-reports); GITHUB_API and GITHUB_UPLOADS only for tests.

const MAX_BYTES = 45 * 1024 * 1024;
const NAME = /^DMB-report-(\d{8})-\d{6}-[0-9a-f]{1,6}\.zip$/;

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

async function releaseFor(env, day) {
  const tag = `reports-${day}`;
  const found = await github(env, `/repos/${env.REPORTS_REPO}/releases/tags/${tag}`);
  if (found.ok) return (await found.json()).id;
  if (found.status !== 404) throw new Error(`release lookup ${found.status}`);
  const made = await github(env, `/repos/${env.REPORTS_REPO}/releases`, {
    method: "POST",
    body: JSON.stringify({ tag_name: tag, name: `Reports ${day}`, prerelease: true }),
  });
  if (made.ok) return (await made.json()).id;
  // two uploads can race to create it: the loser looks again
  const again = await github(env, `/repos/${env.REPORTS_REPO}/releases/tags/${tag}`);
  if (again.ok) return (await again.json()).id;
  throw new Error(`release create ${made.status}`);
}

// the body with its first four bytes read, put back in front of the rest
async function checkedBody(request) {
  const reader = request.body.getReader();
  const first = await reader.read();
  const head = first.value || new Uint8Array(0);
  if (head.length < 4 || head[0] !== 0x50 || head[1] !== 0x4b || head[2] !== 0x03 || head[3] !== 0x04) return null;
  return new ReadableStream({
    start(controller) {
      controller.enqueue(head);
    },
    async pull(controller) {
      const next = await reader.read();
      if (next.done) controller.close();
      else controller.enqueue(next.value);
    },
    cancel(reason) {
      return reader.cancel(reason);
    },
  });
}

async function upload(request, env) {
  const name = request.headers.get("X-DMB-Report-Name") || "";
  const match = NAME.exec(name);
  if (!match) return json({ ok: false, error: "bad name" }, 400);
  if (request.headers.get("content-type") !== "application/zip") return json({ ok: false, error: "not a zip" }, 400);
  const length = Number(request.headers.get("content-length") || "0");
  if (!length || length > MAX_BYTES) return json({ ok: false, error: "size" }, 413);
  if (!env.GITHUB_TOKEN || !env.REPORTS_REPO) return json({ ok: false, error: "relay not set up" }, 500);
  const body = await checkedBody(request);
  if (!body) return json({ ok: false, error: "not a zip" }, 400);

  try {
    const release = await releaseFor(env, match[1]);
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

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "POST" && url.pathname === "/report") return upload(request, env);
    return new Response("Dead Man's Boots report relay", { status: 200 });
  },
};
