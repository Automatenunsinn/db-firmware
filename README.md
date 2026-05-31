# db-firmware

AVR firmware for the DB1MB redesign. The ATmega host sleeps in power-save mode,
wakes on a door-intrusion event, brings up an MC68331 target over BDM, and then
monitors it while it runs.

## Main loop state machine

The `main()` function in [`src/main.c`](src/main.c) is a four-state machine
driven by the global `g_state`.

```mermaid
flowchart TD
    start([main]) --> init["system_init()<br/>g_state = 0"]
    init --> loop{g_state?}

    %% ---- State 0: sleep / wake ----
    loop -->|0| sleep["Enter power-save sleep<br/>sleep_cpu()"]
    sleep --> wake["Wake: disable T2 compare IRQ"]
    wake --> door{check_door_intrusion?}
    door -->|yes| waitclose["Wait for door to close"]
    waitclose --> s1[g_state = 1]
    door -->|no| rtc["update_door_open()<br/>periodic RTC check"]
    rtc --> loop
    s1 --> loop

    %% ---- State 1: battery gate ----
    loop -->|1| ti0{"target_init(0)<br/>verify_voltages"}
    ti0 -->|ok| s2a[g_state = 2]
    ti0 -->|battery low| powerdown["Reset ports<br/>g_state = 0"]
    s2a --> loop
    powerdown --> loop

    %% ---- State 2: full target bring-up ----
    loop -->|2| ti1["target_init(1)<br/>init MC68331, load EEPROM,<br/>checksum, push registers, bdm_go()"]
    ti1 --> s3[g_state = 3]
    s3 --> loop

    %% ---- State 3: runtime monitor ----
    loop -->|3| volt{"verify_voltages(1)?"}
    volt -->|battery dropped| s0a[g_state = 0]
    volt -->|ok| dsclk{DSCLK present? PC0}
    dsclk -->|lost| s0b[g_state = 0]
    dsclk -->|ok| host{Host request? PB2}
    host -->|yes| readregs["Read CPU32 status regs<br/>over BDM<br/>g_state = 2"]
    host -->|no| loop
    s0a --> loop
    s0b --> loop
    readregs --> loop

    %% ---- default ----
    loop -->|other| dflt[g_state = 0]
    dflt --> loop
```

## States

| `g_state` | Purpose |
|-----------|---------|
| 0 | Power-save sleep; wake on door intrusion or do a periodic RTC check |
| 1 | Battery gate — `target_init(0)` verifies voltages before bring-up |
| 2 | Full MC68331 bring-up over BDM (init, EEPROM load, checksum, resume) |
| 3 | Runtime monitor — watch battery, DSCLK, and host requests |
