"""Protocol checks that do not need a connected printer."""

import asyncio
import unittest

import main


class ProtocolTest(unittest.TestCase):
    def test_command_frames(self):
        self.assertEqual(main.frame(main.BEGIN, b"@"), b"MP\x01\x01\x01\x00@")
        self.assertEqual(len(main.frame(main.LINE, bytes(main.LINE_BYTES))), 54)
        with self.assertRaises(ValueError):
            main.frame(main.LINE, b"short")
        with self.assertRaises(ValueError):
            main.frame(0xFF)

    def test_status_frame(self):
        encoded = b"MP\x01\x80\x05\x00\x5c\x1e\x00\x01\x00"
        self.assertEqual(main.decode_status(encoded), main.PrinterStatus(92, 30, 0, 1, 0))
        with self.assertRaises(ValueError):
            main.decode_status(encoded[:-1])

    def test_page_dimensions(self):
        lines = list(main.page())
        self.assertEqual(len(lines), main.PAGE_LINES)
        self.assertTrue(all(len(line) == main.LINE_BYTES for line in lines))
        self.assertGreater(sum(bin(byte).count("1") for line in lines for byte in line), 1000)

    def test_batched_writes_preserve_every_frame(self):
        batches = list(main.line_batches())
        self.assertEqual(len(batches), 12)
        self.assertEqual(batches[-1][0], main.PAGE_LINES)
        self.assertTrue(all(len(data) == 432 for _, data in batches))

        stream = b"".join(data for _, data in batches)
        expected = b"".join(main.frame(main.LINE, line) for line in main.page())
        self.assertEqual(stream, expected)
        self.assertEqual(len(list(main.write_chunks(batches[0][1], 512))), 1)
        small_chunks = list(main.write_chunks(batches[0][1], 20))
        self.assertTrue(all(len(chunk) <= 20 for chunk in small_chunks))
        self.assertEqual(b"".join(small_chunks), batches[0][1])

    def test_idle_before_printing_is_not_completion(self):
        async def check():
            events = asyncio.Queue()
            requests = []

            async def request_status():
                requests.append(True)
                events.put_nowait(main.PrinterStatus(90, 30, 0, 1, 0))
                events.put_nowait(main.PrinterStatus(90, 30, 0, 0, 0))

            events.put_nowait(main.PrinterStatus(90, 30, 0, 0, 0))
            await asyncio.wait_for(main.wait_for_completion(events, request_status), 1)
            self.assertEqual(len(requests), 1)

        asyncio.run(check())


if __name__ == "__main__":
    unittest.main()
