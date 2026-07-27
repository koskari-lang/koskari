# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.fakebroker

An in-memory STOMP broker standing in for UMB, so the umb variety can be
exercised over stompman's real client -- real handshake, subscription,
ack, and send frames -- with no socket and no live bus.

This one test double is Python rather than koskari: it implements
stompman's own ``AbstractConnection`` protocol, whose ``read_frames`` is
an async generator consumed by ``async for`` inside the library. The
tests that drive it are koskari, like the rest of the suite.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio

from stompman.frames import (
    ConnectFrame,
    ConnectedFrame,
    DisconnectFrame,
    MessageFrame,
    ReceiptFrame,
)


class FakeBroker:
    """
    Records the frames a client writes and lets a test push frames back.

    Pass ``connection_class`` to ``stompman.Client`` (the umb job takes it
    as a config override), then ``deliver`` a MessageFrame once the job has
    subscribed.
    """

    def __init__(self, heartbeat="10000,10000"):
        self.heartbeat = heartbeat
        self.written = []
        self.inbound = asyncio.Queue()
        self.connection_class = _make_connection_class(self)


    async def deliver(self, subscription_id, body, destination="/topic/t",
            message_id="msg-1", headers=None):
        """
        Push one MessageFrame at the subscriber, addressed to the given
        subscription id (read off the job's live subscription). Extra
        headers arrive as a dict rather than keywords, because the ones
        that matter here (``reply-to``, ``correlation-id``) carry dashes.
        """

        all_headers = {
            "destination": destination,
            "message-id": message_id,
            "subscription": subscription_id,
            "ack": f"ack-{message_id}"}
        if headers:
            all_headers.update(headers)
        await self.inbound.put(MessageFrame(headers=all_headers, body=body))


    def frames_of(self, frame_class):
        """
        Every client frame of the given class, in write order.
        """

        return [f for f in self.written if isinstance(f, frame_class)]


    async def wait_for_frame(self, frame_class, count=1, timeout=2.0):
        """
        Block until at least ``count`` frames of the class have been
        written, then return them. Raises TimeoutError if they never come,
        which reads as a plain test failure.
        """

        async def poll():
            while len(self.frames_of(frame_class)) < count:
                await asyncio.sleep(0.005)
            return self.frames_of(frame_class)

        return await asyncio.wait_for(poll(), timeout)


def _make_connection_class(broker):
    """
    Build the AbstractConnection implementation bound to this broker. The
    class is per-broker because stompman constructs connections itself,
    handing the test no other seam to reach them through.
    """

    class FakeConnection:

        last_read_time = None

        @classmethod
        async def connect(cls, *, host, port, timeout, read_max_chunk_size,
                ssl, ws_uri_path=None):
            return cls()

        async def close(self):
            pass

        def write_heartbeat(self):
            pass

        async def write_frame(self, frame):
            broker.written.append(frame)
            # answer the two frames the lifespan blocks on
            if isinstance(frame, ConnectFrame):
                await broker.inbound.put(ConnectedFrame(headers={
                    "version": "1.2",
                    "heart-beat": broker.heartbeat}))
            elif isinstance(frame, DisconnectFrame):
                await broker.inbound.put(ReceiptFrame(headers={
                    "receipt-id": frame.headers["receipt"]}))

        async def read_frames(self):
            while True:
                yield await broker.inbound.get()

    return FakeConnection


# The end.
