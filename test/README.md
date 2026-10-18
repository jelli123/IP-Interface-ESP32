# Tests

| Datei | Art | Prüft |
| --- | --- | --- |
| [run_host_tests.sh](run_host_tests.sh) | Host, ohne Gerät | KNX Secure: Rahmen und Kryptografie ([secure_crypto_test.cpp](secure_crypto_test.cpp)), Zugriffsregeln für das Management ([access_policy_test.cpp](access_policy_test.cpp)) |
| [tunnel_ack_test.py](tunnel_ack_test.py) | am Gerät, über das Netz | Überwachung der Tunnel-Quittungen (Patch 12) und die Close-Zeile im Protokoll (Patch 29) |

Den Busweg über das SB-Interface prüft der Funktionstest des TP-UART-2-Emulators,
`misc/TPUART2-Emu/test/knx_function_test.py` in software-arm-incubation.

## Host-Tests

```
test/run_host_tests.sh
```

Braucht einen C++-Compiler und vorher einen PlatformIO-Build, der den KNX-Stack
holt.

## Tunnel-Quittungen

```
test/tunnel_ack_test.py --gateway 192.168.179.105
```

Braucht nur Python 3 und das Gerät im Netz, mit offenem Dashboard (ohne
Passwort), denn das Skript liest das Protokoll über `/api/log`.

Das Skript öffnet zwei Tunnel. Der erste sendet Gruppentelegramme, der Stack
reicht sie dem zweiten als `L_Data.ind` weiter. Der zweite Tunnel stellt
zeitweise seine Quittungen ein. Im Protokoll muss dann Folgendes stehen:

1. Solange beide quittieren: keine Warnung.
2. Nach 9 unquittierten Rahmen: noch keine Warnung. Ab dem 10. Rahmen genau
   eine, mit Kanal und Tunnel-PA des stummen Tunnels.
3. Sobald er wieder quittiert: einmal „acknowledges again“.
4. Beim Abmelden: `Close Tunnel-Connection[<Slot>], Channel: 0x… PA: … (disconnect request)`,
   mit demselben Slot wie in der Zeile `New Tunnel-Connection[…]` beim Aufbau.
5. Eine neue Verbindung beginnt bei null: keine Warnung.

`--heartbeat` wartet zusätzlich gut zwei Minuten. In dieser Zeit sendet keiner
der beiden Tunnel einen Heartbeat, und der Stack muss sie mit „(no heartbeat
for 120 s)“ schließen.

Die Telegramme gehen auch auf TP hinaus, auf `--group` (Vorgabe `15/7/40`)
und die beiden folgenden Adressen. Diese sollten auf der Linie frei sein.
