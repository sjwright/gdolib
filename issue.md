# GDO UART stream desync after idle; light on/off recovers

## Summary
The GDO (garage door opener) link becomes unreliable after some time of *idle* communication. From the ESP (gdolib) logs, subsequent received packets appear garbled, starting with errors like:

- `RX data signature error: 0x...`
- `RX buffer read error, N pending messages.`

After the link enters this "bad state", sending `OPEN`/`CLOSE` commands does not recover communication. However, toggling the **light** eventually restores normal operation for hours.

## Observations (from logs)
During the bad state, the receive loop reports repeated header/signature failures and incomplete reads. Example:

- `RX data signature error ...`
- `RX buffer read error, 6 pending messages.` (repeated down to 1)

Those should correspond to a small set of valid Sec+ packets (e.g. door state updates), but the parser keeps failing to frame the stream correctly.

### Startup sync works, but raw RX frames are not visible during triggers
On first connect, the library reports successful sync and protocol detection, for example:
- `Synced: true, protocol: Security+ 2.0`
- `Client ID: ...`
- `Rolling code: ...`

However, during remote-triggered door movement, higher-level ESPHome component logs (e.g. `cover`/`GDOLight`) appear, but the expected raw RX frame logging from `gdolib` (`print_buffer()` / `RX: [...]` style lines, and `received rolling=...` / `cmd=...` from `decode_packet()`) is not observed.

Implication: either (a) v2 frame header signature matching fails in that window (so `print_buffer()` is never reached), or (b) the library is not successfully decoding frames in that window. Since `print_buffer()` for Sec+ v2 only runs after a successful 3-byte header signature match (`memcmp(rx_buffer, "\x55\x01\x00", 3) == 0`), absence of raw RX output strongly points toward persistent framing mismatch rather than merely “corrupt decoded fields”.

## Where this likely happens in code
In Sec+ v2 mode, `gdo_main_task()` expects fixed-length packets and verifies a 3-byte packet header signature (`0x55 0x01 0x00`) before calling `decode_packet()`:

```1506:1521:/Users/simon/git/gdo/esphome/libs/gdolib/gdo.c
while(rx_pending) {
    if (uart_read_bytes(g_config.uart_num, rx_buffer, GDO_PACKET_SIZE, 0) == GDO_PACKET_SIZE) {
        // check for the GDO packet start (0x55 0x01 0x00)
        if (memcmp(rx_buffer, "\x55\x01\x00", 3) != 0) {
            ESP_LOGE(TAG, "RX data signature error: 0x%02x%02x%02x", rx_buffer[0], rx_buffer[1], rx_buffer[2]);
            rx_pending--;
            continue;
        }

        print_buffer(g_status.protocol, rx_buffer, false);
        decode_packet(rx_buffer);
    } else {
        ESP_LOGE(TAG, "RX buffer read error, %u pending messages.", rx_pending);
    }
    --rx_pending;
}
```

Notably, on these errors the code mostly decrements `rx_pending` and continues; it does **not** perform the same aggressive resync/flush that it does for parity/buffer FIFO overflow cases.

## Why `get_status()` might “fix” it (working theory)
`gdo_light_on()` / `gdo_light_off()` both do the following in Sec+ v2:

1. Queue a `LIGHT` command (`queue_command(GDO_CMD_LIGHT, ...)`)
2. If the command was queued successfully, immediately queue a `GET_STATUS` request (`get_status()`)

```524:540:/Users/simon/git/gdo/esphome/libs/gdolib/gdo.c
esp_err_t gdo_light_on(void) {
    ...
    err = queue_command(GDO_CMD_LIGHT, GDO_LIGHT_ACTION_ON, 0, 0);
    if (err == ESP_OK) {
        err = get_status();
    }
    return err;
}
```

```1805:1807:/Users/simon/git/gdo/esphome/libs/gdolib/gdo.c
inline static esp_err_t get_status() {
    return queue_command(GDO_CMD_GET_STATUS, 0, 0, 0);
}
```

Potential mechanisms (not mutually exclusive):

- **Controller response re-aligns framing**: a `GET_STATUS` can provoke a known, structured status reply sequence that arrives in a way that the receiver's framing logic can re-lock on the correct boundaries.
- **RX flush side effect after TX**: for Sec+ v2, after transmitting a packet, the code calls `uart_flush_input()`:

```1249:1293:/Users/simon/git/gdo/esphome/libs/gdolib/gdo.c
// flush the rx buffer since it will now have the data we just sent.
err = uart_flush_input(g_config.uart_num);
```

So the light path likely causes an exchange that includes both:
1) a new command burst (LIGHT + GET_STATUS), and
2) an RX flush after each Sec+ v2 TX,

which can clear desynchronized bytes and restore packet alignment.

In contrast, `OPEN`/`CLOSE` send door-action packets (and may not request `GET_STATUS` afterward), so a framing slip during idle can persist.

## What to log next (to confirm the theory)
The goal is to determine whether this is primarily:

- (A) header-boundary desync (parser starts reading mid-stream), and/or
- (B) an RX read timing/queueing issue (non-blocking reads not having a full packet yet), and/or
- (C) a physical-layer issue (sporadic corruption, FIFO overruns) that your log simply isn’t catching.

Suggested logging fields to add around the failing receive loop and around transmissions:

### A) On every receive error
For each occurrence of:
- `RX data signature error`
- `RX buffer read error`

log:
- `esp_timer_get_time()/1000` timestamp (ms)
- current `g_status.protocol` (Sec+ v1 vs v2)
- `rx_packet_size` (the UART event size)
- current `rx_pending` value
- the first 3 bytes of `rx_buffer` (already logged for signature mismatch; keep it)
- optionally: also dump the full `GDO_PACKET_SIZE` bytes (or enough bytes to see stable patterns)

### B) On UART event types
Add a log when handling UART event cases:
- `UART_BREAK`
- `UART_DATA` (log `rx_packet_size`)
- `UART_PARITY_ERR`
- `UART_FIFO_OVF`

so we can correlate the first signature/read error with preceding UART conditions.

### C) On TX (especially flush behavior)
When transmitting in Sec+ v2, log:
- the command being transmitted (`tx_message.cmd`)
- the time since last TX (`now - last_tx_time`)
- whether `uart_flush_input()` succeeded (and possibly `uart_get_buffered_data_len()` if available in your ESP-IDF version)

### D) On “recovery” actions
When the application triggers:
- `gdo_light_on/off`
- `gdo_get_status` / `GET_STATUS`
- `gdo_door_open/close`

log:
- the time of the trigger
- whether `queue_command()` returned `ESP_OK` for each queued command
- the order: LIGHT vs GET_STATUS vs any other scheduled commands

## What would confirm the hypothesis
- The first `RX data signature error` after idle should show that the stream is no longer aligned to the `0x55 0x01 0x00` header.
- The moment the light triggers recovery, there should be at least one `uart_flush_input()` right after a Sec+ v2 TX, followed shortly by a successfully framed status packet (i.e., `memcmp(...) == 0` and `decode_packet()` runs cleanly for a few packets).
- If you can log a sequence of `UART_BREAK` + `UART_DATA(rx_packet_size)` events, you may find that the parser’s `rx_pending` count diverges from actual packet boundaries during the bad window.

## Questions for the next data-gathering pass
1. Do the receive errors always start after a consistent idle interval, or at random?
2. Are there any `uart_*` errors that are currently being filtered out by log level?
3. When “bad”, do you ever see `GDO_CMD_STATUS`/`GET_STATUS` replies being logged correctly, or does the parser fail to frame them too?
