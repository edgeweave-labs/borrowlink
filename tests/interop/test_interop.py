"""C node (examples/linux-node) against the EdgeWeave Python Hub.

One Hub per test, listening on 127.0.0.1 with an automatically assigned port.
The C node is started as a child process; everything is torn down at the end of
each test.  The Hub side is EdgeWeave's own hub.hub.Hub, hub.client.Client and
hub.trace.Trace, used unchanged (see tests/interop/README in docs).
"""
import asyncio
import json
import os
import unittest

from interop import support

support.require_env()
from hub import ids  # noqa: E402
from hub.client import Client, RpcError  # noqa: E402
from hub.hub import Hub  # noqa: E402
from hub.trace import Trace  # noqa: E402

TOKEN = "interop-token"
NODE_NAME = "c-node"
NODE_ID = ids.node_id_for("c-node-reference")
TEMP_TOPIC = 0x0100


def run(coro):
    return asyncio.run(asyncio.wait_for(coro, 60))


class Env:
    def __init__(self, name):
        os.makedirs(support.OUT_DIR, exist_ok=True)
        self.trace_path = os.path.join(support.OUT_DIR, "trace-%s.jsonl" % name)
        self.hub = Hub(TOKEN, trace=Trace(self.trace_path), rpc_timeout=5.0)
        self.procs = []
        self.clients = []

    async def __aenter__(self):
        await self.hub.start()
        return self

    async def __aexit__(self, *exc):
        for c in self.clients:
            try:
                await c.close()
            except Exception:  # noqa: BLE001
                pass
        for p in self.procs:
            if p.returncode is None:
                p.kill()
            await p.wait()
        await self.hub.stop()
        self.hub.trace.close()

    async def client(self, name):
        c = Client(name, "test-board", [], TOKEN)
        await c.connect("127.0.0.1", self.hub.port)
        self.clients.append(c)
        return c

    async def node(self, *extra, token=TOKEN, rpcs=1, node_id=NODE_ID):
        args = [support.NODE_BIN, "--host", "127.0.0.1", "--port", str(self.hub.port), "--token", token,
                "--name", NODE_NAME, "--node-id", "%x" % node_id, "--rpcs", str(rpcs), "--temp", "23.5",
                "--timeout-ms", "20000", *extra]
        p = await asyncio.create_subprocess_exec(*args, stdout=asyncio.subprocess.PIPE,
                                                 stderr=asyncio.subprocess.PIPE)
        self.procs.append(p)
        return p

    def events(self, ev, **match):
        return self.hub.trace.find(ev, **match)


async def wait_for(pred, what, timeout=10.0):
    end = asyncio.get_running_loop().time() + timeout
    while asyncio.get_running_loop().time() < end:
        v = pred()
        if v:
            return v
        await asyncio.sleep(0.02)
    raise AssertionError("timed out waiting for " + what)


async def finish(proc, timeout=15.0):
    out, err = await asyncio.wait_for(proc.communicate(), timeout)
    return proc.returncode, out.decode(), err.decode()


def hexid(n):
    return "0x%016x" % n


class CNodeAgainstPythonHub(unittest.TestCase):
    def test_node_joins_pool_publishes_and_answers_rpc(self):
        async def go():
            async with Env("happy") as env:
                caller = await env.client("py-caller")
                await caller.watch_directory()
                proc = await env.node(rpcs=2)

                # 1. the C node shows up in the capability pool
                node = await wait_for(lambda: caller.find_node(NODE_NAME), "node in directory")
                self.assertEqual(node["node_id"], NODE_ID)
                self.assertEqual(node["board"], "linux-reference")
                self.assertEqual({c["kind"] for c in node["caps"]}, {"sense.temperature", "actuator.led"})
                led = next(c for c in node["caps"] if c["kind"] == "actuator.led")
                self.assertEqual(led["methods"], ["set"])
                self.assertEqual(led["spec"], {"colors": ["on"]})
                temp = next(c for c in node["caps"] if c["kind"] == "sense.temperature")
                self.assertEqual(temp["data"], {"value": TEMP_TOPIC})

                # 2. its data point reaches a subscriber
                values = []
                st = await caller.subscribe(NODE_ID, TEMP_TOPIC, values.append)
                self.assertEqual(st, 0)
                await wait_for(lambda: values, "temperature value")
                self.assertEqual(values[0], 23.5)

                # 3. two RPCs are answered by the C node (the second has a >512 byte text to force
                #    the Hub to fragment the DELIVER across frames)
                r1 = await caller.rpc(NODE_ID, "led", "set", {"color": "on", "on": True})
                self.assertEqual(r1, {"ok": True})
                r2 = await caller.rpc(NODE_ID, "led", "set", {"color": "x" * 700, "on": False})
                self.assertEqual(r2, {"ok": True})

                rc, out, err = await finish(proc)
                return env, rc, out, err

        env, rc, out, err = run(go())
        self.assertEqual(rc, 0, (out, err))
        self.assertIn("session closed cleanly", out)
        self.assertIn("on=1", out)
        self.assertIn("on=0", out)  # the text after the 700-byte string parsed correctly
        # 4. what the Hub's trace shows (the evidence)
        nid = hexid(NODE_ID)
        up = env.events("link_up", node_id=nid)
        self.assertEqual(len(up), 1)
        self.assertEqual(up[0]["profile"], "message")
        self.assertEqual(up[0]["delivery"], "reliable")
        self.assertEqual(up[0]["max_rx_payload"], 512)
        man = env.events("manifest", node_id=nid)
        self.assertEqual(len(man), 1)
        self.assertEqual([c["kind"] for c in man[0]["caps"]], ["sense.temperature", "actuator.led"])
        self.assertFalse(env.events("cap_rejected"))
        pub = env.events("publish", node_id=nid, topic=TEMP_TOPIC)
        self.assertEqual([(p["status"], p["value"]) for p in pub], [("ACCEPTED", 23.5)])
        fwd = [e for e in env.events("rpc_request") if e["status"] == "forwarded"]
        self.assertEqual(len(fwd), 2)
        rep = env.events("rpc_reply", node_id=nid)
        self.assertEqual([(e["ok"], e["result"]) for e in rep], [(True, {"ok": True})] * 2)
        self.assertTrue(any(e["how"] == "closed" for e in env.events("link_down", node_id=nid)))
        self.assertTrue(env.events("node_down", node_id=nid))
        self.assertFalse(env.events("protocol_error"))

    def test_wrong_token_is_reset_with_security_error(self):
        async def go():
            async with Env("badtoken") as env:
                proc = await env.node(token="not-the-token", rpcs=0)
                rc, out, err = await finish(proc)
                return env, rc, out, err

        env, rc, out, err = run(go())
        self.assertEqual(rc, 4, (out, err))
        self.assertIn("manifest was not acknowledged", err)
        rej = env.events("link_rejected", node_id=hexid(NODE_ID))
        self.assertEqual(len(rej), 1)
        self.assertIn("token", rej[0]["reason"])
        self.assertNotIn("not-the-token", json.dumps(env.hub.trace.events))
        self.assertFalse(env.events("manifest"))

    def test_hub_going_away_ends_the_node_cleanly(self):
        async def go():
            async with Env("hubgone") as env:
                caller = await env.client("py-caller")
                await caller.watch_directory()
                proc = await env.node(rpcs=1)
                await wait_for(lambda: caller.find_node(NODE_NAME), "node in directory")
                await env.hub.stop()
                rc, out, err = await finish(proc)
                return rc, out, err

        rc, out, err = run(go())
        self.assertEqual(rc, 4, (out, err))     # link lost: transport failure, not a hang
        self.assertNotIn("closed cleanly", out)

    def test_rpc_to_unknown_method_never_reaches_the_node(self):
        async def go():
            async with Env("nomethod") as env:
                caller = await env.client("py-caller")
                await caller.watch_directory()
                proc = await env.node(rpcs=1)
                await wait_for(lambda: caller.find_node(NODE_NAME), "node in directory")
                with self.assertRaises(RpcError) as cm:
                    await caller.rpc(NODE_ID, "led", "blink", {})
                self.assertIn("no_such_method", str(cm.exception))
                ok = await caller.rpc(NODE_ID, "led", "set", {"color": "on", "on": True})
                self.assertEqual(ok, {"ok": True})
                return await finish(proc)

        rc, out, err = run(go())
        self.assertEqual(rc, 0, (out, err))

    def test_node_reconnect_replaces_the_old_connection(self):
        async def go():
            async with Env("reconnect") as env:
                caller = await env.client("py-caller")
                await caller.watch_directory()
                first = await env.node(rpcs=1)
                await wait_for(lambda: caller.find_node(NODE_NAME), "first node")
                r = await caller.rpc(NODE_ID, "led", "set", {"color": "on", "on": True})
                self.assertEqual(r, {"ok": True})
                self.assertEqual((await finish(first))[0], 0)
                await wait_for(lambda: caller.find_node(NODE_NAME) is None, "node leaves the pool")
                second = await env.node(rpcs=1)           # fresh boot_id, same node_id
                await wait_for(lambda: caller.find_node(NODE_NAME), "second node")
                r = await caller.rpc(NODE_ID, "led", "set", {"color": "on", "on": False})
                self.assertEqual(r, {"ok": True})
                self.assertEqual((await finish(second))[0], 0)
                return env

        env = run(go())
        boots = [e["boot_id"] for e in env.events("manifest", node_id=hexid(NODE_ID))]
        self.assertEqual(len(boots), 2)
        self.assertNotEqual(boots[0], boots[1])


if __name__ == "__main__":
    unittest.main()
